/*
 * banco.c - Transferencias concorrentes entre contas bancarias
 *
 * Trabalho Final - Programacao Concorrente
 * Autor: Carlos Eduardo Souza - 211061510
 *
 * Problema: varias threads (clientes) fazem transferencias entre contas
 * guardadas em memoria compartilhada. A soma dos saldos deve ser sempre
 * NUM_CONTAS * SALDO_INICIAL.
 *
 * Uso: ./banco <modo>
 *   0 = sem sincronizacao (condicao de corrida)
 *   1 = mutex por conta, travando origem e depois destino (risco de deadlock)
 *   2 = mutex por conta com ordem global de travamento (solucao correta)
 */
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#define NUM_CONTAS      10
#define SALDO_INICIAL   1000
#define NUM_THREADS     8
#define TRANSF_POR_THREAD 200000

typedef struct {
    int id;
    long saldo;
    pthread_mutex_t lock;
} Conta;

Conta contas[NUM_CONTAS];   /* memoria compartilhada entre as threads */
int modo;

/* Versao 0: sem sincronizacao */
void transferir_inseguro(Conta *origem, Conta *destino, long valor) {
    if (origem->saldo >= valor) {
        origem->saldo  -= valor;
        destino->saldo += valor;
    }
}

/* Versao 1: trava origem e depois destino (pode gerar deadlock) */
void transferir_ingenuo(Conta *origem, Conta *destino, long valor) {
    pthread_mutex_lock(&origem->lock);
    pthread_mutex_lock(&destino->lock);
    if (origem->saldo >= valor) {
        origem->saldo  -= valor;
        destino->saldo += valor;
    }
    pthread_mutex_unlock(&destino->lock);
    pthread_mutex_unlock(&origem->lock);
}

/* Versao 2: trava sempre a conta de menor id primeiro */
void transferir_seguro(Conta *origem, Conta *destino, long valor) {
    Conta *primeira = (origem->id < destino->id) ? origem  : destino;
    Conta *segunda  = (origem->id < destino->id) ? destino : origem;

    pthread_mutex_lock(&primeira->lock);
    pthread_mutex_lock(&segunda->lock);
    if (origem->saldo >= valor) {          /* secao critica */
        origem->saldo  -= valor;
        destino->saldo += valor;
    }
    pthread_mutex_unlock(&segunda->lock);
    pthread_mutex_unlock(&primeira->lock);
}

/* Gerador pseudoaleatorio local a cada thread (thread-safe e portavel) */
static unsigned int proximo_aleatorio(unsigned int *estado) {
    *estado = *estado * 1103515245u + 12345u;
    return (*estado >> 16) & 0x7FFF;
}

void *cliente(void *arg) {
    unsigned int semente = (unsigned int)(long)arg;
    for (int i = 0; i < TRANSF_POR_THREAD; i++) {
        int o = proximo_aleatorio(&semente) % NUM_CONTAS;
        int d = proximo_aleatorio(&semente) % NUM_CONTAS;
        if (o == d) continue;              /* nao transfere para si mesma */
        long valor = 1 + proximo_aleatorio(&semente) % 100;

        if (modo == 0)      transferir_inseguro(&contas[o], &contas[d], valor);
        else if (modo == 1) transferir_ingenuo (&contas[o], &contas[d], valor);
        else                transferir_seguro  (&contas[o], &contas[d], valor);
    }
    return NULL;
}

long saldo_total(void) {
    long total = 0;
    for (int i = 0; i < NUM_CONTAS; i++) total += contas[i].saldo;
    return total;
}

int main(int argc, char *argv[]) {
    modo = (argc > 1) ? atoi(argv[1]) : 2;
    if (modo < 0 || modo > 2) {
        fprintf(stderr, "Uso: %s <modo>  (0 = sem sync, 1 = lock sem ordem, 2 = lock com ordem)\n", argv[0]);
        return 1;
    }
    pthread_t threads[NUM_THREADS];

    for (int i = 0; i < NUM_CONTAS; i++) {
        contas[i].id = i;
        contas[i].saldo = SALDO_INICIAL;
        pthread_mutex_init(&contas[i].lock, NULL);
    }
    printf("Modo %d | Total inicial: %ld\n", modo, saldo_total());

    for (long i = 0; i < NUM_THREADS; i++)
        pthread_create(&threads[i], NULL, cliente, (void *)(i + 1));
    for (int i = 0; i < NUM_THREADS; i++)
        pthread_join(threads[i], NULL);

    long total = saldo_total();
    printf("Modo %d | Total final:   %ld -> %s\n", modo, total,
           total == (long)NUM_CONTAS * SALDO_INICIAL ? "CORRETO" : "ERRO (condicao de corrida)");
    for (int i = 0; i < NUM_CONTAS; i++)
        pthread_mutex_destroy(&contas[i].lock);
    return 0;
}
