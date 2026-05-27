/**
 * @file jugador.c
 * @brief Implementación de la lógica del jugador y los hilos de fichas.
 */

#include "parchis.h"

/* Bandera global de ejecución local */
static volatile sig_atomic_t jugador_running = 1;

/* Estructura para pasar parámetros a los hilos de fichas */
typedef struct {
    int jugador_id;
    int ficha_id;
    Tablero* tablero;
    TurnCoord* coord;
    mqd_t mq;
} FichaThreadArg;

/**
 * @brief Manejador de señales para el proceso jugador.
 * @param sig Número de la señal.
 */
static void handle_signal(int sig) {
    (void)sig;
    jugador_running = 0;
}

/**
 * @brief Función de inicio para el hilo que representa una ficha.
 * @param arg Puntero a la estructura FichaThreadArg.
 * @return NULL.
 */
static void* ficha_worker(void* arg) {
    FichaThreadArg* thread_arg = (FichaThreadArg*)arg;
    int jugador_id = thread_arg->jugador_id;
    int ficha_id = thread_arg->ficha_id;
    Tablero* tablero = thread_arg->tablero;
    TurnCoord* coord = thread_arg->coord;
    mqd_t mq = thread_arg->mq;

    int last_seen_turn = 0;

    while (jugador_running) {
        pthread_mutex_lock(&coord->mutex);
        
        /* Esperar a que el hilo principal del jugador le dé paso */
        while (coord->current_turn_id == last_seen_turn && jugador_running) {
            pthread_cond_wait(&coord->cond_start, &coord->mutex);
        }

        if (!jugador_running) {
            pthread_mutex_unlock(&coord->mutex);
            break;
        }

        int dado = coord->dado;
        pthread_mutex_unlock(&coord->mutex);

        /* 1. Evaluar movimiento leyendo de forma segura la memoria compartida */
        pthread_mutex_lock(&tablero->mutex);
        int pos_actual = tablero->fichas[jugador_id][ficha_id];
        pthread_mutex_unlock(&tablero->mutex);

        EvalFicha eval;
        eval.current_pos = pos_actual;
        eval.valid = 0;
        eval.priority = 0;
        eval.target_pos = pos_actual;

        if (pos_actual == META) {
            /* Ficha en meta no puede moverse */
            eval.valid = 0;
            eval.priority = 0;
        } else if (pos_actual == CASA) {
            if (dado == 5) {
                eval.valid = 1;
                eval.priority = 1; /* Prioridad 3: salir de casa (según reglas: prioridad 1) */
                eval.target_pos = 1;
            }
        } else {
            /* Ficha en tablero */
            eval.valid = 1;
            int pos_nueva = pos_actual + dado;
            if (pos_nueva >= META) {
                eval.target_pos = META;
                if (pos_nueva == META) {
                    eval.priority = 3; /* Máxima prioridad: llegar exactamente a meta */
                } else {
                    eval.priority = 2; /* Ficha en tablero con nueva posición superior a 68 */
                }
            } else {
                eval.target_pos = pos_nueva;
                eval.priority = 2; /* Ficha en tablero regular */
            }
        }

        /* 2. Guardar el resultado y notificar al hilo principal */
        pthread_mutex_lock(&coord->mutex);
        coord->eval_results[ficha_id] = eval;
        coord->threads_ready++;
        
        if (coord->threads_ready == 4) {
            pthread_cond_signal(&coord->cond_eval);
        }

        /* 3. Esperar a que el hilo principal tome una decisión */
        while (coord->chosen_ficha == -2 && jugador_running) {
            pthread_cond_wait(&coord->cond_choice, &coord->mutex);
        }

        if (!jugador_running) {
            pthread_mutex_unlock(&coord->mutex);
            break;
        }

        int es_elegida = (coord->chosen_ficha == ficha_id);
        int dest_pos = eval.target_pos;
        pthread_mutex_unlock(&coord->mutex);

        /* 4. Si fue elegida, realizar el movimiento y notificar mediante IPC */
        if (es_elegida) {
            pthread_mutex_lock(&tablero->mutex);
            
            /* Escribir en memoria compartida */
            tablero->fichas[jugador_id][ficha_id] = dest_pos;
            
            /* Comprobar si ha ganado todas sus fichas en meta */
            int ha_ganado = 1;
            for (int f = 0; f < MAX_FICHAS; f++) {
                if (tablero->fichas[jugador_id][f] != META) {
                    ha_ganado = 0;
                    break;
                }
            }
            if (ha_ganado) {
                tablero->ganador = jugador_id;
            }
            
            pthread_mutex_unlock(&tablero->mutex);

            /* Reportar a la cola de mensajes */
            MsgMovimiento msg;
            msg.jugador = jugador_id;
            msg.ficha = ficha_id;
            msg.posicion_nueva = dest_pos;
            msg.dado = dado;
            enviar_mensaje_movimiento(mq, &msg);

            /* Notificar al hilo principal del jugador que el movimiento ha concluido */
            pthread_mutex_lock(&coord->mutex);
            coord->movement_done = 1;
            pthread_cond_signal(&coord->cond_done);
            pthread_mutex_unlock(&coord->mutex);
        }

        last_seen_turn = coord->current_turn_id;
    }

    return NULL;
}

/**
 * @brief Función principal ejecutada por el proceso hijo (jugador).
 * @param jugador_id Índice del jugador (0 a N-1).
 * @param pipe_read_fd Descriptor de lectura del pipe asignado por el Scheduler.
 * @param parent_pid PID del proceso árbitro/padre.
 * @param num_jugadores Número total de jugadores en juego.
 */
void run_jugador(int jugador_id, int pipe_read_fd, int parent_pid, int num_jugadores) {
    (void)num_jugadores;
    
    /* Configurar manejador de señales para salida robusta */
    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    /* Inicializar semilla de números pseudoaleatorios para el hijo */
    srand((unsigned int)(getpid() ^ time(NULL)));

    /* Conectar a recursos globales (Memoria Compartida, Cola de Mensajes, Semáforos) */
    char shm_name[SHM_NAME_LEN];
    snprintf(shm_name, sizeof(shm_name), SHM_NAME_FMT, parent_pid);
    
    int shm_fd;
    Tablero* tablero = conectar_tablero_compartido(shm_name, &shm_fd);
    if (!tablero) {
        fprintf(stderr, "[Jugador J%d] Error al conectar a la memoria compartida\n", jugador_id + 1);
        exit(EXIT_FAILURE);
    }

    sem_t* sem_ready = abrir_semaforo(SEM_READY_FMT, parent_pid, jugador_id);
    sem_t* sem_done = abrir_semaforo(SEM_DONE_FMT, parent_pid, jugador_id);
    if (sem_ready == SEM_FAILED || sem_done == SEM_FAILED) {
        fprintf(stderr, "[Jugador J%d] Error al abrir semáforos nombrados\n", jugador_id + 1);
        desconectar_tablero_compartido(tablero, shm_fd);
        exit(EXIT_FAILURE);
    }

    char mq_name[MQ_NAME_LEN];
    snprintf(mq_name, sizeof(mq_name), MQ_NAME_FMT, parent_pid);
    mqd_t mq = abrir_cola_mensajes(mq_name);
    if (mq == (mqd_t)-1) {
        fprintf(stderr, "[Jugador J%d] Error al abrir la cola de mensajes\n", jugador_id + 1);
        cerrar_semaforo(sem_ready);
        cerrar_semaforo(sem_done);
        desconectar_tablero_compartido(tablero, shm_fd);
        exit(EXIT_FAILURE);
    }

    /* Estructura para la coordinación de hilos */
    TurnCoord coord;
    coord.dado = 0;
    coord.current_turn_id = 0;
    coord.last_processed_turn_id = 0;
    coord.threads_ready = 0;
    coord.chosen_ficha = -2;
    coord.movement_done = 0;
    
    pthread_mutex_init(&coord.mutex, NULL);
    pthread_cond_init(&coord.cond_start, NULL);
    pthread_cond_init(&coord.cond_eval, NULL);
    pthread_cond_init(&coord.cond_choice, NULL);
    pthread_cond_init(&coord.cond_done, NULL);

    /* Crear los 4 hilos de fichas */
    pthread_t hilos[MAX_FICHAS];
    FichaThreadArg args[MAX_FICHAS];

    for (int i = 0; i < MAX_FICHAS; i++) {
        args[i].jugador_id = jugador_id;
        args[i].ficha_id = i;
        args[i].tablero = tablero;
        args[i].coord = &coord;
        args[i].mq = mq;
        
        if (pthread_create(&hilos[i], NULL, ficha_worker, &args[i]) != 0) {
            perror("Error al crear hilo de ficha");
            /* Limpieza y salida de emergencia */
            jugador_running = 0;
            for (int j = 0; j < i; j++) {
                pthread_join(hilos[j], NULL);
            }
            cerrar_cola_mensajes(mq);
            cerrar_semaforo(sem_ready);
            cerrar_semaforo(sem_done);
            desconectar_tablero_compartido(tablero, shm_fd);
            exit(EXIT_FAILURE);
        }
    }

    /* Bucle principal del jugador */
    while (jugador_running) {
        /* 1. Esperar la señal del Scheduler */
        if (sem_wait(sem_ready) < 0) {
            if (errno == EINTR) continue; /* Interrumpido por señal */
            perror("Error en sem_wait (jugador)");
            break;
        }

        if (!jugador_running) break;

        /* 2. Leer el dado desde el pipe */
        int dado = 0;
        ssize_t bytes = read(pipe_read_fd, &dado, sizeof(int));
        if (bytes <= 0) {
            if (bytes == -1 && errno == EINTR) continue;
            break; /* El extremo de escritura del pipe se cerró */
        }

        /* 3. Iniciar la evaluación de movimiento en los hilos */
        pthread_mutex_lock(&coord.mutex);
        coord.dado = dado;
        coord.chosen_ficha = -2;
        coord.threads_ready = 0;
        coord.movement_done = 0;
        coord.current_turn_id++; /* Despierta a los 4 hilos */
        pthread_cond_broadcast(&coord.cond_start);

        /* Esperar a que los 4 hilos evalúen el movimiento */
        while (coord.threads_ready < 4 && jugador_running) {
            pthread_cond_wait(&coord.cond_eval, &coord.mutex);
        }

        if (!jugador_running) {
            pthread_mutex_unlock(&coord.mutex);
            break;
        }

        /* 4. Implementar las reglas simplificadas de prioridad */
        int mejor_ficha = -1;
        int mejor_prioridad = 0;
        int mejor_posicion = -1;

        for (int i = 0; i < MAX_FICHAS; i++) {
            if (!coord.eval_results[i].valid) continue;

            int prio = coord.eval_results[i].priority;
            
            /* Prioridad de movimiento (en orden de mayor a menor):
             * 1. Llegar exactamente a meta (prio == 3)
             * 2. Mayor posición en el tablero (prio == 2)
             * 3. Salir de casa (prio == 1)
             */
            if (prio > mejor_prioridad) {
                mejor_prioridad = prio;
                mejor_ficha = i;
                mejor_posicion = coord.eval_results[i].current_pos;
            } else if (prio == mejor_prioridad && prio == 2) {
                /* Desempate para fichas en tablero: la que tenga mayor posición */
                if (coord.eval_results[i].current_pos > mejor_posicion) {
                    mejor_ficha = i;
                    mejor_posicion = coord.eval_results[i].current_pos;
                }
            }
            /* Para prioridad 1 o 3, en caso de empate se mantiene la primera encontrada (menor índice) */
        }

        /* 5. Señalar la decisión a los hilos */
        coord.chosen_ficha = mejor_ficha;
        pthread_cond_broadcast(&coord.cond_choice);

        /* Si no se pudo mover ninguna ficha, reportar mensaje dummy al padre para no bloquear la cola */
        if (mejor_ficha == -1) {
            pthread_mutex_unlock(&coord.mutex);

            MsgMovimiento msg;
            msg.jugador = jugador_id;
            msg.ficha = -1;
            msg.posicion_nueva = -1;
            msg.dado = dado;
            enviar_mensaje_movimiento(mq, &msg);
        } else {
            /* Esperar a que el hilo de la ficha elegida termine el movimiento y envíe el mensaje IPC */
            while (!coord.movement_done && jugador_running) {
                pthread_cond_wait(&coord.cond_done, &coord.mutex);
            }
            pthread_mutex_unlock(&coord.mutex);
        }

        /* 6. Notificar al Scheduler que el turno ha concluido */
        sem_post(sem_done);
    }

    /* --- LIMPIEZA DE RECURSOS DEL JUGADOR --- */
    pthread_mutex_lock(&coord.mutex);
    jugador_running = 0;
    pthread_cond_broadcast(&coord.cond_start);
    pthread_cond_broadcast(&coord.cond_choice);
    pthread_cond_broadcast(&coord.cond_eval);
    pthread_cond_broadcast(&coord.cond_done);
    pthread_mutex_unlock(&coord.mutex);

    /* Esperar la finalización de los hilos de fichas */
    for (int i = 0; i < MAX_FICHAS; i++) {
        pthread_join(hilos[i], NULL);
    }

    /* Destruir mutex y variables de condición locales */
    pthread_mutex_destroy(&coord.mutex);
    pthread_cond_destroy(&coord.cond_start);
    pthread_cond_destroy(&coord.cond_eval);
    pthread_cond_destroy(&coord.cond_choice);
    pthread_cond_destroy(&coord.cond_done);

    /* Cerrar conexiones a recursos IPC */
    cerrar_cola_mensajes(mq);
    cerrar_semaforo(sem_ready);
    cerrar_semaforo(sem_done);
    desconectar_tablero_compartido(tablero, shm_fd);

    exit(EXIT_SUCCESS);
}
