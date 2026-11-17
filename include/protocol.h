#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <semaphore.h>
#include <stdint.h>

#define MONITOR_PROTOCOL_VERSION 1u
#define MONITOR_SHM_NAME_MAX 128
#define MONITOR_DEFAULT_SHM "/pfc_monitor"

typedef enum {
    MONITOR_MODE_COMPRESS = 1,
    MONITOR_MODE_DECOMPRESS = 2
} MonitorMode;

typedef struct {
    uint64_t words_read;
    uint64_t words_compressed;
    uint64_t words_not_found;
    uint64_t original_size;
    uint64_t compressed_size;
    uint64_t decompressed_size;
    uint64_t revision;
    uint32_t mode;
    uint32_t finished;
    uint32_t failed;
} MonitorStatistics;

/* O processo que cria a memória prepara os dados antes de iniciar o Monitor.
 * mutex protege as estatísticas e changed avisa quando elas mudam.
 * Os semáforos usam pshared = 1 para funcionar entre processos. */
typedef struct {
    uint32_t version;
    sem_t mutex;
    sem_t changed;
    MonitorStatistics stats;
} MonitorSharedState;

/* Cria a memória compartilhada. Use um nome diferente para cada execução.
 * Quem cria também deve liberar os recursos no final. */
int monitor_shared_create(const char *name, int *fd, MonitorSharedState **state);

/* Abre uma memória existente sem assumir a responsabilidade de destruí-la. */
int monitor_shared_open(const char *name, int *fd, MonitorSharedState **state);

/* Atualiza as estatísticas e avisa o Monitor.
 * Na última atualização, use finished = 1.
 * O número da atualização (revision) é incrementado automaticamente. */
int monitor_shared_publish(MonitorSharedState *state,
                           const MonitorStatistics *statistics);

/* Fecha o acesso à memória, mas não remove a memória compartilhada. */
void monitor_shared_close(int fd, MonitorSharedState *state);

/* Quem criou a memória deve destruí-la depois que os outros processos saírem. */
int monitor_shared_destroy(const char *name, int fd, MonitorSharedState *state);

#endif
