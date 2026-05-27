/**
 * @file main.c
 * @brief Proceso principal (Scheduler / Árbitro) del simulador de Parchís.
 */

#include "parchis.h"

/* Bandera de ejecución global del juego */
static volatile sig_atomic_t game_running = 1;

/* Descriptores de recursos globales para la limpieza en caso de señal */
static int shm_fd = -1;
static Tablero* tablero = NULL;
static mqd_t mq = (mqd_t)-1;
static sem_t* sem_ready[MAX_JUGADORES] = {NULL};
static sem_t* sem_done[MAX_JUGADORES] = {NULL};
static int pipes[MAX_JUGADORES][2];
static pid_t child_pids[MAX_JUGADORES] = {0};
static int num_jugadores = MAX_JUGADORES;
static char shm_name[SHM_NAME_LEN];
static char mq_name[MQ_NAME_LEN];


/**
 * @brief Realiza la limpieza total de los recursos del sistema (SHM, Semáforos, MQ, Pipes y Procesos).
 */
void realizar_limpieza(void) {
    int pid_padre = getpid();
    printf("\n  " ANSI_COLOR_YELLOW "[Árbitro] Iniciando limpieza de recursos..." ANSI_RESET "\n");

    /* 1. Terminar todos los procesos hijos activos */
    for (int i = 0; i < num_jugadores; i++) {
        if (child_pids[i] > 0) {
            /* Enviar señal de terminación */
            kill(child_pids[i], SIGTERM);
            /* Esperar al hijo */
            int status;
            waitpid(child_pids[i], &status, 0);
            printf("  " ANSI_COLOR_GRAY "» Proceso hijo J%d (PID: %d) terminado." ANSI_RESET "\n", i + 1, child_pids[i]);
            child_pids[i] = 0;
        }
    }

    /* 2. Destruir mutex compartido si el tablero está disponible */
    if (tablero != NULL && tablero != MAP_FAILED) {
        pthread_mutex_destroy(&tablero->mutex);
        desconectar_tablero_compartido(tablero, shm_fd);
        tablero = NULL;
    }

    /* 3. Eliminar la memoria compartida */
    if (shm_fd != -1) {
        eliminar_tablero_compartido(shm_name);
        shm_fd = -1;
    }

    /* 4. Cerrar y desvincular semáforos nombrados */
    for (int i = 0; i < num_jugadores; i++) {
        if (sem_ready[i] != NULL && sem_ready[i] != SEM_FAILED) {
            cerrar_semaforo(sem_ready[i]);
            sem_ready[i] = NULL;
        }
        eliminar_semaforo(SEM_READY_FMT, pid_padre, i);

        if (sem_done[i] != NULL && sem_done[i] != SEM_FAILED) {
            cerrar_semaforo(sem_done[i]);
            sem_done[i] = NULL;
        }
        eliminar_semaforo(SEM_DONE_FMT, pid_padre, i);
    }

    /* 5. Cerrar y desvincular cola de mensajes */
    if (mq != (mqd_t)-1) {
        cerrar_cola_mensajes(mq);
        mq = (mqd_t)-1;
    }
    eliminar_cola_mensajes(mq_name);

    /* 6. Cerrar todos los pipes */
    for (int i = 0; i < num_jugadores; i++) {
        if (pipes[i][1] != -1) {
            close(pipes[i][1]);
            pipes[i][1] = -1;
        }
    }

    printf("  " ANSI_COLOR_GREEN "[Árbitro] Limpieza completada con éxito." ANSI_RESET "\n\n");
}

/**
 * @brief Manejador de señal SIGINT (Ctrl+C).
 * @param sig Código de la señal.
 */
void handle_sigint(int sig) {
    (void)sig;
    game_running = 0;
}

int main(int argc, char* argv[]) {
    /* 1. Validar argumentos */
    if (argc > 1) {
        char* endptr;
        long val = strtol(argv[1], &endptr, 10);
        
        /* Validar que sea un número válido, sin caracteres adicionales, y en el rango correcto */
        if (*endptr != '\0' || argv[1] == endptr || val < 2 || val > 4) {
            fprintf(stderr, ANSI_COLOR_RED "Error: Número de jugadores inválido (%s)." ANSI_RESET "\n", argv[1]);
            fprintf(stderr, "Debe ser un número entero entre 2 y 4.\n");
            fprintf(stderr, "Uso: %s [num_jugadores (2-4)]\n", argv[0]);
            exit(EXIT_FAILURE);
        }
        num_jugadores = (int)val;
    }

    printf("\n  " ANSI_COLOR_CYAN "=================================================" ANSI_RESET "\n");
    printf("  " ANSI_COLOR_CYAN "║       SIMULADOR DE PARCHÍS PARA LINUX         ║" ANSI_RESET "\n");
    printf("  " ANSI_COLOR_CYAN "║     Procesos, Hilos, Sincronización e IPC     ║" ANSI_RESET "\n");
    printf("  " ANSI_COLOR_CYAN "=================================================" ANSI_RESET "\n");
    printf("  " ANSI_COLOR_GREEN "» Número de jugadores: %d" ANSI_RESET "\n", num_jugadores);
    printf("  " ANSI_COLOR_GREEN "» PID del Árbitro: %d" ANSI_RESET "\n\n", getpid());

    /* Inicializar semilla de aleatoriedad del árbitro */
    srand((unsigned int)time(NULL));

    /* Configurar manejador de señales SIGINT */
    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("Error al registrar manejador de señales para SIGINT");
        exit(EXIT_FAILURE);
    }

    int parent_pid = getpid();

    /* 2. Crear memoria compartida para el tablero */
    snprintf(shm_name, sizeof(shm_name), SHM_NAME_FMT, parent_pid);
    tablero = crear_tablero_compartido(shm_name, &shm_fd);
    if (!tablero) {
        fprintf(stderr, ANSI_COLOR_RED "Error crítico: No se pudo crear el tablero en memoria compartida." ANSI_RESET "\n");
        exit(EXIT_FAILURE);
    }

    /* Inicializar mutex compartido */
    if (inicializar_mutex_compartido(&tablero->mutex) == -1) {
        fprintf(stderr, ANSI_COLOR_RED "Error crítico: No se pudo inicializar el mutex compartido." ANSI_RESET "\n");
        eliminar_tablero_compartido(shm_name);
        exit(EXIT_FAILURE);
    }

    tablero->jugadores_activos = num_jugadores;

    /* 3. Crear cola de mensajes */
    snprintf(mq_name, sizeof(mq_name), MQ_NAME_FMT, parent_pid);
    mq = crear_cola_mensajes(mq_name);
    if (mq == (mqd_t)-1) {
        fprintf(stderr, ANSI_COLOR_RED "Error crítico: No se pudo crear la cola de mensajes." ANSI_RESET "\n");
        desconectar_tablero_compartido(tablero, shm_fd);
        eliminar_tablero_compartido(shm_name);
        exit(EXIT_FAILURE);
    }

    /* 4. Crear semáforos nombrados y pipes para cada jugador */
    for (int i = 0; i < num_jugadores; i++) {
        sem_ready[i] = crear_semaforo(SEM_READY_FMT, parent_pid, i);
        sem_done[i] = crear_semaforo(SEM_DONE_FMT, parent_pid, i);
        
        if (sem_ready[i] == SEM_FAILED || sem_done[i] == SEM_FAILED) {
            fprintf(stderr, ANSI_COLOR_RED "Error crítico: No se pudieron crear los semáforos del jugador J%d." ANSI_RESET "\n", i + 1);
            realizar_limpieza();
            exit(EXIT_FAILURE);
        }

        pipes[i][0] = -1;
        pipes[i][1] = -1;
        if (crear_pipe(pipes[i]) == -1) {
            fprintf(stderr, ANSI_COLOR_RED "Error crítico: No se pudo crear el pipe del jugador J%d." ANSI_RESET "\n", i + 1);
            realizar_limpieza();
            exit(EXIT_FAILURE);
        }
    }

    /* 5. Lanzar los procesos hijos (Jugadores) con fork() */
    for (int i = 0; i < num_jugadores; i++) {
        pid_t pid = fork();
        
        if (pid < 0) {
            perror("Error al realizar fork");
            realizar_limpieza();
            exit(EXIT_FAILURE);
        } else if (pid == 0) {
            /* --- PROCESO HIJO (JUGADOR) --- */
            /* Cerrar extremos no usados de los pipes */
            for (int k = 0; k < num_jugadores; k++) {
                if (k == i) {
                    close(pipes[k][1]); /* Cierra escritura en su propio pipe */
                } else {
                    close(pipes[k][0]); /* Cierra todo lo de otros jugadores */
                    close(pipes[k][1]);
                }
            }
            
            /* Ejecutar lógica del jugador */
            run_jugador(i, pipes[i][0], parent_pid, num_jugadores);
            exit(EXIT_SUCCESS); /* Por seguridad */
        } else {
            /* --- PROCESO PADRE (ÁRBITRO) --- */
            child_pids[i] = pid;
            close(pipes[i][0]); /* Cierra extremo de lectura en el padre */
            printf("  " ANSI_COLOR_GREEN "» Proceso Jugador J%d (PID: %d) creado correctamente." ANSI_RESET "\n", i + 1, pid);
        }
    }

    printf("\n  " ANSI_COLOR_YELLOW "¡EL SIMULADOR DE PARCHÍS HA COMENZADO!" ANSI_RESET "\n");
    imprimir_tablero(tablero, num_jugadores, 0);

    /* 6. Lógica del Scheduler Round Robin */
    int turno_n = 1;
    int current_queue_idx = 0;

    /* Estructura para registrar movimientos */
    MsgMovimiento msg;

    /* Mientras el juego esté activo y no haya ganador registrado */
    while (game_running) {
        pthread_mutex_lock(&tablero->mutex);
        int ganador_actual = tablero->ganador;
        pthread_mutex_unlock(&tablero->mutex);

        if (ganador_actual != -1) {
            break; /* El juego ha terminado porque alguien ganó */
        }

        int j = current_queue_idx;
        pid_t p_pid = child_pids[j];

        /* Lanzar dado */
        int dado = (rand() % 6) + 1;

        printf("\n  " ANSI_COLOR_CYAN "╔═══════════════════════════════════════════════════════╗" ANSI_RESET "\n");
        printf("  " ANSI_COLOR_CYAN "║" ANSI_RESET " " ANSI_COLOR_GREEN "TURNO %-3d" ANSI_RESET " | Jugador J%d (PID: %5d) | Dado: " ANSI_COLOR_YELLOW "%d" ANSI_RESET "         " ANSI_COLOR_CYAN "║" ANSI_RESET "\n",
               turno_n, j + 1, p_pid, dado);
        printf("  " ANSI_COLOR_CYAN "╚═══════════════════════════════════════════════════════╝" ANSI_RESET "\n");

        /* Enviar dado al jugador activo a través de su pipe */
        if (write(pipes[j][1], &dado, sizeof(int)) == -1) {
            perror("Error al escribir el dado en el pipe del jugador");
            break;
        }

        /* Indicar al jugador que comience su turno */
        if (sem_post(sem_ready[j]) == -1) {
            perror("Error en sem_post (scheduler)");
            break;
        }

        /* Esperar a que el jugador confirme que terminó su turno */
        if (sem_wait(sem_done[j]) == -1) {
            if (errno == EINTR) {
                break; /* Interrumpido por SIGINT */
            }
            perror("Error en sem_wait (scheduler)");
            break;
        }

        /* Leer la actualización de movimiento desde la Cola de Mensajes POSIX */
        if (recibir_mensaje_movimiento(mq, &msg) != -1) {
            imprimir_mensaje_movimiento(&msg, p_pid);
        }

        /* Imprimir representación del tablero actual en consola */
        imprimir_tablero(tablero, num_jugadores, turno_n);

        /* Pequeña pausa para facilitar la lectura de la simulación en tiempo real */
        usleep(400000); /* 400 milisegundos */

        /* Pasar al siguiente turno */
        turno_n++;
        current_queue_idx = (current_queue_idx + 1) % num_jugadores;
    }

    /* 7. Mostrar resultados finales de la partida */
    printf("\n  " ANSI_COLOR_CYAN "=================================================" ANSI_RESET "\n");
    printf("  " ANSI_COLOR_CYAN "║               PARTIDA TERMINADA               ║" ANSI_RESET "\n");
    printf("  " ANSI_COLOR_CYAN "=================================================" ANSI_RESET "\n");

    pthread_mutex_lock(&tablero->mutex);
    int ganador = tablero->ganador;
    pthread_mutex_unlock(&tablero->mutex);

    if (ganador != -1) {
        const char* color_code = (ganador == 0) ? ANSI_COLOR_RED :
                                 (ganador == 1) ? ANSI_COLOR_BLUE :
                                 (ganador == 2) ? ANSI_COLOR_GREEN : ANSI_COLOR_YELLOW;

        printf("  " ANSI_COLOR_GREEN "¡FELICIDADES JUGADOR %sJ%d" ANSI_RESET ANSI_COLOR_GREEN " (PID: %d)!" ANSI_RESET "\n", color_code, ganador + 1, child_pids[ganador]);
        printf("  " ANSI_COLOR_GREEN "Has ganado la partida al llevar tus 4 fichas a META (69)." ANSI_RESET "\n");
    } else {
        printf("  " ANSI_COLOR_YELLOW "Partida cancelada por el usuario (SIGINT)." ANSI_RESET "\n");
    }

    /* 8. Liberación robusta de recursos */
    realizar_limpieza();

    return 0;
}
