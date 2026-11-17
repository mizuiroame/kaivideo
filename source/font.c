#include "font.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "font_bold_ttf.h"
#include "font_regular_ttf.h"
#include "font_system.h"
#include "gfx.h"
#include "stb_truetype.h"
#include "util.h"

#define CACHE_SLOTS 8192  // Docked, each glyph is kept at two sizes (layout and screen).
#define CACHE_MAX_USED (CACHE_SLOTS * 3 / 4)
#define ELLIPSIS "\xE2\x80\xA6"
#define MAX_FALLBACKS 6
#define MAX_FILES (FONT_FACE_COUNT + MAX_FALLBACKS)
#define BLOB_HEADER 8  // Some console font blobs start with an 8-byte header.

typedef struct {
    bool used;
    uint8_t face;
    uint16_t px;
    uint8_t file;  // Font file the glyph comes from (a fallback if needed).
    uint32_t cp;
    int glyph;     // Glyph index in that file.
    int w, h, xoff, yoff;
    float advance;
    uint8_t *bitmap;
} Glyph;

typedef struct {
    stbtt_fontinfo info;
    int ascent;       // In font units.
    int cap_height;   // Height of "H", in font units.
    bool ok;
} Face;

// The previous glyph as a value, not a cache entry: the cache may flush
// mid-string. Kerning only applies between glyphs of the same font file.
typedef struct {
    int file;
    int glyph;
} PrevGlyph;

#define NO_PREV ((PrevGlyph){-1, 0})

// Noto Sans Regular and Bold first (indexed by FontFace), then the console's
// fallback fonts for Japanese, Chinese and Korean.
static Face g_faces[MAX_FILES];
static int g_face_count;
static Glyph g_cache[CACHE_SLOTS];
static int g_cache_used;

static bool initFace(Face *f, const unsigned char *data)
{
    int offset = stbtt_GetFontOffsetForIndex(data, 0);
    f->ok = offset >= 0 && stbtt_InitFont(&f->info, data, offset) != 0;
    if (f->ok) {
        int descent, gap, x0, y0, x1, y1;
        stbtt_GetFontVMetrics(&f->info, &f->ascent, &descent, &gap);
        f->cap_height = stbtt_GetCodepointBox(&f->info, 'H', &x0, &y0, &x1, &y1) ? y1 : f->ascent * 7 / 10;
    }
    return f->ok;
}

// Adds the console fonts that parse as TrueType/OpenType; others are skipped.
static void addFallbacks(void)
{
    FontBlob blobs[MAX_FALLBACKS];
    int count = fontSystemLoad(blobs, MAX_FALLBACKS);
    for (int i = 0; i < count && g_face_count < MAX_FILES; i++) {
        Face *f = &g_faces[g_face_count];
        if (initFace(f, blobs[i].data) ||
            (blobs[i].size > BLOB_HEADER && initFace(f, blobs[i].data + BLOB_HEADER)))
            g_face_count++;
    }
}

bool fontInit(void)
{
    const unsigned char *data[FONT_FACE_COUNT] = {font_regular_ttf, font_bold_ttf};
    for (int i = 0; i < FONT_FACE_COUNT; i++) {
        if (!initFace(&g_faces[i], data[i]))
            return false;
    }
    g_face_count = FONT_FACE_COUNT;
    addFallbacks();
    return true;
}

int fontFallbackCount(void)
{
    return g_face_count > FONT_FACE_COUNT ? g_face_count - FONT_FACE_COUNT : 0;
}

static void flushCache(void)
{
    gfxForgetMasks();  // The bitmaps the GPU's atlas points to go.
    for (int i = 0; i < CACHE_SLOTS; i++)
        free(g_cache[i].bitmap);
    memset(g_cache, 0, sizeof(g_cache));
    g_cache_used = 0;
}

void fontExit(void)
{
    flushCache();
    fontSystemUnload();
    g_face_count = 0;
}

// Noto Sans is sized by its line height. Fallback fonts are scaled to the
// same em size instead, since their line heights differ: sized by line
// height, CJK text would look larger than the Latin text around it.
static float scaleFor(int file, int px)
{
    float noto = stbtt_ScaleForPixelHeight(&g_faces[FONT_REGULAR].info, (float)px);
    if (file < FONT_FACE_COUNT)
        return file == FONT_REGULAR ? noto : stbtt_ScaleForPixelHeight(&g_faces[file].info, (float)px);
    float em_pixels = noto / stbtt_ScaleForMappingEmToPixels(&g_faces[FONT_REGULAR].info, 1.0f);
    return stbtt_ScaleForMappingEmToPixels(&g_faces[file].info, em_pixels);
}

// Finds cp in the face, then in the fallbacks; '?' if no font has it.
static void findGlyph(Glyph *g, FontFace face, uint32_t cp)
{
    g->file = (uint8_t)face;
    g->glyph = stbtt_FindGlyphIndex(&g_faces[face].info, (int)cp);
    for (int i = FONT_FACE_COUNT; g->glyph == 0 && i < g_face_count; i++) {
        g->glyph = stbtt_FindGlyphIndex(&g_faces[i].info, (int)cp);
        if (g->glyph != 0)
            g->file = (uint8_t)i;
    }
    if (g->glyph == 0) {
        g->file = (uint8_t)face;
        g->glyph = stbtt_FindGlyphIndex(&g_faces[face].info, '?');
    }
}

bool fontCanDraw(FontFace face, const char *utf8)
{
    if (!g_faces[face].ok)
        return false;
    while (*utf8) {
        uint32_t cp = utilNextCodePoint(&utf8);
        bool found = stbtt_FindGlyphIndex(&g_faces[face].info, (int)cp) != 0;
        for (int i = FONT_FACE_COUNT; !found && i < g_face_count; i++)
            found = stbtt_FindGlyphIndex(&g_faces[i].info, (int)cp) != 0;
        if (!found)
            return false;
    }
    return true;
}

static void renderGlyph(Glyph *g, FontFace face, int px, uint32_t cp)
{
    findGlyph(g, face, cp);
    Face *f = &g_faces[g->file];
    float scale = scaleFor(g->file, px);
    int advance, lsb;
    stbtt_GetGlyphHMetrics(&f->info, g->glyph, &advance, &lsb);
    g->advance = advance * scale;
    g->bitmap = stbtt_GetGlyphBitmap(&f->info, scale, scale, g->glyph, &g->w, &g->h, &g->xoff, &g->yoff);
}

static Glyph *getGlyph(FontFace face, int px, uint32_t cp)
{
    if (g_cache_used >= CACHE_MAX_USED)
        flushCache();
    uint32_t slot = (cp * 2654435761u ^ (uint32_t)px * 40503u ^ (uint32_t)face) % CACHE_SLOTS;
    for (;;) {
        Glyph *g = &g_cache[slot];
        if (!g->used) {
            *g = (Glyph){.used = true, .face = (uint8_t)face, .px = (uint16_t)px, .cp = cp};
            renderGlyph(g, face, px, cp);
            g_cache_used++;
            return g;
        }
        if (g->cp == cp && g->px == px && g->face == face)
            return g;
        slot = (slot + 1) % CACHE_SLOTS;
    }
}

static float kerning(int px, PrevGlyph prev, const Glyph *next)
{
    if (prev.file != next->file)
        return 0.0f;
    return stbtt_GetGlyphKernAdvance(&g_faces[prev.file].info, prev.glyph, next->glyph) * scaleFor(prev.file, px);
}

// A layout text size in framebuffer pixels (rounded).
static int fbPx(int px)
{
    return (px * 3 + 1) / 2;
}

int fontTextWidth(FontFace face, int px, const char *utf8)
{
    if (!g_faces[face].ok)
        return 0;
    px = fbPx(px);
    float x = 0.0f;
    PrevGlyph prev = NO_PREV;
    while (*utf8) {
        Glyph *g = getGlyph(face, px, utilNextCodePoint(&utf8));
        x += kerning(px, prev, g) + g->advance;
        prev = (PrevGlyph){g->file, g->glyph};
    }
    return (int)ceilf(x / GFX_SCALE);
}

// Draws up to max_width framebuffer pixels of text from x on a baseline (both
// framebuffer pixels, px too); returns the width used. Every glyph,
// fallbacks included, sits on the face's baseline.
static float drawRunFb(FontFace face, int px, float x, int baseline, const char *utf8, float max_width, Color color)
{
    float start = x, detail = gfxDetail();
    int fine_px = detail > 1.0f ? (int)lroundf(px * detail) : px;
    if (fine_px > 255)
        fine_px = 255;
    PrevGlyph prev = NO_PREV;
    while (*utf8) {
        uint32_t cp = utilNextCodePoint(&utf8);
        Glyph *g = getGlyph(face, px, cp);
        float next_x = x + kerning(px, prev, g), advance = g->advance;
        if (next_x + advance - start > max_width)
            break;
        prev = (PrevGlyph){g->file, g->glyph};
        if (fine_px == px) {
            gfxBlendMask((int)lroundf(next_x) + g->xoff, baseline + g->yoff, g->w, g->h, g->bitmap, g->w, color);
        } else {  // The same glyph rasterized for the screen's resolution, in the same place.
            Glyph *f = getGlyph(face, fine_px, cp);
            float scale = (float)px / fine_px;
            gfxBlendMaskScaled(lroundf(next_x) + f->xoff * scale, baseline + f->yoff * scale, f->w * scale,
                               f->h * scale, f->bitmap, f->w, f->h, f->w, color);
        }
        x = next_x + advance;
    }
    return x - start;
}

// The same in layout pixels, with the line box starting at y.
static float drawRun(FontFace face, int px, float x, int y, const char *utf8, float max_width, Color color)
{
    px = fbPx(px);
    int baseline = gfxToFb(y) + (int)lroundf(g_faces[face].ascent * scaleFor(face, px));
    return drawRunFb(face, px, x * GFX_SCALE, baseline, utf8, max_width * GFX_SCALE, color) / GFX_SCALE;
}

// Baseline (framebuffer pixels) that puts the capitals' middle at cy (layout).
static int middleBaseline(FontFace face, int fb_px, float cy)
{
    return (int)lroundf(cy * GFX_SCALE + g_faces[face].cap_height * scaleFor(face, fb_px) / 2.0f);
}

int fontDrawMiddle(FontFace face, int px, int x, float cy, const char *utf8, Color color)
{
    if (!g_faces[face].ok)
        return 0;
    int fb_px = fbPx(px);
    float w = drawRunFb(face, fb_px, x * GFX_SCALE, middleBaseline(face, fb_px, cy), utf8, GFX_FB_WIDTH * 4.0f, color);
    return (int)ceilf(w / GFX_SCALE);
}

void fontDrawCentered(FontFace face, int px, float cx, float cy, const char *utf8, Color color)
{
    if (!g_faces[face].ok)
        return;
    int fb_px = fbPx(px);
    // The ink runs from the first glyph's left edge to the last one's right.
    float x = 0.0f, left = 0.0f, right = 0.0f;
    bool first = true;
    PrevGlyph prev = NO_PREV;
    for (const char *p = utf8; *p;) {
        Glyph *g = getGlyph(face, fb_px, utilNextCodePoint(&p));
        x += kerning(fb_px, prev, g);
        if (g->w > 0) {
            if (first)
                left = x + g->xoff;
            right = x + g->xoff + g->w;
            first = false;
        }
        x += g->advance;
        prev = (PrevGlyph){g->file, g->glyph};
    }
    float start = cx * GFX_SCALE - (left + right) / 2.0f;
    drawRunFb(face, fb_px, start, middleBaseline(face, fb_px, cy), utf8, GFX_FB_WIDTH * 4.0f, color);
}

int fontDraw(FontFace face, int px, int x, int y, const char *utf8, Color color)
{
    if (!g_faces[face].ok)
        return 0;
    return (int)ceilf(drawRun(face, px, (float)x, y, utf8, (float)GFX_WIDTH * 4, color));
}

void fontDrawClipped(FontFace face, int px, int x, int y, int max_width, const char *utf8, Color color)
{
    if (!g_faces[face].ok || max_width <= 0)
        return;
    if (fontTextWidth(face, px, utf8) <= max_width) {
        fontDraw(face, px, x, y, utf8, color);
        return;
    }
    float ellipsis = (float)fontTextWidth(face, px, ELLIPSIS);
    if (ellipsis > (float)max_width)
        return;
    float used = drawRun(face, px, (float)x, y, utf8, (float)max_width - ellipsis, color);
    drawRun(face, px, (float)x + used, y, ELLIPSIS, ellipsis + 1.0f, color);
}
