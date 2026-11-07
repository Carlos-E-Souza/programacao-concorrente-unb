# Makefile - Trabalho Final de Programacao Concorrente (banco.c)

CC      = gcc
CFLAGS  = -Wall -Wextra -O0 -pthread
LDFLAGS = -pthread
TARGET  = banco
SRC     = banco.c
TIMEOUT = 10

.PHONY: all run run-race run-deadlock run-safe clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# Executa as tres versoes em sequencia
run: run-race run-deadlock run-safe

# Versao 0: sem sincronizacao (condicao de corrida)
run-race: $(TARGET)
	@echo "== Versao 0: sem sincronizacao =="
	./$(TARGET) 0

# Versao 1: lock sem ordem (interrompida apos $(TIMEOUT)s se travar)
run-deadlock: $(TARGET)
	@echo "== Versao 1: lock sem ordem (pode travar) =="
	-timeout $(TIMEOUT) ./$(TARGET) 1 || echo "Programa interrompido apos $(TIMEOUT)s: deadlock"

# Versao 2: lock com ordem global (solucao correta)
run-safe: $(TARGET)
	@echo "== Versao 2: lock com ordem global =="
	./$(TARGET) 2

clean:
	rm -f $(TARGET) $(TARGET).exe
