#ifndef DECOMPRESSOR_H
#define DECOMPRESSOR_H

/* Descompacta o arquivo usando o byte de escape e os códigos em LEB128.
 *
 * dict_path  : caminho do dicionário, com códigos e palavras separados por tabulação.
 * input_path : caminho para o arquivo compactado (.mzp).
 * output_path: caminho para o arquivo de saída reconstruído.
 *
 * Inicia build/monitor com fork e exec e envia as estatísticas
 * pela memória compartilhada durante a execução e no final.
 *
 * Retorna 0 se der certo e 1 se houver erro. */
int decompressor_run(const char *dict_path,
                     const char *input_path,
                     const char *output_path);

#endif
