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
    CHECK(swframeRoute(SWFRAME_H264, 8) == SWFRAME_HARDWARE);
    CHECK(swframeRoute(SWFRAME_VP9, 8) == SWFRAME_HARDWARE);
    CHECK(swframeRoute(SWFRAME_AV1, 8) == SWFRAME_HARDWARE);
    CHECK(swframeRoute(SWFRAME_VP8, 8) == SWFRAME_HARDWARE);
    CHECK(swframeRoute(SWFRAME_H264, 0) == SWFRAME_HARDWARE);  // Depth unknown: as usual.

    CHECK(swframeRoute(SWFRAME_HEVC, 8) == SWFRAME_SOFTWARE);
    CHECK(swframeRoute(SWFRAME_HEVC, 10) == SWFRAME_SOFTWARE);
    CHECK(swframeRoute(SWFRAME_HEVC, 0) == SWFRAME_SOFTWARE);
    CHECK(swframeRoute(SWFRAME_H264, 10) == SWFRAME_SOFTWARE);

    CHECK(swframeRoute(SWFRAME_HEVC, 12) == SWFRAME_UNPLAYABLE);
    CHECK(swframeRoute(SWFRAME_AV1, 10) == SWFRAME_UNPLAYABLE);
    CHECK(swframeRoute(SWFRAME_VP9, 10) == SWFRAME_UNPLAYABLE);
    CHECK(swframeRoute(SWFRAME_OTHER, 8) == SWFRAME_UNPLAYABLE);
}

static void testThreads(void)
{
    CHECK(swframeThreads(1920, 1080, 23.976, 6) == 4);
    CHECK(swframeThreads(1920, 1080, 30, 6) == 4);
    CHECK(swframeThreads(1920, 1080, 60, 6) == 6);
    CHECK(swframeThreads(3840, 2160, 24, 6) == 6);
    CHECK(swframeThreads(1920, 1080, 24, 2) == 2);
    CHECK(swframeThreads(1920, 1080, 60, 16) == SWFRAME_MAX_THREADS);
    CHECK(swframeThreads(1920, 1080, 0, 6) == 4);   // Unknown frame rate.
    CHECK(swframeThreads(1920, 1080, 24, 0) == 1);  // Unknown cores.
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
    testThumbFiles();
}
