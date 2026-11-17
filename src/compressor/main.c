#define _POSIX_C_SOURCE 200809L
#include "compressor.h"

#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr,
                "Uso: %s <dicionário> <entrada.txt> <saída.mzp>\n",
                argv[0]);
        return 2;
    }
    return compressor_run(argv[1], argv[2], argv[3]);
}
