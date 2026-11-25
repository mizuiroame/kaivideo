// The colors of a picture (a channel's logo, a poster) for the Showcase's
// large picture when there's no wide one: averaged into a small grid that's
// drawn smoothly blended (a blur of it), darkened so text reads over it.
// Pure logic.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ART_GRID_W 6
#define ART_GRID_H 4
#define ART_NO_IGNORE 0u  // No color left out.

typedef struct {
    uint32_t cells[ART_GRID_H][ART_GRID_W];  // 0xFFBBGGRR, as gfx.h's Color.
} ArtColors;

// The grid of w x h pixels (0xAABBGGRR). Pixels close to ignore (the cards' color a logo sits on), or
// nearly black or white, are left out where there are others; a cell with none takes the picture's
// mean. Then made a little more vivid and dark. False (and out a dark gray) if nothing is left.
bool artColorsFrom(const uint32_t *pixels, int w, int h, uint32_t ignore, ArtColors *out);
// A grid from two colors (a name's card colors): top to bottom, then darkened as above.
void artColorsOfTwo(uint32_t top, uint32_t bottom, ArtColors *out);
// a and b mixed (t 0-255 of b), cell by cell.
void artColorsMix(const ArtColors *a, const ArtColors *b, unsigned t, ArtColors *out);
// One color mixed: k 0-255 of b.
uint32_t artMix(uint32_t a, uint32_t b, unsigned k);
