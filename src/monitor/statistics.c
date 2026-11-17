#include "statistics.h"

#include <inttypes.h>
#include <stdio.h>

double statistics_compression_percent(const MonitorStatistics *statistics) {
    if (statistics == NULL || statistics->original_size == 0) return 0.0;
    return 100.0 * (1.0 - (double)statistics->compressed_size /
                                 (double)statistics->original_size);
}

int statistics_validate(const MonitorStatistics *statistics) {
    if (statistics == NULL) return 0;
    if (statistics->mode != MONITOR_MODE_COMPRESS &&
        statistics->mode != MONITOR_MODE_DECOMPRESS) return 0;
    if (statistics->finished > 1 || statistics->failed > 1) return 0;
    if (statistics->mode == MONITOR_MODE_COMPRESS &&
        (statistics->words_compressed > statistics->words_read ||
         statistics->words_not_found > statistics->words_read ||
         statistics->words_compressed >
            statistics->words_read - statistics->words_not_found)) return 0;
    return 1;
}

void statistics_print(const MonitorStatistics *statistics) {
    if (statistics == NULL) return;
    const char *mode = statistics->mode == MONITOR_MODE_COMPRESS ?
                       "Compactação" : "Descompactação";
    printf("[MONITOR] Modo: %s | Atualização: %" PRIu64 "\n",
           mode, statistics->revision);
    printf("[MONITOR] Palavras lidas: %" PRIu64
           " | Compactadas: %" PRIu64
           " | Não encontradas: %" PRIu64 "\n",
           statistics->words_read, statistics->words_compressed,
           statistics->words_not_found);
    printf("[MONITOR] Tamanho original: %" PRIu64
           " bytes | Tamanho compactado: %" PRIu64 " bytes\n",
           statistics->original_size, statistics->compressed_size);
    if (statistics->mode == MONITOR_MODE_DECOMPRESS)
        printf("[MONITOR] Tamanho descompactado: %" PRIu64 " bytes\n",
               statistics->decompressed_size);
    printf("[MONITOR] Compressão: %.2f%%\n",
           statistics_compression_percent(statistics));
    if (statistics->finished)
        printf("[MONITOR] Execução %s.\n",
               statistics->failed ? "encerrada com erro" : "concluída com sucesso");
    fflush(stdout);
}
