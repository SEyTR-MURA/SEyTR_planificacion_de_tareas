/* periodicas.c - variantes de U3-S5: prioridades y tiempo.
 *
 * Variantes:
 * - «7» dos periódicas: la tarea R, con periodo de 10 ms y 2 ms de trabajo, y
 *   la tarea L, con periodo de 50 ms y 15 ms de trabajo, en el mismo núcleo.
 *   Empieza con R más prioritaria, que es la de periodo más corto. La tecla
 *   «c» añade o quita carga a R (4 ms de trabajo en lugar de 2) y la tecla
 *   «p» intercambia en ejecución las prioridades de las dos. Las dos empiezan
 *   en el mismo tick, para que el resultado no dependa de su desfase.
 * - «8» vuelta larga: como la 7, pero una de cada cinco vueltas de L dura
 *   70 ms, más que su periodo. Se cuentan las veces que xTaskDelayUntil()
 *   devuelve pdFALSE porque el siguiente instante ya había pasado.
 * - «9» temporizadores: un temporizador software de FreeRTOS y una alarma
 *   periódica de esp_timer, los dos de 10 ms, y una tarea de carga de
 *   prioridad 5 que se activa y se desactiva con «c». Se mide el periodo con que se ejecuta
 *   la función de respuesta de cada uno.
 *
 * Todas las tareas de la prueba van en el núcleo 0. La tarea de servicio de
 * los temporizadores se fija también al núcleo 0 en sdkconfig.defaults, y la
 * de esp_timer va en él por omisión.
 *
 * Cómo se mide
 * ------------
 * - Periodo: tiempo entre dos activaciones seguidas, leído con
 *   esp_timer_get_time() nada más despertar. El jitter es la diferencia entre
 *   el máximo y el mínimo.
 * - Retardo de activación: desde el instante en que la tarea debía activarse,
 *   que es el comienzo de un tick, hasta que empieza a ejecutarse. Es el
 *   tiempo que pasa lista mientras se ejecutan otras de más prioridad.
 * - Respuesta: desde el instante en que debía activarse hasta que termina su
 *   trabajo. Incluye el retardo de activación.
 * Las trazas se escriben en el informe, cada 2 s, y no dentro del bucle
 * medido, para que la medida no incluya lo que tarda en escribirse.
 */

#include "comun.h"

#include <inttypes.h>
#include <stdio.h>

#include "freertos/timers.h"
#include "esp_timer.h"

#define PERIODO_R_MS        10
#define TRABAJO_R_US        2000
#define TRABAJO_R_CARGA_US  4000    /* 5 ms empataría con el tick: véase el README */
#define PRIORIDAD_ALTA      5
#define PERIODO_L_MS        50
#define TRABAJO_L_US        15000
#define TRABAJO_L_LARGO_US  70000   /* más que su periodo */
#define CADA_LARGA          5       /* una vuelta larga de cada cinco */
#define PRIORIDAD_BAJA      4

#define PERIODO_TEMPORIZADOR_MS  10
#define CARGA_RAFAGA_US          30000  /* la carga calcula 30 ms... */
#define CARGA_PAUSA_MS           20     /* ...y se bloquea 20 ms */
#define PRIORIDAD_CARGA          5

#define INFORME_MS          2000
#define INICIO_TICKS        20      /* margen hasta la primera activación */

typedef struct {
    const char       *nombre;
    uint32_t          periodo_ms;
    volatile uint32_t trabajo_us;
    uint32_t          cada_larga;   /* 0: ninguna vuelta larga */
    estadistica_t     periodo;
    estadistica_t     retardo;
    estadistica_t     respuesta;
    volatile uint32_t tardias;      /* xTaskDelayUntil() devolvió pdFALSE */
    volatile uint32_t generacion_tardias;
    TaskHandle_t      tarea;
} periodica_t;

static periodica_t s_r = { .nombre = "R", .periodo_ms = PERIODO_R_MS, .trabajo_us = TRABAJO_R_US };
static periodica_t s_l = { .nombre = "L", .periodo_ms = PERIODO_L_MS, .trabajo_us = TRABAJO_L_US };

/* Origen común de R y L: las dos se activan por primera vez en el tick
 * s_origen.tick + INICIO_TICKS. Lo toma la tarea que las crea antes de
 * crearlas, y después ya no cambia. */
static origen_t s_origen;

static estadistica_t s_temporizador_sw;
static estadistica_t s_alarma;
static volatile bool s_carga = false;
static char s_variante = '7';

static void anotar_tardia(periodica_t *p)
{
    const uint32_t g = generacion();
    if (g != p->generacion_tardias) {
        p->tardias = 0;
        p->generacion_tardias = g;  /* después de poner a cero */
    }
    p->tardias = p->tardias + 1;
}

static void tarea_periodica(void *argumento)
{
    periodica_t *p = (periodica_t *)argumento;
    origen_t origen = s_origen;  /* copia propia: la corrige solo esta tarea */
    const TickType_t periodo = pdMS_TO_TICKS(p->periodo_ms);
    /* La primera activación, común a R y L. La referencia de xTaskDelayUntil()
     * tiene que ser un instante ya pasado: si estuviera en el futuro, la
     * función lo tomaría por una vuelta de la cuenta de ticks y no esperaría. */
    TickType_t referencia = origen.tick;
    BaseType_t espero = xTaskDelayUntil(&referencia, INICIO_TICKS);
    int64_t anterior = 0;
    uint32_t vuelta = 0;
    for (;;) {
        const int64_t despertar = esp_timer_get_time();
        /* Tras la llamada, «referencia» es el tick en que debía activarse,
         * también cuando devuelve pdFALSE. */
        const int64_t activacion = origen_activacion_us(&origen, referencia, despertar);
        estadistica_anotar(&p->retardo, (uint32_t)(despertar - activacion));
        if (espero == pdFALSE) {
            anotar_tardia(p);  /* el siguiente instante ya había pasado */
        }
        if (anterior != 0) {
            estadistica_anotar(&p->periodo, (uint32_t)(despertar - anterior));
        }
        anterior = despertar;
        vuelta++;
        const bool larga = (p->cada_larga != 0) && (vuelta % p->cada_larga == 0);
        trabajar_us(larga ? TRABAJO_L_LARGO_US : p->trabajo_us);
        estadistica_anotar(&p->respuesta, (uint32_t)(esp_timer_get_time() - activacion));
        espero = xTaskDelayUntil(&referencia, periodo);
    }
}

static bool crear_periodica(periodica_t *p, UBaseType_t prioridad)
{
    char nombre[16];
    snprintf(nombre, sizeof nombre, "periodica_%s", p->nombre);
    return xTaskCreatePinnedToCore(tarea_periodica, nombre, PILA_TAREA, p,
                                   prioridad, &p->tarea, NUCLEO_PRUEBA) == pdPASS;
}

/* Funciones de respuesta de los temporizadores. Cada una la ejecuta una sola
 * tarea: la del temporizador software, la tarea de servicio de los
 * temporizadores de FreeRTOS; la de la alarma, la tarea de esp_timer. Las dos
 * tienen que ser cortas y no bloquearse: retrasarían a los demás
 * temporizadores, que se atienden en la misma tarea. */
static void respuesta_temporizador_sw(TimerHandle_t temporizador)
{
    (void)temporizador;
    static int64_t anterior = 0;
    const int64_t ahora = esp_timer_get_time();
    if (anterior != 0) {
        estadistica_anotar(&s_temporizador_sw, (uint32_t)(ahora - anterior));
    }
    anterior = ahora;
}

static void respuesta_alarma(void *argumento)
{
    (void)argumento;
    static int64_t anterior = 0;
    const int64_t ahora = esp_timer_get_time();
    if (anterior != 0) {
        estadistica_anotar(&s_alarma, (uint32_t)(ahora - anterior));
    }
    anterior = ahora;
}

static void tarea_carga(void *argumento)
{
    (void)argumento;
    for (;;) {
        if (s_carga) {
            trabajar_us(CARGA_RAFAGA_US);
            vTaskDelay(pdMS_TO_TICKS(CARGA_PAUSA_MS));
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

static bool crear_temporizadores(void)
{
    TimerHandle_t sw = xTimerCreate("temporizador_sw", pdMS_TO_TICKS(PERIODO_TEMPORIZADOR_MS),
                                    pdTRUE, NULL, respuesta_temporizador_sw);
    if (sw == NULL || xTimerStart(sw, portMAX_DELAY) != pdPASS) {
        return false;
    }
    const esp_timer_create_args_t argumentos = {
        .callback = respuesta_alarma,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "alarma",
    };
    esp_timer_handle_t alarma = NULL;
    if (esp_timer_create(&argumentos, &alarma) != ESP_OK) {
        return false;
    }
    if (esp_timer_start_periodic(alarma, (uint64_t)PERIODO_TEMPORIZADOR_MS * 1000) != ESP_OK) {
        return false;
    }
    return xTaskCreatePinnedToCore(tarea_carga, "carga", PILA_TAREA, NULL,
                                   PRIORIDAD_CARGA, NULL, NUCLEO_PRUEBA) == pdPASS;
}

void periodicas_crear(char variante)
{
    bool ok = true;
    s_variante = variante;
    switch (variante) {
    case '8':
        s_l.cada_larga = CADA_LARGA;
        /* El resto es como la variante 7. */
        __attribute__((fallthrough));
    case '7':
        origen_tomar(&s_origen);
        ok = crear_periodica(&s_r, PRIORIDAD_ALTA) && crear_periodica(&s_l, PRIORIDAD_BAJA);
        break;
    case '9':
        ok = crear_temporizadores();
        break;
    default:
        break;
    }
    if (!ok) {
        printf("No se han podido crear las tareas o los temporizadores de la variante.\n");
    }
}

bool periodicas_tecla(char tecla)
{
    switch (tecla) {
    case 'c':
        if (s_variante == '7') {
            s_r.trabajo_us = (s_r.trabajo_us == TRABAJO_R_US) ? TRABAJO_R_CARGA_US : TRABAJO_R_US;
            nueva_generacion();
            printf("\nTrabajo de R: %" PRIu32 " ms. Medidas a cero.\n", s_r.trabajo_us / 1000);
            return true;
        }
        if (s_variante == '9') {
            s_carga = !s_carga;
            nueva_generacion();
            printf("\nCarga %s. Medidas a cero.\n", s_carga ? "activa" : "inactiva");
            return true;
        }
        return false;
    case 'p':
        if (s_variante == '7' && s_r.tarea != NULL && s_l.tarea != NULL) {
            const UBaseType_t pr = uxTaskPriorityGet(s_r.tarea);
            const UBaseType_t pl = uxTaskPriorityGet(s_l.tarea);
            vTaskPrioritySet(s_r.tarea, pl);
            vTaskPrioritySet(s_l.tarea, pr);
            nueva_generacion();
            printf("\nPrioridades intercambiadas: R %u, L %u. Medidas a cero.\n",
                   (unsigned)pl, (unsigned)pr);
            return true;
        }
        return false;
    default:
        return false;
    }
}

static void informar_periodica(const periodica_t *p)
{
    if (p->tarea == NULL) {
        return;  /* no se llegó a crear */
    }
    char etiqueta[48];
    const unsigned prioridad = (unsigned)uxTaskPriorityGet(p->tarea);
    printf("  %s: periodo %" PRIu32 " ms, trabajo %" PRIu32 " ms%s, prioridad %u\n",
           p->nombre, p->periodo_ms, p->trabajo_us / 1000,
           p->cada_larga ? " (70 ms una vuelta de cada cinco)" : "", prioridad);
    snprintf(etiqueta, sizeof etiqueta, "%s, periodo", p->nombre);
    estadistica_escribir(etiqueta, &p->periodo);
    snprintf(etiqueta, sizeof etiqueta, "%s, retardo", p->nombre);
    estadistica_escribir(etiqueta, &p->retardo);
    snprintf(etiqueta, sizeof etiqueta, "%s, respuesta", p->nombre);
    estadistica_escribir(etiqueta, &p->respuesta);
    const uint32_t tardias = (p->generacion_tardias == generacion()) ? p->tardias : 0;
    printf("  %-28s %" PRIu32 "\n", "activaciones tardias", tardias);
}

void periodicas_informe(void)
{
    TickType_t referencia = xTaskGetTickCount();
    for (;;) {
        xTaskDelayUntil(&referencia, pdMS_TO_TICKS(INFORME_MS));
        printf("\n[variante %c] desde la ultima puesta a cero (z):\n", s_variante);
        if (s_variante == '9') {
            printf("  carga de prioridad %d: %s\n", PRIORIDAD_CARGA, s_carga ? "activa" : "inactiva");
            estadistica_escribir("temporizador software", &s_temporizador_sw);
            estadistica_escribir("alarma de esp_timer", &s_alarma);
        } else {
            informar_periodica(&s_r);
            informar_periodica(&s_l);
        }
    }
}
