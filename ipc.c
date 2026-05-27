/**
 * @file ipc.c
 * @brief Implementación de las funciones de comunicación entre procesos (Pipes y Colas de Mensajes).
 */

#include "parchis.h"

/**
 * @brief Crea un pipe unidireccional estándar de UNIX.
 * @param pipefd Arreglo de dos enteros donde se guardarán los descriptores de lectura[0] y escritura[1].
 * @return 0 en caso de éxito, -1 en caso de error.
 */
int crear_pipe(int pipefd[2]) {
    if (pipe(pipefd) == -1) {
        perror("Error al crear pipe");
        return -1;
    }
    return 0;
}

/**
 * @brief Crea una cola de mensajes POSIX con atributos definidos.
 * @param name Nombre de la cola de mensajes (debe empezar con /).
 * @return Descriptor de la cola de mensajes creado, o (mqd_t)-1 en caso de error.
 */
mqd_t crear_cola_mensajes(const char* name) {
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(MsgMovimiento);
    attr.mq_curmsgs = 0;

    /* Eliminar cualquier cola previa con el mismo nombre para evitar residuos */
    mq_unlink(name);

    mqd_t mq = mq_open(name, O_CREAT | O_RDONLY | O_EXCL, 0666, &attr);
    if (mq == (mqd_t)-1) {
        perror("Error en mq_open al crear");
        fprintf(stderr, "No se pudo crear la cola de mensajes %s\n", name);
    }
    return mq;
}

/**
 * @brief Abre una cola de mensajes POSIX existente para escritura (hijos).
 * @param name Nombre de la cola de mensajes.
 * @return Descriptor de la cola de mensajes abierto, o (mqd_t)-1 en caso de error.
 */
mqd_t abrir_cola_mensajes(const char* name) {
    mqd_t mq = mq_open(name, O_WRONLY);
    if (mq == (mqd_t)-1) {
        perror("Error en mq_open al abrir");
        fprintf(stderr, "No se pudo abrir la cola de mensajes %s\n", name);
    }
    return mq;
}

/**
 * @brief Cierra la cola de mensajes para el proceso actual.
 * @param mq Descriptor de la cola a cerrar.
 */
void cerrar_cola_mensajes(mqd_t mq) {
    if (mq != (mqd_t)-1) {
        if (mq_close(mq) == -1) {
            perror("Error en mq_close");
        }
    }
}

/**
 * @brief Elimina la cola de mensajes del sistema de archivos de colas.
 * @param name Nombre de la cola de mensajes.
 */
void eliminar_cola_mensajes(const char* name) {
    if (mq_unlink(name) == -1) {
        if (errno != ENOENT) { /* Ignorar error si ya no existe */
            perror("Error en mq_unlink");
        }
    }
}

/**
 * @brief Envía un mensaje de tipo MsgMovimiento a la cola de mensajes de forma bloqueante.
 * @param mq Descriptor de la cola de mensajes.
 * @param msg Puntero a la estructura MsgMovimiento que contiene los datos a enviar.
 * @return 0 en caso de éxito, -1 en caso de error.
 */
int enviar_mensaje_movimiento(mqd_t mq, const MsgMovimiento* msg) {
    if (mq_send(mq, (const char*)msg, sizeof(MsgMovimiento), 0) == -1) {
        perror("Error en mq_send");
        return -1;
    }
    return 0;
}

/**
 * @brief Recibe un mensaje de tipo MsgMovimiento de la cola de mensajes de forma bloqueante.
 * @param mq Descriptor de la cola de mensajes.
 * @param msg Puntero a la estructura MsgMovimiento donde se guardará el mensaje recibido.
 * @return El número de bytes recibidos en caso de éxito, -1 en caso de error.
 */
int recibir_mensaje_movimiento(mqd_t mq, MsgMovimiento* msg) {
    ssize_t bytes_read = mq_receive(mq, (char*)msg, sizeof(MsgMovimiento), NULL);
    if (bytes_read == -1) {
        perror("Error en mq_receive");
        return -1;
    }
    return (int)bytes_read;
}
