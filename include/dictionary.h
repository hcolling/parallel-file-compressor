#ifndef DICTIONARY_H
#define DICTIONARY_H

#include <stddef.h>
#include <stdint.h>

typedef struct Dictionary Dictionary;

/* Loads a tab-separated mapping of unsigned decimal codes to exact words.
 * Blank lines and lines starting with '#' are ignored.
 * Returns NULL for I/O errors, duplicate entries, or malformed lines. */
Dictionary *dictionary_load(const char *path);

/* Releases the dictionary and all associated resources. */
void dictionary_destroy(Dictionary *dictionary);

/* Returns 1 and writes the code on success; returns 0 if not found. */
int dictionary_encode(const Dictionary *dictionary,
                      const char *word,
                      uint32_t *output_code);

/* Returns a borrowed word pointer, or NULL if the code is unknown. */
const char *dictionary_decode(const Dictionary *dictionary, uint32_t code);

/* Returns the number of entries, or zero for NULL. */
size_t dictionary_size(const Dictionary *dictionary);

#endif
