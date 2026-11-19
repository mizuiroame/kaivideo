#include "stats.h"

#include <stdio.h>

#include "playback.h"

#define NS_PER_S 1e9
#define BITS_PER_BYTE 8.0
#define BITS_PER_MEGABIT 1e6

void statsAdd(StatsWindow *w, StatsSample s)
{
    if (w->count) {
        const StatsSample *last = &w->samples[(w->next + STATS_SAMPLES - 1) % STATS_SAMPLES];
        if (s.shown < last->shown || s.dropped < last->dropped || s.bytes < last->bytes || s.at_ns < last->at_ns)
            *w = (StatsWindow){0};
    }
    w->samples[w->next] = s;
    w->next = (w->next + 1) % STATS_SAMPLES;
    if (w->count < STATS_SAMPLES)
        w->count++;
}

StatsRates statsRates(const StatsWindow *w)
{
    StatsRates r = {0};
    if (w->count < 2)
        return r;
    const StatsSample *oldest = &w->samples[(w->next + STATS_SAMPLES - w->count) % STATS_SAMPLES];
    const StatsSample *newest = &w->samples[(w->next + STATS_SAMPLES - 1) % STATS_SAMPLES];
    double seconds = (double)(newest->at_ns - oldest->at_ns) / NS_PER_S;
    if (seconds <= 0)
        return r;
    unsigned shown = newest->shown - oldest->shown, dropped = newest->dropped - oldest->dropped;
    r.valid = true;
    r.fps = shown / seconds;
    r.late_percent = shown + dropped ? 100.0 * dropped / (shown + dropped) : 0.0;
    r.mbps = (double)(newest->bytes - oldest->bytes) * BITS_PER_BYTE / BITS_PER_MEGABIT / seconds;
    return r;
}

static void describeSource(const StatsInfo *info, char *out, size_t size)
{
    switch (info->source) {
    case STATS_CARD: snprintf(out, size, "Playing: from the SD card"); break;
    case STATS_DIRECT: snprintf(out, size, "Playing: the original file from Jellyfin"); break;
    case STATS_CONVERTED:
        if (info->quality && info->quality[0] && info->quality[0] != 'A')
            snprintf(out, size, "Playing: converted by Jellyfin, at most %s", info->quality);
        else
            snprintf(out, size, "Playing: converted by Jellyfin");
        break;
    }
}

int statsFormat(const StatsInfo *info, char lines[STATS_LINES][STATS_LINE_MAX])
{
    int n = 0;
    describeSource(info, lines[n++], STATS_LINE_MAX);
    snprintf(lines[n++], STATS_LINE_MAX, "Video: %s", info->video ? info->video : "");
    snprintf(lines[n++], STATS_LINE_MAX, "Sound: %s", info->sound ? info->sound : "");
    if (info->rates.valid)
        snprintf(lines[n++], STATS_LINE_MAX, "Frames: %.1f a second, %.1f%% late", info->rates.fps,
                 info->rates.late_percent);
    else
        snprintf(lines[n++], STATS_LINE_MAX, "Frames: measuring...");
    if (info->source != STATS_CARD) {
        if (info->rates.valid)
            snprintf(lines[n++], STATS_LINE_MAX, "Network: %.1f Mb/s", info->rates.mbps);
        if (info->ahead_s >= 0)
            snprintf(lines[n++], STATS_LINE_MAX, "Ready ahead: %.1f s", info->ahead_s);
        else if (info->ahead_mb >= 0)
            snprintf(lines[n++], STATS_LINE_MAX, "Ready ahead: %.0f MB", info->ahead_mb);
    }
    if (info->speed_permille != PLAYBACK_PERMILLE) {
        char speed[16];
        playbackFormatSpeed(speed, sizeof(speed), info->speed_permille);
        snprintf(lines[n++], STATS_LINE_MAX, "Speed: %s", speed);
    }
    return n;
}
