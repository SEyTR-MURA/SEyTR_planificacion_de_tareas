/* comun.c - piezas que comparten las variantes del ejemplo. */

#include "comun.h"

#include <inttypes.h>
#include <stdio.h>

#include "esp_timer.h"

void trabajar_us(int64_t us)
{
    int64_t hecho = 0;
    int64_t previo = esp_timer_get_time();
    while (hecho < us) {
        const int64_t ahora = esp_timer_get_time();
        const int64_t hueco = ahora - previo;
        if (hueco <= HUECO_US) {
            hecho += hueco;  /* tiempo propio */
        }                    /* un hueco mayor es tiempo de otra tarea */
        previo = ahora;
    }
}

static volatile uint32_t s_generacion = 0;

uint32_t generacion(void)
{
    return s_generacion;
}

void nueva_generacion(void)
{
    s_generacion = s_generacion + 1;
}

void origen_tomar(origen_t *o)
{
    const TickType_t antes = xTaskGetTickCount();
    TickType_t ahora;
    do {
        ahora = xTaskGetTickCount();
    } while (ahora == antes);
    o->us = esp_timer_get_time();
    o->tick = ahora;
}

int64_t origen_activacion_us(origen_t *o, TickType_t tick, int64_t despertar)
{
    const TickType_t transcurridos = (TickType_t)(tick - o->tick);  /* admite la vuelta */
    const int64_t activacion = o->us + (int64_t)transcurridos * (1000000 / configTICK_RATE_HZ);
    if (despertar < activacion) {
        o->us -= activacion - despertar;
        return despertar;
    }
    return activacion;
}

void escribir_ms(char *destino, size_t tam, uint32_t us)
{
    snprintf(destino, tam, "%" PRIu32 ",%03" PRIu32 " ms", us / 1000, us % 1000);
}

void estadistica_anotar(estadistica_t *e, uint32_t us)
{
    const uint32_t g = generacion();
    if (g != e->generacion_vista) {
        /* Primero a cero y después la marca: hasta que la marca cambia, el
         * informe da la estadística por vacía. */
        e->n = 0;
        e->suma_us = 0;
        e->min_us = UINT32_MAX;
        e->max_us = 0;
        e->generacion_vista = g;
    }
    if (e->n == 0 || us < e->min_us) {
        e->min_us = us;
    }
    if (us > e->max_us) {
        e->max_us = us;
    }
    e->suma_us = e->suma_us + us;
    e->n = e->n + 1;
}

void estadistica_escribir(const char *etiqueta, const estadistica_t *e)
{
    /* Si se ha pedido la puesta a cero y la tarea que escribe todavía no ha
     * anotado nada desde entonces, lo que hay es de antes. */
    const uint32_t n = (e->generacion_vista == generacion()) ? e->n : 0;
    if (n == 0) {
        printf("  %-28s sin muestras\n", etiqueta);
        return;
    }
    char medio[24], minimo[24], maximo[24];
    escribir_ms(medio, sizeof medio, (uint32_t)(e->suma_us / n));
    escribir_ms(minimo, sizeof minimo, e->min_us);
    escribir_ms(maximo, sizeof maximo, e->max_us);
    printf("  %-28s medio %s  min %s  max %s  (%" PRIu32 " muestras)\n",
           etiqueta, medio, minimo, maximo, n);
}
