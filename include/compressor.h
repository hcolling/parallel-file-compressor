#ifndef COMPRESSOR_H
#define COMPRESSOR_H

/* Compacta um arquivo de texto usando o formato byte de escape + LEB128
 * descrito em docs/dictionary_format.md, de forma paralela.
 *
 * dict_path  : caminho para o arquivo de dicionário separado por tabulação.
 * input_path : caminho para o arquivo de texto original.
 * output_path: caminho para o arquivo compactado (.mzp) a ser gerado.
 *
 * O processo usa três threads em paralelo:
 *   - uma thread de leitura do arquivo de entrada
 *   - uma thread de compactação
 *   - uma thread de gravação do arquivo de saída.
 * As threads se comunicam por filas limitadas com exclusão mútua e
 * variáveis de condição
 */
int compressor_run(const char *dict_path,
                   const char *input_path,
                   const char *output_path);

#endif
