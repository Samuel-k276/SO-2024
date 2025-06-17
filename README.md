# Sistemas Operativos 2024/2025

Este repositório contém os projetos desenvolvidos para a cadeira de Sistemas Operativos do ano letivo 2024/2025. O objetivo principal é implementar conceitos fundamentais de sistemas operativos, como gestão de processos, sincronização, comunicação entre processos, manipulação de ficheiros, utilização de semáforos, variáveis de condição, threads e filas de tarefas.

## Estrutura do Repositório

- **proj1/**: Implementação do primeiro projeto.
  - Este projeto implementa um servidor que processa jobs em paralelo, utilizando uma estrutura de dados Key-Value Store (KVS) para armazenar pares de chave-valor. Ele suporta concorrência através do uso de threads, processos, semáforos e locks para garantir a sincronização e a integridade dos dados.
  - **Funcionalidades Principais:**
    - **Execução Paralela de Jobs:** O servidor processa múltiplos jobs simultaneamente, utilizando um pool de threads.
    - **Sincronização e Concorrência:** Uso de mutexes, semáforos e variáveis de condição para gerenciar o acesso concorrente à KVS.
    - **Operações Suportadas:**
      - `WRITE`: Escreve pares de chave-valor na KVS.
      - `READ`: Lê valores associados a chaves específicas.
      - `DELETE`: Remove pares de chave-valor.
      - `SHOW`: Exibe o estado atual da KVS.
      - `BACKUP`: Cria backups da KVS em arquivos.
      - `WAIT`: Introduz atrasos na execução de comandos.
    - **Gestão de Tarefas:** Uma fila de tarefas é utilizada para distribuir os jobs entre as threads do pool.
    - **Comunicação entre Processos:** Uso de processos filhos para realizar backups de forma assíncrona.
  - **Estrutura do Diretório:**
    - `auxiliar.c`, `auxiliar.h`: Funções auxiliares para o projeto.
    - `constants.h`: Definição de constantes utilizadas no projeto.
    - `kvs.c`, `kvs.h`: Implementação da Key-Value Store.
    - `main.c`: Ponto de entrada do programa.
    - `operations.c`, `operations.h`: Implementação das operações principais.
    - `parser.c`, `parser.h`: Funções para análise e interpretação de comandos.
    - **jobs/**: Diretório contendo arquivos `.job` e `.out` para testes.
    - `Makefile`: Script para compilar o projeto.

- **proj2/**: Expansão do primeiro projeto.
  - Este projeto constrói sobre a base do `proj1`, adicionando um sistema de subscrições para permitir que clientes sejam notificados de eventos relevantes. Ele utiliza pipes para comunicação entre processos, implementando um modelo de publicação e subscrição.
  - **Funcionalidades Adicionais:**
    - **Subscrição de Clientes:** Os clientes podem subscrever eventos específicos.
    - **Notificação de Eventos:** Quando um evento ocorre, os clientes subscritos são notificados através de pipes.
    - **Gestão de Subscrições:** O sistema permite adicionar e remover subscrições dinamicamente.
    - **Comunicação entre Processos:** Implementação de comunicação eficiente utilizando pipes nomeados (named pipes).
  - **Estrutura do Diretório:**
    - **src/**: Contém o código-fonte do projeto.
      - **client/**: Código relacionado aos clientes que interagem com o sistema.
        - `api.c`, `api.h`: API para comunicação com o servidor.
        - `main.c`: Ponto de entrada do cliente.
        - `parser.c`, `parser.h`: Funções para análise de comandos do cliente.
      - **common/**: Código compartilhado entre cliente e servidor.
        - `constants.h`: Definições de constantes globais.
        - `io.c`, `io.h`: Funções de entrada e saída.
        - `protocol.h`: Definições do protocolo de comunicação.
      - **server/**: Código relacionado ao servidor.
        - `constants.h`: Definições de constantes específicas do servidor.
        - `io.c`, `io.h`: Funções de entrada e saída específicas do servidor.
        - `jobs/`: Diretório contendo arquivos de teste e backups de jobs.
        - `kvs.c`, `kvs.h`: Implementação de uma estrutura de dados (Key-Value Store).
        - `main.c`: Ponto de entrada do servidor.
        - `operations.c`, `operations.h`: Implementação das operações principais do servidor.
        - `parser.c`, `parser.h`: Funções para análise de comandos do servidor.
      - **tests/**: Diretório contendo arquivos de teste.

## Dependências

- Compilador C (gcc ou equivalente).
- Sistema operacional com suporte a pipes nomeados (Linux ou similar).