#ifndef DECOMPRESSOR_H
#define DECOMPRESSOR_H

/* Decompresses a file produced by the compressor using the escape-byte +
 * LEB128 format described in docs/dictionary_format.md.
 *
 * dict_path  : path to the tab-separated dictionary file.
 * input_path : path to the compressed file (.mzp).
 * output_path: path for the reconstructed output file.
 *
 * Launches build/monitor as a child process via fork + exec and publishes
 * statistics through POSIX shared memory during and after processing.
 *
 * Returns 0 on success, 1 on any error. */
int decompressor_run(const char *dict_path,
                     const char *input_path,
                     const char *output_path);

#endif
