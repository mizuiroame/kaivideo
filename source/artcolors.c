#include "artcolors.h"

#include <string.h>

#define NEAR_IGNORE 40   // Summed channel distance under which a pixel is the color left out.
#define DARK_SUM 60      // r + g + b under this: too dark to tell a color,
#define LIGHT_MIN 235    // every channel over this: white.
#define VIVID 140        // Saturation: 128 keeps it, more spreads the channels from their gray.
#define SHADE 120        // Then each channel times SHADE / 255: dark enough for white text.
#define EMPTY_GRAY 0x2A  // What there is when no pixel has a color.

static unsigned ch(uint32_t c, int shift)
{
    return (c >> shift) & 0xFF;
}

static uint32_t rgb(unsigned r, unsigned g, unsigned b)
{
    return 0xFF000000u | (b << 16) | (g << 8) | r;
}

uint32_t artMix(uint32_t a, uint32_t b, unsigned k)
{
    if (k > 255)
        k = 255;
    unsigned r = (ch(a, 0) * (255 - k) + ch(b, 0) * k) / 255;
    unsigned g = (ch(a, 8) * (255 - k) + ch(b, 8) * k) / 255;
    unsigned bl = (ch(a, 16) * (255 - k) + ch(b, 16) * k) / 255;
    return rgb(r, g, bl);
}

static unsigned clamp255(int v)
{
    return v < 0 ? 0 : v > 255 ? 255 : (unsigned)v;
}

// More vivid, then darker.
static uint32_t tint(uint32_t c)
{
    int r = (int)ch(c, 0), g = (int)ch(c, 8), b = (int)ch(c, 16), gray = (r + g + b) / 3;
    r = gray + (r - gray) * VIVID / 128;
    g = gray + (g - gray) * VIVID / 128;
    b = gray + (b - gray) * VIVID / 128;
    return rgb(clamp255(r) * SHADE / 255, clamp255(g) * SHADE / 255, clamp255(b) * SHADE / 255);
}

static bool counts(uint32_t p, uint32_t ignore)
{
    unsigned r = ch(p, 0), g = ch(p, 8), b = ch(p, 16);
    if (ch(p, 24) < 128 || r + g + b < DARK_SUM || (r > LIGHT_MIN && g > LIGHT_MIN && b > LIGHT_MIN))
        return false;
    if (ignore == ART_NO_IGNORE)
        return true;
    int d = (int)r - (int)ch(ignore, 0), e = (int)g - (int)ch(ignore, 8), f = (int)b - (int)ch(ignore, 16);
    return (d < 0 ? -d : d) + (e < 0 ? -e : e) + (f < 0 ? -f : f) >= NEAR_IGNORE;
}

typedef struct {
    uint64_t r, g, b, n;
} Sum;

static uint32_t mean(const Sum *s)
{
    return rgb((unsigned)(s->r / s->n), (unsigned)(s->g / s->n), (unsigned)(s->b / s->n));
}

bool artColorsFrom(const uint32_t *pixels, int w, int h, uint32_t ignore, ArtColors *out)
{
    Sum cells[ART_GRID_H][ART_GRID_W], all = {0, 0, 0, 0};
    memset(cells, 0, sizeof(cells));
    for (int y = 0; pixels && y < h; y += 2)  // Every other pixel is plenty.
        for (int x = 0; x < w; x += 2) {
            uint32_t p = pixels[(size_t)y * w + x];
            if (!counts(p, ignore))
                continue;
            Sum *s = &cells[y * ART_GRID_H / h][x * ART_GRID_W / w];
            s->r += ch(p, 0), s->g += ch(p, 8), s->b += ch(p, 16), s->n++;
            all.r += ch(p, 0), all.g += ch(p, 8), all.b += ch(p, 16), all.n++;
        }
    if (all.n == 0) {
        for (int y = 0; y < ART_GRID_H; y++)
            for (int x = 0; x < ART_GRID_W; x++)
                out->cells[y][x] = rgb(EMPTY_GRAY, EMPTY_GRAY, EMPTY_GRAY);
        return false;
    }
    uint32_t whole = mean(&all);
    for (int y = 0; y < ART_GRID_H; y++)
        for (int x = 0; x < ART_GRID_W; x++) {
            // A cell with few colored pixels leans to the whole picture's color: no hard spots.
            const Sum *s = &cells[y][x];
            uint32_t c = s->n ? mean(s) : whole;
            unsigned weight = s->n >= 64 ? 255 : (unsigned)(s->n * 255 / 64);
            out->cells[y][x] = tint(artMix(whole, c, weight));
        }
    return true;
}

void artColorsOfTwo(uint32_t top, uint32_t bottom, ArtColors *out)
{
    for (int y = 0; y < ART_GRID_H; y++)
        for (int x = 0; x < ART_GRID_W; x++)
            out->cells[y][x] = tint(artMix(top, bottom, (unsigned)(y * 255 / (ART_GRID_H - 1))));
}

void artColorsMix(const ArtColors *a, const ArtColors *b, unsigned t, ArtColors *out)
{
    for (int y = 0; y < ART_GRID_H; y++)
        for (int x = 0; x < ART_GRID_W; x++)
            out->cells[y][x] = artMix(a->cells[y][x], b->cells[y][x], t);
}
