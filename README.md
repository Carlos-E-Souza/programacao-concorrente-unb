# Banco Concorrente com Pthreads

Trabalho Final de **Programação Concorrente**
Autor: Carlos Eduardo Souza (211061510)

## Descrição

Este projeto simula um banco em que **clientes** (threads produtoras) colocam pedidos de transferência em uma **fila limitada** e **atendentes** (threads consumidoras) retiram esses pedidos e movem o dinheiro entre contas guardadas em **memória compartilhada**. Como o dinheiro só muda de conta, a soma de todos os saldos deve ser sempre a mesma:

```
total = NUM_CONTAS × SALDO_INICIAL = 10 × 1000 = 10.000
```

O sistema tem três pontos de concorrência, cada um resolvido com um mecanismo diferente:

| Recurso compartilhado | Problema sem sincronização | Mecanismo usado |
|-----------------------|----------------------------|-----------------|
| Fila de pedidos | Dois clientes gravam na mesma posição e pedidos se perdem | Semáforos `vazio`/`cheio` + mutex (produtor-consumidor) |
| Saldos das contas | `saldo = saldo - valor` não é atômico e uma gravação sobrescreve a outra | Um mutex por conta, travado em **ordem global** (menor `id` primeiro) |
| Saldo insuficiente | Recusar o pedido na hora, ou esperar em laço gastando CPU | Uma **variável de condição** por conta, com tempo limite |

Na versão final, quando a conta de origem não tem saldo, o atendente dorme em `pthread_cond_timedwait` até a conta receber um crédito (`pthread_cond_broadcast`). Se o dinheiro não chegar em `ESPERA_MAX_MS`, o pedido é recusado. Esse tempo limite impede que o sistema pare quando todos os atendentes estão esperando ao mesmo tempo e ninguém faz créditos.

## Modos de execução

O mesmo programa tem quatro versões da transferência, escolhidas por um parâmetro na linha de comando. A fila é sempre sincronizada; o que muda é o acesso às contas.

| Modo | Função | Sincronização das contas | Resultado esperado |
|------|--------|--------------------------|--------------------|
| `0` | `transferir_inseguro` | Nenhuma | Total errado (condição de corrida) |
| `1` | `transferir_ingenuo` | Mutex por conta, trava origem e depois destino | Trava (**deadlock**) |
| `2` | `transferir_ordenado` | Mutex por conta em ordem global; recusa se faltar saldo | Total correto |
| `3` | `transferir_com_espera` | Ordem global + variável de condição | Total correto e menos pedidos recusados (**versão final**) |

O modo `1` entra em deadlock quando um atendente executa `transferir(C1, C2)` e outro executa `transferir(C2, C1)` ao mesmo tempo: cada um segura um lock e espera o do outro. A ordem global de travamento elimina essa espera circular. Para que o deadlock apareça em toda execução, `transferir_ingenuo` chama `sched_yield()` entre os dois locks, o que aumenta a chance de outra thread entrar nesse intervalo.

## Parâmetros da simulação

Definidos no início de `banco.c`:

| Constante | Valor | Significado |
|-----------|-------|-------------|
| `NUM_CONTAS` | 10 | Quantidade de contas |
| `SALDO_INICIAL` | 1000 | Saldo inicial de cada conta |
| `VALOR_MAXIMO` | 100 | Cada pedido transfere entre 1 e 100 |
| `NUM_CLIENTES` | 4 | Threads produtoras |
| `NUM_ATENDENTES` | 8 | Threads consumidoras |
| `PEDIDOS_POR_CLIENTE` | 25000 | Pedidos gerados por cada cliente (100.000 no total) |
| `TAM_FILA` | 16 | Capacidade da fila de pedidos |
| `ESPERA_MAX_MS` | 5 | Tempo máximo de espera por saldo, em milissegundos |

## Pré-requisitos

O programa usa **POSIX Pthreads** e **semáforos POSIX** (`semaphore.h`), então precisa de um ambiente compatível com POSIX:

| Sistema | O que instalar |
|---------|----------------|
| Linux (Ubuntu/Debian) | `sudo apt install build-essential` |
| Windows (recomendado) | **WSL** com Ubuntu e depois `sudo apt install build-essential` |
| Windows (alternativa) | **MSYS2** com `pacman -S gcc make` (use o terminal MSYS2) |

São necessários `gcc`, `make` e o comando `timeout` (já vêm no Linux, WSL e MSYS2). O macOS não é suportado, porque não implementa semáforos sem nome (`sem_init`).

## Como compilar e executar

Na pasta do projeto:

```bash
make                # compila e gera o executável ./banco
make run            # executa as quatro versões em sequência
make run-race       # modo 0: sem sincronização
make run-deadlock   # modo 1: lock sem ordem (interrompido após 10 s se travar)
make run-ordem      # modo 2: lock com ordem global
make run-final      # modo 3: ordem global + espera por saldo
make tsan           # modo 3 com ThreadSanitizer (detecta condições de corrida)
make clean          # remove os executáveis
```

Também é possível compilar e executar manualmente:

```bash
gcc -Wall -Wextra -pthread -o banco banco.c
./banco 3
./banco 1    # vai travar: use Ctrl+C para encerrar
```

O tempo limite do modo 1 pode ser alterado: `make run-deadlock TIMEOUT=5`.

## O que aparece no terminal

Cada execução imprime cerca de 25 linhas. Os prints usam contadores atômicos só para limitar a saída; eles não fazem parte da solução.

| Linha | O que mostra |
|-------|--------------|
| `=== Modo N ... ===` | Versão executada e parâmetros da simulação |
| `[Atendente X] pedido Ca -> Cb ...` | Os 3 primeiros pedidos tratados e se foram concluídos ou recusados |
| `[Atendente X] Ca sem saldo ...` | Modo 3: os 3 primeiros atendentes que dormiram na variável de condição, se acordaram com crédito e alguns pedidos recusados por tempo limite |
| `[Progresso] 20% ...` | Andamento a cada 20% dos 100.000 pedidos |
| `[Cliente X] colocou seus ... pedidos` | Fim de cada produtor |
| `[Main] ...` | Envio dos pedidos de encerramento aos atendentes |
| `[Monitor] ... possivel deadlock!` | Modo 1: nenhum pedido andou em 1 s; em seguida mostra qual conta cada atendente segura e qual espera, deixando visível a espera circular |
| `Saldos finais` / `Pedidos por atendente` | Estado final das contas e divisão do trabalho |
| `Modo N \| Total final ...` | Resultado: total, pedidos concluídos e recusados, menor saldo e tempo |

### Modo 3 (versão final)

```
=== Modo 3: ordem global + espera por saldo ===
10 contas x R$ 1000 | 4 clientes, 8 atendentes | fila de 16 posicoes | 100000 pedidos
Modo 3 | Total inicial: 10000
  [Atendente 0] pedido C8 -> C3 de R$  16: concluido
  [Atendente 0] pedido C1 -> C7 de R$  11: concluido
  [Atendente 0] pedido C9 -> C2 de R$  87: concluido
  [Atendente 5] C4 sem saldo (tem 5, precisa 40): dormindo ate receber credito...
  [Atendente 0] C4 sem saldo (tem 22, precisa 83): dormindo ate receber credito...
  [Atendente 7] C4 sem saldo (tem 22, precisa 46): dormindo ate receber credito...
  [Atendente 7] C4 recebeu credito (saldo agora 143): acordou e tenta de novo
  [Atendente 0] C4 recebeu credito (saldo agora 97): acordou e tenta de novo
  [Atendente 5] C4 recebeu credito (saldo agora 275): acordou e tenta de novo
  [Atendente 0] C2 nao recebeu credito em 5 ms (tem 28, precisa 65): pedido recusado
  [Progresso]  20% dos pedidos tratados (20000 de 100000)
  ...
  [Progresso] 100% dos pedidos tratados (100000 de 100000)
Saldos finais: C0=2986 C1=2813 C2=2167 C3=81 C4=44 C5=59 C6=435 C7=33 C8=433 C9=949
Pedidos por atendente: A0=12737 A1=12564 A2=12031 A3=12166 A4=12706 A5=12248 A6=13287 A7=12261
Modo 3 | Total final:   10000 -> CORRETO
Modo 3 | Pedidos: 95628 concluidos, 4372 recusados, 12319 esperaram por saldo
Modo 3 | Menor saldo: 33 | Tempo: 4.39 s
```

### Modo 1 (deadlock)

```
=== Modo 1: lock por conta sem ordem ===
  [Atendente 2] pedido C8 -> C3 de R$  16: concluido
  ...
  [Monitor] nenhum pedido tratado ha 1 s (27 de 100000): possivel deadlock!
  [Monitor]   Atendente 0 segura C8 e espera C7
  [Monitor]   Atendente 3 segura C1 e espera C7
  [Monitor]   Atendente 4 segura C7 e espera C3
  [Monitor]   Atendente 5 segura C4 e espera C7
  [Monitor]   Atendente 6 segura C3 e espera C1
  [Monitor] nenhum pedido tratado ha 3 s (27 de 100000): possivel deadlock!
Programa interrompido apos 10s: deadlock
```

Os atendentes 4, 6 e 3 formam uma espera circular: o 4 segura C7 e espera C3, que está com o 6; o 6 espera C1, que está com o 3; e o 3 espera C7, que está com o 4. Nenhum deles pode avançar.

### Resumo dos quatro modos

| Modo | Total final | Pedidos recusados | Tempo |
|------|-------------|-------------------|-------|
| 0 | Muda a cada execução (ex.: 9.776) | ~5.700 | ~0,2 s |
| 1 | Trava (deadlock) | – | – |
| 2 | 10.000 | ~5.200 | ~0,2 s |
| 3 | 10.000 | ~4.400 | ~4,4 s |

O total do modo 0 muda a cada execução, pois depende da ordem em que o sistema operacional escalona as threads. O modo 3 demora mais que o modo 2 porque os atendentes realmente esperam o dinheiro chegar, em vez de recusar o pedido na hora.

## Estrutura do projeto

```
.
├── banco.c     # código-fonte (fila, contas e as quatro versões da transferência)
├── Makefile    # compilação e execução
└── README.md   # este arquivo
```

## Funções Pthreads utilizadas

Threads: `pthread_create`, `pthread_join` e `pthread_detach` (monitor).
Mutex: `pthread_mutex_init`, `pthread_mutex_lock`, `pthread_mutex_unlock` e `pthread_mutex_destroy`.
Variáveis de condição: `pthread_cond_init`, `pthread_cond_timedwait`, `pthread_cond_broadcast` e `pthread_cond_destroy`.
Semáforos: `sem_init`, `sem_wait`, `sem_post` e `sem_destroy`.
