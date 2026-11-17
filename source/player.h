// Playing a video file: its picture (video.h) and sound (audio.h) kept
// together, with pause and seeking. The sound is the clock; without sound
// (or after it ends) the console's clock is.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"
#include "video.h"

typedef struct Player Player;

// Opens path and starts playing. NULL with a reason if the picture can't be
// played; sound that can't be played is noted (playerAudioInfo) and skipped.
Player *playerOpen(const char *path, uint64_t now_ns, char *error, size_t error_size);
void playerClose(Player *p);

// Moves the clock to now_ns and returns the frame to draw (NULL before the first).
const GfxVideoFrame *playerUpdate(Player *p, uint64_t now_ns);

void playerTogglePause(Player *p, uint64_t now_ns);
bool playerPaused(const Player *p);
// Goes delta_ns from where playback is (kept within the file).
void playerSeekBy(Player *p, int64_t delta_ns, uint64_t now_ns);

int64_t playerPosition(const Player *p);
int64_t playerDuration(const Player *p);
// True once the picture has ended, or failed (error says why).
bool playerFinished(Player *p, char *error, size_t error_size);

const char *playerVideoInfo(const Player *p);
// The sound's format, or why there is none ("No sound", or the reason).
const char *playerAudioInfo(const Player *p);
void playerSize(const Player *p, int *width, int *height, double *aspect);
VideoStats playerStats(const Player *p);
