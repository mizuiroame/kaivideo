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

// The Playback list ends with these, changed with left and right.
typedef enum {
    PLAYBACK_OPT_SPEED,
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

// Puts chapters in order of time and names those without a title ("Chapter 2").
void playbackChaptersPrepare(PlaybackChapter *chapters, int count);
// The chapter playing at pos_ns (they're in order), or -1 before the first.
int playbackChapterAt(const PlaybackChapter *chapters, int count, int64_t pos_ns);
