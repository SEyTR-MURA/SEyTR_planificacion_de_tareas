/* turnos.c - variantes de U3-S3: cómo elige el planificador.
 *
 * Variantes:
 * - «1» turnos: dos tareas de cálculo, A y B, de igual prioridad y en el mismo
 *   núcleo. El planificador las turna en cada tick.
 * - «2» cesión: las mismas, pero cada una cede el control con taskYIELD()
 *   después de cada unidad de trabajo, sin esperar al tick.
 * - «3» expulsión: una tarea de cálculo de prioridad baja y otra periódica de
 *   prioridad más alta, en el mismo núcleo. La periódica pasa delante en cuanto
 *   despierta.
 * - «4» inanición: la de más prioridad deja de bloquearse. La de menos no
 *   avanza, y a los 5 s avisa el watchdog de tareas, porque tampoco avanza la
 *   tarea inactiva del núcleo 0.
 * - «5» turnos sin afinidad: la variante 1 sin fijar el núcleo.
 * - «6» inanición sin afinidad: la variante 4 sin fijar el núcleo.
 *
 * Las tareas de cálculo trabajan por ráfagas: calculan hasta que han pasado
 * 2 s de reloj desde que empezó cada ciclo de 2,5 s, y el resto del ciclo
 * están bloqueadas. La ráfaga se mide en tiempo de reloj y no en tiempo
 * propio: si se midiera en tiempo propio, con dos tareas turnándose cada
 * ráfaga duraría 4 s y no quedaría tiempo libre. Así la tarea inactiva del núcleo se ejecuta
 * dentro del plazo del watchdog de tareas, que es de 5 s, y el aviso solo
 * aparece en las variantes de inanición. Ceder con taskYIELD() no bastaría:
 * no da paso a la tarea inactiva, que tiene prioridad 0.
 *
 * Las tareas de cálculo no usan float: en el ESP32-S3, una tarea que usa la
 * unidad de coma flotante queda fijada al núcleo en que esté, y las variantes
 * sin afinidad dejarían de serlo.
 *
 * Cómo se mide un tramo de ejecución
 * ----------------------------------
 * Un tramo de ejecución es el tiempo que una tarea se ejecuta seguida cada
 * vez. No es lo mismo que un turno, que es el tiempo máximo antes de que el
 * planificador pase a otra tarea de igual prioridad (un periodo del tick): una
 * tarea que cede, o a la que expulsa otra más prioritaria, usa menos.
 * Una tarea de cálculo lee el reloj en un bucle muy corto. Si entre dos
 * lecturas seguidas pasan más de HUECO_US (comun.h), es que entretanto se ha
 * ejecutado otra cosa: ahí acaba un tramo y empieza otro. El tiempo de
 * procesador de la tarea es la suma de los intervalos cortos. Las
 * interrupciones del tick duran mucho menos que HUECO_US y no cortan el tramo.
 * Un tramo también acaba si la tarea aparece en otro núcleo, aunque el hueco
 * haya sido corto.
 *
 * Cómo se mide la periódica
 * -------------------------
 * - Retardo de activación: desde el instante en que debía activarse, que es
 *   el comienzo de un tick, hasta que empieza a ejecutarse.
 * - Respuesta: desde ese mismo instante hasta que termina su trabajo.
 */

#include "comun.h"

#include <inttypes.h>
#include <stdio.h>

#include "esp_timer.h"

#define RAFAGA_US         2000000  /* cálculo en cada ciclo */
#define CICLO_MS          2500     /* ciclo de las tareas de cálculo y del informe */
/* Trabajo entre dos cesiones en la variante 2. Con el tick a 1000 Hz el
 * reparto entre A y B no sale igualado, ni con esta unidad ni con otras: cada
 * tick provoca un cambio de tarea y la secuencia de cesiones vuelve a empezar
 * en cada periodo del tick. Lo explica el README, en «Qué deberías ver». */
#define UNIDAD_CESION_US  170

#define PERIODO_P_MS      10       /* periodo de la tarea periódica (variante 3) */
#define TRABAJO_P_US      1000     /* trabajo de cada activación de la periódica */

/* Cuenta de una tarea de cálculo. La escribe solo esa tarea; la lee el
 * informe (véase la nota de comun.h sobre datos compartidos). */
typedef struct {
    const char       *nombre;
    UBaseType_t       prioridad;
    BaseType_t        afinidad;       /* 0, 1 o tskNO_AFFINITY */
    volatile uint32_t cpu_us;         /* tiempo de procesador acumulado */
    volatile uint32_t tramos;         /* tramos de ejecución terminados, acumulados */
    volatile uint32_t tramo_max_us;   /* desde la última puesta a cero */
    volatile uint32_t generacion_vista;
    volatile uint32_t nucleo;         /* núcleo en que se ejecutó por última vez */
    volatile uint32_t cambios_nucleo; /* veces que apareció en otro núcleo, acumuladas */
} cuenta_t;

typedef struct {
    cuenta_t *cuenta;
    bool      ceder;    /* variante 2 */
    bool      sin_fin;  /* no se bloquea nunca: variantes 4 y 6 */
} parametros_t;

#define SIN_NUCLEO UINT32_MAX  /* todavía no se ha ejecutado */

static cuenta_t s_a = { .nombre = "A", .nucleo = SIN_NUCLEO };
static cuenta_t s_b = { .nombre = "B", .nucleo = SIN_NUCLEO };
static parametros_t s_param_a = { .cuenta = &s_a };
static parametros_t s_param_b = { .cuenta = &s_b };

static estadistica_t s_periodo_p;   /* intervalo entre activaciones de la periódica */
static estadistica_t s_retardo_p;   /* del instante de activación a empezar */
static estadistica_t s_respuesta_p; /* del instante de activación a terminar */
static bool s_hay_p = false;
static char s_variante = '1';

/* Anota el núcleo en que se ejecuta la tarea. Devuelve true si ha cambiado. */
static bool mirar_nucleo(cuenta_t *c)
{
    const uint32_t nucleo = (uint32_t)xPortGetCoreID();
    if (nucleo == c->nucleo) {
        return false;
    }
    const bool primera = (c->nucleo == SIN_NUCLEO);
    c->nucleo = nucleo;
    if (primera) {
        return false;
    }
    c->cambios_nucleo = c->cambios_nucleo + 1;
    return true;
}

static void cerrar_tramo(cuenta_t *c, int64_t duracion_us)
{
    const uint32_t g = generacion();
    if (g != c->generacion_vista) {
        c->tramo_max_us = 0;
        c->generacion_vista = g;  /* después de poner a cero */
    }
    if ((uint32_t)duracion_us > c->tramo_max_us) {
        c->tramo_max_us = (uint32_t)duracion_us;
    }
    c->tramos = c->tramos + 1;
}

/* Calcula hasta el instante «fin_us» del reloj, contando los tramos de ejecución. */
static void calcular(cuenta_t *c, int64_t fin_us, bool ceder)
{
    int64_t ahora = esp_timer_get_time();
    int64_t previo = ahora;
    int64_t inicio = ahora;
    int64_t desde_cesion = 0;

    (void)mirar_nucleo(c);
    while (ahora < fin_us) {
        if (ceder && desde_cesion >= UNIDAD_CESION_US) {
            desde_cesion = 0;
            taskYIELD();  /* cede el control a otra tarea lista de igual prioridad */
        }
        ahora = esp_timer_get_time();
        const int64_t hueco = ahora - previo;
        const bool otro_nucleo = mirar_nucleo(c);
        if (hueco > HUECO_US || otro_nucleo) {
            cerrar_tramo(c, previo - inicio);  /* se ejecutó otra cosa */
            inicio = ahora;
        } else {
            c->cpu_us = c->cpu_us + (uint32_t)hueco;
            desde_cesion += hueco;
        }
        previo = ahora;
    }
    cerrar_tramo(c, previo - inicio);
}

static void tarea_calculo(void *argumento)
{
    const parametros_t *p = (const parametros_t *)argumento;
    if (p->sin_fin) {
        calcular(p->cuenta, INT64_MAX, false);  /* no vuelve nunca */
    }
    TickType_t referencia = xTaskGetTickCount();
    for (;;) {
        calcular(p->cuenta, esp_timer_get_time() + RAFAGA_US, p->ceder);
        /* Bloqueada hasta el siguiente ciclo: aquí se ejecuta la tarea
         * inactiva de su núcleo. */
        xTaskDelayUntil(&referencia, pdMS_TO_TICKS(CICLO_MS));
    }
}

static void tarea_periodica(void *argumento)
{
    (void)argumento;
    origen_t origen;
    origen_tomar(&origen);
    TickType_t referencia = origen.tick;
    int64_t anterior = 0;
    for (;;) {
        xTaskDelayUntil(&referencia, pdMS_TO_TICKS(PERIODO_P_MS));
        const int64_t despertar = esp_timer_get_time();
        /* Tras la llamada, «referencia» es el tick en que debía activarse. */
        const int64_t activacion = origen_activacion_us(&origen, referencia, despertar);
        estadistica_anotar(&s_retardo_p, (uint32_t)(despertar - activacion));
        if (anterior != 0) {
            estadistica_anotar(&s_periodo_p, (uint32_t)(despertar - anterior));
        }
        anterior = despertar;
        trabajar_us(TRABAJO_P_US);
        estadistica_anotar(&s_respuesta_p, (uint32_t)(esp_timer_get_time() - activacion));
    }
}

static bool crear_calculo(parametros_t *p, UBaseType_t prioridad, BaseType_t afinidad)
{
    p->cuenta->prioridad = prioridad;
    p->cuenta->afinidad = afinidad;
    char nombre[12];
    snprintf(nombre, sizeof nombre, "calculo_%s", p->cuenta->nombre);
    return xTaskCreatePinnedToCore(tarea_calculo, nombre, PILA_TAREA, p,
                                   prioridad, NULL, afinidad) == pdPASS;
}

void turnos_crear(char variante)
{
    bool ok = true;
    s_variante = variante;
    switch (variante) {
    case '1':
        ok = crear_calculo(&s_param_a, 3, NUCLEO_PRUEBA)
          && crear_calculo(&s_param_b, 3, NUCLEO_PRUEBA);
        break;
    case '2':
        s_param_a.ceder = true;
        s_param_b.ceder = true;
        ok = crear_calculo(&s_param_a, 3, NUCLEO_PRUEBA)
          && crear_calculo(&s_param_b, 3, NUCLEO_PRUEBA);
        break;
    case '3':
        ok = crear_calculo(&s_param_a, 2, NUCLEO_PRUEBA)
          && xTaskCreatePinnedToCore(tarea_periodica, "periodica", PILA_TAREA, NULL,
                                     5, NULL, NUCLEO_PRUEBA) == pdPASS;
        s_hay_p = true;
        break;
    case '4':
        /* B primero: una tarea recién creada empieza enseguida si su núcleo
         * está libre, y A llegaría a ejecutarse un momento antes que B. */
        s_param_b.sin_fin = true;
        ok = crear_calculo(&s_param_b, 4, NUCLEO_PRUEBA)
          && crear_calculo(&s_param_a, 3, NUCLEO_PRUEBA);
        break;
    case '5':
        ok = crear_calculo(&s_param_a, 3, tskNO_AFFINITY)
          && crear_calculo(&s_param_b, 3, tskNO_AFFINITY);
        break;
    case '6':
        s_param_b.sin_fin = true;
        ok = crear_calculo(&s_param_b, 4, tskNO_AFFINITY)
          && crear_calculo(&s_param_a, 3, tskNO_AFFINITY);
        break;
    default:
        break;
    }
    if (!ok) {
        printf("No hay memoria para las tareas de la variante.\n");
    }
}

/* Lo que el informe vio la vez anterior, para dar lo de cada ciclo. Solo lo
 * toca la tarea del informe. */
typedef struct {
    uint32_t cpu_us;
    uint32_t tramos;
    uint32_t cambios_nucleo;
} vista_t;

static void informar_cuenta(const cuenta_t *c, vista_t *v)
{
    if (c->prioridad == 0) {
        return;  /* esta variante no la usa */
    }
    const uint32_t cpu = c->cpu_us;
    const uint32_t tramos = c->tramos;
    const uint32_t d_cpu = cpu - v->cpu_us;  /* aritmética sin signo: admite la vuelta */
    const uint32_t d_tramos = tramos - v->tramos;
    const uint32_t cambios = c->cambios_nucleo;
    const uint32_t d_cambios = cambios - v->cambios_nucleo;
    v->cpu_us = cpu;
    v->tramos = tramos;
    v->cambios_nucleo = cambios;

    char afinidad[24];
    if (c->afinidad == tskNO_AFFINITY) {
        snprintf(afinidad, sizeof afinidad, "sin afinidad");
    } else {
        snprintf(afinidad, sizeof afinidad, "nucleo %d", (int)c->afinidad);
    }
    char t_cpu[24];
    escribir_ms(t_cpu, sizeof t_cpu, d_cpu);
    printf("  %s (prioridad %u, %s): %s de procesador en el ciclo",
           c->nombre, (unsigned)c->prioridad, afinidad, t_cpu);
    if (d_cpu == 0) {
        printf(". No se ha ejecutado\n");
        return;
    }
    if (d_tramos == 0) {
        printf(", sin cambios de tarea");
    } else {
        char medio[24], maximo[24];
        escribir_ms(medio, sizeof medio, d_cpu / d_tramos);
        const uint32_t max = (c->generacion_vista == generacion()) ? c->tramo_max_us : 0;
        escribir_ms(maximo, sizeof maximo, max);
        printf(", %" PRIu32 " tramos de ejecucion, medio %s, maximo %s", d_tramos, medio, maximo);
    }
    if (c->afinidad == tskNO_AFFINITY) {
        printf(", ultimo nucleo %" PRIu32 ", %" PRIu32 " cambios de nucleo en el ciclo",
               c->nucleo, d_cambios);
    }
    printf("\n");
}

void turnos_informe(void)
{
    static vista_t vista_a, vista_b;
    TickType_t referencia = xTaskGetTickCount();
    for (;;) {
        xTaskDelayUntil(&referencia, pdMS_TO_TICKS(CICLO_MS));
        printf("\n[variante %c] ultimos 2,5 s:\n", s_variante);
        informar_cuenta(&s_a, &vista_a);
        informar_cuenta(&s_b, &vista_b);
        if (s_hay_p) {
            estadistica_escribir("periodica, periodo", &s_periodo_p);
            estadistica_escribir("periodica, retardo", &s_retardo_p);
            estadistica_escribir("periodica, respuesta", &s_respuesta_p);
        }
    }
}
