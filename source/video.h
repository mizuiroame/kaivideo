// Playing a video file's picture: a thread reads it with FFmpeg and decodes
// it with the console's hardware decoder (libounce's VideoDecoder), into a
// few frames that the window's thread shows when their time comes.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"

#define VIDEO_QUEUE 3  // Decoded frames waiting to be shown.

typedef struct {
    // Counted while it plays, for the log.
    unsigned shown;      // Frames drawn.
    unsigned dropped;    // Decoded but late: skipped to keep up.
    unsigned decoded;    // Out of the decoder.
    double copy_ms;      // Average time to take a frame from the decoder.
} VideoStats;

typedef struct Video Video;

// Opens path and starts decoding. NULL with a reason in error if the file
// can't be read or its video isn't H.264, VP9, AV1 or VP8.
Video *videoOpen(const char *path, char *error, size_t error_size);
// Stops the thread and frees everything.
void videoClose(Video *v);

// "H.264 1920x1080, 60 fps".
const char *videoDescription(const Video *v);
// The picture's size and shape: width / height as shown (sample aspect included).
void videoSize(const Video *v, int *width, int *height, double *aspect);

// The frame to show at now_ns (monotonic): the latest whose time has come;
// the clock starts with the first frame. NULL before the first. The frame
// stays valid until the next call.
const GfxVideoFrame *videoFrameAt(Video *v, uint64_t now_ns);
// True once the last frame has been shown, or decoding failed (see error).
bool videoFinished(Video *v, char *error, size_t error_size);
VideoStats videoStats(const Video *v);
