// Smooths the audio clock for the picture. The output's played-sample count
// moves in steps (a buffer at a time, about 43 ms), longer than a 30 fps
// frame (33 ms): read as is, two frames often come due at once and one is
// dropped. Between steps the clock runs on the system's time, never more
// than a step ahead, and never goes back by less than a jump (a seek). Pure logic.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool have;
    int64_t raw_ns;          // The stepped clock last read.
    int64_t changed_at_ns;   // System time when it last moved.
    int64_t out_ns;          // What was last returned.
} SmoothClock;

void smoothClockReset(SmoothClock *c);

// raw_ns: the stepped clock; now_ns: the system's monotonic time; step_ns:
// how far the stepped clock moves at once; running: false while paused.
int64_t smoothClockRead(SmoothClock *c, int64_t raw_ns, int64_t now_ns, int64_t step_ns, bool running);
