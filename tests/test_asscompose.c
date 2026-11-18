// Styled subtitles: the box around the layers, and how they blend.
#include <stdint.h>
#include <string.h>

#include "asscompose.h"
#include "check.h"

static const uint32_t OPAQUE_RED = 0xFF0000FFu, OPAQUE_BLUE = 0xFFFF0000u;

static void testBox(void)
{
    AssBox box = {0, 0, 0, 0};
    box = assBoxAdd(box, 10, 20, 5, 6, 100, 100);
    CHECK(box.x == 10 && box.y == 20 && box.w == 5 && box.h == 6);
    box = assBoxAdd(box, 30, 10, 10, 2, 100, 100);
    CHECK(box.x == 10 && box.y == 10 && box.w == 30 && box.h == 16);
    // Outside the frame: only what's inside counts.
    box = assBoxAdd((AssBox){0, 0, 0, 0}, -5, 95, 10, 10, 100, 100);
    CHECK(box.x == 0 && box.y == 95 && box.w == 5 && box.h == 5);
    box = assBoxAdd((AssBox){0, 0, 0, 0}, 200, 200, 10, 10, 100, 100);
    CHECK(box.w == 0 || box.h == 0);
}

static void testOpaque(void)
{
    uint32_t pixels[4] = {0};
    const uint8_t mask[4] = {255, 0, 0, 255};
    AssLayer layer = {.x = 10, .y = 20, .w = 2, .h = 2, .stride = 2, .mask = mask, .rgba = 0xFF000000u};
    assComposeLayer(pixels, (AssBox){10, 20, 2, 2}, &layer);
    CHECK(pixels[0] == OPAQUE_RED);
    CHECK(pixels[1] == 0);
    CHECK(pixels[2] == 0);
    CHECK(pixels[3] == OPAQUE_RED);
}

static void testTransparency(void)
{
    // The layer's own transparency scales its coverage, and keeps its color.
    uint32_t pixel = 0;
    const uint8_t mask[1] = {255};
    AssLayer layer = {.w = 1, .h = 1, .stride = 1, .mask = mask, .rgba = 0xFF000080u};
    assComposeLayer(&pixel, (AssBox){0, 0, 1, 1}, &layer);
    CHECK(pixel >> 24 == 127);
    CHECK((pixel & 0xFFFFFF) == 0x0000FF);
}

static void testOver(void)
{
    // Half blue over opaque red: opaque, half way between.
    uint32_t pixel = OPAQUE_RED;
    const uint8_t mask[1] = {128};
    AssLayer layer = {.w = 1, .h = 1, .stride = 1, .mask = mask, .rgba = 0x0000FF00u};
    assComposeLayer(&pixel, (AssBox){0, 0, 1, 1}, &layer);
    CHECK(pixel >> 24 == 255);
    CHECK((pixel & 0xFF) == 127);
    CHECK(((pixel >> 16) & 0xFF) == 128);
    // Fully opaque covers what was there.
    const uint8_t full[1] = {255};
    layer.mask = full;
    assComposeLayer(&pixel, (AssBox){0, 0, 1, 1}, &layer);
    CHECK(pixel == OPAQUE_BLUE);
}

static void testClipped(void)
{
    // Only the part of the layer inside the box is drawn, at the right place.
    uint32_t pixels[2] = {0};
    const uint8_t mask[9] = {1, 2, 3, 255, 255, 6, 7, 8, 9};
    AssLayer layer = {.x = 0, .y = 0, .w = 3, .h = 3, .stride = 3, .mask = mask, .rgba = 0xFF000000u};
    assComposeLayer(pixels, (AssBox){0, 1, 2, 1}, &layer);
    CHECK(pixels[0] == OPAQUE_RED);
    CHECK(pixels[1] == OPAQUE_RED);
}

void testAssCompose(void)
{
    testBox();
    testOpaque();
    testTransparency();
    testOver();
    testClipped();
}
