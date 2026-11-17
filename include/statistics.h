#ifndef STATISTICS_H
#define STATISTICS_H

#include "protocol.h"

/* Calcula o percentual de compressão. Retorna zero se o original estiver vazio.
 * Um resultado negativo indica que o arquivo compactado ficou maior. */
double statistics_compression_percent(const MonitorStatistics *statistics);

/* Confere se as estatísticas são válidas para o modo escolhido. */
int statistics_validate(const MonitorStatistics *statistics);

/* Mostra as estatísticas atuais. */
void statistics_print(const MonitorStatistics *statistics);

#endif
