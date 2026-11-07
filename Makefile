# Makefile - Trabalho Final de Programacao Concorrente (banco.c)

CC      = gcc
CFLAGS  = -Wall -Wextra -O0 -pthread
LDFLAGS = -pthread
TARGET  = banco
SRC     = banco.c
TIMEOUT = 10

.PHONY: all run run-race run-deadlock run-ordem run-final tsan clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# Executa as quatro versoes em sequencia
run: run-race run-deadlock run-ordem run-final

# Modo 0: contas sem sincronizacao (condicao de corrida)
run-race: $(TARGET)
	@echo "== Versao 0: contas sem sincronizacao =="
	./$(TARGET) 0

# Modo 1: lock sem ordem (interrompido apos $(TIMEOUT)s se travar)
run-deadlock: $(TARGET)
	@echo "== Versao 1: lock sem ordem (pode travar) =="
	-timeout $(TIMEOUT) ./$(TARGET) 1 || echo "Programa interrompido apos $(TIMEOUT)s: deadlock"

# Modo 2: lock com ordem global, recusa se faltar saldo
run-ordem: $(TARGET)
	@echo "== Versao 2: lock com ordem global =="
	./$(TARGET) 2

# Modo 3: ordem global + espera por saldo (versao final)
run-final: $(TARGET)
	@echo "== Versao 3: ordem global + espera por saldo =="
	./$(TARGET) 3

# Versao final compilada com ThreadSanitizer (detecta condicoes de corrida)
tsan: $(SRC)
	$(CC) -fsanitize=thread -g -O1 -pthread -o $(TARGET)_tsan $<
	./$(TARGET)_tsan 3

clean:
	rm -f $(TARGET) $(TARGET).exe $(TARGET)_tsan
