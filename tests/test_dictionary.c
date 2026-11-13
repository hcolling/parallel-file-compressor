#define _POSIX_C_SOURCE 200809L
#include "dictionary.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char *write_temp_file(const char *content) {
    char pattern[] = "/tmp/dictionary-test-XXXXXX";
    int descriptor = mkstemp(pattern);
    assert(descriptor >= 0);
    FILE *file = fdopen(descriptor, "wb");
    assert(file != NULL);
    assert(fwrite(content, 1, strlen(content), file) == strlen(content));
    assert(fclose(file) == 0);
    char *path = strdup(pattern);
    assert(path != NULL);
    return path;
}

static void test_valid_dictionary(void) {
    char *path = write_temp_file("# Example\n1\thello\n2\tworld\n42\tcafé\n");
    Dictionary *dictionary = dictionary_load(path);
    assert(dictionary != NULL);
    assert(dictionary_size(dictionary) == 3);

    uint32_t code = 0;
    assert(dictionary_encode(dictionary, "hello", &code) && code == 1);
    assert(dictionary_encode(dictionary, "café", &code) && code == 42);
    assert(!dictionary_encode(dictionary, "missing", &code));
    assert(strcmp(dictionary_decode(dictionary, 2), "world") == 0);
    assert(dictionary_decode(dictionary, 999) == NULL);
    assert(!dictionary_encode(NULL, "hello", &code));
    assert(dictionary_decode(NULL, 1) == NULL);
    assert(dictionary_size(NULL) == 0);

    dictionary_destroy(dictionary);
    unlink(path);
    free(path);
}

static void test_invalid_dictionary(const char *content) {
    char *path = write_temp_file(content);
    assert(dictionary_load(path) == NULL);
    unlink(path);
    free(path);
}

int main(void) {
    test_valid_dictionary();
    test_invalid_dictionary("1\thello\n1\tworld\n");
    test_invalid_dictionary("1\thello\n2\thello\n");
    test_invalid_dictionary("0\tinvalid\n");
    test_invalid_dictionary("4294967296\tinvalid\n");
    test_invalid_dictionary("abc\tinvalid\n");
    test_invalid_dictionary("1 missing-tab\n");
    test_invalid_dictionary("1\t\n");
    test_invalid_dictionary("1\tbad\textra\n");
    assert(dictionary_load("/path/that/does/not/exist") == NULL);
    puts("[INFO] All dictionary tests passed.");
    return 0;
}
