#define _POSIX_C_SOURCE 200809L
#include "dictionary.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t code;
    char *word;
} DictionaryEntry;

struct Dictionary {
    DictionaryEntry *entries;
    size_t count;
    size_t capacity;
};

static char *copy_string(const char *source) {
    size_t length = strlen(source);
    char *copy = malloc(length + 1);
    if (copy != NULL) {
        memcpy(copy, source, length + 1);
    }
    return copy;
}

void dictionary_destroy(Dictionary *dictionary) {
    if (dictionary == NULL) {
        return;
    }
    for (size_t i = 0; i < dictionary->count; ++i) {
        free(dictionary->entries[i].word);
    }
    free(dictionary->entries);
    free(dictionary);
}

size_t dictionary_size(const Dictionary *dictionary) {
    return dictionary == NULL ? 0 : dictionary->count;
}

int dictionary_encode(const Dictionary *dictionary,
                      const char *word,
                      uint32_t *output_code) {
    if (dictionary == NULL || word == NULL || output_code == NULL) {
        return 0;
    }
    for (size_t i = 0; i < dictionary->count; ++i) {
        if (strcmp(dictionary->entries[i].word, word) == 0) {
            *output_code = dictionary->entries[i].code;
            return 1;
        }
    }
    return 0;
}

const char *dictionary_decode(const Dictionary *dictionary, uint32_t code) {
    if (dictionary == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < dictionary->count; ++i) {
        if (dictionary->entries[i].code == code) {
            return dictionary->entries[i].word;
        }
    }
    return NULL;
}

static int add_entry(Dictionary *dictionary, uint32_t code, const char *word) {
    if (code == 0 || word == NULL || *word == '\0' ||
        strchr(word, '\t') != NULL || strchr(word, '\r') != NULL ||
        strchr(word, '\n') != NULL) {
        return 0;
    }
    if (dictionary_decode(dictionary, code) != NULL) {
        return 0;
    }
    uint32_t existing_code;
    if (dictionary_encode(dictionary, word, &existing_code)) {
        return 0;
    }
    if (dictionary->count == dictionary->capacity) {
        size_t capacity = dictionary->capacity == 0 ? 16 : dictionary->capacity * 2;
        if (capacity < dictionary->capacity ||
            capacity > SIZE_MAX / sizeof(*dictionary->entries)) {
            return 0;
        }
        DictionaryEntry *entries = realloc(dictionary->entries,
                                            capacity * sizeof(*entries));
        if (entries == NULL) {
            return 0;
        }
        dictionary->entries = entries;
        dictionary->capacity = capacity;
    }
    char *copy = copy_string(word);
    if (copy == NULL) {
        return 0;
    }
    dictionary->entries[dictionary->count].code = code;
    dictionary->entries[dictionary->count].word = copy;
    ++dictionary->count;
    return 1;
}

Dictionary *dictionary_load(const char *path) {
    if (path == NULL) {
        return NULL;
    }
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "[ERROR] Cannot open dictionary: %s\n", path);
        return NULL;
    }
    Dictionary *dictionary = calloc(1, sizeof(*dictionary));
    if (dictionary == NULL) {
        fclose(file);
        return NULL;
    }
    char *line = NULL;
    size_t line_capacity = 0;
    ssize_t line_length;
    size_t line_number = 0;
    int valid = 1;

    while ((line_length = getline(&line, &line_capacity, file)) != -1) {
        ++line_number;
        while (line_length > 0 &&
               (line[line_length - 1] == '\n' || line[line_length - 1] == '\r')) {
            line[--line_length] = '\0';
        }
        if (line_length == 0 || line[0] == '#') {
            continue;
        }
        char *separator = strchr(line, '\t');
        if (separator == NULL || separator == line ||
            separator[1] == '\0' || strchr(separator + 1, '\t') != NULL) {
            valid = 0;
        } else {
            *separator = '\0';
            errno = 0;
            char *end = NULL;
            uintmax_t parsed = strtoumax(line, &end, 10);
            if (errno != 0 || end == line || *end != '\0' || line[0] < '0' || line[0] > '9' ||
                parsed == 0 || parsed > UINT32_MAX ||
                !add_entry(dictionary, (uint32_t)parsed, separator + 1)) {
                valid = 0;
            }
        }
        if (!valid) {
            fprintf(stderr, "[ERROR] Invalid dictionary entry at line %zu\n",
                    line_number);
            break;
        }
    }
    if (ferror(file)) {
        fprintf(stderr, "[ERROR] Failed to read dictionary\n");
        valid = 0;
    }
    free(line);
    if (fclose(file) != 0) {
        valid = 0;
    }
    if (!valid) {
        dictionary_destroy(dictionary);
        return NULL;
    }
    return dictionary;
}
