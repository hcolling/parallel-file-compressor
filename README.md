# Parallel File Compressor

Compactador e descompactador de texto em C para Linux, usando dicionário de
palavras e processos/threads em paralelo. O código, os comentários e os nomes de
arquivos estão em inglês; este README é a exceção, em português.

## Arquivos

- `include/`: APIs públicas (`dictionary.h`, `compressor.h`, `decompressor.h`,
  `protocol.h`).
- `src/shared/dictionary.c`: leitura, validação e consultas do dicionário.
- `src/compressor/`: o Compactador (três threads em paralelo).
- `src/decompressor/`: o Descompactador.
- `src/monitor/`: o Monitor (processo separado que mostra as estatísticas).
- `data/dictionary.txt`: dicionário usado na compactação.
- `tests/`: testes unitários e de corpus, com os textos em `tests/fixtures/`.
- `docs/`: formato do dicionário e protocolo do Monitor.

## Como compilar e testar

Em Linux ou WSL, com compilador C11 e `make`:

```bash
make        # compila tudo
make test   # roda a bateria de testes
```

O `make test` executa, em ordem: os testes do dicionário (`test_dictionary`), a
verificação de que o dicionário cobre todas as palavras dos dois textos
(`test_corpus`) e o **round-trip completo** de cada arquivo de teste — compacta,
descompacta e compara com o original usando `cmp`. Se nenhum `cmp` reclamar, a
compactação é reversível byte a byte.

## Dicionário

A API converte palavra em código e código em palavra, preservando maiúsculas e
minúsculas. Os códigos são identificadores lógicos; a gravação binária fica a
cargo do Compactador, no formato `0x1B` + LEB128 descrito em
`docs/dictionary_format.md`.

O `data/dictionary.txt` foi ampliado para cobrir **todas** as palavras dos dois
textos de teste (184 entradas = 184 palavras únicas do corpus), atendendo ao
desafio do enunciado. O compactador reporta `Not found: 0` nos dois cenários, e
`test_corpus` confirma a cobertura.

## Compactador

O Compactador é um processo Linux que roda **três threads em paralelo**:

- **Leitura**: lê blocos do arquivo de entrada e os corta em um separador, de
  modo que nenhuma palavra seja dividida entre unidades.
- **Compactação**: consulta o dicionário e codifica cada palavra como token
  (apenas quando o token é menor que a palavra) ou grava a palavra literal,
  preservando espaços e pontuação.
- **Gravação**: escreve as unidades codificadas no arquivo `.mzp` de saída.

As threads se comunicam por **filas limitadas** (buffer limitado clássico) com
`pthread_mutex_t` e variáveis de condição. A thread principal é dona da memória
compartilhada POSIX, lança `build/monitor` via `fork` + `exec` e publica as
estatísticas reais (palavras lidas, compactadas, não encontradas e tamanhos).

```bash
./build/compressor data/dictionary.txt tests/fixtures/input_one.txt saida.mzp
```

## Monitor

O Monitor é um processo Linux separado, que usa memória compartilhada POSIX e
semáforos compartilhados entre processos para exibir as estatísticas da
compactação e da descompactação em tempo real. O protocolo está em
`docs/monitor_protocol.md`.

## Ver os dois cenários

`make demo` roda os dois arquivos de teste: para cada um executa a compactação
(Compactador + Monitor), a descompactação (Descompactador + Monitor) e a
conferência `cmp` do resultado contra o original.

```bash
make demo          # os dois cenários
make demo-one      # apenas tests/fixtures/input_one.txt
make demo-two      # apenas tests/fixtures/input_two.txt
```

Requer Linux com suporte a POSIX shared memory e semáforos.
