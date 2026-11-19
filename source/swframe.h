// The rules of software video decoding, for what the console's decoder can't
// play (HEVC, 10-bit H.264): which way a video goes, how many threads it gets,
// which frames can be skipped, and the conversion to the NV12 the screen
// takes. Pure logic; video.c runs FFmpeg with it.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SWFRAME_MAX_THREADS 6
#define SWFRAME_NO_CLOCK INT64_MIN        // Playback hasn't said where it is.
#define SWFRAME_LATE_NS 250000000ll       // This far behind, frames can go.

typedef enum { SWFRAME_H264, SWFRAME_HEVC, SWFRAME_VP9, SWFRAME_AV1, SWFRAME_VP8, SWFRAME_OTHER } SwframeCodec;
typedef enum { SWFRAME_HARDWARE, SWFRAME_SOFTWARE, SWFRAME_UNPLAYABLE } SwframeRoute;

// The way a video goes. bits: its depth, 0 if unknown; width and height: its
// size, 0 if unknown.
SwframeRoute swframeRoute(SwframeCodec codec, int bits, int width, int height);
// Decoding threads with this many CPU cores. All of them, whatever the video:
// playing they mostly wait, and after a seek they get to the target sooner.
int swframeThreads(int cores);
// True if a frame that nothing depends on needn't be decoded: it comes before
// a seek's target, or playback (clock_ns) has passed it by far.
bool swframeCanSkip(int64_t pts_ns, int64_t skip_until_ns, int64_t clock_ns);
// Planar 4:2:0 (8 or 10 bits, little-endian) to 8-bit NV12, width bytes a row
// or pitch if wider. Sizes are even.
void swframeToNv12(const uint8_t *const planes[3], const ptrdiff_t linesizes[3], int width, int height, int bits,
                   uint8_t *out_y, uint8_t *out_uv, int pitch);
