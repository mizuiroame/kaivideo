// The rules of the Playback list: the speeds, the sync delays and the
// chapters of a video. Pure logic; player.c and ui.c use it.
#pragma once

#include <stddef.h>
#include <stdint.h>

#define PLAYBACK_PERMILLE 1000       // Speeds are in thousandths: 1250 is 1.25x.
#define PLAYBACK_SPEED_COUNT 6
#define PLAYBACK_SPEED_NORMAL 2      // The index of 1x.
#define PLAYBACK_DELAY_STEP_MS 100
#define PLAYBACK_DELAY_MAX_MS 10000
#define PLAYBACK_CHAPTER_TITLE 64
#define PLAYBACK_VOLUME_NORMAL 100   // Percent.
#define PLAYBACK_VOLUME_MIN 50
#define PLAYBACK_VOLUME_MAX 200
#define PLAYBACK_VOLUME_STEP 10

// The Playback list ends with these, changed with left and right.
typedef enum {
    PLAYBACK_OPT_SPEED,
    PLAYBACK_OPT_VOLUME,
    PLAYBACK_OPT_ASPECT,
    PLAYBACK_OPT_AUDIO_DELAY,
    PLAYBACK_OPT_SUBTITLE_DELAY,
    PLAYBACK_OPT_QUALITY,  // Only for videos from a server.
    PLAYBACK_OPT_COUNT,
} PlaybackOption;

typedef struct {
    int64_t start_ns;
    char title[PLAYBACK_CHAPTER_TITLE];
} PlaybackChapter;

// The speed at index, in thousandths (normal speed if there's no such index).
int playbackSpeedPermille(int index);
// The index delta steps from index, going around.
int playbackSpeedStep(int index, int delta);
// "1x", "1.25x".
void playbackFormatSpeed(char *out, size_t size, int permille);
// How much of the video passes in elapsed_ns at this speed.
int64_t playbackScaled(int64_t elapsed_ns, int permille);

// A delay delta steps on, kept within PLAYBACK_DELAY_MAX_MS either way.
int playbackDelayStep(int delay_ms, int delta);
// "0.0 s", "+0.3 s", "-1.2 s".
void playbackFormatDelay(char *out, size_t size, int delay_ms);

// A volume delta steps on, kept within PLAYBACK_VOLUME_MIN and _MAX.
int playbackVolumeStep(int percent, int delta);
// "150%".
void playbackFormatVolume(char *out, size_t size, int percent);
// Sound samples at percent of their volume, in place. Over 100%, sound near
// the top bends under it (a soft limiter) instead of clipping.
void playbackApplyVolume(int16_t *pcm, size_t samples, int percent);

// How the picture fills the screen.
typedef enum {
    PLAYBACK_ASPECT_ORIGINAL,  // Its own shape, as large as fits.
    PLAYBACK_ASPECT_FILL,      // Its own shape, covering the screen (cut at the edges).
    PLAYBACK_ASPECT_STRETCH,   // The screen's shape.
    PLAYBACK_ASPECT_4_3,
    PLAYBACK_ASPECT_16_9,
    PLAYBACK_ASPECT_COUNT,
} PlaybackAspect;

PlaybackAspect playbackAspectStep(PlaybackAspect aspect, int delta);
const char *playbackAspectName(PlaybackAspect aspect);
// The picture's size on a screen_w x screen_h screen, centered on it (it may
// be larger, for FILL). video_aspect: its shape, width over height; 0 if unknown.
void playbackVideoBox(double video_aspect, PlaybackAspect aspect, float screen_w, float screen_h, float *w, float *h);

// Puts chapters in order of time and names those without a title ("Chapter 2").
void playbackChaptersPrepare(PlaybackChapter *chapters, int count);
// The chapter playing at pos_ns (they're in order), or -1 before the first.
int playbackChapterAt(const PlaybackChapter *chapters, int count, int64_t pos_ns);
