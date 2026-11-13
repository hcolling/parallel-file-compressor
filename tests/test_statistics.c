#include "statistics.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    MonitorStatistics stats = {0};
    stats.mode = MONITOR_MODE_COMPRESS;
    stats.words_read = 10;
    stats.words_compressed = 8;
    stats.words_not_found = 2;
    stats.original_size = 1000;
    stats.compressed_size = 400;
    assert(statistics_validate(&stats));
    assert(statistics_compression_percent(&stats) == 60.0);
    stats.words_compressed = 9;
    assert(!statistics_validate(&stats));
    stats.words_compressed = 8;
    stats.original_size = 0;
    assert(statistics_compression_percent(&stats) == 0.0);
    stats.mode = MONITOR_MODE_DECOMPRESS;
    assert(statistics_validate(&stats));
    stats.mode = 99;
    assert(!statistics_validate(&stats));
    puts("[INFO] All monitor statistics tests passed.");
    return 0;
}
