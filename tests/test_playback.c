// Playback speed, sync delays and chapters: the rules, without a player.
#include <string.h>

#include "check.h"
#include "playback.h"

static const int64_t S = 1000000000;

static void testSpeed(void)
{
    CHECK(playbackSpeedPermille(PLAYBACK_SPEED_NORMAL) == PLAYBACK_PERMILLE);
    CHECK(playbackSpeedPermille(0) == 500);
    CHECK(playbackSpeedPermille(PLAYBACK_SPEED_COUNT - 1) == 2000);
    // Out of range is normal speed, not a read past the table.
    CHECK(playbackSpeedPermille(-1) == PLAYBACK_PERMILLE);
    CHECK(playbackSpeedPermille(PLAYBACK_SPEED_COUNT) == PLAYBACK_PERMILLE);

    // Steps go around, as the other options of the lists do.
    CHECK(playbackSpeedStep(PLAYBACK_SPEED_NORMAL, 1) == PLAYBACK_SPEED_NORMAL + 1);
    CHECK(playbackSpeedStep(PLAYBACK_SPEED_COUNT - 1, 1) == 0);
    CHECK(playbackSpeedStep(0, -1) == PLAYBACK_SPEED_COUNT - 1);

    char text[16];
    playbackFormatSpeed(text, sizeof(text), 1000);
    CHECK(strcmp(text, "1x") == 0);
    playbackFormatSpeed(text, sizeof(text), 1250);
    CHECK(strcmp(text, "1.25x") == 0);
    playbackFormatSpeed(text, sizeof(text), 1500);
    CHECK(strcmp(text, "1.5x") == 0);
    playbackFormatSpeed(text, sizeof(text), 500);
    CHECK(strcmp(text, "0.5x") == 0);
    playbackFormatSpeed(text, sizeof(text), 2000);
    CHECK(strcmp(text, "2x") == 0);

    CHECK(playbackScaled(10 * S, 1000) == 10 * S);
    CHECK(playbackScaled(10 * S, 1500) == 15 * S);
    CHECK(playbackScaled(10 * S, 500) == 5 * S);
    // A console that has been on for weeks doesn't overflow at the fastest speed.
    CHECK(playbackScaled(30 * 86400 * S, 2000) == 60 * 86400 * S);
}

static void testDelay(void)
{
    CHECK(playbackDelayStep(0, 1) == PLAYBACK_DELAY_STEP_MS);
    CHECK(playbackDelayStep(0, -1) == -PLAYBACK_DELAY_STEP_MS);
    CHECK(playbackDelayStep(PLAYBACK_DELAY_MAX_MS, 1) == PLAYBACK_DELAY_MAX_MS);
    CHECK(playbackDelayStep(-PLAYBACK_DELAY_MAX_MS, -1) == -PLAYBACK_DELAY_MAX_MS);

    char text[16];
    playbackFormatDelay(text, sizeof(text), 0);
    CHECK(strcmp(text, "0.0 s") == 0);
    playbackFormatDelay(text, sizeof(text), 300);
    CHECK(strcmp(text, "+0.3 s") == 0);
    playbackFormatDelay(text, sizeof(text), -1200);
    CHECK(strcmp(text, "-1.2 s") == 0);
    playbackFormatDelay(text, sizeof(text), -100);
    CHECK(strcmp(text, "-0.1 s") == 0);
}

static void testChapters(void)
{
    PlaybackChapter c[4] = {
        {.start_ns = 600 * S, .title = "Third"},
        {.start_ns = 0, .title = ""},
        {.start_ns = 300 * S, .title = "Second"},
        {.start_ns = 900 * S, .title = ""},
    };
    playbackChaptersPrepare(c, 4);
    CHECK(c[0].start_ns == 0 && c[1].start_ns == 300 * S && c[2].start_ns == 600 * S && c[3].start_ns == 900 * S);
    // Without a title, a chapter is called by its place.
    CHECK(strcmp(c[0].title, "Chapter 1") == 0);
    CHECK(strcmp(c[1].title, "Second") == 0);
    CHECK(strcmp(c[3].title, "Chapter 4") == 0);

    CHECK(playbackChapterAt(c, 4, 0) == 0);
    CHECK(playbackChapterAt(c, 4, 299 * S) == 0);
    CHECK(playbackChapterAt(c, 4, 300 * S) == 1);
    CHECK(playbackChapterAt(c, 4, 5000 * S) == 3);
    CHECK(playbackChapterAt(c, 0, 10 * S) == -1);

    PlaybackChapter late = {.start_ns = 60 * S, .title = "Late"};
    CHECK(playbackChapterAt(&late, 1, 10 * S) == -1);  // Before the first one.
    playbackChaptersPrepare(c, 0);                     // Nothing to do, and no crash.
}

void testPlayback(void)
{
    testSpeed();
    testDelay();
    testChapters();
}
