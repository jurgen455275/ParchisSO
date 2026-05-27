/**
 * @file display.c
 * @brief Implementación de las funciones de visualización del juego en la consola.
 */

#include "parchis.h"


static const char* get_player_color_ansi(int player_idx) {
    switch (player_idx) {
        case 0: return ANSI_COLOR_RED;
        case 1: return ANSI_COLOR_BLUE;
        case 2: return ANSI_COLOR_GREEN;
        case 3: return ANSI_COLOR_YELLOW;
        default: return ANSI_RESET;
    }
}

static char get_player_color_char(int player_idx) {
    switch (player_idx) {
        case 0: return 'R'; /* Rojo */
        case 1: return 'B'; /* Blue / Azul */
        case 2: return 'V'; /* Verde */
        case 3: return 'A'; /* Amarillo */
        default: return '?';
    }
}

/**
 * @brief Imprime de forma estilizada el estado actual del tablero en consola.
 * @param tablero Puntero al tablero compartido.
 * @param num_jugadores Número de jugadores en la partida.
 * @param turno_n Número del turno actual.
 */
void imprimir_tablero(const Tablero* tablero, int num_jugadores, int turno_n) {
    /* Imprimir encabezado de la tabla */
    printf("\n  " ANSI_COLOR_CYAN "╔══════════════════════════════════╗" ANSI_RESET "\n");
    
    char titulo[64];
    snprintf(titulo, sizeof(titulo), "ESTADO DEL TABLERO - TURNO %-3d", turno_n);
    printf("  " ANSI_COLOR_CYAN "║" ANSI_RESET "   %-30s " ANSI_COLOR_CYAN "║" ANSI_RESET "\n", titulo);
    
    printf("  " ANSI_COLOR_CYAN "╠══════════╦═══╦═══╦═══╦══════════╣" ANSI_RESET "\n");
    printf("  " ANSI_COLOR_CYAN "║" ANSI_RESET " Jugador  " ANSI_COLOR_CYAN "║" ANSI_RESET " F1" ANSI_COLOR_CYAN "║" ANSI_RESET " F2" ANSI_COLOR_CYAN "║" ANSI_RESET " F3" ANSI_COLOR_CYAN "║" ANSI_RESET " F4       " ANSI_COLOR_CYAN "║" ANSI_RESET "\n");
    printf("  " ANSI_COLOR_CYAN "╠══════════╬═══╬═══╬═══╬══════════╣" ANSI_RESET "\n");

    /* Imprimir la fila de cada jugador activo */
    for (int i = 0; i < num_jugadores; i++) {
        const char* color_code = get_player_color_ansi(i);
        char c_char = get_player_color_char(i);

        /* Formatear columnas */
        printf("  " ANSI_COLOR_CYAN "║" ANSI_RESET "  %sJ%d(%c)" ANSI_RESET "   " ANSI_COLOR_CYAN "║" ANSI_RESET, color_code, i + 1, c_char);
        
        /* Imprimir F1, F2, F3 */
        for (int f = 0; f < 3; f++) {
            int pos = tablero->fichas[i][f];
            if (pos == META) {
                printf(" " ANSI_COLOR_GREEN "%2d" ANSI_RESET ANSI_COLOR_CYAN "║" ANSI_RESET, pos);
            } else if (pos == CASA) {
                printf(" " ANSI_COLOR_GRAY "%2d" ANSI_RESET ANSI_COLOR_CYAN "║" ANSI_RESET, pos);
            } else {
                printf(" %s%2d" ANSI_RESET ANSI_COLOR_CYAN "║" ANSI_RESET, color_code, pos);
            }
        }
        
        /* Imprimir F4 (columna de ancho 10) */
        int pos_f4 = tablero->fichas[i][3];
        if (pos_f4 == META) {
            printf("  " ANSI_COLOR_GREEN "%-8d" ANSI_RESET ANSI_COLOR_CYAN "║" ANSI_RESET "\n", pos_f4);
        } else if (pos_f4 == CASA) {
            printf("  " ANSI_COLOR_GRAY "%-8d" ANSI_RESET ANSI_COLOR_CYAN "║" ANSI_RESET "\n", pos_f4);
        } else {
            printf("  %s%-8d" ANSI_RESET ANSI_COLOR_CYAN "║" ANSI_RESET "\n", color_code, pos_f4);
        }
    }

    printf("  " ANSI_COLOR_CYAN "╚══════════╩═══╩═══╩═══╩══════════╝" ANSI_RESET "\n");
    printf("  " ANSI_COLOR_GRAY "(0=casa, 1-68=tablero, 69=meta)" ANSI_RESET "\n\n");
}

/**
 * @brief Registra e imprime el movimiento de un jugador a partir del mensaje de la cola.
 * @param msg Puntero al mensaje de movimiento recibido.
 * @param pid PID del proceso jugador correspondiente.
 */
void imprimir_mensaje_movimiento(const MsgMovimiento* msg, int pid) {
    const char* p_color = get_player_color_ansi(msg->jugador);
    
    if (msg->ficha != -1) {
        printf("  " ANSI_COLOR_CYAN "»" ANSI_RESET " [%sJ%d" ANSI_RESET " (PID: %d)] movió ficha " ANSI_COLOR_CYAN "F%d" ANSI_RESET " a posición %s%d" ANSI_RESET " | Dado: " ANSI_COLOR_YELLOW "%d" ANSI_RESET "\n",
               p_color, msg->jugador + 1, pid, msg->ficha + 1, p_color, msg->posicion_nueva, msg->dado);
    } else {
        printf("  " ANSI_COLOR_GRAY "»" ANSI_RESET " [%sJ%d" ANSI_RESET " (PID: %d)] no pudo realizar ningún movimiento | Dado: " ANSI_COLOR_YELLOW "%d" ANSI_RESET "\n",
               p_color, msg->jugador + 1, pid, msg->dado);
    }
}
