#include "swframe.h"

#include <string.h>

#define EASY_PIXEL_RATE (1920.0 * 1080.0 * 30.5)  // Up to 1080p30: fewer threads are plenty.
#define EASY_THREADS 4
#define TEN_TO_EIGHT 2                            // Bits dropped from a 10-bit sample.

SwframeRoute swframeRoute(SwframeCodec codec, int bits)
{
    bool eight = bits == 0 || bits == 8;
    switch (codec) {
    case SWFRAME_HEVC:
        return eight || bits == 10 ? SWFRAME_SOFTWARE : SWFRAME_UNPLAYABLE;
    case SWFRAME_H264:
        return eight ? SWFRAME_HARDWARE : bits == 10 ? SWFRAME_SOFTWARE : SWFRAME_UNPLAYABLE;
    case SWFRAME_VP9:
    case SWFRAME_AV1:
    case SWFRAME_VP8:
        return eight ? SWFRAME_HARDWARE : SWFRAME_UNPLAYABLE;
    default:
        return SWFRAME_UNPLAYABLE;
    }
}

int swframeThreads(int width, int height, double fps, int cores)
{
    double rate = (double)width * height * (fps > 0 ? fps : 24.0);
    int wanted = rate <= EASY_PIXEL_RATE ? EASY_THREADS : SWFRAME_MAX_THREADS;
    if (wanted > cores)
        wanted = cores;
    return wanted > 0 ? wanted : 1;
}

bool swframeCanSkip(int64_t pts_ns, int64_t skip_until_ns, int64_t clock_ns)
{
    if (pts_ns < skip_until_ns)
        return true;
    return clock_ns != SWFRAME_NO_CLOCK && clock_ns > pts_ns &&
           (uint64_t)clock_ns - (uint64_t)pts_ns > (uint64_t)SWFRAME_LATE_NS;
}

static void luma8(const uint8_t *src, uint8_t *dst, int width)
{
    memcpy(dst, src, (size_t)width);
}

static void luma10(const uint8_t *src, uint8_t *dst, int width)
{
    const uint16_t *wide = (const uint16_t *)(const void *)src;
    for (int x = 0; x < width; x++)
        dst[x] = (uint8_t)(wide[x] >> TEN_TO_EIGHT);
}

static void chroma8(const uint8_t *u, const uint8_t *v, uint8_t *dst, int samples)
{
    for (int x = 0; x < samples; x++) {
        dst[x * 2] = u[x];
        dst[x * 2 + 1] = v[x];
    }
}

static void chroma10(const uint8_t *u, const uint8_t *v, uint8_t *dst, int samples)
{
    const uint16_t *wide_u = (const uint16_t *)(const void *)u, *wide_v = (const uint16_t *)(const void *)v;
    for (int x = 0; x < samples; x++) {
        dst[x * 2] = (uint8_t)(wide_u[x] >> TEN_TO_EIGHT);
        dst[x * 2 + 1] = (uint8_t)(wide_v[x] >> TEN_TO_EIGHT);
    }
}

void swframeToNv12(const uint8_t *const planes[3], const ptrdiff_t linesizes[3], int width, int height, int bits,
                   uint8_t *out_y, uint8_t *out_uv, int pitch)
{
    bool ten = bits == 10;
    for (int y = 0; y < height; y++) {
        const uint8_t *src = planes[0] + y * linesizes[0];
        uint8_t *dst = out_y + (ptrdiff_t)y * pitch;
        if (ten)
            luma10(src, dst, width);
        else
            luma8(src, dst, width);
    }
    for (int y = 0; y < height / 2; y++) {
        const uint8_t *u = planes[1] + y * linesizes[1], *v = planes[2] + y * linesizes[2];
        uint8_t *dst = out_uv + (ptrdiff_t)y * pitch;
        if (ten)
            chroma10(u, v, dst, width / 2);
        else
            chroma8(u, v, dst, width / 2);
    }
}
