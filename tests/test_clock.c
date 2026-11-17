// The smoothed audio clock: runs between steps, stops when paused or stalled,
// never goes back a little, and follows a seek.
#include "check.h"
#include "clock.h"

#define MS 1000000ll
#define STEP (43 * MS)

// How many 30 fps frames (33.3 ms apart) come due together on a 60 Hz
// screen, over 10 s of steps: with the raw clock some do, smoothed none.
static int doubleFrames(bool smooth)
{
    SmoothClock c;
    smoothClockReset(&c);
    int doubles = 0;
    int64_t next_frame = 0;
    for (int64_t now = 0; now < 10000 * MS; now += 16667 * 1000) {
        int64_t raw = (now / STEP) * STEP;  // The output's count, a buffer at a time.
        int64_t clock = smooth ? smoothClockRead(&c, raw, now, STEP, true) : raw;
        int due = 0;
        while (next_frame <= clock) {
            due++;
            next_frame += 33333333;
        }
        if (due > 1)
            doubles++;
    }
    return doubles;
}

void testClock(void)
{
    SmoothClock c;
    smoothClockReset(&c);
    CHECK(smoothClockRead(&c, 0, 1000 * MS, STEP, true) == 0);
    CHECK(smoothClockRead(&c, 0, 1020 * MS, STEP, true) == 20 * MS);  // Runs between steps.
    CHECK(smoothClockRead(&c, 0, 1100 * MS, STEP, true) == STEP);      // At most a step ahead.
    CHECK(smoothClockRead(&c, STEP, 1101 * MS, STEP, true) == STEP);   // The step catches up.
    // A step that lands early doesn't take it back.
    smoothClockReset(&c);
    smoothClockRead(&c, 0, 0, STEP, true);
    CHECK(smoothClockRead(&c, 0, 40 * MS, STEP, true) == 40 * MS);
    CHECK(smoothClockRead(&c, STEP - 10 * MS, 41 * MS, STEP, true) == 40 * MS);
    // Paused: stays on the stepped clock.
    smoothClockReset(&c);
    smoothClockRead(&c, 500 * MS, 0, STEP, false);
    CHECK(smoothClockRead(&c, 500 * MS, 30 * MS, STEP, false) == 500 * MS);
    // A seek back is followed.
    CHECK(smoothClockRead(&c, 100 * MS, 60 * MS, STEP, true) == 100 * MS);

    CHECK(doubleFrames(false) > 50);
    CHECK(doubleFrames(true) == 0);
}
