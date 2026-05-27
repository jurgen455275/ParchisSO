# Simulador de Parchís Simplificado en C11 para Linux

Este proyecto es un simulador simplificado de una partida de Parchís desarrollado en **C estándar (C11)** para sistemas **Linux / POSIX**. El objetivo primordial de esta aplicación no es ser un videojuego jugable ni gráfico, sino servir como demostración práctica e integral de múltiples conceptos avanzados de **Sistemas Operativos** y programación de sistemas concurrentes.

---

## ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
## CONCEPTOS DE SISTEMAS OPERATIVOS DEMOSTRADOS
## ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

El simulador implementa los siguientes mecanismos nativos de UNIX/Linux:

1. **Multiprocesamiento (`fork`, `waitpid`, `kill`)**: 
   El sistema se estructura jerárquicamente a nivel de procesos. El proceso padre (Árbitro/Scheduler) engendra $N$ procesos hijos, donde cada hijo representa a un jugador independiente en su propio espacio de memoria.
   
2. **Multihilo (`pthread_create`, `pthread_join`)**: 
   Cada proceso jugador bifurca su ejecución en exactamente 4 hilos de ejecución ligeros (`pthreads`), donde cada hilo representa y gestiona individualmente una de las 4 fichas asignadas a ese jugador.

3. **Memoria Compartida POSIX (`shm_open`, `mmap`)**: 
   Se crea una región de memoria compartida global estructurada bajo el tipo `Tablero`. Esta estructura permite que todos los procesos hijos accedan en tiempo real a las posiciones de todas las fichas del tablero y consulten variables de control comunes (turno actual, ganador).

4. **Sincronización Inter-Proceso (Mutex en Memoria Compartida)**: 
   Para evitar condiciones de carrera al acceder de forma concurrente a la memoria compartida, se utiliza un mutex de POSIX (`pthread_mutex_t`) inicializado con el atributo `PTHREAD_PROCESS_SHARED` para ser operativo entre límites de procesos.

5. **Sincronización Inter-Hilo (Variables de Condición y Mutex Locales)**: 
   Dentro del proceso de cada jugador, el hilo principal del proceso coordina a sus 4 hilos fichas utilizando variables de condición (`pthread_cond_t`) y un mutex local. Los hilos evalúan sus movimientos concurrentemente, informan sus prioridades y esperan a que el hilo principal seleccione la ficha ganadora para proceder con el movimiento.

6. **Sincronización de Turnos (Semáforos Nombrados POSIX)**: 
   Se utilizan semáforos nombrados (`sem_open`) con identificadores únicos basados en el PID del proceso padre (`/parchis_ready_<PID>_<ID>` y `/parchis_done_<PID>_<ID>`) para orquestar la comunicación Round Robin del Scheduler.
   
7. **IPC - Tuberías Unidireccionales (`pipe`)**: 
   Se utiliza una tubería por jugador para la comunicación unidireccional Padre $\rightarrow$ Hijo. El Árbitro genera el resultado del dado en su turno y lo escribe en el pipe del jugador activo. El proceso hijo lee este valor para iniciar su jugada.

8. **IPC - Cola de Mensajes POSIX (`mq_open`, `mq_send`, `mq_receive`)**: 
   Se utiliza una cola de mensajes global `/parchis_moves_<PID>` para la comunicación síncrona Hijos $\rightarrow$ Padre. El hilo ficha elegido realiza el movimiento en memoria compartida y reporta los detalles (`MsgMovimiento`) al padre mediante esta cola.
   
9. **Gestión de Señales y Limpieza Robusta (`sigaction`, `kill`)**: 
   Se instala un manejador de señales para `SIGINT` (Ctrl+C) y `SIGTERM` tanto en el padre como en los hijos. Esto garantiza la terminación limpia de hilos, procesos y, sobre todo, la desvinculación completa de recursos del sistema (`munmap`, `shm_unlink`, `sem_unlink`, `mq_unlink`) previniendo fugas de memoria y descriptores huérfanos.

---

## ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
## ARQUITECTURA GENERAL Y FLUJO DE TURNOS
## ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

```
       ┌────────────────────────────────────────────────────────┐
       │             Proceso Padre (Árbitro/Scheduler)           │
       └─────┬───────────────────┬───────────────────┬──────────┘
             │                   │                   │
      [sem_turno_listo]     [Escribe Dado]     [Lee Movimiento]
             │                   │                   │
             ▼                   ▼                   │
       ┌─────────────────────────┴───────────────────┼──────────┐
       │             Proceso Hijo (Jugador activo)   │          │
       │  ┌──────────────────────────────────────────┼───────┐  │
       │  │  Hilos Fichas (F1, F2, F3, F4)           ▼       │  │
       │  │  - Evalúan posiciones en Tablero (SHM)           │  │
       │  │  - Hilo elegido escribe en Tablero (SHM)         │  │
       │  │  - Hilo elegido envía reporte IPC ───────────────┘  │
       │  └──────────────────────────────────────────────────┘  │
       └─────────────────────────┬──────────────────────────────┘
                                 │
                          [sem_turno_done]
                                 │
                                 ▼
                         Siguiente Jugador
```

### Reglas de Movimiento Simplificadas
* **Tablero lineal**: Cada ficha progresa en un tablero desde la posición `0` (CASA) hasta la `69` (META).
* **Salida**: Una ficha solo puede salir de CASA (`0` $\rightarrow$ `1`) si el valor del dado es exactamente `5`.
* **Avance**: Las fichas en tablero avanzan el valor del dado. Si la suma supera la posición `68`, la ficha aterriza automáticamente en META (`69`).
* **Prioridades de Movimiento** (en orden descendente):
  1. Ficha que logre llegar **exactamente** a META (`69`).
  2. Ficha en tablero (`1` a `68`) con la **mayor posición actual** (desempate: menor índice de ficha).
  3. Ficha en CASA si el dado es `5` (desempate: menor índice de ficha).
  4. Si ninguna ficha cumple las condiciones, **el turno se pierde**.
* **Condición de Victoria**: El juego finaliza cuando el primer jugador consiga colocar sus 4 fichas en la META (`69`).

---

## ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
## ESTRUCTURA DEL PROYECTO
## ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

* **`parchis.h`**: Contiene la definición de constantes (`CASA`, `META`, etc.), las estructuras del juego, los límites de longitud de recursos, las definiciones de colores ANSI globales y las declaraciones de funciones.
* **`main.c`**: Contiene el punto de entrada, la inicialización del entorno (SHM, MQ, Semáforos, Pipes y Fork), el bucle Scheduler Round Robin, la rutina de limpieza completa de recursos y la captura de `SIGINT`.
* **`jugador.c`**: Implementa la vida del proceso jugador, la creación/espera de los 4 hilos fichas, la coordinación local mediante variables de condición y la resolución de prioridades de movimiento.
* **`tablero.c`**: Gestiona el ciclo de vida de la Memoria Compartida POSIX.
* **`sync.c`**: Inicializa el mutex compartido entre procesos y gestiona la creación/apertura de semáforos POSIX.
* **`ipc.c`**: Centraliza el uso de Pipes y de la Cola de Mensajes POSIX.
* **`display.c`**: Renderiza el tablero de juego en un formato de tabla ASCII premium utilizando colores ANSI.
* **`Makefile`**: Script de compilación automatizada bajo estándares estrictos de advertencias.

---

## ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
## COMPILACIÓN Y USO
## ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

### Requisitos del Sistema
Debido al uso de llamadas del sistema de POSIX estándar de Linux (como `mqueue.h`, `sys/mman.h`, `fork`, etc.), **este simulador debe ejecutarse en un entorno Linux nativo, en WSL (Windows Subsystem for Linux), o en un contenedor Docker**.

### 1. Compilación
Abre tu terminal y compila el proyecto con la herramienta `make`:
```bash
make
```
Esto compilará todos los archivos fuente bajo la bandera `-Wall -Wextra` y generará un ejecutable único llamado `parchis`.

### 2. Ejecución
El programa acepta como argumento opcional el número de jugadores (de 2 a 4). Si no se provee argumento, se asume **4** por defecto.

* **Ejecutar con 4 jugadores (por defecto)**:
  ```bash
  ./parchis
  ```
* **Ejecutar con 3 jugadores**:
  ```bash
  ./parchis 3
  ```
* **Ejecutar con 2 jugadores**:
  ```bash
  ./parchis 2
  ```

### 3. Validación de Argumentos
El programa cuenta con un parser robusto que impide la entrada de argumentos inválidos, desbordamientos de memoria o decimales:
```bash
./parchis 5      # Rechazado (límite superior excedido)
./parchis abc    # Rechazado (entrada no numérica)
./parchis 3abc   # Rechazado (entrada mixta corrupta)
```

### 4. Limpieza del Entorno
Para eliminar los archivos binarios y los archivos objeto intermedios compilados:
```bash
make clean
```

---

## ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
## ROBUSTEZ Y TRATAMIENTO DE ERRORES
## ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

* **Prevención de Bloqueos en Colas (Mensajes Dummy)**: 
  Si un jugador obtiene un dado con el cual **ninguna ficha puede moverse**, el bucle normal de hilos no enviaría mensajes a la Cola. Para evitar que el Árbitro quede bloqueado indefinidamente en `mq_receive()`, el proceso del jugador detecta esta condición y envía un mensaje especial (con ficha `-1` y posición `-1`). Esto garantiza la continuidad de la simulación de forma determinista y fluida.
* **Liberación de Recursos Residuales**:
  Si el simulador es interrumpido mediante **`Ctrl + C` (SIGINT)**, el proceso padre intercepta la señal, envía una señal de terminación a los hijos en cascada, espera que finalicen (`waitpid`), destruye los mutex compartidos, cierra los descriptores de archivo y elimina semáforos, memoria compartida y cola de mensajes del sistema de archivos interno de Linux.
