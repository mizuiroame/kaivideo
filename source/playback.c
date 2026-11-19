#include "playback.h"

#include <math.h>
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

int playbackVolumeStep(int percent, int delta)
{
    int next = percent + delta * PLAYBACK_VOLUME_STEP;
    return next > PLAYBACK_VOLUME_MAX ? PLAYBACK_VOLUME_MAX : next < PLAYBACK_VOLUME_MIN ? PLAYBACK_VOLUME_MIN : next;
}

void playbackFormatVolume(char *out, size_t size, int percent)
{
    snprintf(out, size, "%d%%", percent);
}

#define SAMPLE_TOP 32767.0f
#define LIMIT_KNEE 0.6f  // Of the top: louder than this, sound is bent under the top.

// The limiter's curve over the knee: rises like the input there, and nears
// the top without reaching it.
static float limit(float x)
{
    float knee = LIMIT_KNEE * SAMPLE_TOP, room = SAMPLE_TOP - knee;
    float magnitude = fabsf(x);
    if (magnitude <= knee)
        return x;
    float bent = knee + room * tanhf((magnitude - knee) / room);
    return x < 0 ? -bent : bent;
}

void playbackApplyVolume(int16_t *pcm, size_t samples, int percent)
{
    if (percent == PLAYBACK_VOLUME_NORMAL)
        return;
    float gain = percent / 100.0f;
    for (size_t i = 0; i < samples; i++) {
        float y = pcm[i] * gain;
        if (percent > PLAYBACK_VOLUME_NORMAL)
            y = limit(y);
        pcm[i] = (int16_t)lrintf(y > SAMPLE_TOP ? SAMPLE_TOP : y < -SAMPLE_TOP ? -SAMPLE_TOP : y);
    }
}

PlaybackAspect playbackAspectStep(PlaybackAspect aspect, int delta)
{
    return (PlaybackAspect)((((int)aspect + delta) % PLAYBACK_ASPECT_COUNT + PLAYBACK_ASPECT_COUNT) %
                            PLAYBACK_ASPECT_COUNT);
}

const char *playbackAspectName(PlaybackAspect aspect)
{
    static const char *const NAMES[PLAYBACK_ASPECT_COUNT] = {"Original", "Fill", "Stretch", "4:3", "16:9"};
    return aspect >= 0 && aspect < PLAYBACK_ASPECT_COUNT ? NAMES[aspect] : NAMES[0];
}

void playbackVideoBox(double video_aspect, PlaybackAspect aspect, float screen_w, float screen_h, float *w, float *h)
{
    double screen = screen_w / screen_h;
    double shape = aspect == PLAYBACK_ASPECT_4_3    ? 4.0 / 3.0
                   : aspect == PLAYBACK_ASPECT_16_9 ? 16.0 / 9.0
                   : video_aspect > 0               ? video_aspect
                                                    : screen;
    if (aspect == PLAYBACK_ASPECT_STRETCH) {
        *w = screen_w;
        *h = screen_h;
    } else if ((shape > screen) != (aspect == PLAYBACK_ASPECT_FILL)) {
        *w = screen_w;  // Wider than the screen (or, filling, narrower): as wide as it.
        *h = (float)(screen_w / shape);
    } else {
        *h = screen_h;
        *w = (float)(screen_h * shape);
    }
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
