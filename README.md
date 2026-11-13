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

**Observação:** o anexo recebido nesta etapa continha `mini_zip.c`,
`entrada.txt` e `entrada2.txt`, mas não continha o PDF do enunciado. Os
requisitos específicos do PDF ainda precisam ser confirmados.

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
