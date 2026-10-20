# SEyTR · Planificación de tareas

Proyecto de ejemplo de **Sistemas Empotrados y de Tiempo Real**. Es el de las
sesiones **U3-S3**, el planificador, y **U3-S5**, prioridades y tiempo. Pone en
marcha unas tareas de prueba cuyo comportamiento enseña cómo elige el
planificador de FreeRTOS entre las tareas listas y qué consecuencias tienen las
prioridades y la forma de medir el tiempo. Cada 2 o 2,5 s escribe un informe
con lo medido.

No hace falta cuenta ni permisos para clonarlo: el repositorio es público.

## Qué necesitas

El entorno instalado y verificado con la guía de instalación de la asignatura,
y la placa con su cable USB de datos, conectado al conector **USB-a-UART** de
la placa, que es por donde llegan las teclas al programa. **No hay montaje**:
todo se ve en el monitor.

## Cómo obtenerlo

Clona el repositorio en una carpeta de tu equipo con una ruta **corta y sin
espacios ni tildes**, y abre en el editor la carpeta que contiene el
`CMakeLists.txt` de la raíz. Compila, graba y abre el monitor desde la
extensión de ESP-IDF, como en las sesiones anteriores. Si prefieres la línea
de órdenes, `idf.py build flash monitor` hace lo mismo.

## Cómo está organizado

Las tareas de la prueba van en el **núcleo 0**, salvo en las variantes sin
afinidad. La tarea del informe y la del teclado van fijadas al **núcleo 1**,
con prioridad más alta que cualquier tarea de la prueba, para que observar no
altere lo que se observa y para que sigan respondiendo aunque el núcleo 0 quede
ocupado.

El trabajo de las tareas se simula con esperas activas, de dos formas:

- **Las ráfagas de cálculo** de las variantes 1, 2, 4, 5 y 6 duran hasta un
  instante del reloj: la tarea calcula hasta que han pasado 2 s desde que
  empezó la ráfaga, se ejecute o no en ese tiempo.
- **El trabajo de las tareas periódicas** de las variantes 3, 7, 8 y 9 cuenta
  solo el tiempo en que la tarea se ejecuta: si otra tarea pasa delante a
  mitad, el trabajo se alarga lo que tarde esa otra, como pasaría con un
  cálculo de verdad.

## Cómo se usa

Al arrancar, el programa imprime la lista de teclas y empieza con la variante
1. Las teclas se pulsan sin Intro. **Cambiar de variante reinicia la placa**,
que arranca con la variante nueva.

| Tecla | Qué hace |
| --- | --- |
| `1` | Turnos: dos tareas de cálculo, A y B, de igual prioridad en el núcleo 0 |
| `2` | Cesión: las mismas, cediendo el control con `taskYIELD()` |
| `3` | Expulsión: una tarea de cálculo de prioridad 2 y una periódica de prioridad 5 |
| `4` | Inanición: la tarea de prioridad 4 no se bloquea nunca |
| `5` | Turnos sin afinidad: la variante 1 sin fijar el núcleo |
| `6` | Inanición sin afinidad: la variante 4 sin fijar el núcleo |
| `7` | Dos periódicas: R, de 10 ms, y L, de 50 ms |
| `8` | Vuelta larga: una vuelta de L de cada cinco dura más que su periodo |
| `9` | Temporizadores: temporizador software frente a alarma de `esp_timer` |
| `c` | En la 7, pone o quita carga a R; en la 9, activa o desactiva la tarea de carga |
| `p` | En la 7, intercambia en ejecución las prioridades de R y L |
| `z` | Pone las medidas a cero |
| `h` | Vuelve a imprimir la lista |

## Las variantes de U3-S3

Las dos tareas de cálculo, A y B, trabajan por **ráfagas**: 2 s de cálculo en
cada ciclo de 2,5 s, y el resto bloqueadas. Así la tarea inactiva de su núcleo
se ejecuta dentro del plazo del *watchdog* de tareas, que es de 5 s, y el aviso
solo aparece en las variantes de inanición. Ceder con `taskYIELD()` no
bastaría, porque no da paso a la tarea inactiva, que tiene prioridad 0.

**Tramos de ejecución y turnos.** El informe mide **tramos de ejecución**:
el tiempo que una tarea se ejecuta seguida cada vez. No es lo mismo que un
turno, que es el tiempo máximo que el planificador deja a una tarea antes de
pasar a la siguiente lista de su prioridad, un periodo del tick. Una tarea que
cede el control, o a la que expulsa otra más prioritaria, se ejecuta menos que
un turno.

**Cómo se mide un tramo.** Cada tarea de cálculo lee el reloj en un bucle muy
corto. Si entre dos lecturas seguidas pasan más de 50 µs, es que entretanto se
ha ejecutado otra cosa: ahí acaba un tramo y empieza otro. También acaba si la
tarea aparece en otro núcleo. El tiempo de procesador de la tarea es la suma de
los intervalos cortos. El informe da, para cada tarea, el tiempo de procesador
de los últimos 2,5 s, cuántos tramos, su duración media y el tramo más largo
desde la última puesta a cero; en las variantes sin afinidad, también los
cambios de núcleo del ciclo.

**Cómo se mide la periódica de la variante 3.** El **retardo de activación**
va desde el instante en que debía activarse, que es el comienzo de un tick,
hasta que empieza a ejecutarse. La **respuesta** va desde ese mismo instante
hasta que termina su trabajo.

Las tareas de cálculo no usan `float`: en el ESP32-S3, una tarea que usa la
unidad de coma flotante queda fijada al núcleo en que esté, y las variantes sin
afinidad dejarían de serlo.

## Las variantes de U3-S5

- **Dos periódicas.** R tiene un periodo de 10 ms y 2 ms de trabajo, y L, un
  periodo de 50 ms y 15 ms de trabajo. Las dos se activan por primera vez en
  el mismo tick. Empieza con R más prioritaria (5) que L (4), porque su periodo
  es más corto. Con `c`, R pasa a 4 ms de trabajo, y con otra pulsación vuelve
  a 2. Con `p`, se intercambian las prioridades con `vTaskPrioritySet()`.
- **Vuelta larga.** Como la anterior, pero una de cada cinco vueltas de L dura
  70 ms, más que su periodo. Se cuentan las veces que `xTaskDelayUntil()`
  devuelve `pdFALSE` porque el siguiente instante ya había pasado: son las
  activaciones tardías.
- **Temporizadores.** Un temporizador software de FreeRTOS y una alarma
  periódica de `esp_timer`, los dos de 10 ms. Con `c` se activa una tarea de
  carga de prioridad 5 que calcula 30 ms y se bloquea 20 ms; con otra
  pulsación se desactiva.

**Cómo se mide.** El periodo es el tiempo entre dos activaciones seguidas,
leído con `esp_timer_get_time()` nada más despertar; el *jitter* es la
diferencia entre el máximo y el mínimo. El retardo de activación y la
respuesta se miden como en la variante 3, desde el instante en que la tarea
debía activarse: el tiempo que pasa lista, esperando a otra más prioritaria,
cuenta. Ese instante se calcula a partir de la cuenta de ticks; en el ESP32-S3
el tick y `esp_timer` salen del mismo temporizador del sistema. Las trazas se
escriben en el informe, y no dentro del bucle medido, para que la medida no
incluya lo que tarda en escribirse.

**La tarea de servicio de los temporizadores** se fija al núcleo 0 en
`sdkconfig.defaults`, con la opción `CONFIG_FREERTOS_TIMER_TASK_AFFINITY_CPU0`.
En la versión del SDK del curso, la configuración la crea por omisión sin
afinidad y con prioridad 1 (`Kconfig` de FreeRTOS); la guía de optimización de
la velocidad dice que va fijada al núcleo 0, pero no es lo que hace la
configuración. Sin afinidad se ejecutaría en el núcleo 1, que está casi libre,
y la carga no la retrasaría. La tarea de `esp_timer` va en el núcleo 0, con
prioridad 22.

## Qué deberías ver

Para cada variante se dan las cifras calculadas con la configuración de
partida, tick a 1000 Hz, y, después de «Medido:», las obtenidas en una placa del
curso. Tus cifras pueden variar algo.

- **`1`, turnos.** A y B se reparten la ráfaga: cada una, unos 1000 ms de
  procesador por ciclo. Cada una se ejecuta un turno entero cada vez: tramos de
  alrededor de un tick, 1 ms, y del orden de mil por tarea y ciclo. Medido:
  996 y 992 ms, 1001 y 1000 tramos de 0,99 ms de media; alguno llega a 2 ms.
- **`2`, cesión.** Cada tarea cede el control con `taskYIELD()` después de
  cada unidad de 170 µs de trabajo, así que sus tramos duran como mucho una
  unidad, mucho menos que un turno, y hay unas seis veces más que en la 1. Lo
  que no sale es el mismo reparto: una de las dos recibe algo más de tiempo
  que la otra, y la diferencia se repite igual en todos los ciclos. Las
  medidas encajan con esta explicación. Cada tick también provoca un cambio
  de tarea, porque hay otra lista de igual prioridad, y corta la unidad que
  esté a medias; desde ahí, las cesiones vuelven a turnarse hasta el tick
  siguiente. Como en un periodo del tick no cabe un número entero de parejas
  de unidades, el corte cae siempre en las unidades de la misma tarea, y esa
  recibe menos. La otra completa sus unidades enteras: tres por cada periodo
  del tick. Medido: A, 909 ms en 6668 tramos de 0,136 ms de media; B,
  1023 ms en 6000 tramos de 0,170 ms; las mismas cifras en todos los ciclos.
  Con una unidad de 200 µs el reparto salió más desigual, 1200 ms frente a
  800, y la tarea favorecida cambiaba de un ciclo a otro. La documentación de
  ESP-IDF lo advierte: el reparto entre tareas de igual prioridad es de «mejor
  esfuerzo» y no garantiza que reciban el mismo tiempo.
- **`3`, expulsión.** La periódica despierta cada 10 ms, con un periodo que
  apenas varía, y su retardo de activación es de microsegundos, porque pasa
  delante de A en cuanto despierta. Su respuesta es de 1 ms, su trabajo. A, en
  cambio, se ve expulsada cada 10 ms: tramos de unos 9 ms. Medido: periodo de
  10,000 ms, entre 9,997 y 10,001 ms; retardo de 3 a 6 µs; respuesta de
  1,006 ms; A, 1794 ms por ciclo en tramos de 8,9 ms de media.
- **`4`, inanición.** A no se ejecuta: «No se ha ejecutado». B ocupa el núcleo 0
  entero. A los 5 s aparece el aviso del *watchdog* de tareas: dice que la
  tarea inactiva del núcleo 0, `IDLE0`, no lo ha alimentado a tiempo y qué
  tarea ocupa cada núcleo, y lo repite mientras dure la situación. El programa
  sigue funcionando: el aviso no reinicia la placa. **El propio aviso se ve en
  la medida.** Se escribe desde una interrupción del núcleo 0, y mientras se
  escribe B está parada, del orden de decenas de milisegundos: en los ciclos en
  que salta, B aparece con algo menos de 2500 ms y un tramo cortado, y en los
  demás, «sin cambios de tarea». Su texto puede salir mezclado con el del
  informe, que se escribe desde el núcleo 1. Medido: el aviso, a los 5,5 s de
  arrancar, 5 s después de crearse B, y cada 5 s desde entonces, con
  `IDLE0 (CPU 0)`, `calculo_B` en el CPU 0 e `IDLE1` en el CPU 1; la placa no
  se reinicia. B, 2500,0 ms «sin cambios de tarea» en los ciclos sin aviso, y
  2469,8 ms en un tramo en los ciclos con aviso: pierde unos 30 ms cada vez.
  Su tramo más largo, 4999,4 ms, va de un aviso al siguiente.
- **`5`, turnos sin afinidad.** A y B ya no se turnan: cada una se ejecuta en un
  núcleo, y las dos reciben casi los 2000 ms de la ráfaga. Los tramos los corta
  sobre todo la tarea del informe, que ocupa el núcleo 1 mientras escribe; la
  tarea que estaba allí puede seguir en el núcleo 0 en el siguiente tick, y el
  informe cuenta los cambios de núcleo. Medido: 1989 ms cada una por ciclo, en
  unos doce tramos, con cero a dos cambios de núcleo.
- **`6`, inanición sin afinidad.** A recibe casi los 2000 ms de su ráfaga en el
  núcleo 1: la inanición de A desaparece. Pero B no se bloquea nunca. Como se
  crea primero, empieza en el núcleo 0, que está libre, y allí no la expulsa
  nadie: la tarea inactiva del núcleo 0 no corre, y el aviso del *watchdog*
  aparece igual, también con su efecto en la medida de B. Medido: A, 2000,0 ms
  por ciclo en un solo tramo, en el núcleo 1 y sin cambios de núcleo; B, en el
  núcleo 0, 2500,0 ms o 2468 ms según salte o no el aviso; el aviso, con
  `IDLE0 (CPU 0)`, `calculo_B` en el CPU 0 e `IDLE1` en el CPU 1, que en ese
  momento estaba entre dos ráfagas de A.
- **`7`, dos periódicas.** R mantiene su periodo de 10 ms y responde en unos
  2 ms. L mantiene su periodo de 50 ms, empieza unos 2 ms tarde, cuando R
  termina, y responde en unos 19 ms: sus 15 ms de trabajo más dos ejecuciones
  de R. Con `c`, R ocupa 4 de cada 10 ms y la respuesta de L sube a unos 27 ms;
  L sigue cumpliendo su periodo. Con `p`, R queda por debajo de L: cuando se
  activan a la vez, R espera los 15 ms de L. Su retardo de activación llega a
  15 ms, su respuesta a unos 17 ms, y su siguiente activación sale tardía: una
  cada 50 ms, unas veinte por segundo. Con `c` y `p` a la vez, la respuesta de R
  llega a unos 19 ms y salen dos tardías cada 50 ms. Medido: R, respuesta de
  2,009 ms y retardo de 5 µs; L, retardo de 2,023 ms y respuesta de 19,05 ms;
  con `c`, L responde en 27,09 ms; con `p`, retardo máximo de R de 15,03 ms,
  respuesta máxima de 17,04 ms y veinte tardías por segundo, y L responde en
  15,01 ms; con `c` y `p`, respuesta máxima de R de 19,04 ms y cuarenta
  tardías por segundo, y L responde en 15,01 ms.
- **`8`, vuelta larga.** La vuelta larga de L, con lo que ocupa R, termina unos
  88 ms después de su activación: ya han pasado su siguiente instante y, al
  terminar la vuelta normal que sigue, el otro. Son dos activaciones tardías
  por cada vuelta larga, unas ocho por segundo, con retardos de activación de
  hasta unos 38 ms. R no se entera. Medido: respuesta máxima de L de 88,3 ms,
  retardo máximo de 38,3 ms y dieciséis tardías por informe; R, sin tardías.
- **`9`, temporizadores.** Sin carga, los dos mantienen 10 ms. Con la carga
  activa, la función del temporizador software no se ejecuta mientras calcula
  la carga, porque la tarea de servicio tiene prioridad 1 y la carga 5: el
  intervalo largo va de la última llamada antes de que empiece la carga a la
  primera después de que se bloquee, entre 30 y 40 ms según la fase. Con este
  código, la carga empieza en el mismo tick en que vence el temporizador, y el
  intervalo largo debería ser de unos 40 ms. Lo siguen varios intervalos casi
  nulos con los que se recupera la cuenta, y la media sigue en 10 ms. La alarma
  de `esp_timer` no se entera, porque su tarea tiene prioridad 22. Medido: sin
  carga, el temporizador software entre 9,98 y 10,02 ms y la alarma entre 9,97
  y 10,00 ms; con carga, el temporizador software llega a 40,02 ms y baja a
  0,001 ms, con media de 10,000 ms, y la alarma sigue entre 9,999 y 10,001 ms.

**Con el tick a 100 Hz.** En la variante 1 los turnos, y con ellos los tramos,
pasan a durar unos 10 ms, y hay unos cien por tarea y ciclo. En la 2 los
tramos los sigue marcando la cesión, y los cortes del tick son diez veces menos
frecuentes. En las variantes 3 y 7 a 9 los periodos son múltiplos de 10 ms, y
periodos, retardos y respuestas no deberían cambiar apenas. Es una prueba de
U3-S5. Medido: en la 1, 101 tramos de 9,9 ms de media; en la 2, con la unidad
de 200 µs, el reparto salió igualado, 970 ms cada una en tramos de 0,198 ms;
en la 3, la periódica con periodo de 10,000 ms, retardo de 3 a 7 µs y
respuesta de 1,007 ms, y A con 1795 ms en tramos de 8,9 ms; en la 7 y la 9,
las mismas cifras que a 1000 Hz.

## Qué puedes cambiar sin tocar el código

El proyecto fija en `sdkconfig.defaults` el destino `esp32s3`, el tick a
1000 Hz y la tarea de servicio de los temporizadores en el núcleo 0. El
programa escribe al arrancar a qué frecuencia está el tick. Si en tu `sdkconfig`
hay otro valor fijado a mano, manda sobre `sdkconfig.defaults`: cámbialo en la
configuración o borra `sdkconfig` y vuelve a fijar el destino. Sin la afinidad
de la tarea de servicio, la variante 9 no enseña nada.

## Estructura

```
CMakeLists.txt              define el proyecto
sdkconfig.defaults          configuración de partida; se escribe a mano y se versiona
main/
  CMakeLists.txt            registra las fuentes del componente principal
  planificacion_de_tareas.c app_main, la elección de variante, el teclado y el informe
  turnos.c                  las variantes de U3-S3
  periodicas.c              las variantes de U3-S5
  comun.c, comun.h          el trabajo simulado, el instante de activación y las estadísticas
```

No se versionan el fichero `sdkconfig`, que genera la herramienta a partir de
`sdkconfig.defaults`, ni la carpeta `build/`.

Para cambiar de variante, el programa guarda la elegida en una variable
marcada con `__NOINIT_ATTR`, que conserva su valor tras un reinicio por
programa, y reinicia la placa.

## Fuentes

- Espressif, guía de ESP-IDF v6.1 para el ESP32-S3,
  [FreeRTOS (IDF)](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/system/freertos_idf.html):
  planificador en los dos núcleos, turnos y su reparto de «mejor esfuerzo»
  (*Best Effort Round Robin*), tareas inactivas y uso de la unidad de coma
  flotante.
- Espressif, guía de ESP-IDF v6.1,
  [ESP Timer](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/system/esp_timer.html):
  apartado sobre los temporizadores de FreeRTOS.
- Espressif, guía de ESP-IDF v6.1,
  [vigilantes (watchdogs)](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/system/wdts.html):
  comportamiento por omisión del *watchdog* de tareas.
- Espressif, guía de ESP-IDF v6.1,
  [optimización de la velocidad](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-guides/performance/speed.html):
  prioridad y núcleo de las tareas propias de ESP-IDF.
- Espressif, ESP-IDF v6.1,
  [`Kconfig` de FreeRTOS](https://github.com/espressif/esp-idf/blob/v6.1/components/freertos/Kconfig):
  afinidad y prioridad por omisión de la tarea de servicio de los temporizadores.
- Espressif, guía de ESP-IDF v6.1,
  [tipos de memoria](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-guides/memory-types.html):
  `__NOINIT_ATTR`.
- R. Barry, [*Mastering the FreeRTOS Real Time Kernel*](https://github.com/FreeRTOS/FreeRTOS-Kernel-Book),
  capítulos 4, gestión de tareas y algoritmos de planificación, y 6,
  temporizadores software.

## Un aviso

Este repositorio es material de la asignatura. El repositorio de tu **proyecto
final** es otro distinto, con su propio nombre y sus propias reglas.
