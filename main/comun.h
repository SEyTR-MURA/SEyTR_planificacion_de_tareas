/* comun.h - piezas que comparten las variantes del ejemplo.
 *
 * Las tareas de la prueba se ejecutan en el núcleo 0 del procesador, salvo en
 * las variantes sin afinidad. Las que observan (el informe y el teclado) van
 * fijadas al núcleo 1 y con prioridad más alta, para que observar no altere lo
 * que se observa.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define NUCLEO_PRUEBA       0
#define NUCLEO_OBSERVACION  1

/* Por encima de todas las tareas de la prueba y por debajo de las del
 * sistema que importan (la de esp_timer tiene 22). */
#define PRIORIDAD_INFORME   10
#define PRIORIDAD_TECLADO   9

#define PILA_TAREA          3072  /* en bytes, como pide ESP-IDF */

/* Simula un trabajo de cálculo de «us» microsegundos de procesador, con una
 * espera activa que no se bloquea. Cuenta solo el tiempo en que la tarea se
 * ejecuta: si otra tarea pasa delante a mitad, el trabajo se alarga lo que
 * tarde esa otra, como pasaría con un cálculo de verdad. Para saberlo, lee el
 * reloj en un bucle corto y no cuenta los huecos de más de HUECO_US, que son
 * el tiempo en que se ejecutó otra cosa. */
#define HUECO_US 50
void trabajar_us(int64_t us);

/* Puesta a cero de las medidas. La pide la tecla «z»; cada tarea pone a cero
 * sus propias medidas cuando ve que la generación ha cambiado, de modo que
 * cada medida la escribe una sola tarea. */
uint32_t generacion(void);
void     nueva_generacion(void);

/* Relación entre la cuenta de ticks y el reloj de esp_timer, para saber en
 * qué microsegundo empezó un tick y medir desde el instante en que una tarea
 * periódica debía activarse, y no desde que consigue el procesador. En el
 * ESP32-S3 el tick y esp_timer salen del mismo temporizador del sistema, así
 * que no se separan con el tiempo. */
typedef struct {
    TickType_t tick;
    int64_t    us;
} origen_t;

/* Espera, con una espera activa de menos de un tick, a que empiece el
 * siguiente tick, y anota su número y el microsegundo en que empezó. */
void origen_tomar(origen_t *o);
/* Microsegundo en que empezó el tick «tick», en que debía activarse una tarea
 * que acaba de despertar en el microsegundo «despertar». Una tarea no puede
 * despertar antes de su instante: si el cálculo lo da, el origen iba
 * adelantado y se corrige. En la placa no debería ocurrir más que por unos
 * microsegundos; en el emulador, el tick y el reloj no van a la par y la
 * corrección lo compensa en parte. */
int64_t origen_activacion_us(origen_t *o, TickType_t tick, int64_t despertar);

/* Escribe una duración en microsegundos como milisegundos con tres
 * decimales y coma decimal, por ejemplo «10,002 ms». */
void escribir_ms(char *destino, size_t tam, uint32_t us);

/* Estadística de intervalos o duraciones, en microsegundos. La escribe una
 * sola tarea y la lee la del informe. Los campos de 32 bits se leen y se
 * escriben de una vez en este procesador; la suma es de 64 bits, para que no
 * se desborde, y su lectura puede pillar a medias una escritura. Además, dos
 * campos leídos seguidos pueden corresponder a muestras distintas. Para un
 * informe basta; cómo compartir datos entre tareas sin ese problema es materia
 * de la U4. */
typedef struct {
    volatile uint32_t n;
    volatile uint64_t suma_us;
    volatile uint32_t min_us;
    volatile uint32_t max_us;
    volatile uint32_t generacion_vista;  /* la escribe la misma tarea */
} estadistica_t;

void estadistica_anotar(estadistica_t *e, uint32_t us);
/* Escribe una línea «etiqueta: medio, mín y máx». */
void estadistica_escribir(const char *etiqueta, const estadistica_t *e);

/* Variantes de U3-S3: turnos, cesión, expulsión, inanición y dos núcleos. */
void turnos_crear(char variante);
void turnos_informe(void);

/* Variantes de U3-S5: dos periódicas, vuelta larga y temporizadores. */
void periodicas_crear(char variante);
void periodicas_informe(void);
/* Atiende las teclas propias de estas variantes. Devuelve true si la tecla
 * era suya. */
bool periodicas_tecla(char tecla);
