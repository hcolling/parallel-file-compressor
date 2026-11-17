#include "protocol.h"

#include <stdio.h>

int monitor_run(const char *name);

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <nome-da-memória-compartilhada>\n", argv[0]);
        return 2;
    }
    return monitor_run(argv[1]);
}
