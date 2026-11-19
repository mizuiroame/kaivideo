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

static void testVolume(void)
{
    CHECK(playbackVolumeStep(PLAYBACK_VOLUME_NORMAL, 1) == PLAYBACK_VOLUME_NORMAL + PLAYBACK_VOLUME_STEP);
    CHECK(playbackVolumeStep(PLAYBACK_VOLUME_MAX, 1) == PLAYBACK_VOLUME_MAX);
    CHECK(playbackVolumeStep(PLAYBACK_VOLUME_MIN, -1) == PLAYBACK_VOLUME_MIN);
    char text[16];
    playbackFormatVolume(text, sizeof(text), 150);
    CHECK(!strcmp(text, "150%"));

    // 100%: untouched. Below: quieter, in proportion.
    int16_t pcm[6] = {1000, -1000, 32767, -32768, 0, 20000};
    int16_t copy[6];
    memcpy(copy, pcm, sizeof(pcm));
    playbackApplyVolume(copy, 6, PLAYBACK_VOLUME_NORMAL);
    CHECK(!memcmp(copy, pcm, sizeof(pcm)));
    memcpy(copy, pcm, sizeof(pcm));
    playbackApplyVolume(copy, 6, 50);
    CHECK(copy[0] == 500 && copy[1] == -500 && copy[4] == 0);

    // Louder: quiet sound doubles; loud sound bends under the limit instead of clipping.
    memcpy(copy, pcm, sizeof(pcm));
    playbackApplyVolume(copy, 6, 200);
    CHECK(copy[0] == 2000 && copy[1] == -2000);
    CHECK(copy[2] < 32767 && copy[2] > 30000 && copy[3] > -32768 && copy[3] < -30000);
    CHECK(copy[5] > 20000 && copy[5] < 32767);
    // Louder input stays louder (no folding back).
    int16_t ramp[4] = {15000, 20000, 25000, 30000};
    playbackApplyVolume(ramp, 4, 200);
    CHECK(ramp[0] < ramp[1] && ramp[1] < ramp[2] && ramp[2] < ramp[3]);
}

static void testAspect(void)
{
    float w, h;
    // A 2.39:1 film on the 16:9 screen (1920x1080).
    playbackVideoBox(2.39, PLAYBACK_ASPECT_ORIGINAL, 1920, 1080, &w, &h);
    CHECK(w == 1920 && h > 803 && h < 804);
    playbackVideoBox(2.39, PLAYBACK_ASPECT_FILL, 1920, 1080, &w, &h);  // Covers the screen, cut at the sides.
    CHECK(h == 1080 && w > 2581 && w < 2582);
    playbackVideoBox(2.39, PLAYBACK_ASPECT_STRETCH, 1920, 1080, &w, &h);
    CHECK(w == 1920 && h == 1080);
    // A 16:9 video shown as 4:3, and a 4:3 one as 16:9.
    playbackVideoBox(16.0 / 9.0, PLAYBACK_ASPECT_4_3, 1920, 1080, &w, &h);
    CHECK(h == 1080 && w == 1440);
    playbackVideoBox(4.0 / 3.0, PLAYBACK_ASPECT_16_9, 1920, 1080, &w, &h);
    CHECK(w == 1920 && h == 1080);
    playbackVideoBox(4.0 / 3.0, PLAYBACK_ASPECT_FILL, 1920, 1080, &w, &h);  // Cut at the top and bottom.
    CHECK(w == 1920 && h == 1440);
    playbackVideoBox(0, PLAYBACK_ASPECT_ORIGINAL, 1920, 1080, &w, &h);  // Unknown shape: the screen's.
    CHECK(w == 1920 && h == 1080);
    CHECK(!strcmp(playbackAspectName(PLAYBACK_ASPECT_4_3), "4:3"));
    CHECK(playbackAspectStep(PLAYBACK_ASPECT_16_9, 1) == PLAYBACK_ASPECT_ORIGINAL);  // Around.
    CHECK(playbackAspectStep(PLAYBACK_ASPECT_ORIGINAL, -1) == PLAYBACK_ASPECT_16_9);
}

void testPlayback(void)
{
    testSpeed();
    testDelay();
    testChapters();
    testVolume();
    testAspect();
}
