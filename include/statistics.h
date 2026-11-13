#ifndef STATISTICS_H
#define STATISTICS_H

#include "protocol.h"

/* Returns 0 when the original size is zero, avoiding division by zero.
 * A negative result means the compressed file is larger than the original. */
double statistics_compression_percent(const MonitorStatistics *statistics);

/* Validates counter consistency for the selected execution mode. */
int statistics_validate(const MonitorStatistics *statistics);

/* Prints an English-language status snapshot. */
void statistics_print(const MonitorStatistics *statistics);

#endif
