// Puts styled subtitle layers (a coverage mask and a color each) together into
// one image with its own alpha, for drawing over the video. Pure logic.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int x, y, w, h;  // Empty when w or h is 0.
} AssBox;

typedef struct {
    int x, y, w, h, stride;
    const uint8_t *mask;  // 0-255 coverage.
    uint32_t rgba;        // 0xRRGGBBAA as libass gives it: AA is transparency, 0 opaque.
} AssLayer;

// box grown to hold this rectangle, as far as it's inside a frame_w x frame_h frame.
AssBox assBoxAdd(AssBox box, int x, int y, int w, int h, int frame_w, int frame_h);

// Draws the layer over pixels: box.w x box.h, 0xAABBGGRR with straight alpha, at
// box's place in the frame. What falls outside box is left out.
void assComposeLayer(uint32_t *pixels, AssBox box, const AssLayer *layer);
