// Runs the HEVC measurement on a PC, to check it before it goes to the console:
//   host_main VIDEO...
#include <stdio.h>
#include <time.h>

#include "source/bench.h"

static uint64_t nowNs(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

int main(int argc, char **argv)
{
    static const int THREADS[] = {1, 4};
    int failures = 0;
    for (int i = 1; i < argc; i++) {
        BenchInfo info;
        char error[96] = "";
        if (!benchProbe(argv[i], &info, error, sizeof(error))) {
            printf("%s: %s\n", argv[i], error);
            failures++;
            continue;
        }
        printf("%s: hevc=%d %dx%d %d-bit %s %.2f fps\n", argv[i], info.is_hevc, info.width, info.height, info.bits,
               info.profile, info.fps);
        for (size_t t = 0; t < sizeof(THREADS) / sizeof(THREADS[0]); t++) {
            BenchResult r;
            if (!benchRun(argv[i], THREADS[t], 3.0, 120, nowNs, NULL, NULL, &r, error, sizeof(error))) {
                printf("  %d threads: FAILED %s\n", THREADS[t], error);
                failures++;
                continue;
            }
            printf("  %d threads (active %d, frame=%d): %d frames, %.1f fps, slowest %.1f, convert %.2f ms\n",
                   THREADS[t], r.thread_count, r.frame_threads, r.frames, r.average_fps, r.slowest_fps, r.convert_ms);
            if (r.frames == 0)
                failures++;
        }
    }
    return failures ? 1 : 0;
}
