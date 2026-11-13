# Monitor protocol

## Execution modes

The assignment requires two modes:

- Compression: compressor process + monitor process.
- Decompression: decompressor process + monitor process.

The monitor is a separate Linux process. The producer creates POSIX shared
memory, initializes process-shared semaphores, then launches the monitor.

## Shared-memory contract

`include/protocol.h` defines `MonitorSharedState`. All cooperating executables
must be built against the same header and ABI.

1. The owner creates a **unique** shared-memory name with
   `monitor_shared_create`.
2. The owner starts `build/monitor <shared-memory-name>` (fork + exec).
3. The producer fills `MonitorStatistics` and calls `monitor_shared_publish`.
4. `monitor_shared_publish` locks `mutex`, copies the snapshot, increments
   `revision`, unlocks, and posts `changed`.
5. The monitor wakes, reads a consistent snapshot, and displays the counters.
6. The producer sends a final snapshot with `finished = 1`, even on failure.
7. The owner waits for the monitor to exit, then calls `monitor_shared_destroy`.

Only the creator may destroy the semaphores or unlink the shared memory.
Never destroy a region while the monitor is still attached.

`changed` is a notification, not a queue. Rapid intermediate snapshots may
be coalesced. The final snapshot is always displayed.

## Counters

- `words_read`: number of word tokens read by the current mode.
- `words_compressed`: words encoded using the dictionary (compression mode).
- `words_not_found`: words without a dictionary entry (compression mode).
- `original_size`: original file size in bytes.
- `compressed_size`: compressed file size in bytes.
- `decompressed_size`: reconstructed file size in bytes (decompression mode).
- `finished`: producer has completed.
- `failed`: producer terminated with an error.

Compression percentage: `100 * (1 - compressed_size / original_size)`.
If original size is zero, the displayed percentage is 0.00%.

## Integration notes

A and B must publish **actual** counters and file sizes. The simulator's
numbers are illustrative and are not derived from the assignment's input files.

For compression, A can publish periodic updates from its coordinator thread.
For decompression, B can publish updates during processing. The monitor does
not depend on the dictionary implementation.

Do not launch multiple producers against one shared-memory name. Use a
distinct name for each execution. The owner must ensure that a terminal
snapshot is sent or terminate a stalled monitor during exceptional shutdown.

All code, logs, comments, filenames, and technical documentation are in
English. `README.md` is the only Portuguese-language project artifact.
