#define _POSIX_C_SOURCE 200809L
#include "decompressor.h"

#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr,
                "Uso: %s <dicionário> <entrada.mzp> <saída.txt>\n",
                argv[0]);
        return 2;
    }
    return decompressor_run(argv[1], argv[2], argv[3]);
}
