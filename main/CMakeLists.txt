# CMakeLists.txt de componente. Registra las fuentes del programa.
# Si anades un fichero .c, anadelo tambien a SRCS.
# PRIV_REQUIRES dice de que componentes depende: esp_timer, que da el reloj en
# microsegundos con el que se mide todo y las alarmas de alta resolucion; y
# esp_system, que da el reinicio con el que se cambia de variante.
idf_component_register(SRCS "planificacion_de_tareas.c" "comun.c" "turnos.c" "periodicas.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_timer esp_system)
