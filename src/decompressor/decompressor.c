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

/* Lê um varint sem sinal codificado em LEB128 do fluxo de entrada.
 * Retorna 0 em caso de sucesso e escreve o valor decodificado em *out.
 * Retorna -1 se encontrar EOF antes do primeiro byte, -2 se a sequência for inválida. */
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

/* Publica as estatísticas atuais na memória compartilhada. */
static int publish(MonitorSharedState *shm_state, const MonitorStatistics *stats) {
    if (monitor_shared_publish(shm_state, stats) < 0) {
        fprintf(stderr, "[ERROR] Failed to publish statistics: %s\n",
                strerror(errno));
        return -1;
    }
    return 0;
}

/* Lança build/monitor como processo filho.
 * Retorna o PID do filho em caso de sucesso, -1 em caso de falha. */
static pid_t launch_monitor(const char *shm_name, MonitorSharedState *shm_state,
                             int shm_fd) {
    pid_t child = fork();
    if (child < 0) {
        perror("[ERROR] fork");
        return -1;
    }
    if (child == 0) {
        monitor_shared_close(shm_fd, shm_state);
        execl("./build/monitor", "monitor", shm_name, (char *)NULL);
        perror("[ERROR] exec monitor");
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
        fprintf(stderr, "[ERROR] Failed to load dictionary: %s\n", dict_path);
        return 1;
    }

    char shm_name[MONITOR_SHM_NAME_MAX];
    int len = snprintf(shm_name, sizeof(shm_name), "/pfc_decomp_%ld", (long)getpid());
    if (len < 0 || (size_t)len >= sizeof(shm_name)) {
        fprintf(stderr, "[ERROR] Shared memory name too long.\n");
        dictionary_destroy(dict);
        return 1;
    }

    int shm_fd = -1;
    MonitorSharedState *shm_state = NULL;
    if (monitor_shared_create(shm_name, &shm_fd, &shm_state) < 0) {
        fprintf(stderr, "[ERROR] Failed to create shared memory: %s\n",
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
        fprintf(stderr, "[ERROR] Cannot open input file: %s\n", input_path);
        goto cleanup_monitor;
    }

    FILE *output = fopen(output_path, "wb");
    if (output == NULL) {
        fprintf(stderr, "[ERROR] Cannot open output file: %s\n", output_path);
        fclose(input);
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
                fprintf(stderr, "[ERROR] Write error on output file.\n");
                decode_error = 1;
                break;
            }
            stats.decompressed_size++;
            continue;
        }

        /* Byte de escape: o próximo é 0x00 (literal 0x1B) ou um código LEB128. */
        uint32_t code = 0;
        int varint_result = read_varint(input, &code);
        if (varint_result == -1) {
            fprintf(stderr, "[ERROR] Unexpected EOF after escape byte.\n");
            decode_error = 1;
            break;
        }
        if (varint_result == -2) {
            fprintf(stderr, "[ERROR] Malformed LEB128 varint in input.\n");
            decode_error = 1;
            break;
        }

        if (code == 0) {
            /* 0x1B literal escapado. */
            if (fputc(ESCAPE_BYTE, output) == EOF) {
                fprintf(stderr, "[ERROR] Write error on output file.\n");
                decode_error = 1;
                break;
            }
            stats.decompressed_size++;
            continue;
        }

        /* Token do dicionário. */
        const char *word = dictionary_decode(dict, code);
        if (word == NULL) {
            fprintf(stderr, "[ERROR] Unknown dictionary code %" PRIu32 ".\n", code);
            decode_error = 1;
            break;
        }

        size_t word_len = strlen(word);
        if (fwrite(word, 1, word_len, output) != word_len) {
            fprintf(stderr, "[ERROR] Write error on output file.\n");
            decode_error = 1;
            break;
        }

        stats.words_read++;
        stats.decompressed_size += word_len;
        tokens_since_publish++;

        if (tokens_since_publish >= PUBLISH_INTERVAL) {
            /* Atualiza os tamanhos antes de cada publicação parcial. */
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
        fprintf(stderr, "[ERROR] Read error on input file.\n");
        decode_error = 1;
    }

    fclose(input);
    if (fclose(output) != 0 && !decode_error) {
        fprintf(stderr, "[ERROR] Failed to flush output file.\n");
        decode_error = 1;
    }

    /* Os tamanhos finais via ftell não estão disponíveis após fclose;
     * compressed_size foi atualizado pelo ftell durante o loop na última
     * publicação parcial. Reabre o arquivo para obter o tamanho exato. */
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
            perror("[ERROR] waitpid");
            exit_code = 1;
        } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            exit_code = 1;
        }
    }
    if (monitor_shared_destroy(shm_name, shm_fd, shm_state) < 0) {
        perror("[ERROR] shared memory cleanup");
        exit_code = 1;
    }
    dictionary_destroy(dict);
    return exit_code;
}
