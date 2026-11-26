// Thumbnails: a decoded NV12 frame to a small RGB picture.
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "thumbgen.h"

// An HDR frame's picture is brought down to SDR as the player's shader does
// (hdr.c's hdrPixel, the shader's maths in C), not shown washed out.
static void testThumbsHdr(void)
{
    enum { W = 32, H = 32 };
    static uint8_t y[W * H], uv[W * H / 2];
    uint8_t rgb[8 * 8 * 3], want[3];
    HdrLut lut;
    // PQ, 1000 nits: a muted red (as an SDR conversion shows it) and a gray at
    // 100 nits, each a flat frame, limited range.
    static const uint8_t PIXELS[][3] = {{120, 110, 170}, {128, 128, 128}, {60, 100, 150}, {200, 90, 200}, {16, 128, 128}};
    hdrLutInit(&lut, HDR_PQ, 1000);
    HdrParams p = hdrParams(HDR_PQ, 1000);
    for (size_t i = 0; i < sizeof(PIXELS) / sizeof(PIXELS[0]); i++) {
        memset(y, PIXELS[i][0], sizeof(y));
        for (int k = 0; k < W * H / 2; k += 2)
            uv[k] = PIXELS[i][1], uv[k + 1] = PIXELS[i][2];
        thumbgenConvert(y, uv, W, H, W, W, true, false, &lut, rgb, 8, 8);
        hdrPixelYuv8(&p, PIXELS[i][0], PIXELS[i][1], PIXELS[i][2], want);
        for (int c = 0; c < 3; c++)
            CHECK(abs(rgb[c] - want[c]) <= 1 && abs(rgb[8 * 8 * 3 - 3 + c] - want[c]) <= 1);
    }
    // The red: plainly red, more than the SDR conversion of the same frame makes it.
    memset(y, 120, sizeof(y));
    for (int k = 0; k < W * H / 2; k += 2)
        uv[k] = 110, uv[k + 1] = 170;
    thumbgenConvert(y, uv, W, H, W, W, true, false, &lut, rgb, 8, 8);
    uint8_t sdr[8 * 8 * 3];
    thumbgenConvert(y, uv, W, H, W, W, true, false, NULL, sdr, 8, 8);
    CHECK(rgb[0] - rgb[1] > sdr[0] - sdr[1] + 20);
    // HLG, full range: as hdrPixel from the same R'G'B'.
    HdrLut hlg;
    hdrLutInit(&hlg, HDR_HLG, HDR_DEFAULT_PEAK_NITS);
    memset(y, 190, sizeof(y));
    for (int k = 0; k < W * H / 2; k += 2)
        uv[k] = 100, uv[k + 1] = 160;
    thumbgenConvert(y, uv, W, H, W, W, true, true, &hlg, rgb, 8, 8);
    float in[3], out[3];
    hdrYuvToRgb(190 / 255.0f, (100 - 128) / 255.0f, (160 - 128) / 255.0f, in);
    HdrParams hp = hdrParams(HDR_HLG, HDR_DEFAULT_PEAK_NITS);
    hdrPixel(&hp, in, out);
    for (int c = 0; c < 3; c++)
        CHECK(abs(rgb[c] - (int)(out[c] * 255 + 0.5f)) <= 1);
}

void testThumbs(void)
{
    // A 64x32 frame: the left half black, the right half white (full range,
    // no color). Cropped to fill 16x16, the middle 32 columns are kept.
    enum { W = 64, H = 32 };
    static uint8_t y[W * H], uv[W * H / 2];
    for (int r = 0; r < H; r++)
        for (int c = 0; c < W; c++)
            y[r * W + c] = c < W / 2 ? 0 : 255;
    memset(uv, 128, sizeof(uv));
    uint8_t rgb[16 * 16 * 3];
    thumbgenConvert(y, uv, W, H, W, W, true, true, NULL, rgb, 16, 16);
    CHECK(rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0);                      // Left: black.
    CHECK(rgb[(16 - 1) * 3] == 255 && rgb[(16 - 1) * 3 + 2] == 255);        // Right: white.
    CHECK(thumbgenLuma(y, W, H, W) > 100 && thumbgenLuma(y, W, H, W) < 160);

    // Limited range: 16 is black, 235 white; red chroma makes it red.
    memset(y, 126, sizeof(y));
    for (int i = 0; i < W * H / 2; i += 2) {
        uv[i] = 90;       // Cb low.
        uv[i + 1] = 240;  // Cr high.
    }
    thumbgenConvert(y, uv, W, H, W, W, false, false, NULL, rgb, 16, 16);
    CHECK(rgb[0] > 200 && rgb[1] < 120 && rgb[2] < 120);

    testThumbsHdr();
}
