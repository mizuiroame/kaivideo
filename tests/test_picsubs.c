// Picture subtitles: keeping them, their times, and drawing them scaled.
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "picsubs.h"

#define S_NS 1000000000ll

static const uint32_t PALETTE[4] = {0x00000000, 0xFFFFFFFF, 0xFF000000, 0x80FF0000};  // Clear, white, black, red 50%.

// A w x h rectangle: a white middle in a black frame, on clear.
static PicRect frameRect(uint8_t *indices, int x, int y, int w, int h)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            indices[j * w + i] = (i == 0 || j == 0 || i == w - 1 || j == h - 1) ? 2 : 1;
    return (PicRect){x, y, w, h, indices, w, PALETTE, 4};
}

static void testTimes(void)
{
    PicSubs *s = picSubsCreate();
    uint8_t px[16 * 8];
    PicRect r = frameRect(px, 10, 100, 16, 8);
    AssArea area = {720, 480, 1.0, 0.0};
    AssPicture out;
    CHECK(picSubsAdd(s, 2 * S_NS, 4 * S_NS, 720, 480, &r, 1));
    CHECK(picSubsAdd(s, 10 * S_NS, -1, 720, 480, &r, 1));   // PGS: shown until the next picture.
    CHECK(picSubsAdd(s, 12 * S_NS, -1, 720, 480, NULL, 0));  // which ends it.
    CHECK(picSubsAdd(s, 2 * S_NS, 4 * S_NS, 720, 480, &r, 1));  // Read again after a seek: kept once.
    CHECK(picSubsCount(s) == 3 && picSubsBytes(s) > 0);
    CHECK(picSubsRender(s, 1 * S_NS, &area, &out) && out.w == 0);
    CHECK(picSubsRender(s, 3 * S_NS, &area, &out) && out.w == 16 && out.h == 8 && out.x == 10 && out.y == 100);
    unsigned first = out.id;
    CHECK(picSubsRender(s, 3 * S_NS + 1, &area, &out) && out.id == first);  // The same: not drawn again.
    CHECK(picSubsRender(s, 4 * S_NS, &area, &out) && out.w == 0);           // Its end.
    CHECK(picSubsRender(s, 11 * S_NS, &area, &out) && out.w == 16 && out.id != first);
    CHECK(picSubsRender(s, 12 * S_NS, &area, &out) && out.w == 0);
    CHECK(picSubsAdd(s, 100 * S_NS, -1, 720, 480, &r, 1));                    // Nothing after it:
    CHECK(picSubsRender(s, 100 * S_NS + PICSUBS_OPEN_NS - 1, &area, &out) && out.w == 16);
    CHECK(picSubsRender(s, 100 * S_NS + PICSUBS_OPEN_NS, &area, &out) && out.w == 0);  // not forever.
    CHECK(picSubsAdd(s, 50 * S_NS, 51 * S_NS, 720, 480, &r, 1));  // Out of order (a seek back): in its place.
    CHECK(picSubsRender(s, 50 * S_NS, &area, &out) && out.w == 16);
    // Nonsense is refused.
    PicRect outside = r;
    outside.x = 710;
    CHECK(!picSubsAdd(s, 70 * S_NS, -1, 720, 480, &outside, 1));
    CHECK(!picSubsAdd(s, 70 * S_NS, -1, 0, 480, &r, 1));
    CHECK(!picSubsAdd(s, 70 * S_NS, -1, 720, PICSUBS_CANVAS_MAX + 1, &r, 1));
    PicRect no_palette = r;
    no_palette.palette_count = 0;
    CHECK(!picSubsAdd(s, 70 * S_NS, -1, 720, 480, &no_palette, 1));
    picSubsFree(s);
    picSubsFree(NULL);
}

static void testDrawing(void)
{
    PicSubs *s = picSubsCreate();
    uint8_t px[20 * 10], px2[4 * 2];
    PicRect rects[2] = {frameRect(px, 100, 400, 20, 10), frameRect(px2, 300, 300, 4, 2)};
    CHECK(picSubsAdd(s, 0, 5 * S_NS, 720, 480, rects, 2));
    AssPicture out;
    AssArea same = {720, 480, 1.0, 0.0};
    CHECK(picSubsRender(s, S_NS, &same, &out));
    // Both rectangles in one image: from (100, 300) to (304, 410).
    CHECK(out.x == 100 && out.y == 300 && out.w == 204 && out.h == 110);
    CHECK(out.pixels[(400 - 300 + 5) * out.w + 5] == 0xFFFFFFFF);    // White, inside the first.
    CHECK(out.pixels[(400 - 300) * out.w + 0] == 0xFF000000);        // Its black frame.
    CHECK(out.pixels[50 * out.w + 50] >> 24 == 0);                    // Between them: clear.
    // On a 1440 x 960 box: twice the size, in the same place.
    AssArea twice = {1440, 960, 1.0, 0.0};
    CHECK(picSubsRender(s, S_NS, &twice, &out));
    CHECK(out.x == 200 && out.y == 600 && out.w == 408 && out.h == 220);
    CHECK(out.pixels[(800 - 600 + 10) * out.w + 10] == 0xFFFFFFFF);
    // The bar covers the bottom fifth: the picture (in the lower half) goes up above it.
    AssArea lifted = {720, 480, 1.0, 20.0};
    CHECK(picSubsRender(s, S_NS, &lifted, &out) && out.y + out.h <= 384 && out.h == 110);
    picSubsFree(s);

    // A half-transparent color stays so, without a dark edge when scaled.
    s = picSubsCreate();
    uint8_t red[4] = {3, 3, 3, 3};
    PicRect r = {0, 0, 2, 2, red, 2, PALETTE, 4};
    CHECK(picSubsAdd(s, 0, S_NS, 2, 2, &r, 1));
    AssArea big = {8, 8, 1.0, 0.0};
    CHECK(picSubsRender(s, 0, &big, &out) && out.w == 8);
    uint32_t mid = out.pixels[3 * 8 + 3];
    CHECK((mid >> 24) == 0x80 && (mid & 0xFF) == 0xFF && ((mid >> 8) & 0xFF) == 0);
    picSubsFree(s);
}

// A long film's pictures don't grow without end: the oldest go.
static void testLimit(void)
{
    PicSubs *s = picSubsCreate();
    int w = 1000, h = 200;
    uint8_t *noise = malloc((size_t)w * h);  // Runs of one: as big as run-length coding gets.
    for (int i = 0; i < w * h; i++)
        noise[i] = (uint8_t)(i % 2);
    PicRect r = {0, 0, w, h, noise, w, PALETTE, 4};
    int added = 0;
    while (picSubsBytes(s) <= PICSUBS_BYTES_MAX && added < 200)
        added += picSubsAdd(s, (int64_t)added * S_NS, -1, 1920, 1080, &r, 1);
    CHECK(picSubsBytes(s) <= PICSUBS_BYTES_MAX && picSubsCount(s) < added);
    AssPicture out;
    AssArea area = {1920, 1080, 1.0, 0.0};
    CHECK(picSubsRender(s, 0, &area, &out) && out.w == 0);  // The first went.
    free(noise);
    picSubsFree(s);
}

void testPicSubs(void)
{
    testTimes();
    testDrawing();
    testLimit();
}
