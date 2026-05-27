# Makefile para el Simulador de Parchís
# Compilador y Banderas
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2
LDFLAGS = -lpthread -lrt

# Nombre del Ejecutable
TARGET = parchis

# Archivos de Origen y Objetos
SRCS = main.c jugador.c tablero.c ipc.c sync.c display.c
OBJS = $(SRCS:.c=.o)

# Regla por Defecto
all: $(TARGET)

# Enlace del Ejecutable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

# Compilación de Objetos Individales con Dependencia de Cabecera
%.o: %.c parchis.h
	$(CC) $(CFLAGS) -c $< -o $@

# Regla de Limpieza
clean:
	rm -f $(TARGET) $(OBJS)

# Evitar colisión de nombres de archivos con objetivos del Makefile
.PHONY: all clean
