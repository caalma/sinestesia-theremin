# Compilador y opciones
CC = gcc
CFLAGS = -Wall -Wextra -O2
LIBS = -lm -lportaudio -lX11 -lpthread -lyaml

# Directorios
SRC_DIR = src
OBJ_DIR = tmp
BIN_DIR = bin

# Lista de archivos fuente y objetos
SRCS = rpnf.c sinestesia-theremin.c

OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(SRCS))

# Binario final
TARGET = $(BIN_DIR)/sinestesia-theremin

# Regla principal: construir el binario
build: $(TARGET)

# Construcción del binario
$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $^ -o $@ $(LIBS)

# Reglas para compilar cada archivo objeto
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Limpiar
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

# Probar
run:
	./$(TARGET)
