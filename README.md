# Banco Concorrente com Pthreads

Trabalho Final de **Programação Concorrente**
Autor: Carlos Eduardo Souza (211061510)

## Descrição

Este projeto simula um banco em que várias threads (clientes) fazem transferências aleatórias entre contas guardadas em **memória compartilhada**. Como o dinheiro só muda de conta, a soma de todos os saldos deve ser sempre a mesma:

```
total = NUM_CONTAS × SALDO_INICIAL = 10 × 1000 = 10.000
```

Sem sincronização, a operação `saldo = saldo - valor` sofre **condição de corrida**: duas threads leem o mesmo saldo e uma gravação sobrescreve a outra. Com isso, dinheiro é criado ou destruído e o total final fica errado.

O programa tem três versões da função de transferência, escolhidas por um parâmetro na linha de comando:

| Modo | Função | Sincronização | Resultado esperado |
|------|--------|---------------|--------------------|
| `0` | `transferir_inseguro` | Nenhuma | Total errado (condição de corrida) |
| `1` | `transferir_ingenuo` | Um mutex por conta, trava origem e depois destino | Trava (**deadlock**) |
| `2` | `transferir_seguro` | Um mutex por conta, trava sempre o menor `id` primeiro | Total correto (10.000) |

O modo `1` entra em deadlock quando a thread A executa `transferir(C1, C2)` e a thread B executa `transferir(C2, C1)` ao mesmo tempo: cada uma segura um lock e espera o da outra. O modo `2` resolve isso com uma **ordem global de travamento**, que elimina a espera circular.

## Parâmetros da simulação

Definidos no início de `banco.c`:

| Constante | Valor | Significado |
|-----------|-------|-------------|
| `NUM_CONTAS` | 10 | Quantidade de contas |
| `SALDO_INICIAL` | 1000 | Saldo inicial de cada conta |
| `NUM_THREADS` | 8 | Quantidade de clientes (threads) |
| `TRANSF_POR_THREAD` | 200000 | Transferências feitas por cada thread |

## Pré-requisitos

O programa usa a biblioteca **POSIX Pthreads**, então precisa de um ambiente compatível com POSIX:

| Sistema | O que instalar |
|---------|----------------|
| Linux (Ubuntu/Debian) | `sudo apt install build-essential` |
| Windows (recomendado) | **WSL** com Ubuntu e depois `sudo apt install build-essential` |
| Windows (alternativa) | **MSYS2** com `pacman -S gcc make` (use o terminal MSYS2) |
| macOS | Xcode Command Line Tools: `xcode-select --install` |

São necessários `gcc`, `make` e o comando `timeout` (já vem no Linux/WSL e no MSYS2).

## Como compilar e executar

Na pasta do projeto:

```bash
make                # compila e gera o executável ./banco
make run            # executa as três versões em sequência
make run-race       # modo 0: sem sincronização
make run-deadlock   # modo 1: lock sem ordem (interrompido após 10 s se travar)
make run-safe       # modo 2: lock com ordem global
make clean          # remove o executável
```

Também é possível compilar e executar manualmente:

```bash
gcc -Wall -Wextra -pthread -o banco banco.c
./banco 0
./banco 1    # vai travar: use Ctrl+C para encerrar
./banco 2
```

O tempo limite do modo 1 pode ser alterado: `make run-deadlock TIMEOUT=5`.

## Exemplo de saída

```
== Versao 0: sem sincronizacao ==
Modo 0 | Total inicial: 10000
Modo 0 | Total final:   37823 -> ERRO (condicao de corrida)
== Versao 1: lock sem ordem (pode travar) ==
Programa interrompido apos 10s: deadlock
== Versao 2: lock com ordem global ==
Modo 2 | Total inicial: 10000
Modo 2 | Total final:   10000 -> CORRETO
```

O total do modo 0 muda a cada execução, pois depende da ordem em que o sistema operacional escalona as threads.

## Estrutura do projeto

```
.
├── banco.c     # código-fonte (as três versões da transferência)
├── Makefile    # compilação e execução
└── README.md   # este arquivo
```

## Funções Pthreads utilizadas

`pthread_create`, `pthread_join`, `pthread_mutex_init`, `pthread_mutex_lock`, `pthread_mutex_unlock` e `pthread_mutex_destroy`.
