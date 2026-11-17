# Módulo de dicionário — Parallel File Compressor

Implementação do dicionário compartilhado, correspondente à parte da Pessoa C.
O código, os comentários, os logs, os nomes de arquivos e a documentação técnica
estão em inglês. Este README é a exceção, escrita em português.

## Arquivos

- `include/dictionary.h`: API pública para compactação e descompactação.
- `src/shared/dictionary.c`: leitura, validação e consultas bidirecionais.
- `data/dictionary.txt`: dicionário ampliado a partir dos dois arquivos do professor.
- `tests/test_dictionary.c`: testes unitários, incluindo entradas inválidas.
- `tests/test_corpus.c`: verifica todas as palavras dos dois textos fornecidos.
- `tests/fixtures/`: cópias fiéis dos textos de teste originais, com nomes em inglês.
- `docs/dictionary_format.md`: formato do dicionário e proposta de codificação.

## Como executar

Em Linux ou WSL, com compilador C11 e `make`:

```bash
make test
```

## Compatibilidade e integração

A API permite converter palavra em código e código em palavra, preservando
maiúsculas e minúsculas. Os códigos são identificadores lógicos: o dicionário
não implementa a gravação do arquivo compactado. A proposta de formato binário
está em `docs/dictionary_format.md` e precisa ser acordada com A e B.

O código de referência `mini_zip.c` foi analisado, mas seu formato original
possui colisões entre símbolos e caracteres literais, além de transformar
palavras em minúsculas. Por isso, a implementação não promete compatibilidade
binária com arquivos `.mzp` gerados por aquele programa.

O dicionário em `data/dictionary.txt` foi ampliado para cobrir **todas** as
palavras dos dois textos de teste (184 entradas = 184 palavras únicas do
corpus), atendendo ao desafio do enunciado. O compactador reporta
`Not found: 0` nos dois cenários, e `test_corpus` confirma a cobertura.

## Monitor

O Monitor foi implementado como um processo Linux separado, usando memória
compartilhada POSIX e semáforos compartilhados entre processos. A integração
com o Compactador e o Descompactador utiliza `include/protocol.h`.

### Executar todos os testes

```bash
make test
```

### Ver o Monitor funcionando

```bash
make demo-monitor
```

Para simular a descompactação:

```bash
./build/monitor_simulator decompress
```

O simulador cria a memória compartilhada, inicia o Monitor como outro processo,
publica três atualizações e finaliza os recursos. Os números exibidos são
**simulados**, não são resultados reais de compactação dos textos.

A documentação técnica da comunicação está em `docs/monitor_protocol.md`.
A integração com os processos das Pessoas A e B ainda depende da implementação
do envio das estatísticas reais, conforme o protocolo.

Requer Linux com suporte a POSIX shared memory e semáforos.

## Compactador

O Compactador foi implementado como um processo Linux que roda **três threads
em paralelo**, correspondendo à parte da Pessoa A. É a evolução paralela da
lógica de `mini_zip.c`, mantendo a mesma regra de tokenização (letras, dígitos
e `_` formam palavras; o resto são separadores), mas gerando o formato
reversível `0x1B` + LEB128 acordado com o Descompactador.

As três threads se comunicam por **filas limitadas** (buffer limitado clássico)
com `pthread_mutex_t` e variáveis de condição:

- **Leitura**: lê blocos do arquivo de entrada e os corta em um separador, de
  modo que nenhuma palavra seja dividida entre unidades.
- **Compactação**: consulta o dicionário e codifica cada palavra como token
  (apenas quando o token é menor que a palavra) ou grava a palavra literal,
  preservando espaços e pontuação.
- **Gravação**: escreve as unidades codificadas no arquivo `.mzp` de saída.

A thread principal é dona da memória compartilhada POSIX, lança `build/monitor`
via `fork` + `exec` e publica as estatísticas reais (palavras lidas,
compactadas, não encontradas e tamanhos) durante e ao final do processamento.

### Executar o Compactador

```bash
./build/compressor data/dictionary.txt tests/fixtures/input_one.txt saida.mzp
```

O round-trip completo (compactar e depois descompactar, conferindo byte a byte)
faz parte de `make test`.

### Ver os dois cenários

`make demo` roda os dois arquivos de teste no mesmo estilo do `make test`: para
cada um executa a compactação (Compactador + Monitor, com estatísticas reais), a
descompactação (Descompactador + Monitor) e a conferência `cmp` do resultado
contra o original.

```bash
make demo          # os dois cenários
make demo-one      # apenas tests/fixtures/input_one.txt
make demo-two      # apenas tests/fixtures/input_two.txt
```
