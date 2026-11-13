#define _POSIX_C_SOURCE 200809L
#include "protocol.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static int valid_name(const char *name) {
    if (name == NULL || name[0] != '/' || name[1] == '\0' ||
        strlen(name) >= MONITOR_SHM_NAME_MAX) {
        return 0;
    }
    for (const char *p = name + 1; *p; ++p) {
        if (*p == '/') return 0;
    }
    return 1;
}

static int wait_sem(sem_t *sem) {
    while (sem_wait(sem) == -1) {
        if (errno != EINTR) return -1;
    }
    return 0;
}

void monitor_shared_close(int fd, MonitorSharedState *state) {
    if (state != NULL && state != MAP_FAILED)
        munmap(state, sizeof(*state));
    if (fd >= 0) close(fd);
}

int monitor_shared_create(const char *name, int *fd, MonitorSharedState **state) {
    if (!valid_name(name) || fd == NULL || state == NULL) {
        errno = EINVAL;
        return -1;
    }
    *fd = -1;
    *state = NULL;
    int descriptor = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (descriptor < 0) return -1;
    if (ftruncate(descriptor, (off_t)sizeof(MonitorSharedState)) < 0) {
        close(descriptor);
        shm_unlink(name);
        return -1;
    }
    MonitorSharedState *mapping = mmap(NULL, sizeof(*mapping),
                                      PROT_READ | PROT_WRITE, MAP_SHARED,
                                      descriptor, 0);
    if (mapping == MAP_FAILED) {
        close(descriptor);
        shm_unlink(name);
        return -1;
    }
    memset(mapping, 0, sizeof(*mapping));
    if (sem_init(&mapping->mutex, 1, 1) < 0) {
        monitor_shared_close(descriptor, mapping);
        shm_unlink(name);
        return -1;
    }
    if (sem_init(&mapping->changed, 1, 0) < 0) {
        sem_destroy(&mapping->mutex);
        monitor_shared_close(descriptor, mapping);
        shm_unlink(name);
        return -1;
    }
    mapping->version = MONITOR_PROTOCOL_VERSION;
    *fd = descriptor;
    *state = mapping;
    return 0;
}

int monitor_shared_open(const char *name, int *fd, MonitorSharedState **state) {
    if (!valid_name(name) || fd == NULL || state == NULL) {
        errno = EINVAL;
        return -1;
    }
    *fd = -1;
    *state = NULL;
    int descriptor = shm_open(name, O_RDWR, 0);
    if (descriptor < 0) return -1;
    struct stat info;
    if (fstat(descriptor, &info) < 0 ||
        info.st_size != (off_t)sizeof(MonitorSharedState)) {
        close(descriptor);
        errno = EPROTO;
        return -1;
    }
    MonitorSharedState *mapping = mmap(NULL, sizeof(*mapping),
                                      PROT_READ | PROT_WRITE, MAP_SHARED,
                                      descriptor, 0);
    if (mapping == MAP_FAILED) {
        close(descriptor);
        return -1;
    }
    if (mapping->version != MONITOR_PROTOCOL_VERSION) {
        monitor_shared_close(descriptor, mapping);
        errno = EPROTO;
        return -1;
    }
    *fd = descriptor;
    *state = mapping;
    return 0;
}

int monitor_shared_publish(MonitorSharedState *state,
                           const MonitorStatistics *statistics) {
    if (state == NULL || statistics == NULL ||
        state->version != MONITOR_PROTOCOL_VERSION) {
        errno = EINVAL;
        return -1;
    }
    if (wait_sem(&state->mutex) < 0) return -1;
    uint64_t revision = state->stats.revision + 1;
    state->stats = *statistics;
    state->stats.revision = revision;
    if (sem_post(&state->mutex) < 0) return -1;
    return sem_post(&state->changed);
}

int monitor_shared_destroy(const char *name, int fd, MonitorSharedState *state) {
    if (!valid_name(name) || state == NULL) {
        errno = EINVAL;
        return -1;
    }
    int result = 0;
    if (sem_destroy(&state->changed) < 0) result = -1;
    if (sem_destroy(&state->mutex) < 0) result = -1;
    monitor_shared_close(fd, state);
    if (shm_unlink(name) < 0) result = -1;
    return result;
}
