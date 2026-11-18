#include "playback.h"

#include <stdio.h>
#include <stdlib.h>

static const int SPEEDS[PLAYBACK_SPEED_COUNT] = {500, 750, PLAYBACK_PERMILLE, 1250, 1500, 2000};

int playbackSpeedPermille(int index)
{
    return index >= 0 && index < PLAYBACK_SPEED_COUNT ? SPEEDS[index] : PLAYBACK_PERMILLE;
}

int playbackSpeedStep(int index, int delta)
{
    return ((index + delta) % PLAYBACK_SPEED_COUNT + PLAYBACK_SPEED_COUNT) % PLAYBACK_SPEED_COUNT;
}

void playbackFormatSpeed(char *out, size_t size, int permille)
{
    int whole = permille / PLAYBACK_PERMILLE, part = permille % PLAYBACK_PERMILLE;
    if (part == 0)
        snprintf(out, size, "%dx", whole);
    else if (part % 100 == 0)
        snprintf(out, size, "%d.%dx", whole, part / 100);
    else
        snprintf(out, size, "%d.%02dx", whole, part / 10);
}

int64_t playbackScaled(int64_t elapsed_ns, int permille)
{
    return elapsed_ns / PLAYBACK_PERMILLE * permille + elapsed_ns % PLAYBACK_PERMILLE * permille / PLAYBACK_PERMILLE;
}

int playbackDelayStep(int delay_ms, int delta)
{
    int next = delay_ms + delta * PLAYBACK_DELAY_STEP_MS;
    return next > PLAYBACK_DELAY_MAX_MS ? PLAYBACK_DELAY_MAX_MS : next < -PLAYBACK_DELAY_MAX_MS ? -PLAYBACK_DELAY_MAX_MS : next;
}

void playbackFormatDelay(char *out, size_t size, int delay_ms)
{
    int magnitude = abs(delay_ms);
    snprintf(out, size, "%s%d.%d s", delay_ms > 0 ? "+" : delay_ms < 0 ? "-" : "", magnitude / 1000,
             magnitude % 1000 / 100);
}

static int compareChapters(const void *a, const void *b)
{
    int64_t x = ((const PlaybackChapter *)a)->start_ns, y = ((const PlaybackChapter *)b)->start_ns;
    return x < y ? -1 : x > y;
}

void playbackChaptersPrepare(PlaybackChapter *chapters, int count)
{
    if (count <= 0)
        return;
    qsort(chapters, (size_t)count, sizeof(*chapters), compareChapters);
    for (int i = 0; i < count; i++)
        if (!chapters[i].title[0])
            snprintf(chapters[i].title, sizeof(chapters[i].title), "Chapter %d", i + 1);
}

int playbackChapterAt(const PlaybackChapter *chapters, int count, int64_t pos_ns)
{
    int found = -1;
    for (int i = 0; i < count && chapters[i].start_ns <= pos_ns; i++)
        found = i;
    return found;
}
