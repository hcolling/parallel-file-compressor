#include "protocol.h"

#include <stdio.h>

int monitor_run(const char *name);

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <shared-memory-name>\n", argv[0]);
        return 2;
    }
    return monitor_run(argv[1]);
}
