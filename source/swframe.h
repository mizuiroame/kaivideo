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
// Lighter deblocking: frames nothing refers to are decoded without their
// deblocking filter (FFmpeg's skip_loop_filter), about 5% faster on the
// console's 4K HEVC and a little softer on those frames only. Settings'
// "Faster 4K": off, when needed, or always.
typedef enum { SWFRAME_FAST_OFF, SWFRAME_FAST_AUTO, SWFRAME_FAST_ALWAYS, SWFRAME_FAST_COUNT } SwframeFast;
#define SWFRAME_KEEPUP_FRAMES 48  // Frames looked at together: 2 s at 24 fps.
#define SWFRAME_KEEPUP_LATE 3     // Of those, this many came out of the decoder after their time.

// Frames out of the decoder at normal play (not on the way to a seek's
// target), and how many of them were already late.
typedef struct {
    unsigned frames, late;  // As counted when the window started.
    bool light;             // Once on, it stays on for the video.
} SwframeKeepUp;

// True when the lighter deblocking should be used: only for video over 1080p
// decoded on the CPU; when needed, once SWFRAME_KEEPUP_LATE of
// SWFRAME_KEEPUP_FRAMES frames were late, and from then on.
bool swframeLightDeblock(SwframeKeepUp *k, SwframeFast mode, int width, int height, unsigned frames, unsigned late);
// Planar 4:2:0 (8 or 10 bits, little-endian) to 8-bit NV12, width bytes a row
// or pitch if wider. Sizes are even.
void swframeToNv12(const uint8_t *const planes[3], const ptrdiff_t linesizes[3], int width, int height, int bits,
                   uint8_t *out_y, uint8_t *out_uv, int pitch);
