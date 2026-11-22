// Measures how fast the CPU decodes a video's HEVC with FFmpeg, including the
// conversion to the 8-bit NV12 KaiVideo draws. No console calls: main.c runs
// it on the console and host_main.c on a PC.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint64_t (*BenchClock)(void);       // Nanoseconds.
typedef bool (*BenchCancelled)(void *data);

typedef struct {
    bool is_hevc;
    int width, height, bits;
    double fps, duration_s;
    char profile[32];
} BenchInfo;

typedef struct {
    int frames;              // Measured, after the warm-up.
    int thread_count;        // What FFmpeg ended up using.
    bool frame_threads;
    double seconds;
    double average_fps;
    double slowest_fps;      // The slowest stretch of BENCH_STRETCH_FRAMES.
    double convert_ms;       // Average time to convert a frame to NV12.
    bool cancelled;
} BenchResult;

#define BENCH_STRETCH_FRAMES 24

// Reads what the file holds. False, with a reason, if it can't be read.
bool benchProbe(const char *path, BenchInfo *info, char *error, size_t error_size);

// Decodes from a quarter of the way in, for up to max_seconds or max_frames.
// skip_nonref_deblock: no deblocking on frames nothing refers to (faster, a
// little blockier). False, with a reason, if decoding can't go on.
bool benchRun(const char *path, int threads, bool skip_nonref_deblock, double max_seconds, int max_frames,
              BenchClock clock, BenchCancelled cancelled, void *cancel_data, BenchResult *result, char *error,
              size_t error_size);
