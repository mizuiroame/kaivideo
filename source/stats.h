// The statistics panel shown over a playing video (↓): what plays, how
// smoothly, and how the network keeps up. Pure logic; ui_stats.c draws it.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define STATS_SAMPLES 12     // About 3 s, sampled every STATS_EVERY_NS.
#define STATS_EVERY_NS 250000000ull
#define STATS_LINES 8
#define STATS_LINE_MAX 112

// Counters as they stand at a moment; they only grow while a video plays.
typedef struct {
    uint64_t at_ns;
    unsigned shown, dropped;  // Frames drawn, and frames skipped as late.
    uint64_t bytes;           // Received from the network.
} StatsSample;

typedef struct {
    StatsSample samples[STATS_SAMPLES];
    int count, next;
} StatsWindow;

typedef struct {
    bool valid;           // False until two samples cover some time.
    double fps;           // Frames shown a second.
    double late_percent;  // Of the frames due, how many were skipped as late.
    double mbps;          // Megabits a second from the network.
} StatsRates;

// Counters lower than the last sample's (another video) start it over.
void statsAdd(StatsWindow *w, StatsSample s);
StatsRates statsRates(const StatsWindow *w);

typedef enum {
    STATS_CARD,       // A file on the SD card.
    STATS_DIRECT,     // The original file from Jellyfin.
    STATS_CONVERTED,  // Converted by the server.
} StatsSource;

typedef struct {
    StatsSource source;
    const char *quality;     // Converted: the setting's name, "" on Auto.
    const char *video, *sound;
    StatsRates rates;
    double ahead_s;          // Video in memory past where it plays; < 0 if unknown.
    double ahead_mb;         // The same in megabytes, when seconds aren't known; < 0 if unknown.
    int speed_permille;
} StatsInfo;

// The panel's lines. Returns how many.
int statsFormat(const StatsInfo *info, char lines[STATS_LINES][STATS_LINE_MAX]);
