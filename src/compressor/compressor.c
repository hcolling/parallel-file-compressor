#define _POSIX_C_SOURCE 200809L
#include "compressor.h"
#include "dictionary.h"
#include "protocol.h"

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define ESCAPE_BYTE     0x1B // marca um codigo no meio dos bytes
#define READ_BLOCK      4096 // quanto a thread de leitura le por vez
#define QUEUE_CAPACITY  8    // limita o uso de memoria
#define PUBLISH_INTERVAL 50  // de quantas em quantas unidades avisa o Monitor

static int is_word_byte(int byte) {
    return (byte >= 'A' && byte <= 'Z') ||
           (byte >= 'a' && byte <= 'z') ||
           (byte >= '0' && byte <= '9') ||
           byte == '_';
}

// uma unidade que anda pelas filas. na entrada data = texto puro; na saida
// vem codificado
typedef struct {
    unsigned char *data;
    size_t length;
    int is_last;
} WorkUnit;

// fila circular com tamanho fixo
typedef struct {
    WorkUnit *slots[QUEUE_CAPACITY];
    size_t head;
    size_t tail;
    size_t count;
    pthread_mutex_t mutex;
    pthread_cond_t not_full;
    pthread_cond_t not_empty;
} BoundedQueue;

// contadores que as 3 threads mexem, cada um com seu mutex
typedef struct {
    pthread_mutex_t mutex;
    uint64_t words_read;
    uint64_t words_compressed;
    uint64_t words_not_found;
    uint64_t original_size;
    uint64_t compressed_size;
} SharedStats;

// o que as threads compartilham
typedef struct {
    BoundedQueue read_queue;    // leitura -> compactacao
    BoundedQueue write_queue;   // compactacao -> gravacao
    SharedStats stats;
    const Dictionary *dict;
    FILE *input;
    FILE *output;
    MonitorSharedState *shm_state;
    int error;                  // qualquer thread seta se der ruim
    pthread_mutex_t error_mutex;
} CompressorContext;


//-------- FILA LIMITADA --------

static int queue_init(BoundedQueue *queue) {
    queue->head = queue->tail = queue->count = 0;
    if (pthread_mutex_init(&queue->mutex, NULL) != 0) {
        return -1;
    }
    if (pthread_cond_init(&queue->not_full, NULL) != 0) {
        pthread_mutex_destroy(&queue->mutex);
        return -1;
    }
    if (pthread_cond_init(&queue->not_empty, NULL) != 0) {
        pthread_cond_destroy(&queue->not_full);
        pthread_mutex_destroy(&queue->mutex);
        return -1;
    }
    return 0;
}

static void queue_destroy(BoundedQueue *queue) {
    pthread_cond_destroy(&queue->not_empty);
    pthread_cond_destroy(&queue->not_full);
    pthread_mutex_destroy(&queue->mutex);
}

// coloca na fila; se tiver cheia, espera abrir espaco
static void queue_push(BoundedQueue *queue, WorkUnit *unit) {
    pthread_mutex_lock(&queue->mutex);
    while (queue->count == QUEUE_CAPACITY) {
        pthread_cond_wait(&queue->not_full, &queue->mutex);
    }
    queue->slots[queue->tail] = unit;
    queue->tail = (queue->tail + 1) % QUEUE_CAPACITY;
    ++queue->count;
    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);
}

// tira da fila; se tiver vazia, espera chegar alguma coisa
static WorkUnit *queue_pop(BoundedQueue *queue) {
    pthread_mutex_lock(&queue->mutex);
    while (queue->count == 0) {
        pthread_cond_wait(&queue->not_empty, &queue->mutex);
    }
    WorkUnit *unit = queue->slots[queue->head];
    queue->head = (queue->head + 1) % QUEUE_CAPACITY;
    --queue->count;
    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);
    return unit;
}

//-------- UNIDADES DE TRABALHO --------

// cria uma unidade copiando `length` bytes de `data`. se data == NULL so
// reserva o espaco (quem chamou preenche depois)
static WorkUnit *unit_create(const unsigned char *data, size_t length,
                             int is_last) {
    WorkUnit *unit = malloc(sizeof(*unit));
    if (unit == NULL) {
        return NULL;
    }
    unit->length = length;
    unit->is_last = is_last;
    unit->data = NULL;
    if (length > 0) {
        unit->data = malloc(length);
        if (unit->data == NULL) {
            free(unit);
            return NULL;
        }
        // reader passa NULL aqui e monta o conteudo manualmente
        if (data != NULL) {
            memcpy(unit->data, data, length);
        }
    }
    return unit;
}

static void unit_destroy(WorkUnit *unit) {
    if (unit != NULL) {
        free(unit->data);
        free(unit);
    }
}

// marca que deu erro (qualquer thread pode chamar)
static void flag_error(CompressorContext *ctx) {
    pthread_mutex_lock(&ctx->error_mutex);
    ctx->error = 1;
    pthread_mutex_unlock(&ctx->error_mutex);
}

static int has_error(CompressorContext *ctx) {
    pthread_mutex_lock(&ctx->error_mutex);
    int value = ctx->error;
    pthread_mutex_unlock(&ctx->error_mutex);
    return value;
}


//-------- LOGICA DE COMPACTACAO --------

// buffer que cresce sozinho, usado pra montar a saida codificada
typedef struct {
    unsigned char *data;
    size_t length;
    size_t capacity;
} ByteBuffer;

static int buffer_reserve(ByteBuffer *buffer, size_t extra) {
    if (buffer->length + extra <= buffer->capacity) {
        return 0;
    }
    size_t capacity = buffer->capacity == 0 ? READ_BLOCK : buffer->capacity;
    while (capacity < buffer->length + extra) {
        if (capacity > SIZE_MAX / 2) {
            return -1;
        }
        capacity *= 2;
    }
    unsigned char *novo = realloc(buffer->data, capacity);
    if (novo == NULL) {
        return -1;
    }
    buffer->data = novo;
    buffer->capacity = capacity;
    return 0;
}

static int buffer_put(ByteBuffer *buffer, unsigned char byte) {
    if (buffer_reserve(buffer, 1) < 0) {
        return -1;
    }
    buffer->data[buffer->length++] = byte;
    return 0;
}

// grava 0x1B + o codigo. tem que bater com o jeito que o decompressor le
static int buffer_put_token(ByteBuffer *buffer, uint32_t code) {
    if (buffer_put(buffer, ESCAPE_BYTE) < 0) {
        return -1;
    }
    // 7 bits por byte, bit mais alto diz se ainda tem mais byte depois
    do {
        unsigned char chunk = (unsigned char)(code & 0x7F);
        code >>= 7;
        if (code != 0) {
            chunk |= 0x80;
        }
        if (buffer_put(buffer, chunk) < 0) {
            return -1;
        }
    } while (code != 0);
    return 0;
}

// grava a palavra do jeito que ela e, mas se aparecer 0x1B tem que virar 0x1B 0x00
static int buffer_put_literal_word(ByteBuffer *buffer,
                                    const unsigned char *word, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        if (word[i] == ESCAPE_BYTE) {
            if (buffer_put(buffer, ESCAPE_BYTE) < 0 ||
                buffer_put(buffer, 0x00) < 0) {
                return -1;
            }
        } else if (buffer_put(buffer, word[i]) < 0) {
            return -1;
        }
    }
    return 0;
}

// quantos bytes o token ocupa (pra decidir se vale a pena compactar)
static size_t token_length(uint32_t code) {
    size_t length = 1; // o byte de escape
    do {
        ++length;
        code >>= 7;
    } while (code != 0);
    return length;
}

// pega a palavra que a gente juntou e grava na saida, atualizando os contadores.
// so troca pelo token se ele ficar menor que a palavra, senao grava literal
static int emit_word(CompressorContext *ctx, ByteBuffer *out,
                     const unsigned char *word, size_t word_len,
                     uint64_t *words_read, uint64_t *words_compressed,
                     uint64_t *words_not_found) {
    ++(*words_read);

    char buffer[256];
    uint32_t code = 0;
    int found = 0;
    if (word_len < sizeof(buffer)) {
        memcpy(buffer, word, word_len);
        buffer[word_len] = '\0';
        found = dictionary_encode(ctx->dict, buffer, &code);
    }

    if (found && token_length(code) < word_len) {
        ++(*words_compressed);
        return buffer_put_token(out, code);
    }
    if (!found) {
        ++(*words_not_found);
    }
    // nao achou no dicionario, ou o token nao compensa: grava a palavra mesmo
    return buffer_put_literal_word(out, word, word_len);
}

// codifica uma unidade inteira. como o reader corta sempre num separador,
// nenhuma palavra fica partida no meio, entao da pra processar unidade por unidade
static int encode_unit(CompressorContext *ctx, const WorkUnit *in,
                       ByteBuffer *out) {
    uint64_t words_read = 0;
    uint64_t words_compressed = 0;
    uint64_t words_not_found = 0;

    size_t word_start = 0;
    size_t i = 0;
    for (; i < in->length; ++i) {
        if (is_word_byte(in->data[i])) {
            continue;
        }
        if (i > word_start) {
            if (emit_word(ctx, out, in->data + word_start, i - word_start,
                          &words_read, &words_compressed, &words_not_found) < 0) {
                return -1;
            }
        }
        // separador vai do jeito que ta (escapando o 0x1B se cair aqui)
        if (in->data[i] == ESCAPE_BYTE) {
            if (buffer_put(out, ESCAPE_BYTE) < 0 || buffer_put(out, 0x00) < 0) {
                return -1;
            }
        } else if (buffer_put(out, in->data[i]) < 0) {
            return -1;
        }
        word_start = i + 1;
    }
    // palavra que termina bem no fim da unidade
    if (i > word_start) {
        if (emit_word(ctx, out, in->data + word_start, i - word_start,
                      &words_read, &words_compressed, &words_not_found) < 0) {
            return -1;
        }
    }

    // soma os contadores dessa unidade (com mutex porque as threads mexem junto)
    pthread_mutex_lock(&ctx->stats.mutex);
    ctx->stats.words_read += words_read;
    ctx->stats.words_compressed += words_compressed;
    ctx->stats.words_not_found += words_not_found;
    pthread_mutex_unlock(&ctx->stats.mutex);
    return 0;
}

//-------- PUBLICACAO NO MONITOR --------

// monta a foto das estatisticas e manda pro Monitor
static int publish_snapshot(CompressorContext *ctx, int finished, int failed) {
    MonitorStatistics stats = {0};
    stats.mode = MONITOR_MODE_COMPRESS;
    pthread_mutex_lock(&ctx->stats.mutex);
    stats.words_read = ctx->stats.words_read;
    stats.words_compressed = ctx->stats.words_compressed;
    stats.words_not_found = ctx->stats.words_not_found;
    stats.original_size = ctx->stats.original_size;
    stats.compressed_size = ctx->stats.compressed_size;
    pthread_mutex_unlock(&ctx->stats.mutex);
    stats.finished = finished ? 1u : 0u;
    stats.failed = failed ? 1u : 0u;

    if (monitor_shared_publish(ctx->shm_state, &stats) < 0) {
        fprintf(stderr, "[ERRO] Falha ao publicar estatísticas: %s\n",
                strerror(errno));
        return -1;
    }
    return 0;
}

//-------- THREADS --------

// thread de leitura: le blocos de tamanho fixo e corta no ultimo separador,
// guardando o pedaco de palavra que sobrou pro proximo bloco. assim nenhuma
// palavra fica dividida entre unidades
static void *reader_thread(void *arg) {
    CompressorContext *ctx = arg;
    unsigned char block[READ_BLOCK];

    // buffer que vai juntando a palavra parcial entre um bloco e outro. cresce
    // conforme precisa, entao nao estoura nem com palavra maior que um bloco
    ByteBuffer carry = {0};

    for (;;) {
        if (has_error(ctx)) {
            break;
        }
        size_t got = fread(block, 1, sizeof(block), ctx->input);
        if (got == 0) {
            break; // chegou no fim ou deu erro: trato depois do laco
        }

        // acha o ultimo separador do bloco
        size_t split = got;
        while (split > 0 && is_word_byte(block[split - 1])) {
            --split;
        }

        if (split == 0) {
            // o bloco todo e palavra ainda sem terminar: joga no carry e segue
            if (buffer_reserve(&carry, got) < 0) { flag_error(ctx); break; }
            memcpy(carry.data + carry.length, block, got);
            carry.length += got;
            continue;
        }

        // monta a unidade: o que tava no carry + o bloco ate o separador
        WorkUnit *unit = unit_create(NULL, carry.length + split, 0);
        if (unit == NULL) { flag_error(ctx); break; }
        if (carry.length > 0) {
            memcpy(unit->data, carry.data, carry.length);
        }
        memcpy(unit->data + carry.length, block, split);
        queue_push(&ctx->read_queue, unit);

        // o que sobrou depois do separador vira o carry novo
        carry.length = 0;
        size_t rest = got - split;
        if (rest > 0) {
            if (buffer_reserve(&carry, rest) < 0) { flag_error(ctx); break; }
            memcpy(carry.data, block + split, rest);
            carry.length = rest;
        }
    }

    if (ferror(ctx->input)) {
        fprintf(stderr, "[ERRO] Falha na leitura do arquivo de entrada.\n");
        flag_error(ctx);
    }

    // emite a ultima palavra que ficou pendente, se tiver
    if (!has_error(ctx) && carry.length > 0) {
        WorkUnit *unit = unit_create(carry.data, carry.length, 0);
        if (unit == NULL) {
            flag_error(ctx);
        } else {
            queue_push(&ctx->read_queue, unit);
        }
    }
    free(carry.data);

    // sentinela: avisa a thread de compactacao que acabou
    WorkUnit *sentinel = unit_create(NULL, 0, 1);
    if (sentinel == NULL) {
        flag_error(ctx);
    } else {
        queue_push(&ctx->read_queue, sentinel);
    }
    return NULL;
}

// thread de compactacao: pega unidade bruta e devolve unidade codificada
static void *compressor_thread(void *arg) {
    CompressorContext *ctx = arg;
    for (;;) {
        WorkUnit *in = queue_pop(&ctx->read_queue);
        if (in->is_last) {
            unit_destroy(in);
            break;
        }
        if (has_error(ctx)) {
            unit_destroy(in);
            continue; // vai esvaziando a fila ate chegar na sentinela
        }

        ByteBuffer out = {0};
        if (encode_unit(ctx, in, &out) < 0) {
            fprintf(stderr, "[ERRO] Falha ao codificar unidade (memória).\n");
            flag_error(ctx);
            free(out.data);
            unit_destroy(in);
            continue;
        }
        unit_destroy(in);

        // passa o buffer codificado pra uma unidade de saida
        WorkUnit *encoded = malloc(sizeof(*encoded));
        if (encoded == NULL) {
            flag_error(ctx);
            free(out.data);
            continue;
        }
        encoded->data = out.data;
        encoded->length = out.length;
        encoded->is_last = 0;
        queue_push(&ctx->write_queue, encoded);
    }

    // repassa a sentinela pra thread de gravacao
    WorkUnit *sentinel = unit_create(NULL, 0, 1);
    if (sentinel == NULL) {
        flag_error(ctx);
    } else {
        queue_push(&ctx->write_queue, sentinel);
    }
    return NULL;
}

// thread de gravacao: grava as unidades ja codificadas no arquivo de saida
static void *writer_thread(void *arg) {
    CompressorContext *ctx = arg;
    uint64_t contador = 0;

    for (;;) {
        WorkUnit *unit = queue_pop(&ctx->write_queue);
        if (unit->is_last) {
            unit_destroy(unit);
            break;
        }
        if (!has_error(ctx) && unit->length > 0) {
            if (fwrite(unit->data, 1, unit->length, ctx->output)
                    != unit->length) {
                fprintf(stderr, "[ERRO] Falha ao gravar no arquivo de saída.\n");
                flag_error(ctx);
            } else {
                pthread_mutex_lock(&ctx->stats.mutex);
                ctx->stats.compressed_size += unit->length;
                pthread_mutex_unlock(&ctx->stats.mutex);
            }
        }
        unit_destroy(unit);

        // de tempos em tempos manda o progresso pro Monitor
        if (++contador >= PUBLISH_INTERVAL && !has_error(ctx)) {
            if (publish_snapshot(ctx, 0, 0) < 0) {
                flag_error(ctx);
            }
            contador = 0;
        }
    }
    return NULL;
}

//-------- ORQUESTRACAO (thread principal) --------

// sobe o build/monitor como processo filho.
// devolve o PID do filho se deu certo, -1 se falhou
static pid_t launch_monitor(const char *shm_name, MonitorSharedState *shm_state,
                            int shm_fd) {
    pid_t child = fork();
    if (child < 0) {
        perror("[ERRO] fork");
        return -1;
    }
    if (child == 0) {
        monitor_shared_close(shm_fd, shm_state);
        execl("./build/monitor", "monitor", shm_name, (char *)NULL);
        perror("[ERRO] exec monitor");
        _exit(127);
    }
    return child;
}

// descobre o tamanho do arquivo de entrada em bytes. -1 se der erro
static long file_size(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return -1;
    }
    long size = -1;
    if (fseek(file, 0, SEEK_END) == 0) {
        size = ftell(file);
    }
    fclose(file);
    return size;
}

int compressor_run(const char *dict_path,
                   const char *input_path,
                   const char *output_path) {
    int exit_code = 1;

    Dictionary *dict = dictionary_load(dict_path);
    if (dict == NULL) {
        fprintf(stderr, "[ERRO] Falha ao carregar o dicionário: %s\n", dict_path);
        return 1;
    }

    char shm_name[MONITOR_SHM_NAME_MAX];
    int len = snprintf(shm_name, sizeof(shm_name), "/pfc_comp_%ld", (long)getpid());
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

    // daqui pra frente, se der erro, tem que publicar uma snapshot final com
    // failed = 1 e esperar o Monitor, senao ele fica travado esperando
    CompressorContext ctx = {0};
    ctx.dict = dict;
    ctx.shm_state = shm_state;
    pthread_mutex_init(&ctx.error_mutex, NULL);
    pthread_mutex_init(&ctx.stats.mutex, NULL);

    int filas_ok = 0;
    int threads_started = 0;
    pthread_t reader, compressor, writer;

    long original = file_size(input_path);
    if (original < 0) {
        fprintf(stderr, "[ERRO] Não foi possível medir o arquivo de entrada: %s\n",
                input_path);
        flag_error(&ctx);
        goto finalize;
    }
    ctx.stats.original_size = (uint64_t)original;

    ctx.input = fopen(input_path, "rb");
    if (ctx.input == NULL) {
        fprintf(stderr, "[ERRO] Não foi possível abrir o arquivo de entrada: %s\n",
                input_path);
        flag_error(&ctx);
        goto finalize;
    }

    ctx.output = fopen(output_path, "wb");
    if (ctx.output == NULL) {
        fprintf(stderr, "[ERRO] Não foi possível abrir o arquivo de saída: %s\n",
                output_path);
        flag_error(&ctx);
        goto finalize;
    }

    if (queue_init(&ctx.read_queue) < 0 || queue_init(&ctx.write_queue) < 0) {
        fprintf(stderr, "[ERRO] Falha ao inicializar as filas.\n");
        flag_error(&ctx);
        goto finalize;
    }
    filas_ok = 1;

    if (pthread_create(&reader, NULL, reader_thread, &ctx) != 0 ||
        pthread_create(&compressor, NULL, compressor_thread, &ctx) != 0 ||
        pthread_create(&writer, NULL, writer_thread, &ctx) != 0) {
        fprintf(stderr, "[ERRO] Falha ao criar as threads.\n");
        flag_error(&ctx);
        // nao da pra matar threads criadas pela metade de um jeito seguro,
        // entao aborta antes de comecar o pipeline
        goto finalize;
    }
    threads_started = 1;

    pthread_join(reader, NULL);
    pthread_join(compressor, NULL);
    pthread_join(writer, NULL);

finalize:
    if (ctx.output != NULL) {
        if (fclose(ctx.output) != 0 && !has_error(&ctx)) {
            fprintf(stderr, "[ERRO] Falha ao finalizar o arquivo de saída.\n");
            flag_error(&ctx);
        }
    }
    if (ctx.input != NULL) {
        fclose(ctx.input);
    }
    if (filas_ok) {
        queue_destroy(&ctx.read_queue);
        queue_destroy(&ctx.write_queue);
    }

    (void)threads_started;
    int failed = has_error(&ctx);
    publish_snapshot(&ctx, 1, failed);
    exit_code = failed ? 1 : 0;

    {
        int status = 0;
        if (waitpid(monitor_pid, &status, 0) < 0) {
            perror("[ERRO] waitpid");
            exit_code = 1;
        } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            exit_code = 1;
        }
    }
    if (monitor_shared_destroy(shm_name, shm_fd, shm_state) < 0) {
        perror("[ERRO] limpeza da memória compartilhada");
        exit_code = 1;
    }
    pthread_mutex_destroy(&ctx.stats.mutex);
    pthread_mutex_destroy(&ctx.error_mutex);
    dictionary_destroy(dict);
    return exit_code;
}
