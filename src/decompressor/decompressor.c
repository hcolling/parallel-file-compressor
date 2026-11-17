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

#define ESCAPE_BYTE      0x1B
#define PUBLISH_INTERVAL 100

/* Lê um código em LEB128 e guarda o resultado em *out.
 * Retorna 0 se der certo, -1 se o arquivo acabou antes do código
 * e -2 se a sequência estiver inválida ou incompleta. */
static int read_varint(FILE *stream, uint32_t *out) {
    uint32_t value = 0;
    unsigned shift = 0;
    for (;;) {
        int byte = fgetc(stream);
        if (byte == EOF) {
            return (shift == 0) ? -1 : -2;
        }
        value |= (uint32_t)(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            *out = value;
            return 0;
        }
        shift += 7;
        if (shift >= 35) {
            return -2;
        }
    }
}

/* Envia as estatísticas atuais ao Monitor pela memória compartilhada. */
static int publish(MonitorSharedState *shm_state, const MonitorStatistics *stats) {
    if (monitor_shared_publish(shm_state, stats) < 0) {
        fprintf(stderr, "[ERRO] Falha ao publicar estatísticas: %s\n",
                strerror(errno));
        return -1;
    }
    return 0;
}

/* Inicia o Monitor em outro processo. Retorna o PID ou -1 se houver erro. */
static pid_t launch_monitor(const char *shm_name, MonitorSharedState *shm_state,
                             int shm_fd) {
    pid_t child = fork();
    if (child < 0) {
        perror("[ERRO] Falha ao criar o processo do Monitor");
        return -1;
    }
    if (child == 0) {
        monitor_shared_close(shm_fd, shm_state);
        execl("./build/monitor", "monitor", shm_name, (char *)NULL);
        perror("[ERRO] Falha ao iniciar o Monitor");
        _exit(127);
    }
    return child;
}

int decompressor_run(const char *dict_path,
                     const char *input_path,
                     const char *output_path) {
    int exit_code = 1;

    Dictionary *dict = dictionary_load(dict_path);
    if (dict == NULL) {
        fprintf(stderr, "[ERRO] Falha ao carregar o dicionário: %s\n", dict_path);
        return 1;
    }

    char shm_name[MONITOR_SHM_NAME_MAX];
    int len = snprintf(shm_name, sizeof(shm_name), "/pfc_decomp_%ld", (long)getpid());
    if (len < 0 || (size_t)len >= sizeof(shm_name)) {
        fprintf(stderr, "[ERRO] Nome da memória compartilhada muito longo.\n");
        dictionary_destroy(dict);
        return 1;
    }

    int shm_fd = -1;
    MonitorSharedState *shm_state = NULL;
    if (monitor_shared_create(shm_name, &shm_fd, &shm_state) < 0) {
        fprintf(stderr, "[ERRO] Falha ao criar memória compartilhada: %s\n",
                strerror(errno));
        dictionary_destroy(dict);
        return 1;
    }

    pid_t monitor_pid = launch_monitor(shm_name, shm_state, shm_fd);
    if (monitor_pid < 0) {
        monitor_shared_destroy(shm_name, shm_fd, shm_state);
        dictionary_destroy(dict);
        return 1;
    }

    FILE *input = fopen(input_path, "rb");
    if (input == NULL) {
        fprintf(stderr, "[ERRO] Não foi possível abrir o arquivo de entrada: %s\n", input_path);
        MonitorStatistics err_stats = {0};
        err_stats.mode     = MONITOR_MODE_DECOMPRESS;
        err_stats.finished = 1;
        err_stats.failed   = 1;
        publish(shm_state, &err_stats);
        goto cleanup_monitor;
    }

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

    while ((byte = fgetc(input)) != EOF) {
        if (byte != ESCAPE_BYTE) {
            if (fputc(byte, output) == EOF) {
                fprintf(stderr, "[ERRO] Falha ao gravar no arquivo de saída.\n");
                decode_error = 1;
                break;
            }
            stats.decompressed_size++;
            continue;
        }

        /* Após o escape, zero representa o byte 0x1B; os demais valores são códigos. */
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
            /* Recupera o byte 0x1B do texto original. */
            if (fputc(ESCAPE_BYTE, output) == EOF) {
                fprintf(stderr, "[ERRO] Falha ao gravar no arquivo de saída.\n");
                decode_error = 1;
                break;
            }
            stats.decompressed_size++;
            continue;
        }

        /* Busca a palavra correspondente ao código. */
        const char *word = dictionary_decode(dict, code);
        if (word == NULL) {
            fprintf(stderr, "[ERRO] Código de dicionário desconhecido %" PRIu32 ".\n", code);
            decode_error = 1;
            break;
        }

        size_t word_len = strlen(word);
        if (fwrite(word, 1, word_len, output) != word_len) {
            fprintf(stderr, "[ERRO] Falha ao gravar no arquivo de saída.\n");
            decode_error = 1;
            break;
        }

        stats.words_read++;
        stats.decompressed_size += word_len;
        tokens_since_publish++;

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

    if (!decode_error && ferror(input)) {
        fprintf(stderr, "[ERRO] Falha na leitura do arquivo de entrada.\n");
        decode_error = 1;
    }

    fclose(input);
    if (fclose(output) != 0 && !decode_error) {
        fprintf(stderr, "[ERRO] Falha ao finalizar a gravação do arquivo de saída.\n");
        decode_error = 1;
    }

    /* Reabre a entrada para medir o tamanho final do arquivo compactado. */
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
    stats.original_size = stats.decompressed_size;
    stats.finished = 1;
    stats.failed   = decode_error ? 1u : 0u;
    publish(shm_state, &stats);

    exit_code = decode_error ? 1 : 0;

cleanup_monitor:
    {
        int status = 0;
        if (waitpid(monitor_pid, &status, 0) < 0) {
            perror("[ERRO] Falha ao aguardar o Monitor");
            exit_code = 1;
        } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            exit_code = 1;
        }
    }
    if (monitor_shared_destroy(shm_name, shm_fd, shm_state) < 0) {
        perror("[ERRO] Falha ao liberar a memória compartilhada");
        exit_code = 1;
    }
    dictionary_destroy(dict);
    return exit_code;
}
