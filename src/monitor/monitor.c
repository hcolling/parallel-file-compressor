#include "protocol.h"
#include "statistics.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

static int wait_notification(sem_t *sem) {
    while (sem_wait(sem) == -1) {
        if (errno != EINTR) return -1;
    }
    return 0;
}

/* Processes snapshots until the producer publishes a final status.
 * Multiple rapid updates may be coalesced, but the final state is preserved. */
int monitor_run(const char *name) {
    int fd = -1;
    MonitorSharedState *state = NULL;
    if (monitor_shared_open(name, &fd, &state) < 0) {
        fprintf(stderr, "[ERROR] Failed to attach monitor: %s\n",
                strerror(errno));
        return 1;
    }
    uint64_t last_revision = 0;
    int result = 0;
    for (;;) {
        if (wait_notification(&state->changed) < 0) {
            fprintf(stderr, "[ERROR] Failed to wait for update: %s\n",
                    strerror(errno));
            result = 1;
            break;
        }
        if (wait_notification(&state->mutex) < 0) {
            fprintf(stderr, "[ERROR] Failed to lock statistics: %s\n",
                    strerror(errno));
            result = 1;
            break;
        }
        MonitorStatistics snapshot = state->stats;
        if (sem_post(&state->mutex) < 0) {
            result = 1;
            break;
        }
        if (snapshot.revision == last_revision) continue;
        last_revision = snapshot.revision;
        if (!statistics_validate(&snapshot)) {
            fprintf(stderr, "[ERROR] Invalid statistics snapshot.\n");
            result = 1;
            break;
        }
        statistics_print(&snapshot);
        if (snapshot.finished) {
            result = snapshot.failed ? 1 : 0;
            break;
        }
    }
    monitor_shared_close(fd, state);
    return result;
}
