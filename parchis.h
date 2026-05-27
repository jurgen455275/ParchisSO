/**
 * @file parchis.h
 * @brief Estructuras, constantes y prototipos de funciones para el simulador de Parchís.
 * 
 * Este archivo define las estructuras y constantes compartidas entre el árbitro,
 * los jugadores y los hilos de fichas, así como los prototipos globales.
 */

#ifndef PARCHIS_H
#define PARCHIS_H

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <mqueue.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <time.h>

/* Constantes del juego */
#define MAX_JUGADORES 4
#define MAX_FICHAS    4
#define CASA          0
#define TABLERO_MAX   68
#define META          69

/* Estructura del Tablero Compartido */
typedef struct {
    pthread_mutex_t mutex;           /* Mutex compartido para proteger el acceso */
    int fichas[MAX_JUGADORES][MAX_FICHAS]; /* Posición de cada ficha (0-69) */
    int turno_actual;               /* Índice del jugador en turno (0 a N-1) */
    int jugadores_activos;          /* Número de jugadores en juego */
    int ganador;                    /* Índice del jugador ganador, -1 si no hay */
} Tablero;

/* Estructura para el IPC de cola de mensajes (hilos -> padre) */
typedef struct {
    int jugador;        /* Índice del jugador (0 a N-1) */
    int ficha;          /* Índice de la ficha (0 a 3, o -1 si no pudo mover) */
    int posicion_nueva; /* Nueva posición de la ficha, -1 si no hubo cambio */
    int dado;           /* Valor del dado en el turno */
} MsgMovimiento;

/* Estructura para la evaluación de movimiento de cada ficha */
typedef struct {
    int valid;         /* 1 si el movimiento es válido, 0 si no */
    int priority;      /* Nivel de prioridad (3: meta exacta, 2: tablero, 1: salir de casa, 0: inválido) */
    int current_pos;   /* Posición antes de mover */
    int target_pos;    /* Posición después de mover */
} EvalFicha;

/* Estructura interna para coordinar los hilos de un jugador */
typedef struct {
    int dado;
    int current_turn_id;
    int last_processed_turn_id;
    int threads_ready;
    int chosen_ficha;       /* Ficha elegida para mover (0-3), -1 si ninguna, -2 si pendiente */
    int movement_done;      /* Bandera para indicar que el movimiento terminó */
    pthread_mutex_t mutex;  /* Mutex local del proceso jugador */
    pthread_cond_t cond_start;  /* Condición para iniciar evaluación */
    pthread_cond_t cond_eval;   /* Condición para indicar que todas las evaluaciones terminaron */
    pthread_cond_t cond_choice; /* Condición para indicar la elección final de movimiento */
    pthread_cond_t cond_done;   /* Condición para indicar fin del movimiento por parte del hilo */
    EvalFicha eval_results[MAX_FICHAS];
} TurnCoord;

/* Límites de longitud de nombres de recursos */
#define SHM_NAME_LEN  64
#define MQ_NAME_LEN   64
#define SEM_NAME_LEN  64

/* Códigos de colores ANSI para una visualización premium */
#define ANSI_COLOR_RED     "\033[1;31m"
#define ANSI_COLOR_GREEN   "\033[1;32m"
#define ANSI_COLOR_YELLOW  "\033[1;33m"
#define ANSI_COLOR_BLUE    "\033[1;34m"
#define ANSI_COLOR_CYAN    "\033[1;36m"
#define ANSI_COLOR_GRAY    "\033[0;37m"
#define ANSI_RESET         "\033[0m"

/* Nombres únicos de recursos IPC */
#define SHM_NAME_FMT "/parchis_shm_%d"
#define MQ_NAME_FMT "/parchis_moves_%d"
#define SEM_READY_FMT "/parchis_ready_%d_%d"
#define SEM_DONE_FMT "/parchis_done_%d_%d"

/* --- PROTOTIPOS DE FUNCIONES --- */

/* Funciones de Memoria Compartida (tablero.c) */
Tablero* crear_tablero_compartido(const char* name, int* shm_fd);
Tablero* conectar_tablero_compartido(const char* name, int* shm_fd);
void desconectar_tablero_compartido(Tablero* tablero, int shm_fd);
void eliminar_tablero_compartido(const char* name);

/* Funciones de Sincronización (sync.c) */
int inicializar_mutex_compartido(pthread_mutex_t* mutex);
sem_t* crear_semaforo(const char* fmt, int pid, int id);
sem_t* abrir_semaforo(const char* fmt, int pid, int id);
void cerrar_semaforo(sem_t* sem);
void eliminar_semaforo(const char* fmt, int pid, int id);

/* Funciones de IPC (ipc.c) */
int crear_pipe(int pipefd[2]);
mqd_t crear_cola_mensajes(const char* name);
mqd_t abrir_cola_mensajes(const char* name);
void cerrar_cola_mensajes(mqd_t mq);
void eliminar_cola_mensajes(const char* name);
int enviar_mensaje_movimiento(mqd_t mq, const MsgMovimiento* msg);
int recibir_mensaje_movimiento(mqd_t mq, MsgMovimiento* msg);

/* Funciones de Visualización (display.c) */
void imprimir_tablero(const Tablero* tablero, int num_jugadores, int turno_n);
void imprimir_mensaje_movimiento(const MsgMovimiento* msg, int pid);

/* Funciones del Proceso Jugador (jugador.c) */
void run_jugador(int jugador_id, int pipe_read_fd, int parent_pid, int num_jugadores);

#endif /* PARCHIS_H */
