#define _POSIX_C_SOURCE 200809L
#include "decompressor.h"
#include "dictionary.h"
#include "protocol.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* Byte que sinaliza um código no arquivo compactado. */
#define ESCAPE_BYTE      0x1B

/* A cada 100 palavras descompactadas, envia o progresso ao Monitor. */
#define PUBLISH_INTERVAL 100

/* Lê um código numérico no formato LEB128 do arquivo e guarda em *out.
 *
 * O formato LEB128 usa 7 bits úteis por byte. Se o bit mais alto do byte
 * for 1, ainda há mais bytes para ler. Se for 0, é o último byte do código.
 *
 * Retorna:
 *   0  — leitura bem-sucedida
 *  -1  — arquivo terminou antes de começar o código (EOF no primeiro byte)
 *  -2  — sequência inválida: arquivo terminou no meio do código ou passou de 35 bits */
static int read_varint(FILE *stream, uint32_t *out) {
    uint32_t value = 0;
    unsigned shift = 0;
    for (;;) {
        int byte = fgetc(stream);
        if (byte == EOF) {
            /* Se ainda não lemos nenhum byte, é EOF normal antes do código.
             * Se já lemos algum byte, o código está incompleto — erro. */
            return (shift == 0) ? -1 : -2;
        }
        /* Pega os 7 bits úteis e coloca na posição certa dentro do valor. */
        value |= (uint32_t)(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            /* Bit alto em zero: esse é o último byte do código. */
            *out = value;
            return 0;
        }
        shift += 7;
        if (shift >= 35) {
            /* Código grande demais para caber em uint32_t — arquivo corrompido. */
            return -2;
        }
    }
}

/* Envia uma cópia das estatísticas atuais ao Monitor pela memória compartilhada.
 * Retorna 0 se der certo ou -1 se a publicação falhar. */
static int publish(MonitorSharedState *shm_state, const MonitorStatistics *stats) {
    if (monitor_shared_publish(shm_state, stats) < 0) {
        fprintf(stderr, "[ERRO] Falha ao publicar estatísticas: %s\n",
                strerror(errno));
        return -1;
    }
    return 0;
}

/* Inicia o Monitor como processo filho usando fork + exec.
 *
 * O processo filho fecha o acesso à memória compartilhada herdado do pai
 * (ele vai reabrir pelo nome) e executa build/monitor passando o nome da
 * memória compartilhada como argumento.
 *
 * Retorna o PID do filho em caso de sucesso ou -1 se o fork falhar. */
static pid_t launch_monitor(const char *shm_name, MonitorSharedState *shm_state,
                             int shm_fd) {
    pid_t child = fork();
    if (child < 0) {
        perror("[ERRO] Falha ao criar o processo do Monitor");
        return -1;
    }
    if (child == 0) {
        /* Processo filho: fecha o descritor herdado antes de executar o Monitor. */
        monitor_shared_close(shm_fd, shm_state);
        execl("./build/monitor", "monitor", shm_name, (char *)NULL);
        perror("[ERRO] Falha ao iniciar o Monitor");
        _exit(127);
    }
    /* Processo pai: retorna o PID do filho. */
    return child;
}

/* Descompacta o arquivo compactado e reconstrói o texto original.
 *
 * Fluxo principal:
 *   1. Carrega o dicionário.
 *   2. Cria a memória compartilhada e inicia o Monitor.
 *   3. Lê o arquivo .mzp byte a byte:
 *        - Byte comum      → copia direto para a saída.
 *        - 0x1B + 0x00     → grava o byte 0x1B literal.
 *        - 0x1B + código   → busca a palavra no dicionário e grava.
 *   4. Envia as estatísticas finais ao Monitor e aguarda ele encerrar.
 *   5. Libera todos os recursos.
 *
 * Retorna 0 se a descompactação for bem-sucedida ou 1 se houver erro. */
int decompressor_run(const char *dict_path,
                     const char *input_path,
                     const char *output_path) {
    int exit_code = 1;

    /* Carrega o dicionário de códigos e palavras. */
    Dictionary *dict = dictionary_load(dict_path);
    if (dict == NULL) {
        fprintf(stderr, "[ERRO] Falha ao carregar o dicionário: %s\n", dict_path);
        return 1;
    }

    /* Cria um nome único para a memória compartilhada usando o PID do processo,
     * evitando colisões caso o programa seja executado mais de uma vez ao mesmo tempo. */
    char shm_name[MONITOR_SHM_NAME_MAX];
    int len = snprintf(shm_name, sizeof(shm_name), "/pfc_decomp_%ld", (long)getpid());
    if (len < 0 || (size_t)len >= sizeof(shm_name)) {
        fprintf(stderr, "[ERRO] Nome da memória compartilhada muito longo.\n");
        dictionary_destroy(dict);
        return 1;
    }

    /* Cria a memória compartilhada POSIX e inicializa os semáforos. */
    int shm_fd = -1;
    MonitorSharedState *shm_state = NULL;
    if (monitor_shared_create(shm_name, &shm_fd, &shm_state) < 0) {
        fprintf(stderr, "[ERRO] Falha ao criar memória compartilhada: %s\n",
                strerror(errno));
        dictionary_destroy(dict);
        return 1;
    }

    /* Inicia o Monitor como processo filho. */
    pid_t monitor_pid = launch_monitor(shm_name, shm_state, shm_fd);
    if (monitor_pid < 0) {
        monitor_shared_destroy(shm_name, shm_fd, shm_state);
        dictionary_destroy(dict);
        return 1;
    }

    /* Abre o arquivo compactado para leitura binária. */
    FILE *input = fopen(input_path, "rb");
    if (input == NULL) {
        fprintf(stderr, "[ERRO] Não foi possível abrir o arquivo de entrada: %s\n", input_path);
        /* Avisa o Monitor que houve erro antes mesmo de começar. */
        MonitorStatistics err_stats = {0};
        err_stats.mode     = MONITOR_MODE_DECOMPRESS;
        err_stats.finished = 1;
        err_stats.failed   = 1;
        publish(shm_state, &err_stats);
        goto cleanup_monitor;
    }

    /* Abre o arquivo de saída para gravação binária. */
    FILE *output = fopen(output_path, "wb");
    if (output == NULL) {
        fprintf(stderr, "[ERRO] Não foi possível abrir o arquivo de saída: %s\n", output_path);
        fclose(input);
        MonitorStatistics err_stats = {0};
        err_stats.mode     = MONITOR_MODE_DECOMPRESS;
        err_stats.finished = 1;
        err_stats.failed   = 1;
        publish(shm_state, &err_stats);
        goto cleanup_monitor;
    }

    MonitorStatistics stats = {0};
    stats.mode = MONITOR_MODE_DECOMPRESS;

    int decode_error = 0;
    uint64_t tokens_since_publish = 0;
    int byte;

    /* Loop principal: processa o arquivo compactado byte a byte. */
    while ((byte = fgetc(input)) != EOF) {
        if (byte != ESCAPE_BYTE) {
            /* Byte comum (espaço, pontuação, letra sem código etc.): copia direto. */
            if (fputc(byte, output) == EOF) {
                fprintf(stderr, "[ERRO] Falha ao gravar no arquivo de saída.\n");
                decode_error = 1;
                break;
            }
            stats.decompressed_size++;
            continue;
        }

        /* Encontrou o byte de escape (0x1B): lê o código que vem a seguir.
         * Código zero significa o byte 0x1B literal; qualquer outro é um código do dicionário. */
        uint32_t code = 0;
        int varint_result = read_varint(input, &code);
        if (varint_result == -1) {
            fprintf(stderr, "[ERRO] O arquivo terminou logo após o byte de escape.\n");
            decode_error = 1;
            break;
        }
        if (varint_result == -2) {
            fprintf(stderr, "[ERRO] Código LEB128 inválido no arquivo de entrada.\n");
            decode_error = 1;
            break;
        }

        if (code == 0) {
            /* Sequência 0x1B 0x00: recupera o byte 0x1B que existia no texto original. */
            if (fputc(ESCAPE_BYTE, output) == EOF) {
                fprintf(stderr, "[ERRO] Falha ao gravar no arquivo de saída.\n");
                decode_error = 1;
                break;
            }
            stats.decompressed_size++;
            continue;
        }

        /* Código maior que zero: busca a palavra correspondente no dicionário. */
        const char *word = dictionary_decode(dict, code);
        if (word == NULL) {
            fprintf(stderr, "[ERRO] Código de dicionário desconhecido %" PRIu32 ".\n", code);
            decode_error = 1;
            break;
        }

        /* Grava a palavra inteira no arquivo de saída. */
        size_t word_len = strlen(word);
        if (fwrite(word, 1, word_len, output) != word_len) {
            fprintf(stderr, "[ERRO] Falha ao gravar no arquivo de saída.\n");
            decode_error = 1;
            break;
        }

        stats.words_read++;
        stats.decompressed_size += word_len;
        tokens_since_publish++;

        /* A cada PUBLISH_INTERVAL palavras, envia o progresso parcial ao Monitor. */
        if (tokens_since_publish >= PUBLISH_INTERVAL) {
            /* Atualiza os tamanhos antes de enviar o progresso ao Monitor. */
            stats.compressed_size = (uint64_t)ftell(input);
            stats.original_size   = stats.decompressed_size;
            if (publish(shm_state, &stats) < 0) {
                decode_error = 1;
                break;
            }
            tokens_since_publish = 0;
        }
    }

    /* Verifica se o laço terminou por erro de leitura ou por EOF normal. */
    if (!decode_error && ferror(input)) {
        fprintf(stderr, "[ERRO] Falha na leitura do arquivo de entrada.\n");
        decode_error = 1;
    }

    fclose(input);
    if (fclose(output) != 0 && !decode_error) {
        fprintf(stderr, "[ERRO] Falha ao finalizar a gravação do arquivo de saída.\n");
        decode_error = 1;
    }

    /* Reabre a entrada para medir o tamanho final do arquivo compactado.
     * O ftell durante o loop perde precisão após o fclose, por isso reabrimos. */
    {
        FILE *in_check = fopen(input_path, "rb");
        if (in_check != NULL) {
            if (fseek(in_check, 0, SEEK_END) == 0) {
                long sz = ftell(in_check);
                if (sz >= 0) stats.compressed_size = (uint64_t)sz;
            }
            fclose(in_check);
        }
    }

    /* Envia as estatísticas finais ao Monitor com finished = 1 para ele encerrar. */
    stats.original_size = stats.decompressed_size;
    stats.finished = 1;
    stats.failed   = decode_error ? 1u : 0u;
    publish(shm_state, &stats);

    exit_code = decode_error ? 1 : 0;

cleanup_monitor:
    /* Aguarda o Monitor terminar antes de liberar a memória compartilhada. */
    {
        int status = 0;
        if (waitpid(monitor_pid, &status, 0) < 0) {
            perror("[ERRO] Falha ao aguardar o Monitor");
            exit_code = 1;
        } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            exit_code = 1;
        }
    }
    /* Remove a memória compartilhada e destroi os semáforos. */
    if (monitor_shared_destroy(shm_name, shm_fd, shm_state) < 0) {
        perror("[ERRO] Falha ao liberar a memória compartilhada");
        exit_code = 1;
    }
    dictionary_destroy(dict);
    return exit_code;
}
