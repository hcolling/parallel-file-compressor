#ifndef DICTIONARY_H
#define DICTIONARY_H

#include <stddef.h>
#include <stdint.h>

typedef struct Dictionary Dictionary;

/* Carrega os códigos e palavras separados por tabulação.
 * Ignora linhas vazias e comentários iniciados por '#'.
 * Retorna NULL se houver erro na leitura ou entradas inválidas. */
Dictionary *dictionary_load(const char *path);

/* Libera a memória do dicionário. */
void dictionary_destroy(Dictionary *dictionary);

/* Busca o código da palavra. Retorna 1 se encontrar e 0 caso contrário. */
int dictionary_encode(const Dictionary *dictionary,
                      const char *word,
                      uint32_t *output_code);

/* Retorna a palavra do código ou NULL se não encontrar. Não libera a palavra. */
const char *dictionary_decode(const Dictionary *dictionary, uint32_t code);

/* Retorna a quantidade de palavras ou zero se o dicionário for NULL. */
size_t dictionary_size(const Dictionary *dictionary);

#endif
