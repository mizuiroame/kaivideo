// Software decoding: which decoder a video goes to, how many threads it gets,
// when frames can be skipped, and the conversion to NV12.
#include <stdint.h>
#include <string.h>

#include "check.h"
#include "swframe.h"
#include "thumbgen.h"

static const int64_t MS = 1000000;

static void testRoute(void)
{
    CHECK(swframeRoute(SWFRAME_H264, 8, 1920, 1080) == SWFRAME_HARDWARE);
    CHECK(swframeRoute(SWFRAME_VP9, 8, 1920, 1080) == SWFRAME_HARDWARE);
    CHECK(swframeRoute(SWFRAME_AV1, 8, 1920, 1080) == SWFRAME_HARDWARE);
    CHECK(swframeRoute(SWFRAME_VP8, 8, 1920, 1080) == SWFRAME_HARDWARE);
    CHECK(swframeRoute(SWFRAME_H264, 0, 1920, 1080) == SWFRAME_HARDWARE);  // Depth unknown: as usual.

    CHECK(swframeRoute(SWFRAME_HEVC, 8, 1920, 1080) == SWFRAME_SOFTWARE);
    CHECK(swframeRoute(SWFRAME_HEVC, 10, 1920, 1080) == SWFRAME_SOFTWARE);
    CHECK(swframeRoute(SWFRAME_HEVC, 0, 1920, 1080) == SWFRAME_SOFTWARE);
    CHECK(swframeRoute(SWFRAME_H264, 10, 1920, 1080) == SWFRAME_SOFTWARE);

    // Over 1080p the console's decoder takes AV1 only (measured: H.264 and VP9 4K stall it).
    CHECK(swframeRoute(SWFRAME_AV1, 8, 3840, 2160) == SWFRAME_HARDWARE);
    CHECK(swframeRoute(SWFRAME_H264, 8, 3840, 2160) == SWFRAME_SOFTWARE);
    CHECK(swframeRoute(SWFRAME_VP9, 8, 3840, 2160) == SWFRAME_SOFTWARE);
    CHECK(swframeRoute(SWFRAME_H264, 8, 1920, 1088) == SWFRAME_HARDWARE);  // 1080p coded as 1088.
    CHECK(swframeRoute(SWFRAME_VP9, 8, 2560, 1440) == SWFRAME_SOFTWARE);
    CHECK(swframeRoute(SWFRAME_VP9, 8, 0, 0) == SWFRAME_HARDWARE);  // Size unknown: as usual.
    CHECK(swframeRoute(SWFRAME_VP9, 10, 1920, 1080) == SWFRAME_SOFTWARE);  // FFmpeg's VP9 takes 10 bits.

    CHECK(swframeRoute(SWFRAME_HEVC, 12, 1920, 1080) == SWFRAME_UNPLAYABLE);
    CHECK(swframeRoute(SWFRAME_AV1, 10, 1920, 1080) == SWFRAME_UNPLAYABLE);
    CHECK(swframeRoute(SWFRAME_VP9, 12, 1920, 1080) == SWFRAME_UNPLAYABLE);
    CHECK(swframeRoute(SWFRAME_OTHER, 8, 1920, 1080) == SWFRAME_UNPLAYABLE);
}

static void testThreads(void)
{
    CHECK(swframeThreads(6) == 6);
    CHECK(swframeThreads(2) == 2);
    CHECK(swframeThreads(16) == SWFRAME_MAX_THREADS);
    CHECK(swframeThreads(0) == 1);  // Unknown cores.
}

static void testSkipping(void)
{
    // Before a seek's target: not shown, so what nothing depends on is skipped.
    CHECK(swframeCanSkip(100 * MS, 200 * MS, SWFRAME_NO_CLOCK));
    CHECK(!swframeCanSkip(200 * MS, 200 * MS, SWFRAME_NO_CLOCK));
    // Far behind the clock: it would show late anyway.
    CHECK(swframeCanSkip(100 * MS, 0, 100 * MS + SWFRAME_LATE_NS + 1));
    CHECK(!swframeCanSkip(100 * MS, 0, 100 * MS + SWFRAME_LATE_NS));
    CHECK(!swframeCanSkip(500 * MS, 0, 100 * MS));
}

static void testConvert8(void)
{
    // 4x2 with padded rows, as FFmpeg's planes have.
    enum { PAD = 3 };
    uint8_t y[2][4 + PAD] = {{1, 2, 3, 4, 99, 99, 99}, {5, 6, 7, 8, 99, 99, 99}};
    uint8_t u[1][2 + PAD] = {{10, 20, 99, 99, 99}};
    uint8_t v[1][2 + PAD] = {{30, 40, 99, 99, 99}};
    const uint8_t *planes[3] = {y[0], u[0], v[0]};
    const ptrdiff_t lines[3] = {4 + PAD, 2 + PAD, 2 + PAD};
    uint8_t out_y[8], out_uv[4];
    swframeToNv12(planes, lines, 4, 2, 8, out_y, out_uv, 4);
    static const uint8_t WANT_Y[8] = {1, 2, 3, 4, 5, 6, 7, 8}, WANT_UV[4] = {10, 30, 20, 40};
    CHECK(memcmp(out_y, WANT_Y, 8) == 0);
    CHECK(memcmp(out_uv, WANT_UV, 4) == 0);
}

static void testConvert10(void)
{
    uint16_t y[2][2] = {{0, 1023}, {512, 4}}, u[1] = {1020}, v[1] = {8};
    const uint8_t *planes[3] = {(const uint8_t *)y[0], (const uint8_t *)u, (const uint8_t *)v};
    const ptrdiff_t lines[3] = {4, 2, 2};
    uint8_t out_y[4], out_uv[2];
    swframeToNv12(planes, lines, 2, 2, 10, out_y, out_uv, 2);
    static const uint8_t WANT_Y[4] = {0, 255, 128, 1}, WANT_UV[2] = {255, 2};
    CHECK(memcmp(out_y, WANT_Y, 4) == 0);
    CHECK(memcmp(out_uv, WANT_UV, 2) == 0);
}

// Rows long enough for the NEON steps on ARM (16 samples) plus a remainder,
// against the conversion done by hand. Same results with or without NEON.
static void testConvertWide(void)
{
    enum { W = 38, H = 4, PAD = 10, LINE = W + PAD, CLINE = W / 2 + PAD };
    static uint16_t y10[H][LINE], cb10[H / 2][CLINE], cr10[H / 2][CLINE];
    static uint8_t y8[H][LINE], cb8[H / 2][CLINE], cr8[H / 2][CLINE];
    for (int r = 0; r < H; r++)
        for (int x = 0; x < LINE; x++) {
            y10[r][x] = (uint16_t)((r * 251 + x * 37) % 1024);
            y8[r][x] = (uint8_t)(r * 13 + x * 7);
        }
    for (int r = 0; r < H / 2; r++)
        for (int x = 0; x < CLINE; x++) {
            cb10[r][x] = (uint16_t)((r * 97 + x * 53) % 1024);
            cr10[r][x] = (uint16_t)(1023 - cb10[r][x]);
            cb8[r][x] = (uint8_t)(x * 11 + r);
            cr8[r][x] = (uint8_t)(255 - x * 3 - r);
        }
    enum { PITCH = W + 2 };  // Wider than the picture, as the screen's buffer may be.
    uint8_t out_y[H * PITCH], out_uv[H / 2 * PITCH];

    const uint8_t *p10[3] = {(const uint8_t *)y10[0], (const uint8_t *)cb10[0], (const uint8_t *)cr10[0]};
    const ptrdiff_t l10[3] = {LINE * 2, CLINE * 2, CLINE * 2};
    swframeToNv12(p10, l10, W, H, 10, out_y, out_uv, PITCH);
    bool ok10 = true;
    for (int r = 0; r < H; r++)
        for (int x = 0; x < W; x++)
            ok10 &= out_y[r * PITCH + x] == (uint8_t)(y10[r][x] >> 2);
    for (int r = 0; r < H / 2; r++)
        for (int x = 0; x < W / 2; x++)
            ok10 &= out_uv[r * PITCH + x * 2] == (uint8_t)(cb10[r][x] >> 2) &&
                    out_uv[r * PITCH + x * 2 + 1] == (uint8_t)(cr10[r][x] >> 2);
    CHECK(ok10);

    const uint8_t *p8[3] = {y8[0], cb8[0], cr8[0]};
    const ptrdiff_t l8[3] = {LINE, CLINE, CLINE};
    swframeToNv12(p8, l8, W, H, 8, out_y, out_uv, PITCH);
    bool ok8 = true;
    for (int r = 0; r < H; r++)
        ok8 &= memcmp(out_y + r * PITCH, y8[r], W) == 0;
    for (int r = 0; r < H / 2; r++)
        for (int x = 0; x < W / 2; x++)
            ok8 &= out_uv[r * PITCH + x * 2] == cb8[r][x] && out_uv[r * PITCH + x * 2 + 1] == cr8[r][x];
    CHECK(ok8);
}

static void testThumbFiles(void)
{
    CHECK(thumbgenFileKind(0) == THUMB_FILE_RETRY);       // From a version that couldn't decode it.
    CHECK(thumbgenFileKind(THUMB_NONE_MARK_SIZE) == THUMB_FILE_NONE);
    CHECK(thumbgenFileKind(20000) == THUMB_FILE_PICTURE);
}

void testSwFrame(void)
{
    testRoute();
    testThreads();
    testSkipping();
    testConvert8();
    testConvert10();
    testConvertWide();
    testThumbFiles();
}
