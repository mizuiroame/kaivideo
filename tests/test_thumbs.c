// Thumbnails: a decoded NV12 frame to a small RGB picture.
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "thumbgen.h"

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
    thumbgenConvert(y, uv, W, H, W, W, true, true, rgb, 16, 16);
    CHECK(rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0);                      // Left: black.
    CHECK(rgb[(16 - 1) * 3] == 255 && rgb[(16 - 1) * 3 + 2] == 255);        // Right: white.
    CHECK(thumbgenLuma(y, W, H, W) > 100 && thumbgenLuma(y, W, H, W) < 160);

    // Limited range: 16 is black, 235 white; red chroma makes it red.
    memset(y, 126, sizeof(y));
    for (int i = 0; i < W * H / 2; i += 2) {
        uv[i] = 90;       // Cb low.
        uv[i + 1] = 240;  // Cr high.
    }
    thumbgenConvert(y, uv, W, H, W, W, false, false, rgb, 16, 16);
    CHECK(rgb[0] > 200 && rgb[1] < 120 && rgb[2] < 120);
}
