#define _POSIX_C_SOURCE 200809L
#include "protocol.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void delay(void) {
    struct timespec pause_time = {0, 150000000};
    nanosleep(&pause_time, NULL);
}

int main(int argc, char **argv) {
    if (argc != 2 || (strcmp(argv[1], "compress") != 0 &&
                      strcmp(argv[1], "decompress") != 0)) {
        fprintf(stderr, "Usage: %s <compress|decompress>\n", argv[0]);
        return 2;
    }
    char name[MONITOR_SHM_NAME_MAX];
    int length = snprintf(name, sizeof(name), "/pfc_demo_%ld", (long)getpid());
    if (length < 0 || (size_t)length >= sizeof(name)) return 1;

    int fd = -1;
    MonitorSharedState *state = NULL;
    if (monitor_shared_create(name, &fd, &state) < 0) {
        fprintf(stderr, "[ERROR] Failed to create shared memory: %s\n",
                strerror(errno));
        return 1;
    }
    pid_t child = fork();
    if (child < 0) {
        perror("[ERROR] fork");
        monitor_shared_destroy(name, fd, state);
        return 1;
    }
    if (child == 0) {
        monitor_shared_close(fd, state);
        execl("./build/monitor", "monitor", name, (char *)NULL);
        perror("[ERROR] exec monitor");
        _exit(127);
    }
    MonitorStatistics stats = {0};
    stats.mode = strcmp(argv[1], "compress") == 0 ?
                 MONITOR_MODE_COMPRESS : MONITOR_MODE_DECOMPRESS;
    stats.original_size = 1938;
    for (int step = 1; step <= 3; ++step) {
        stats.words_read = (uint64_t)(step * 100);
        stats.words_compressed = stats.mode == MONITOR_MODE_COMPRESS ?
                                 (uint64_t)(step * 90) : 0;
        stats.words_not_found = stats.mode == MONITOR_MODE_COMPRESS ?
                                (uint64_t)(step * 10) : 0;
        stats.compressed_size = (uint64_t)(step * 250);
        stats.decompressed_size = stats.mode == MONITOR_MODE_DECOMPRESS ?
                                  (uint64_t)(step * 646) : 0;
        stats.finished = step == 3 ? 1u : 0u;
        if (monitor_shared_publish(state, &stats) < 0) {
            perror("[ERROR] publish");
            stats.failed = 1;
            stats.finished = 1;
            monitor_shared_publish(state, &stats);
            break;
        }
        delay();
    }
    int status = 0;
    if (waitpid(child, &status, 0) < 0) {
        perror("[ERROR] waitpid");
        status = -1;
    }
    int cleanup_result = monitor_shared_destroy(name, fd, state);
    if (cleanup_result < 0) perror("[ERROR] shared memory cleanup");
    if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0 ||
        cleanup_result < 0) return 1;
    puts("[INFO] Monitor simulator completed successfully.");
    return 0;
}
