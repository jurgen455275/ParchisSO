/**
 * @file tablero.c
 * @brief Implementación de las funciones de gestión de memoria compartida para el tablero.
 */

#include "parchis.h"

/**
 * @brief Crea una nueva sección de memoria compartida para el tablero e inicializa sus campos.
 * @param name Nombre del segmento de memoria compartida.
 * @param shm_fd Puntero donde se guardará el descriptor de archivo de la memoria compartida.
 * @return Puntero a la estructura Tablero en memoria compartida, o NULL en caso de error.
 */
Tablero* crear_tablero_compartido(const char* name, int* shm_fd) {
    int fd = shm_open(name, O_CREAT | O_RDWR | O_TRUNC, 0666);
    if (fd == -1) {
        perror("Error en shm_open al crear tablero");
        return NULL;
    }

    if (ftruncate(fd, sizeof(Tablero)) == -1) {
        perror("Error en ftruncate al dimensionar tablero");
        close(fd);
        shm_unlink(name);
        return NULL;
    }

    Tablero* tablero = (Tablero*)mmap(NULL, sizeof(Tablero), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (tablero == MAP_FAILED) {
        perror("Error en mmap al mapear tablero");
        close(fd);
        shm_unlink(name);
        return NULL;
    }

    *shm_fd = fd;

    /* Inicialización del tablero */
    memset(tablero->fichas, CASA, sizeof(tablero->fichas));
    tablero->turno_actual = 0;
    tablero->jugadores_activos = 0;
    tablero->ganador = -1;

    return tablero;
}

/**
 * @brief Conecta un proceso a una sección de memoria compartida existente.
 * @param name Nombre del segmento de memoria compartida.
 * @param shm_fd Puntero donde se guardará el descriptor de archivo de la memoria compartida.
 * @return Puntero a la estructura Tablero en memoria compartida, o NULL en caso de error.
 */
Tablero* conectar_tablero_compartido(const char* name, int* shm_fd) {
    int fd = shm_open(name, O_RDWR, 0666);
    if (fd == -1) {
        perror("Error en shm_open al conectar tablero");
        return NULL;
    }

    Tablero* tablero = (Tablero*)mmap(NULL, sizeof(Tablero), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (tablero == MAP_FAILED) {
        perror("Error en mmap al conectar tablero");
        close(fd);
        return NULL;
    }

    *shm_fd = fd;
    return tablero;
}

/**
 * @brief Desconecta el proceso de la memoria compartida (unmap y close).
 * @param tablero Puntero al tablero mapeado.
 * @param shm_fd Descriptor de archivo de la memoria compartida.
 */
void desconectar_tablero_compartido(Tablero* tablero, int shm_fd) {
    if (tablero != NULL && tablero != MAP_FAILED) {
        if (munmap(tablero, sizeof(Tablero)) == -1) {
            perror("Error en munmap al desconectar tablero");
        }
    }
    if (shm_fd != -1) {
        if (close(shm_fd) == -1) {
            perror("Error en close al desconectar tablero");
        }
    }
}

/**
 * @brief Elimina por completo el segmento de memoria compartida del sistema.
 * @param name Nombre del segmento de memoria compartida.
 */
void eliminar_tablero_compartido(const char* name) {
    if (shm_unlink(name) == -1) {
        if (errno != ENOENT) { /* Ignorar error si ya no existe */
            perror("Error en shm_unlink al eliminar tablero");
        }
    }
}
