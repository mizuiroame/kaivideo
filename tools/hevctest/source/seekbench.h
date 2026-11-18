// Measures where the time goes when an HEVC video opens and when it seeks, the
// way KaiVideo does both. No console calls, like bench.h.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "bench.h"

#define SEEKBENCH_ROUNDS 2  // Openings averaged.

typedef struct {
    double open_ms;   // Reading the file's headers.
    double info_ms;   // avformat_find_stream_info.
    double index_ms;  // The first seek, which loads the file's index.
} SeekOpenResult;

typedef struct {
    int seeks;
    double first_ms;     // Average, until any picture comes out (the keyframe).
    double target_ms;    // Average, until the picture at the target.
    double worst_ms;     // The slowest seek to its target.
    double packets;      // Average video packets read to get there.
    bool cancelled;
} SeekRunResult;

// Opens the file SEEKBENCH_ROUNDS times with these decoders allowed (NULL: all).
bool seekbenchOpen(const char *path, const char *decoders, BenchClock clock, SeekOpenResult *result, char *error,
                   size_t error_size);

// Seeks to fixed places through the file, decoding up to each.
bool seekbenchRun(const char *path, int threads, BenchClock clock, BenchCancelled cancelled, void *cancel_data,
                  SeekRunResult *result, char *error, size_t error_size);
