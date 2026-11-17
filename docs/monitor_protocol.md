# Protocolo do Monitor

## Modos de execução

O trabalho exige dois modos:

- Compactação: processo Compactador + processo Monitor.
- Descompactação: processo Descompactador + processo Monitor.

O Monitor é um processo Linux separado. O processo produtor cria a memória
compartilhada POSIX, inicializa os semáforos entre processos e então inicia o Monitor.

## Contrato da memória compartilhada

`include/protocol.h` define `MonitorSharedState`. Todos os executáveis que
se comunicam devem ser compilados com o mesmo cabeçalho.

1. O dono cria um nome **único** de memória compartilhada com `monitor_shared_create`.
2. O dono inicia `build/monitor <nome-da-memória>` via fork + exec.
3. O produtor preenche `MonitorStatistics` e chama `monitor_shared_publish`.
4. `monitor_shared_publish` bloqueia o `mutex`, copia os dados, incrementa
   `revision`, libera o mutex e sinaliza `changed`.
5. O Monitor acorda, lê os dados e exibe os contadores.
6. O produtor envia a atualização final com `finished = 1`, mesmo em caso de erro.
7. O dono aguarda o Monitor terminar e então chama `monitor_shared_destroy`.

Somente quem criou pode destruir os semáforos ou remover a memória compartilhada.
Nunca destrua a memória enquanto o Monitor ainda estiver conectado.

`changed` é uma notificação, não uma fila. Atualizações intermediárias muito
rápidas podem ser agrupadas, mas a atualização final é sempre exibida.

## Contadores

- `words_read`: palavras lidas no modo atual.
- `words_compressed`: palavras codificadas pelo dicionário (modo compactação).
- `words_not_found`: palavras sem entrada no dicionário (modo compactação).
- `original_size`: tamanho original do arquivo em bytes.
- `compressed_size`: tamanho do arquivo compactado em bytes.
- `decompressed_size`: tamanho do arquivo reconstruído em bytes (modo descompactação).
- `finished`: o produtor terminou.
- `failed`: o produtor encerrou com erro.

Percentual de compressão: `100 * (1 - compressed_size / original_size)`.
Se o tamanho original for zero, o percentual exibido é 0,00%.

## Observações de integração

As pessoas A e B devem publicar contadores e tamanhos **reais**. Os números do
simulador são ilustrativos e não derivam dos arquivos de teste do trabalho.

Na compactação, A pode publicar atualizações periódicas a partir da thread coordenadora.
Na descompactação, B pode publicar atualizações durante o processamento. O Monitor
não depende da implementação do dicionário.

Não inicie mais de um produtor com o mesmo nome de memória compartilhada. Use um
nome diferente para cada execução. O dono deve garantir que a atualização final
seja enviada, ou encerrar um Monitor travado em caso de erro grave.

Os comentários e mensagens do código estão em português (PT-BR).
Os nomes de funções, variáveis e arquivos estão em inglês.
