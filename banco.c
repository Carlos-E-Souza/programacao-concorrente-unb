/*
 * banco.c - Sistema bancario concorrente com fila de pedidos
 *
 * Trabalho Final - Programacao Concorrente
 * Autor: Carlos Eduardo Souza - 211061510
 *
 * Clientes (produtores) colocam pedidos de transferencia em uma fila
 * limitada. Atendentes (consumidores) retiram os pedidos e executam a
 * transferencia entre contas guardadas em memoria compartilhada.
 * A soma dos saldos deve ser sempre NUM_CONTAS * SALDO_INICIAL.
 *
 * Mecanismos usados:
 *   - semaforos (vazio/cheio) + mutex    -> fila de pedidos (produtor-consumidor)
 *   - um mutex por conta                 -> exclusao mutua nos saldos
 *   - uma variavel de condicao por conta -> espera por saldo suficiente
 *
 * Uso: ./banco <modo>
 *   0 = contas sem sincronizacao (condicao de corrida)
 *   1 = mutex por conta, travando origem e depois destino (deadlock)
 *   2 = mutex por conta com ordem global; recusa se nao houver saldo
 *   3 = ordem global + espera por saldo com variavel de condicao (versao final)
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <sched.h>
#include <unistd.h>

#define NUM_CONTAS          10
#define SALDO_INICIAL       1000
#define VALOR_MAXIMO        100      /* valor de cada pedido: 1..VALOR_MAXIMO */
#define NUM_CLIENTES        4        /* produtores */
#define NUM_ATENDENTES      8        /* consumidores */
#define PEDIDOS_POR_CLIENTE 25000
#define TAM_FILA            16
#define ESPERA_MAX_MS       5        /* tempo limite da espera por saldo */

#define TOTAL_PEDIDOS       (NUM_CLIENTES * PEDIDOS_POR_CLIENTE)
#define EXEMPLOS            3        /* quantos exemplos de cada evento imprimir */
#define PASSO_PROGRESSO     20       /* imprime o progresso a cada 20% */

/* ------------------------------------------------------------------ */
/* Contas (memoria compartilhada)                                      */
/* ------------------------------------------------------------------ */
typedef struct {
    int id;
    long saldo;
    pthread_mutex_t lock;            /* protege o saldo                  */
    pthread_cond_t  tem_saldo;       /* sinalizada quando a conta recebe */
} Conta;

Conta contas[NUM_CONTAS];
int modo;

/* ------------------------------------------------------------------ */
/* Visualizacao: contadores atomicos usados apenas para os prints.     */
/* Nao fazem parte da solucao; so limitam e organizam a saida.         */
/* ------------------------------------------------------------------ */
static __thread int meu_id = -1;            /* id do atendente desta thread */
static long processados = 0;                /* pedidos ja tratados          */
static int  terminou = 0;                   /* avisa o monitor para parar   */
static int  ex_transf = 0, ex_espera = 0, ex_recusa = 0;  /* exemplos ja impressos */
static int  segura[NUM_ATENDENTES];         /* modo 1: conta travada        */
static int  aguarda[NUM_ATENDENTES];        /* modo 1: conta esperada       */

/* Retorna 1 enquanto ainda nao foram impressos EXEMPLOS eventos deste tipo */
static int mostrar(int *contador) {
    return __atomic_fetch_add(contador, 1, __ATOMIC_RELAXED) < EXEMPLOS;
}

static const char *NOME_MODO[] = {
    "contas sem sincronizacao",
    "lock por conta sem ordem",
    "lock por conta com ordem global",
    "ordem global + espera por saldo"
};

/* ------------------------------------------------------------------ */
/* Fila limitada de pedidos (produtor-consumidor)                      */
/* ------------------------------------------------------------------ */
typedef struct {
    int origem, destino;             /* origem == -1 encerra o atendente */
    long valor;
} Pedido;

Pedido fila[TAM_FILA];
int inicio = 0, fim = 0;
sem_t vazio;                         /* posicoes livres  (comeca TAM_FILA) */
sem_t cheio;                         /* pedidos na fila  (comeca 0)        */
pthread_mutex_t lock_fila = PTHREAD_MUTEX_INITIALIZER;

void fila_inserir(Pedido p) {
    sem_wait(&vazio);                /* espera haver espaco */
    pthread_mutex_lock(&lock_fila);
    fila[fim] = p;
    fim = (fim + 1) % TAM_FILA;
    pthread_mutex_unlock(&lock_fila);
    sem_post(&cheio);                /* avisa que ha um pedido */
}

Pedido fila_retirar(void) {
    sem_wait(&cheio);                /* espera haver pedido */
    pthread_mutex_lock(&lock_fila);
    Pedido p = fila[inicio];
    inicio = (inicio + 1) % TAM_FILA;
    pthread_mutex_unlock(&lock_fila);
    sem_post(&vazio);                /* libera uma posicao */
    return p;
}

/* ------------------------------------------------------------------ */
/* Versoes da transferencia. Retornam 1 se concluida e 0 se recusada.  */
/* ------------------------------------------------------------------ */

/* Modo 0: sem sincronizacao */
int transferir_inseguro(Conta *origem, Conta *destino, long valor) {
    if (origem->saldo >= valor) {
        origem->saldo  -= valor;
        destino->saldo += valor;
        return 1;
    }
    return 0;
}

/* Modo 1: trava origem e depois destino (pode gerar deadlock) */
int transferir_ingenuo(Conta *origem, Conta *destino, long valor) {
    int ok = 0;
    pthread_mutex_lock(&origem->lock);
    __atomic_store_n(&segura[meu_id], origem->id, __ATOMIC_RELAXED);
    __atomic_store_n(&aguarda[meu_id], destino->id, __ATOMIC_RELAXED);
    sched_yield();   /* cede a CPU entre os dois locks: torna o deadlock reproduzivel */
    pthread_mutex_lock(&destino->lock);
    __atomic_store_n(&aguarda[meu_id], -1, __ATOMIC_RELAXED);
    if (origem->saldo >= valor) {
        origem->saldo  -= valor;
        destino->saldo += valor;
        ok = 1;
    }
    pthread_mutex_unlock(&destino->lock);
    pthread_mutex_unlock(&origem->lock);
    __atomic_store_n(&segura[meu_id], -1, __ATOMIC_RELAXED);
    return ok;
}

/* Trava as duas contas sempre na ordem do menor id */
static void travar_par(Conta *a, Conta *b) {
    Conta *primeira = (a->id < b->id) ? a : b;
    Conta *segunda  = (a->id < b->id) ? b : a;
    pthread_mutex_lock(&primeira->lock);
    pthread_mutex_lock(&segunda->lock);
}

static void destravar_par(Conta *a, Conta *b) {
    pthread_mutex_unlock(&a->lock);
    pthread_mutex_unlock(&b->lock);
}

/* Modo 2: ordem global de travamento; recusa se nao houver saldo */
int transferir_ordenado(Conta *origem, Conta *destino, long valor) {
    int ok = 0;
    travar_par(origem, destino);
    if (origem->saldo >= valor) {
        origem->saldo  -= valor;
        destino->saldo += valor;
        ok = 1;
    }
    destravar_par(origem, destino);
    return ok;
}

/* Modo 3: ordem global + espera por saldo com variavel de condicao */
int transferir_com_espera(Conta *origem, Conta *destino, long valor, int *esperou) {
    struct timespec limite;
    clock_gettime(CLOCK_REALTIME, &limite);
    limite.tv_sec  += ESPERA_MAX_MS / 1000;
    limite.tv_nsec += (ESPERA_MAX_MS % 1000) * 1000000L;
    if (limite.tv_nsec >= 1000000000L) { limite.tv_sec++; limite.tv_nsec -= 1000000000L; }

    while (1) {
        travar_par(origem, destino);
        if (origem->saldo >= valor) {                    /* regiao critica */
            origem->saldo  -= valor;
            destino->saldo += valor;
            pthread_cond_broadcast(&destino->tem_saldo); /* destino recebeu */
            destravar_par(origem, destino);
            return 1;
        }
        destravar_par(origem, destino);

        /* Sem saldo: espera so com o lock da origem, sem segurar outro lock */
        *esperou = 1;
        pthread_mutex_lock(&origem->lock);
        int log = (origem->saldo < valor) && mostrar(&ex_espera);
        if (log)
            printf("  [Atendente %d] C%d sem saldo (tem %ld, precisa %ld): dormindo ate receber credito...\n",
                   meu_id, origem->id, origem->saldo, valor);
        while (origem->saldo < valor) {
            int r = pthread_cond_timedwait(&origem->tem_saldo, &origem->lock, &limite);
            if (r == ETIMEDOUT) {                        /* desiste: recusa */
                if (log || mostrar(&ex_recusa))
                    printf("  [Atendente %d] C%d nao recebeu credito em %d ms (tem %ld, precisa %ld): pedido recusado\n",
                           meu_id, origem->id, ESPERA_MAX_MS, origem->saldo, valor);
                pthread_mutex_unlock(&origem->lock);
                return 0;
            }
        }
        if (log)
            printf("  [Atendente %d] C%d recebeu credito (saldo agora %ld): acordou e tenta de novo\n",
                   meu_id, origem->id, origem->saldo);
        pthread_mutex_unlock(&origem->lock);
        /* volta ao inicio e trava as duas contas na ordem correta */
    }
}

/* ------------------------------------------------------------------ */
/* Threads                                                              */
/* ------------------------------------------------------------------ */
typedef struct {
    int id;
    long concluidas, recusadas, esperas;
} Estatistica;

/* Gerador pseudoaleatorio local a cada thread (thread-safe e portavel) */
static unsigned int proximo_aleatorio(unsigned int *estado) {
    *estado = *estado * 1103515245u + 12345u;
    return (*estado >> 16) & 0x7FFF;
}

void *cliente(void *arg) {               /* produtor */
    unsigned int semente = (unsigned int)(long)arg;
    for (int i = 0; i < PEDIDOS_POR_CLIENTE; i++) {
        Pedido p;
        p.origem = proximo_aleatorio(&semente) % NUM_CONTAS;
        do { p.destino = proximo_aleatorio(&semente) % NUM_CONTAS; }
        while (p.destino == p.origem);
        p.valor = 1 + proximo_aleatorio(&semente) % VALOR_MAXIMO;
        fila_inserir(p);
    }
    printf("  [Cliente %d] colocou seus %d pedidos na fila\n", (int)(long)arg, PEDIDOS_POR_CLIENTE);
    return NULL;
}

void *atendente(void *arg) {             /* consumidor */
    Estatistica *e = (Estatistica *)arg; /* cada atendente tem a sua */
    meu_id = e->id;
    while (1) {
        Pedido p = fila_retirar();
        if (p.origem < 0) break;         /* pedido de encerramento */

        Conta *o = &contas[p.origem], *d = &contas[p.destino];
        int ok, esperou = 0;
        if      (modo == 0) ok = transferir_inseguro(o, d, p.valor);
        else if (modo == 1) ok = transferir_ingenuo(o, d, p.valor);
        else if (modo == 2) ok = transferir_ordenado(o, d, p.valor);
        else                ok = transferir_com_espera(o, d, p.valor, &esperou);

        if (ok) e->concluidas++; else e->recusadas++;
        e->esperas += esperou;

        /* Prints: alguns exemplos de pedidos e o progresso geral */
        if (mostrar(&ex_transf))
            printf("  [Atendente %d] pedido C%d -> C%d de R$ %3ld: %s\n", meu_id,
                   p.origem, p.destino, p.valor, ok ? "concluido" : "recusado (sem saldo)");
        long n = __atomic_add_fetch(&processados, 1, __ATOMIC_RELAXED);
        if (n % (TOTAL_PEDIDOS * PASSO_PROGRESSO / 100) == 0)
            printf("  [Progresso] %3ld%% dos pedidos tratados (%ld de %d)\n",
                   n * 100 / TOTAL_PEDIDOS, n, TOTAL_PEDIDOS);
    }
    return NULL;
}

/* Monitor: a cada segundo verifica se os pedidos continuam andando.
 * Se nada muda, avisa um possivel deadlock e mostra quem espera quem. */
void *monitor(void *arg) {
    (void)arg;
    long anterior = -1;
    int parado = 0, mostrou_ciclo = 0;
    while (1) {
        for (int i = 0; i < 10; i++) {               /* dorme 1 s em passos de 100 ms */
            if (__atomic_load_n(&terminou, __ATOMIC_RELAXED)) return NULL;
            usleep(100000);
        }
        long atual = __atomic_load_n(&processados, __ATOMIC_RELAXED);
        parado = (atual == anterior) ? parado + 1 : 0;
        anterior = atual;
        if (parado == 0) continue;
        if (parado == 1 || parado % 3 == 0)
            printf("  [Monitor] nenhum pedido tratado ha %d s (%ld de %d): possivel deadlock!\n",
                   parado, atual, TOTAL_PEDIDOS);
        if (modo == 1 && !mostrou_ciclo) {
            mostrou_ciclo = 1;
            for (int i = 0; i < NUM_ATENDENTES; i++) {
                int s = __atomic_load_n(&segura[i], __ATOMIC_RELAXED);
                int w = __atomic_load_n(&aguarda[i], __ATOMIC_RELAXED);
                if (s >= 0 && w >= 0)
                    printf("  [Monitor]   Atendente %d segura C%d e espera C%d\n", i, s, w);
            }
        }
    }
}

long saldo_total(void) {
    long total = 0;
    for (int i = 0; i < NUM_CONTAS; i++) total += contas[i].saldo;
    return total;
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IOLBF, 0);    /* imprime cada linha na hora */
    modo = (argc > 1) ? atoi(argv[1]) : 3;
    if (modo < 0 || modo > 3) {
        fprintf(stderr, "Uso: %s <modo>  (0 = sem sync, 1 = lock sem ordem, "
                        "2 = lock com ordem, 3 = ordem + espera por saldo)\n", argv[0]);
        return 1;
    }

    pthread_t clientes[NUM_CLIENTES], atendentes[NUM_ATENDENTES];
    Estatistica est[NUM_ATENDENTES] = {{0}};
    struct timespec t0, t1;

    for (int i = 0; i < NUM_CONTAS; i++) {
        contas[i].id = i;
        contas[i].saldo = SALDO_INICIAL;
        pthread_mutex_init(&contas[i].lock, NULL);
        pthread_cond_init(&contas[i].tem_saldo, NULL);
    }
    sem_init(&vazio, 0, TAM_FILA);
    sem_init(&cheio, 0, 0);

    printf("=== Modo %d: %s ===\n", modo, NOME_MODO[modo]);
    printf("%d contas x R$ %d | %d clientes, %d atendentes | fila de %d posicoes | %d pedidos\n",
           NUM_CONTAS, SALDO_INICIAL, NUM_CLIENTES, NUM_ATENDENTES, TAM_FILA, TOTAL_PEDIDOS);
    printf("Modo %d | Total inicial: %ld\n", modo, saldo_total());
    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < NUM_ATENDENTES; i++) { segura[i] = -1; aguarda[i] = -1; }
    pthread_t mon;
    pthread_create(&mon, NULL, monitor, NULL);
    pthread_detach(mon);

    for (int i = 0; i < NUM_ATENDENTES; i++) {
        est[i].id = i;
        pthread_create(&atendentes[i], NULL, atendente, &est[i]);
    }
    for (long i = 0; i < NUM_CLIENTES; i++)
        pthread_create(&clientes[i], NULL, cliente, (void *)(i + 1));

    for (int i = 0; i < NUM_CLIENTES; i++)
        pthread_join(clientes[i], NULL);

    /* Todos os pedidos foram enfileirados: envia um encerramento por atendente */
    printf("  [Main] todos os clientes terminaram: enviando %d pedidos de encerramento\n", NUM_ATENDENTES);
    Pedido encerrar = { -1, -1, 0 };
    for (int i = 0; i < NUM_ATENDENTES; i++)
        fila_inserir(encerrar);
    for (int i = 0; i < NUM_ATENDENTES; i++)
        pthread_join(atendentes[i], NULL);

    __atomic_store_n(&terminou, 1, __ATOMIC_RELAXED);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double segundos = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;

    long concluidas = 0, recusadas = 0, esperas = 0, menor = contas[0].saldo;
    for (int i = 0; i < NUM_ATENDENTES; i++) {
        concluidas += est[i].concluidas;
        recusadas  += est[i].recusadas;
        esperas    += est[i].esperas;
    }
    for (int i = 1; i < NUM_CONTAS; i++)
        if (contas[i].saldo < menor) menor = contas[i].saldo;

    printf("Saldos finais:");
    for (int i = 0; i < NUM_CONTAS; i++) printf(" C%d=%ld", i, contas[i].saldo);
    printf("\nPedidos por atendente:");
    for (int i = 0; i < NUM_ATENDENTES; i++) printf(" A%d=%ld", i, est[i].concluidas + est[i].recusadas);
    printf("\n");

    long total = saldo_total();
    int correto = (total == (long)NUM_CONTAS * SALDO_INICIAL) && menor >= 0;
    printf("Modo %d | Total final:   %ld -> %s\n", modo, total,
           correto ? "CORRETO" : "ERRO (condicao de corrida)");
    printf("Modo %d | Pedidos: %ld concluidos, %ld recusados, %ld esperaram por saldo\n",
           modo, concluidas, recusadas, esperas);
    printf("Modo %d | Menor saldo: %ld | Tempo: %.2f s\n", modo, menor, segundos);

    for (int i = 0; i < NUM_CONTAS; i++) {
        pthread_mutex_destroy(&contas[i].lock);
        pthread_cond_destroy(&contas[i].tem_saldo);
    }
    sem_destroy(&vazio);
    sem_destroy(&cheio);
    return 0;
}
