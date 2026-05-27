/**
 * @file sync.c
 * @brief Implementación de las funciones de sincronización (mutex compartidos y semáforos).
 */

#include "parchis.h"

/**
 * @brief Inicializa un mutex en memoria compartida para ser usado entre múltiples procesos.
 * @param mutex Puntero al mutex a inicializar.
 * @return 0 en caso de éxito, -1 en caso de error.
 */
int inicializar_mutex_compartido(pthread_mutex_t* mutex) {
    pthread_mutexattr_t attr;

    if (pthread_mutexattr_init(&attr) != 0) {
        perror("Error al inicializar atributos del mutex");
        return -1;
    }

    if (pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED) != 0) {
        perror("Error al establecer mutex como compartido entre procesos");
        pthread_mutexattr_destroy(&attr);
        return -1;
    }

    if (pthread_mutex_init(mutex, &attr) != 0) {
        perror("Error al inicializar el mutex");
        pthread_mutexattr_destroy(&attr);
        return -1;
    }

    if (pthread_mutexattr_destroy(&attr) != 0) {
        perror("Error al destruir atributos del mutex");
        return -1;
    }

    return 0;
}

/**
 * @brief Crea un semáforo POSIX nombrado con valor inicial 0.
 * @param fmt Formato del nombre del semáforo.
 * @param pid PID del proceso padre (para unicidad).
 * @param id ID adicional del semáforo (ej: índice del jugador).
 * @return Puntero al semáforo sem_t creado, o SEM_FAILED en caso de error.
 */
sem_t* crear_semaforo(const char* fmt, int pid, int id) {
    char name[SEM_NAME_LEN];
    snprintf(name, sizeof(name), fmt, pid, id);

    /* Eliminar cualquier semáforo anterior con el mismo nombre para evitar basura */
    sem_unlink(name);

    sem_t* sem = sem_open(name, O_CREAT | O_EXCL, 0666, 0);
    if (sem == SEM_FAILED) {
        perror("Error en sem_open al crear");
        fprintf(stderr, "No se pudo crear el semáforo %s\n", name);
    }
    return sem;
}

/**
 * @brief Abre un semáforo POSIX nombrado existente.
 * @param fmt Formato del nombre del semáforo.
 * @param pid PID del proceso padre.
 * @param id ID adicional del semáforo.
 * @return Puntero al semáforo sem_t abierto, o SEM_FAILED en caso de error.
 */
sem_t* abrir_semaforo(const char* fmt, int pid, int id) {
    char name[SEM_NAME_LEN];
    snprintf(name, sizeof(name), fmt, pid, id);

    sem_t* sem = sem_open(name, 0);
    if (sem == SEM_FAILED) {
        perror("Error en sem_open al abrir");
        fprintf(stderr, "No se pudo abrir el semáforo %s\n", name);
    }
    return sem;
}

/**
 * @brief Cierra la conexión de un semáforo en el proceso actual.
 * @param sem Puntero al semáforo a cerrar.
 */
void cerrar_semaforo(sem_t* sem) {
    if (sem != NULL && sem != SEM_FAILED) {
        if (sem_close(sem) == -1) {
            perror("Error en sem_close");
        }
    }
}

/**
 * @brief Desvincula (elimina) un semáforo del sistema.
 * @param fmt Formato del nombre del semáforo.
 * @param pid PID del proceso padre.
 * @param id ID adicional del semáforo.
 */
void eliminar_semaforo(const char* fmt, int pid, int id) {
    char name[SEM_NAME_LEN];
    snprintf(name, sizeof(name), fmt, pid, id);

    if (sem_unlink(name) == -1) {
        if (errno != ENOENT) { /* Ignorar error si ya no existe */
            perror("Error en sem_unlink");
        }
    }
}
