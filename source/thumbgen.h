// Makes the pictures on the library's cards: a thread opens each video with
// the console's decoder, takes a frame a fifth of the way in (later if it's
// black) and keeps it, shrunk, as a JPEG. It runs only while no video plays:
// the decoder is the player's.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define THUMB_W 384
#define THUMB_H 216

bool thumbgenStart(void);
void thumbgenStop(void);

// Asks for path's picture, to be written to jpg_path (an empty file there
// means the video has none). The latest asked are made first; old requests
// drop off when too many wait.
void thumbgenRequest(const char *path, const char *jpg_path, uint64_t key);
// True once for each finished request: its key.
bool thumbgenTakeDone(uint64_t *key);
// Paused, the thread lets go of the decoder (waiting for it) and makes
// nothing until resumed.
void thumbgenPause(bool paused);

// Turns an NV12 frame (w x h, pitches in bytes) into an RGB picture of
// out_w x out_h, cropped to fill it. Exposed for tests.
void thumbgenConvert(const uint8_t *y, const uint8_t *uv, int w, int h, int y_pitch, int uv_pitch, bool bt709,
                     bool full_range, uint8_t *rgb, int out_w, int out_h);
// The average brightness (0-255) of an NV12 frame's luma: a black frame is dark.
int thumbgenLuma(const uint8_t *y, int w, int h, int pitch);
