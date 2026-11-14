#ifndef DECOMPRESSOR_H
#define DECOMPRESSOR_H

/* Descompacta um arquivo produzido pelo compactador usando o formato
 * byte de escape + LEB128 descrito em docs/dictionary_format.md.
 *
 * dict_path  : caminho para o arquivo de dicionário separado por tabulação.
 * input_path : caminho para o arquivo compactado (.mzp).
 * output_path: caminho para o arquivo de saída reconstruído.
 *
 * Lança build/monitor como processo filho via fork + exec e publica
 * estatísticas via memória compartilhada POSIX durante e após o processamento.
 *
 * Retorna 0 em caso de sucesso, 1 em caso de erro. */
int decompressor_run(const char *dict_path,
                     const char *input_path,
                     const char *output_path);

#endif
