/* planificacion_de_tareas.c - SEyTR, Unidad 3, sesiones U3-S3 y U3-S5.
 *
 * Qué hace este programa
 * ----------------------
 * Pone en marcha, según la variante que se elija desde el monitor, unas
 * tareas de prueba cuyo comportamiento enseña cómo elige el planificador de
 * FreeRTOS (U3-S3) y cómo se reparten prioridades y tiempo (U3-S5). Cada 2 o
 * 2,5 s escribe un informe con lo medido.
 *
 * Variantes de U3-S3 (fichero turnos.c):
 *   1 turnos · 2 cesión · 3 expulsión · 4 inanición
 *   5 turnos sin afinidad · 6 inanición sin afinidad
 * Variantes de U3-S5 (fichero periodicas.c):
 *   7 dos periódicas · 8 vuelta larga · 9 temporizadores
 * Las teclas se listan al arrancar y con «h».
 *
 * Qué hardware necesita
 * ---------------------
 * Solo la placa del curso, conectada por su conector USB-a-UART, que es por
 * donde llegan las teclas. No hay montaje.
 *
 * Cómo está organizado
 * --------------------
 * Las tareas de la prueba van en el núcleo 0, salvo en las variantes sin
 * afinidad. La tarea del informe y la del teclado van fijadas al núcleo 1, con
 * prioridad más alta que cualquier tarea de la prueba: así siguen funcionando
 * aunque el núcleo 0 quede ocupado, como en la variante de inanición.
 *
 * Cambiar de variante reinicia la placa, porque deshacer las tareas en marcha
 * complicaría el ejemplo sin enseñar nada. La variante elegida se guarda en
 * variables marcadas con __NOINIT_ATTR, que según la documentación de ESP-IDF
 * conservan su valor tras un reinicio por programa. Al encender la placa su
 * contenido no está definido, y por eso se comprueba con un número mágico.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"

#include "comun.h"

static const char *TAG = "planificacion";

#define MAGIA 0x53455452u  /* "SETR": dice que lo guardado es válido */

static __NOINIT_ATTR uint32_t s_guardado_magia;
static __NOINIT_ATTR uint32_t s_guardado_variante;

static char s_variante = '1';

static bool es_variante(char v)
{
    return v >= '1' && v <= '9';
}

static bool es_de_u3s3(char v)
{
    return v >= '1' && v <= '6';
}

static const char *nombre_variante(char v)
{
    switch (v) {
    case '1': return "turnos: dos tareas de calculo de igual prioridad en el nucleo 0";
    case '2': return "cesion: las mismas, cediendo el control con taskYIELD()";
    case '3': return "expulsion: calculo de prioridad 2 y periodica de prioridad 5";
    case '4': return "inanicion: la de prioridad 4 no se bloquea nunca";
    case '5': return "turnos sin afinidad";
    case '6': return "inanicion sin afinidad";
    case '7': return "dos periodicas: R de 10 ms y L de 50 ms";
    case '8': return "vuelta larga: una vuelta de L de cada cinco dura mas que su periodo";
    case '9': return "temporizadores: temporizador software frente a alarma de esp_timer";
    }
    return "?";
}

static void cargar_variante(void)
{
    if (esp_reset_reason() == ESP_RST_SW && s_guardado_magia == MAGIA
        && es_variante((char)s_guardado_variante)) {
        s_variante = (char)s_guardado_variante;
    } else {
        s_variante = '1';
    }
}

static void cambiar_variante(char nueva)
{
    s_guardado_magia = MAGIA;
    s_guardado_variante = (uint32_t)nueva;
    printf("\nCambio a la variante %c: %s. Se reinicia la placa.\n", nueva, nombre_variante(nueva));
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100));  /* que salga el mensaje */
    esp_restart();
}

static void mostrar_ayuda(void)
{
    printf("\n=== SEyTR - planificacion de tareas ===\n");
    printf("Variante actual: %c, %s\n", s_variante, nombre_variante(s_variante));
    printf("Teclas (sin Intro). Cambiar de variante reinicia la placa.\n");
    printf("  U3-S3:  1 turnos      2 cesion      3 expulsion\n");
    printf("          4 inanicion   5 turnos sin afinidad   6 inanicion sin afinidad\n");
    printf("  U3-S5:  7 dos periodicas   8 vuelta larga   9 temporizadores\n");
    printf("  c  en 7: pone o quita carga a R (4 ms de trabajo); en 9: activa o desactiva la carga\n");
    printf("  p  en 7: intercambia las prioridades de R y L\n");
    printf("  z  pone las medidas a cero      h  esta ayuda\n");
    printf("Tick a %d Hz.\n\n", configTICK_RATE_HZ);
}

/* Lee una tecla, si ha llegado alguna, y la atiende. No espera: sin el
 * controlador de la UART instalado, read() sobre la consola devuelve -1
 * enseguida si no ha llegado nada. */
static void atender_teclado(void)
{
    char tecla;
    if (read(STDIN_FILENO, &tecla, 1) != 1) {
        return;
    }
    if (es_variante(tecla)) {
        if (tecla != s_variante) {
            cambiar_variante(tecla);
        }
        return;
    }
    if (!es_de_u3s3(s_variante) && periodicas_tecla(tecla)) {
        return;
    }
    switch (tecla) {
    case 'z':
        nueva_generacion();
        printf("\nMedidas a cero.\n");
        break;
    case 'h': case '?':
        mostrar_ayuda();
        break;
    default:
        break;
    }
}

#define ESPERA_ANTES_DE_CREAR_MS  200

static void tarea_teclado(void *argumento)
{
    (void)argumento;
    /* Las tareas de la prueba las crea esta tarea, que tiene más prioridad que
     * todas ellas: así ninguna puede quitarle el procesador a mitad de la
     * creación, ni siquiera la que no se bloquea nunca. Cada tarea creada,
     * eso sí, puede empezar enseguida en el otro núcleo si lo encuentra
     * libre; por eso, en las variantes de inanición, se crea primero la que
     * no se bloquea.
     *
     * Antes de crearlas espera a que termine la tarea main, que va en el
     * núcleo 0 con prioridad 1 y, al volver de app_main, todavía escribe una
     * línea en el monitor. Si una tarea de la prueba que no se bloquea la
     * expulsara a mitad de esa línea, main se quedaría con la consola tomada
     * para siempre y ninguna otra tarea podría escribir: es lo que pasó en las
     * variantes 4 y 6 en la prueba en placa. Compartir recursos
     * entre tareas es materia de la U4. */
    vTaskDelay(pdMS_TO_TICKS(ESPERA_ANTES_DE_CREAR_MS));
    if (es_de_u3s3(s_variante)) {
        turnos_crear(s_variante);
    } else {
        periodicas_crear(s_variante);
    }
    for (;;) {
        atender_teclado();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void tarea_informe(void *argumento)
{
    (void)argumento;
    if (es_de_u3s3(s_variante)) {
        turnos_informe();      /* no vuelve */
    } else {
        periodicas_informe();  /* no vuelve */
    }
}

void app_main(void)
{
    cargar_variante();

    ESP_LOGI(TAG, "SEyTR - planificacion de tareas. Tick a %d Hz", configTICK_RATE_HZ);
    if (configTICK_RATE_HZ != 1000) {
        ESP_LOGW(TAG, "el tick no esta a 1000 Hz, que es el valor del curso");
    }
    mostrar_ayuda();

    bool ok = xTaskCreatePinnedToCore(tarea_informe, "informe", 4096, NULL,
                                      PRIORIDAD_INFORME, NULL, NUCLEO_OBSERVACION) == pdPASS;
    ok = ok && xTaskCreatePinnedToCore(tarea_teclado, "teclado", 4096, NULL,
                                       PRIORIDAD_TECLADO, NULL, NUCLEO_OBSERVACION) == pdPASS;
    if (!ok) {
        ESP_LOGE(TAG, "no hay memoria para las tareas de observacion");
    }
    /* app_main termina aquí; las tareas siguen. */
}
