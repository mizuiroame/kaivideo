// The statistics panel: rates over a moving window, and its lines.
#include <string.h>

#include "check.h"
#include "stats.h"

static const uint64_t MS = 1000000;

static void testRates(void)
{
    StatsWindow w = {0};
    CHECK(!statsRates(&w).valid);  // Nothing yet.
    statsAdd(&w, (StatsSample){.at_ns = 0, .shown = 100, .dropped = 2, .bytes = 1000});
    CHECK(!statsRates(&w).valid);  // One sample says nothing about a rate.

    // A second later: 24 frames shown, 1 late, 1.5 MB in.
    statsAdd(&w, (StatsSample){.at_ns = 1000 * MS, .shown = 124, .dropped = 3, .bytes = 1501000});
    StatsRates r = statsRates(&w);
    CHECK(r.valid);
    CHECK(r.fps > 23.99 && r.fps < 24.01);
    CHECK(r.late_percent > 3.99 && r.late_percent < 4.01);  // 1 of 25.
    CHECK(r.mbps > 11.99 && r.mbps < 12.01);

    // The window keeps the latest STATS_SAMPLES: older ones stop counting.
    StatsWindow moving = {0};
    for (int i = 0; i < STATS_SAMPLES + 10; i++) {
        unsigned late = i < 10 ? 10u * (unsigned)i : 90u;  // Late frames only at first.
        statsAdd(&moving, (StatsSample){.at_ns = (uint64_t)i * 250 * MS, .shown = 6u * (unsigned)i,
                                        .dropped = late, .bytes = 0});
    }
    r = statsRates(&moving);
    CHECK(r.valid && r.late_percent == 0.0 && r.mbps == 0.0);
    CHECK(r.fps > 23.99 && r.fps < 24.01);

    // Counters that went back (a new video) start the window over.
    statsAdd(&moving, (StatsSample){.at_ns = 99000 * MS, .shown = 3, .dropped = 0, .bytes = 0});
    CHECK(!statsRates(&moving).valid);
}

static bool hasLine(char lines[][STATS_LINE_MAX], int count, const char *text)
{
    for (int i = 0; i < count; i++)
        if (strcmp(lines[i], text) == 0)
            return true;
    return false;
}

static void testLines(void)
{
    char lines[STATS_LINES][STATS_LINE_MAX];
    StatsInfo converted = {
        .source = STATS_CONVERTED,
        .quality = "8 Mb/s",
        .video = "H.264 1920x1080, 24 fps",
        .sound = "aac, 48 kHz, 2 channels",
        .rates = {.valid = true, .fps = 23.98, .late_percent = 0.4, .mbps = 7.84},
        .ahead_s = 9.5,
        .ahead_mb = -1,
        .speed_permille = 1000,
    };
    int n = statsFormat(&converted, lines);
    CHECK(n >= 5 && n <= STATS_LINES);
    CHECK(hasLine(lines, n, "Playing: converted by Jellyfin, at most 8 Mb/s"));
    CHECK(hasLine(lines, n, "Video: H.264 1920x1080, 24 fps"));
    CHECK(hasLine(lines, n, "Sound: aac, 48 kHz, 2 channels"));
    CHECK(hasLine(lines, n, "Frames: 24.0 a second, 0.4% late"));
    CHECK(hasLine(lines, n, "Network: 7.8 Mb/s"));
    CHECK(hasLine(lines, n, "Ready ahead: 9.5 s"));

    StatsInfo card = {
        .source = STATS_CARD,
        .video = "HEVC (H.265) 1920x1080, 24 fps, software",
        .sound = "No sound",
        .rates = {.valid = false},
        .ahead_s = -1,
        .ahead_mb = -1,
        .speed_permille = 1250,
    };
    n = statsFormat(&card, lines);
    CHECK(hasLine(lines, n, "Playing: from the SD card"));
    CHECK(hasLine(lines, n, "Frames: measuring..."));
    CHECK(hasLine(lines, n, "Speed: 1.25x"));
    for (int i = 0; i < n; i++)  // Nothing about the network for the card.
        CHECK(strncmp(lines[i], "Network", 7) != 0 && strncmp(lines[i], "Ready", 5) != 0);

    StatsInfo direct = card;
    direct.source = STATS_DIRECT;
    direct.ahead_mb = 24.0;
    direct.rates = (StatsRates){.valid = true, .fps = 30, .late_percent = 0, .mbps = 21.04};
    direct.speed_permille = 1000;
    n = statsFormat(&direct, lines);
    CHECK(hasLine(lines, n, "Playing: the original file from Jellyfin"));
    CHECK(hasLine(lines, n, "Network: 21.0 Mb/s"));
    CHECK(hasLine(lines, n, "Ready ahead: 24 MB"));
    for (int i = 0; i < n; i++)
        CHECK(strncmp(lines[i], "Speed", 5) != 0);  // Normal speed isn't worth a line.
}

void testStats(void)
{
    testRates();
    testLines();
}
