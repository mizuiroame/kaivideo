// Makes the pictures on the library's cards: a thread opens each video as the
// player does, takes a frame a fifth of the way in (later if it's black) and
// keeps it, shrunk, as a JPEG. It runs only while no video plays: the decoder
// is the player's.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define THUMB_W 384
#define THUMB_H 216
#define THUMB_LARGE_W 960         // For the details panel.
#define THUMB_LARGE_H 540
#define THUMB_NONE_MARK "none"     // The file left for a video without a picture.
#define THUMB_NONE_MARK_SIZE 4

typedef enum {
    THUMB_FILE_RETRY,    // Empty: an older version couldn't decode it; this one may.
    THUMB_FILE_NONE,     // The mark: this video has no picture.
    THUMB_FILE_PICTURE,
} ThumbFileKind;

bool thumbgenStart(void);
void thumbgenStop(void);

// What a file of this size in the thumbnails' folder is.
ThumbFileKind thumbgenFileKind(long long size);

// Asks for path's picture, to be written to jpg_path (THUMB_NONE_MARK there
// means the video has none). The latest asked are made first; old requests
// drop off when too many wait.
void thumbgenRequest(const char *path, const char *jpg_path, uint64_t key, bool large);
// True once for each finished request: its key.
bool thumbgenTakeDone(uint64_t *key);
// Paused, the thread lets go of the decoder (waiting for it) and makes
// nothing until resumed.
void thumbgenPause(bool paused);

// Turns an NV12 frame (w x h, pitches in bytes) into an RGB picture of
// out_w x out_h, cropped to fill it. Exposed for tests.
void thumbgenConvert(const uint8_t *y, const uint8_t *uv, int w, int h, int y_pitch, int uv_pitch, bool bt709,
                     bool full_range, uint8_t *rgb, int out_w, int out_h);
// An RGB picture of w x h into one of out_w x out_h: its middle, cropped to
// that shape, each output pixel the average of those under it.
void thumbgenFit(const uint8_t *rgb, int w, int h, uint8_t *out, int out_w, int out_h);
// The average brightness (0-255) of an NV12 frame's luma: a black frame is dark.
int thumbgenLuma(const uint8_t *y, int w, int h, int pitch);
