// HDR shown on an SDR screen: which streams are HDR, and the tone mapping.
#include <math.h>
#include <stdlib.h>

#include "check.h"
#include "hdr.h"

#define NEAR(a, b, tolerance) (fabsf((float)(a) - (float)(b)) <= (tolerance))

static void testDetect(void)
{
    enum { TRC_BT709 = 1, PRI_BT709 = 1, TRC_UNSPECIFIED = 2 };
    CHECK(hdrDetect(HDR_TRC_PQ, HDR_PRIMARIES_BT2020, true) == HDR_PQ);
    CHECK(hdrDetect(HDR_TRC_PQ, HDR_PRIMARIES_BT2020, false) == HDR_PQ);
    CHECK(hdrDetect(HDR_TRC_HLG, HDR_PRIMARIES_BT2020, false) == HDR_HLG);
    // The transfer isn't tagged, but HDR10's data is there: PQ.
    CHECK(hdrDetect(TRC_UNSPECIFIED, HDR_PRIMARIES_BT2020, true) == HDR_PQ);
    // BT.2020 alone isn't HDR (SDR in wide colors), nor is SDR with stray data.
    CHECK(hdrDetect(TRC_UNSPECIFIED, HDR_PRIMARIES_BT2020, false) == HDR_NONE);
    CHECK(hdrDetect(TRC_BT709, PRI_BT709, false) == HDR_NONE);
    CHECK(hdrDetect(TRC_BT709, PRI_BT709, true) == HDR_NONE);
}

static void testPeak(void)
{
    CHECK(hdrSourcePeak(HDR_PQ, 0, 0) == HDR_DEFAULT_PEAK_NITS);
    CHECK(hdrSourcePeak(HDR_PQ, 0, 4000) == 4000);   // The mastering display's.
    CHECK(hdrSourcePeak(HDR_PQ, 1500, 4000) == 1500);  // The content's own comes first.
    CHECK(hdrSourcePeak(HDR_PQ, 50, 0) == HDR_MIN_PEAK_NITS);  // Too low to be right.
    CHECK(hdrSourcePeak(HDR_PQ, 0, 20000) == HDR_MAX_PEAK_NITS);
    CHECK(hdrSourcePeak(HDR_HLG, 4000, 4000) == HDR_DEFAULT_PEAK_NITS);
}

static void testTransfers(void)
{
    // ST 2084's known points.
    CHECK(NEAR(hdrNitsToPq(100), 0.5081f, 0.001f));
    CHECK(NEAR(hdrNitsToPq(1000), 0.7518f, 0.001f));
    CHECK(NEAR(hdrNitsToPq(HDR_SDR_WHITE_NITS), 0.5806f, 0.001f));
    CHECK(hdrNitsToPq(0) < 1e-6f && NEAR(hdrNitsToPq(10000), 1.0f, 1e-5f));
    for (float nits = 0.5f; nits < 10000; nits *= 2)
        CHECK(NEAR(hdrPqToNits(hdrNitsToPq(nits)), nits, nits * 0.002f));
    CHECK(hdrPqToNits(-1) == 0 && NEAR(hdrPqToNits(2), 10000, 1));
    // HLG: half the signal is a twelfth of the light, all of it is all.
    CHECK(NEAR(hdrHlgToScene(0.5f), 1.0f / 12, 1e-5f));
    CHECK(NEAR(hdrHlgToScene(1.0f), 1.0f, 0.001f));
    CHECK(hdrHlgToScene(0) == 0);
}

static void testToneMap(void)
{
    HdrParams p = hdrParams(HDR_PQ, 1000);
    CHECK(p.knee > 0.6f && p.knee < 0.7f && p.max_lum < 1);
    // The dark and middle part is kept as mastered.
    for (float nits = 0.1f; nits < 90; nits *= 1.5f)
        CHECK(NEAR(hdrToneMap(&p, nits), nits, nits * 0.001f + 0.001f));
    // Brighter light is squeezed under the SDR white, smoothly and in order.
    float last = 0;
    for (float nits = 1; nits <= 1000; nits += 7) {
        float out = hdrToneMap(&p, nits);
        CHECK(out >= last && out <= HDR_SDR_WHITE_NITS);
        last = out;
    }
    CHECK(NEAR(hdrToneMap(&p, 1000), HDR_SDR_WHITE_NITS, 0.5f));
    CHECK(hdrToneMap(&p, 400) < hdrToneMap(&p, 900) - 2);  // Highlights still differ.
    CHECK(hdrToneMap(&p, 5000) == hdrToneMap(&p, 1000));  // Over the peak: as the peak.
    // A brighter master bends earlier.
    HdrParams bright = hdrParams(HDR_PQ, 4000);
    CHECK(bright.knee < p.knee && hdrToneMap(&bright, 400) < hdrToneMap(&p, 400));
    CHECK(NEAR(hdrToneMap(&bright, 4000), HDR_SDR_WHITE_NITS, 0.5f));
}

static void testPixels(void)
{
    HdrParams p = hdrParams(HDR_PQ, 1000);
    float out[3];
    hdrPixel(&p, (const float[3]){0, 0, 0}, out);
    CHECK(out[0] == 0 && out[1] == 0 && out[2] == 0);
    // Gray stays gray; 100 nits shows as 100 of the SDR white's 203.
    float gray = hdrNitsToPq(100);
    hdrPixel(&p, (const float[3]){gray, gray, gray}, out);
    CHECK(NEAR(out[0], out[1], 0.002f) && NEAR(out[1], out[2], 0.002f));
    CHECK(NEAR(out[0], powf(100.0f / 203.0f, 1 / 2.4f), 0.01f));
    // A highlight at the peak is the screen's white.
    float peak = hdrNitsToPq(1000);
    hdrPixel(&p, (const float[3]){peak, peak, peak}, out);
    CHECK(out[0] > 0.99f && out[1] > 0.99f && out[2] > 0.99f);
    // BT.709's red, written in BT.2020, is BT.709's red again.
    float r709[3] = {100 * 0.6274f, 100 * 0.0691f, 100 * 0.0164f};  // 100-nit red in BT.2020's terms.
    float red[3] = {hdrNitsToPq(r709[0]), hdrNitsToPq(r709[1]), hdrNitsToPq(r709[2])};
    hdrPixel(&p, red, out);
    CHECK(out[0] > 0.7f && out[1] < 0.03f && out[2] < 0.03f);
    // A bright orange keeps its hue: red over green as before (in light).
    float orange_nits[3] = {800, 400, 0};
    float orange[3] = {hdrNitsToPq(orange_nits[0]), hdrNitsToPq(orange_nits[1]), 0};
    hdrPixel(&p, orange, out);
    float red_light = powf(out[0], 2.4f), green_light = powf(out[1], 2.4f);
    CHECK(green_light > 0 && red_light / green_light > 2.5f);  // 1093 over 353 nits in BT.709.

    // From 8-bit limited-range Y'CbCr: black, and full PQ white.
    uint8_t rgb[3];
    hdrPixelYuv8(&p, 16, 128, 128, rgb);
    CHECK(rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0);
    hdrPixelYuv8(&p, 235, 128, 128, rgb);
    CHECK(rgb[0] == 255 && rgb[1] == 255 && rgb[2] == 255);

    // HLG: its reference white (75% signal) is close to the SDR white, a bit under it.
    HdrParams hlg = hdrParams(HDR_HLG, HDR_DEFAULT_PEAK_NITS);
    hdrPixel(&hlg, (const float[3]){0.75f, 0.75f, 0.75f}, out);
    CHECK(out[0] > 0.8f && out[0] < 1.0f && NEAR(out[0], out[2], 0.002f));
}

// The tables thumbnails use against hdrPixel (the shader's maths), over a
// grid of R'G'B' values: the same 8-bit level nearly always, one off now and
// then, and never more than a few (a channel BT.709 clips to black, just under it).
static void testLut(void)
{
    static const struct { HdrKind kind; float peak; } CASES[] = {
        {HDR_PQ, 1000}, {HDR_PQ, 4000}, {HDR_PQ, 400}, {HDR_PQ, 10000}, {HDR_HLG, HDR_DEFAULT_PEAK_NITS}};
    static HdrLut lut;
    for (size_t k = 0; k < sizeof(CASES) / sizeof(CASES[0]); k++) {
        hdrLutInit(&lut, CASES[k].kind, CASES[k].peak);
        HdrParams p = hdrParams(CASES[k].kind, CASES[k].peak);
        int worst = 0, off = 0, over_one = 0, count = 0;
        enum { STEPS = 60 };
        for (int r = 0; r <= STEPS; r++)
            for (int g = 0; g <= STEPS; g++)
                for (int b = 0; b <= STEPS; b++) {
                    float in[3] = {(float)r / STEPS, (float)g / STEPS, (float)b / STEPS}, want[3];
                    uint8_t got[3];
                    hdrPixel(&p, in, want);
                    hdrLutPixel(&lut, in, got);
                    for (int c = 0; c < 3; c++) {
                        int diff = abs(got[c] - (int)(want[c] * 255.0f + 0.5f));
                        worst = diff > worst ? diff : worst;
                        off += diff > 0;
                        over_one += diff > 1;
                        count++;
                    }
                }
        CHECK(worst <= 3 && off * 100 < count && over_one * 10000 < count);
    }
    // Black is black, and the peak's white is white.
    uint8_t out[3];
    hdrLutInit(&lut, HDR_PQ, 1000);
    hdrLutPixel(&lut, (const float[3]){0, 0, 0}, out);
    CHECK(out[0] == 0 && out[1] == 0 && out[2] == 0);
    float white = hdrNitsToPq(1000);
    hdrLutPixel(&lut, (const float[3]){white, white, white}, out);
    CHECK(out[0] == 255 && out[1] == 255 && out[2] == 255);
}

void testHdr(void)
{
    testLut();
    testDetect();
    testPeak();
    testTransfers();
    testToneMap();
    testPixels();
}
