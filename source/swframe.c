#include "swframe.h"

#include <string.h>

#define TEN_TO_EIGHT 2  // Bits dropped from a 10-bit sample.
#define HARDWARE_MAX_WIDTH 1920
#define HARDWARE_MAX_HEIGHT 1088  // 1080p, coded as 1088.

SwframeRoute swframeRoute(SwframeCodec codec, int bits, int width, int height)
{
    bool eight = bits == 0 || bits == 8;
    // Over 1080p the console's decoder stalls on H.264 and VP9 (it takes AV1).
    bool large = width > HARDWARE_MAX_WIDTH || height > HARDWARE_MAX_HEIGHT;
    switch (codec) {
    case SWFRAME_HEVC:
        return eight || bits == 10 ? SWFRAME_SOFTWARE : SWFRAME_UNPLAYABLE;
    case SWFRAME_H264:
    case SWFRAME_VP9:
        if (eight)
            return large ? SWFRAME_SOFTWARE : SWFRAME_HARDWARE;
        return bits == 10 ? SWFRAME_SOFTWARE : SWFRAME_UNPLAYABLE;
    case SWFRAME_AV1:
    case SWFRAME_VP8:
        return eight ? SWFRAME_HARDWARE : SWFRAME_UNPLAYABLE;
    default:
        return SWFRAME_UNPLAYABLE;
    }
}

int swframeThreads(int cores)
{
    int wanted = cores < SWFRAME_MAX_THREADS ? cores : SWFRAME_MAX_THREADS;
    return wanted > 0 ? wanted : 1;
}

bool swframeCanSkip(int64_t pts_ns, int64_t skip_until_ns, int64_t clock_ns)
{
    if (pts_ns < skip_until_ns)
        return true;
    return clock_ns != SWFRAME_NO_CLOCK && clock_ns > pts_ns &&
           (uint64_t)clock_ns - (uint64_t)pts_ns > (uint64_t)SWFRAME_LATE_NS;
}

bool swframeLightDeblock(SwframeKeepUp *k, SwframeFast mode, int width, int height, unsigned frames, unsigned late)
{
    bool large = width > HARDWARE_MAX_WIDTH || height > HARDWARE_MAX_HEIGHT;
    if (mode == SWFRAME_FAST_OFF || !large)
        return false;
    if (mode == SWFRAME_FAST_ALWAYS || k->light)
        return true;
    if (frames < k->frames || late < k->late) {  // Counted over again: a new start.
        k->frames = frames;
        k->late = late;
        return false;
    }
    if (frames - k->frames < SWFRAME_KEEPUP_FRAMES)
        return false;
    k->light = late - k->late >= SWFRAME_KEEPUP_LATE;
    k->frames = frames;
    k->late = late;
    return k->light;
}

// The conversion runs on the decoding thread, once a frame (about 18 ms a 4K
// frame in plain C on the console), so on ARM it takes 16 samples at a time
// with NEON. The plain loops do the rest of a row, and everything elsewhere
// (and everything with SWFRAME_NO_NEON, for measuring the difference).
#if defined(__aarch64__) && defined(__ARM_NEON) && !defined(SWFRAME_NO_NEON)
#include <arm_neon.h>
#define SWFRAME_NEON 1
#define NEON_STEP 16
#endif

static void luma8(const uint8_t *src, uint8_t *dst, int width)
{
    memcpy(dst, src, (size_t)width);
}

static void luma10(const uint8_t *src, uint8_t *restrict dst, int width)
{
    const uint16_t *restrict wide = (const uint16_t *)(const void *)src;
    int x = 0;
#ifdef SWFRAME_NEON
    for (; x + NEON_STEP <= width; x += NEON_STEP) {
        uint8x8_t low = vshrn_n_u16(vld1q_u16(wide + x), TEN_TO_EIGHT);
        uint8x8_t high = vshrn_n_u16(vld1q_u16(wide + x + 8), TEN_TO_EIGHT);
        vst1q_u8(dst + x, vcombine_u8(low, high));
    }
#endif
    for (; x < width; x++)
        dst[x] = (uint8_t)(wide[x] >> TEN_TO_EIGHT);
}

static void chroma8(const uint8_t *restrict u, const uint8_t *restrict v, uint8_t *restrict dst, int samples)
{
    int x = 0;
#ifdef SWFRAME_NEON
    for (; x + NEON_STEP <= samples; x += NEON_STEP) {
        uint8x16x2_t pair = {{vld1q_u8(u + x), vld1q_u8(v + x)}};
        vst2q_u8(dst + x * 2, pair);
    }
#endif
    for (; x < samples; x++) {
        dst[x * 2] = u[x];
        dst[x * 2 + 1] = v[x];
    }
}

static void chroma10(const uint8_t *u, const uint8_t *v, uint8_t *restrict dst, int samples)
{
    const uint16_t *restrict wide_u = (const uint16_t *)(const void *)u;
    const uint16_t *restrict wide_v = (const uint16_t *)(const void *)v;
    int x = 0;
#ifdef SWFRAME_NEON
    for (; x + NEON_STEP <= samples; x += NEON_STEP) {
        uint8x16x2_t pair = {{
            vcombine_u8(vshrn_n_u16(vld1q_u16(wide_u + x), TEN_TO_EIGHT),
                        vshrn_n_u16(vld1q_u16(wide_u + x + 8), TEN_TO_EIGHT)),
            vcombine_u8(vshrn_n_u16(vld1q_u16(wide_v + x), TEN_TO_EIGHT),
                        vshrn_n_u16(vld1q_u16(wide_v + x + 8), TEN_TO_EIGHT)),
        }};
        vst2q_u8(dst + x * 2, pair);
    }
#endif
    for (; x < samples; x++) {
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
