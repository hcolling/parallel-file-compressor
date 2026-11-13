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

/* Shared layout is initialized by the owner before the monitor attaches.
 * mutex protects statistics; changed wakes the monitor after publication.
 * Both semaphores must be initialized with pshared = 1.
 */
typedef struct {
    uint32_t version;
    sem_t mutex;
    sem_t changed;
    MonitorStatistics stats;
} MonitorSharedState;

/* Creates and initializes a POSIX shared-memory region.
 * The caller owns cleanup (munmap, sem_destroy and shm_unlink).
 * Use a unique name for concurrent runs. */
int monitor_shared_create(const char *name, int *fd, MonitorSharedState **state);

/* Opens an existing region; does not own its semaphores or shm_unlink. */
int monitor_shared_open(const char *name, int *fd, MonitorSharedState **state);

/* Publishes one complete snapshot and notifies the monitor.
 * The owner should set finished = 1 on its final update.
 * revision is assigned automatically. */
int monitor_shared_publish(MonitorSharedState *state,
                           const MonitorStatistics *statistics);

/* Releases a mapping and descriptor, without unlinking. */
void monitor_shared_close(int fd, MonitorSharedState *state);

/* Only the creator should call this after all processes have exited. */
int monitor_shared_destroy(const char *name, int fd, MonitorSharedState *state);

#endif
