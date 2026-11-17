#define _POSIX_C_SOURCE 200809L
#include "dictionary.h"

#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check_file(const Dictionary *dictionary, const char *path) {
    FILE *input = fopen(path, "rb");
    assert(input != NULL);
    char word[4096];
    size_t length = 0;
    size_t count = 0;
    int ch;
    while ((ch = fgetc(input)) != EOF) {
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || ch == '_') {
            assert(length + 1 < sizeof(word));
            word[length++] = (char)ch;
        } else if (length != 0) {
            word[length] = '\0';
            uint32_t code;
            assert(dictionary_encode(dictionary, word, &code));
            assert(strcmp(dictionary_decode(dictionary, code), word) == 0);
            ++count;
            length = 0;
        }
    }
    if (length != 0) {
        word[length] = '\0';
        uint32_t code;
        assert(dictionary_encode(dictionary, word, &code));
        assert(strcmp(dictionary_decode(dictionary, code), word) == 0);
        ++count;
    }
    assert(fclose(input) == 0);
    printf("[INFO] Verificadas %zu palavras em %s.\n", count, path);
}

int main(int argc, char **argv) {
    assert(argc == 4);
    Dictionary *dictionary = dictionary_load(argv[1]);
    assert(dictionary != NULL);
    check_file(dictionary, argv[2]);
    check_file(dictionary, argv[3]);
    printf("[INFO] Verificado o dicionário com %zu entradas.\n",
           dictionary_size(dictionary));
    dictionary_destroy(dictionary);
    return 0;
}
