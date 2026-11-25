#include "ui_tiles.h"

#include <stdio.h>
#include <string.h>

#include "font.h"
#include "gfx.h"
#include "home.h"
#include "jellyfin.h"
#include "lang.h"
#include "thumbs.h"
#include "ui_library.h"
#include "ui_library_shared.h"
#include "ui_style.h"

#define CASE_R 5            // The case's corners,
#define SPINE_W 9           // its spine on the left (where a DVD case's hinge is),
#define COVER_INSET 2       // and the cover's edge inside it.
#define COVER_R 3
#define FADE_STEP 2         // Gradients under names: a band every this many pixels.
#define NAME_FADE_ALPHA 230
#define FOCUS_GAP 5         // The focus frame: this far out,
#define FOCUS_CUT 12        // its two corners cut this much (the seal's),
#define FOCUS_LINE 2.5f     // this thick.
#define SHADOW_ALPHA 110
#define SEEN_R 10
#define WIDE_R 8

// Tiles are drawn only between these (the rows' place under the large picture): their own round
// clips are cut to it, and drawing returns to it after them.
static int g_top = 0, g_bottom = GFX_HEIGHT;

void uiTilesSetRegion(int top, int bottom)
{
    g_top = top;
    g_bottom = bottom;
    gfxSetClip(0, top, GFX_WIDTH, bottom - top);
}

// A round clip cut to the region. False if nothing of it shows.
static bool clipTo(int x, int y, int w, int h, int radius)
{
    int y0 = y < g_top ? g_top : y, y1 = y + h > g_bottom ? g_bottom : y + h;
    if (y1 <= y0)
        return false;
    gfxSetClipRound(x, y0, w, y1 - y0, radius);
    return true;
}

static void unclip(void)
{
    gfxSetClip(0, g_top, GFX_WIDTH, g_bottom - g_top);
}

// Text only where it's whole in the region (text isn't clipped).
static bool textFits(int y, int h)
{
    return y >= g_top && y + h <= g_bottom;
}

// --- What a tile shows ---

static const LibraryItem *firstItem(const Library *lib, LibraryRef ref)
{
    if (!ref.is_show)
        return &lib->items[ref.index];
    const LibraryShow *s = &lib->shows[ref.index];
    return s->count ? &lib->items[s->episodes[0]] : NULL;
}

const char *uiTilePosterArt(const Library *lib, LibraryRef ref)
{
    const LibraryItem *it = firstItem(lib, ref);
    if (!it)
        return NULL;
    if (ref.is_show || it->show >= 0)  // A series, or an episode: its series' poster.
        return it->series_art;
    if (it->art)
        return it->art;
    return jfIsRemoteLocation(it->path) ? it->path : NULL;  // A server's film: its own poster.
}

const char *uiTileName(const Library *lib, LibraryRef ref)
{
    if (ref.is_show)
        return lib->shows[ref.index].name;
    const LibraryItem *it = &lib->items[ref.index];
    return it->show >= 0 ? lib->shows[it->show].name : it->name.title;
}

// --- Pictures ---

// An image of iw x ih filling the box (x, y, w, h), its middle kept, with round corners.
static void drawFilled(const uint32_t *pixels, int iw, int ih, int x, int y, int w, int h, int radius)
{
    float scale = (float)w / iw > (float)h / ih ? (float)w / iw : (float)h / ih;
    float dw = iw * scale, dh = ih * scale, dx = x + (w - dw) / 2, dy = y + (h - dh) / 2;
    if (!clipTo(x, y, w, h, radius))
        return;
    gfxDrawImageScaledFb(dx * GFX_SCALE, dy * GFX_SCALE, dw * GFX_SCALE, dh * GFX_SCALE, iw, ih, pixels);
    unclip();
}

// Darkens the bottom of a box from nothing at from_y down, so a name reads over it.
static void fadeBottom(int x, int from_y, int w, int bottom, int alpha)
{
    int h = bottom - from_y;
    for (int i = 0; i < h; i += FADE_STEP) {
        float t = (float)i / (float)h;
        gfxBlendRect(x, from_y + i, w, FADE_STEP, GFX_RGB(0, 0, 0), (unsigned)(alpha * t * t * (3 - 2 * t)));
    }
}

static void drawSeen(float cx, float cy)
{
    gfxFillCircle((int)cx, (int)cy, SEEN_R + 2, GFX_RGB(0, 0, 0));
    gfxFillCircle((int)cx, (int)cy, SEEN_R, COLOR_ACCENT);
    gfxLine(cx - 4.5f, cy + 0.5f, cx - 1.5f, cy + 3.5f, 2.2f, COLOR_BG);
    gfxLine(cx - 1.5f, cy + 3.5f, cx + 4.5f, cy - 3.0f, 2.2f, COLOR_BG);
}

static void drawProgress(int x, int y, int w, double progress)
{
    int done = (int)(w * (progress > 1 ? 1 : progress));
    gfxFillRoundRect(x, y, w, 4, 2, GFX_RGB(0, 0, 0));
    gfxFillRoundRect(x, y, done > 4 ? done : 4, 4, 2, COLOR_ACCENT);
}

// The video's frame (its middle) over the cover, or the name's colors, with the name at the bottom:
// a cover for what has no poster.
static void drawFrameCover(const Library *lib, LibraryRef ref, int x, int y, int w, int h)
{
    const uint32_t *frame = uiLibPicture(lib, ref);
    const char *name = uiTileName(lib, ref);
    if (frame) {
        drawFilled(frame, THUMB_W, THUMB_H, x, y, w, h, COVER_R);
    } else {
        Color top, bottom;
        uiLibCardColors(name, &top, &bottom);
        if (clipTo(x, y, w, h, COVER_R)) {
            gfxGradientV(x, y, w, h, top, bottom);
            unclip();
        }
    }
    if (clipTo(x, y, w, h, COVER_R)) {
        fadeBottom(x, y + h * 2 / 5, w, y + h, NAME_FADE_ALPHA);
        unclip();
    }
    gfxFillRect(x + 10, y + h - 74, 18, 2, COLOR_ACCENT);  // The brand's short line over the name.
    if (textFits(y + h - 66, 60))
        uiLibDrawWrapped(FONT_BOLD, 16, x + 10, y + h - 66, w - 18, 20, 3, name, COLOR_TEXT);
}

// --- Tiles ---

void uiDrawTileFocus(int x, int y, int w, int h)
{
    float x0 = x - FOCUS_GAP, y0 = y - FOCUS_GAP, x1 = x + w + FOCUS_GAP, y1 = y + h + FOCUS_GAP, c = FOCUS_CUT;
    float pts[6][2] = {{x0 + c, y0}, {x1, y0}, {x1, y1 - c}, {x1 - c, y1}, {x0, y1}, {x0, y0 + c}};
    for (int i = 0; i < 6; i++)
        gfxLine(pts[i][0], pts[i][1], pts[(i + 1) % 6][0], pts[(i + 1) % 6][1], FOCUS_LINE, COLOR_ACCENT);
}

static void drawShadow(int x, int y, int w, int h)
{
    gfxBlendRect(x + 3, y + 6, w, h, GFX_RGB(0, 0, 0), SHADOW_ALPHA / 2);
    gfxBlendRect(x + 1, y + 3, w, h, GFX_RGB(0, 0, 0), SHADOW_ALPHA / 2);
}

void uiDrawCaseTile(const Library *lib, LibraryRef ref, int x, int y, bool focused, double progress, bool seen)
{
    int w = TILE_CASE_W, h = TILE_CASE_H;
    if (focused)
        drawShadow(x, y, w, h);
    // The case: a dark shell, its spine a shade lighter with two ridges, then the cover in it.
    gfxFillRoundRect(x, y, w, h, CASE_R, GFX_RGB(0x0B, 0x0C, 0x0F));
    gfxFillRoundRect(x + 1, y + 1, SPINE_W + 2, h - 2, CASE_R - 1, COLOR_SURFACE_HI);
    gfxFillRect(x + 4, y + 8, 1, h - 16, COLOR_SURFACE);
    gfxFillRect(x + 7, y + 8, 1, h - 16, COLOR_SURFACE);
    int cx = x + SPINE_W + 1, cy = y + COVER_INSET, cw = w - SPINE_W - 1 - COVER_INSET, ch = h - 2 * COVER_INSET;
    const char *art = uiLibThumbnailsOn() ? uiTilePosterArt(lib, ref) : NULL;
    const uint32_t *poster = art ? thumbGetPoster(art) : NULL;
    if (poster)
        drawFilled(poster, THUMB_POSTER_W, THUMB_POSTER_H, cx, cy, cw, ch, COVER_R);
    else
        drawFrameCover(lib, ref, cx, cy, cw, ch);
    gfxFillRect(cx, cy, 1, ch, GFX_RGB(0x0B, 0x0C, 0x0F));  // The cover's edge against the spine.
    if (progress >= 0)
        drawProgress(cx + 8, y + h - 12, cw - 16, progress);
    if (seen && textFits(y, 32))
        drawSeen(x + w - 16.0f, y + 16.0f);
    if (focused)
        uiDrawTileFocus(x, y, w, h);
}

void uiDrawChannelTile(const IptvChannel *c, const char *on_now, double progress, int x, int y, bool focused)
{
    int w = TILE_WIDE_W, h = TILE_WIDE_H;
    if (focused)
        drawShadow(x, y, w, h);
    const uint32_t *logo = uiLibThumbnailsOn() && c->logo[0] ? thumbGet(c->logo, 0) : NULL;
    bool no_logo = !c->logo[0] || !uiLibThumbnailsOn() || thumbIsNone(c->logo, 0);
    if (logo) {
        drawFilled(logo, THUMB_W, THUMB_H, x, y, w, h, WIDE_R);
    } else if (no_logo) {
        Color top, bottom;
        uiLibCardColors(c->name, &top, &bottom);
        if (clipTo(x, y, w, h, WIDE_R)) {
            gfxGradientV(x, y, w, h, top, bottom);
            unclip();
        }
        if (textFits(y + 14, 84))
            uiLibDrawWrapped(FONT_BOLD, 20, x + 14, y + 14, w - 28, 25, 3, c->name, COLOR_TEXT);
    } else if (clipTo(x, y, w, h, WIDE_R)) {  // Its logo is on its way.
        gfxFillRect(x, y, w, h, COLOR_SURFACE);
        unclip();
    }
    if (progress >= 0 && textFits(y + h - 4, 4)) {  // A thin line along the bottom: how far into what's on.
        int done = (int)((w - 2 * WIDE_R) * (progress > 1 ? 1 : progress));
        gfxFillRect(x + WIDE_R, y + h - 4, w - 2 * WIDE_R, 2, GFX_RGB(0x3A, 0x3B, 0x44));
        gfxFillRect(x + WIDE_R, y + h - 4, done > 2 ? done : 2, 2, COLOR_ACCENT);
    }
    if (focused)
        uiDrawTileFocus(x, y, w, h);
    if (textFits(y + h + 8, 22))
        fontDrawClipped(FONT_BOLD, 17, x + 2, y + h + 8, w - 4, c->name, focused ? COLOR_TEXT : COLOR_DIM);
    if (on_now && on_now[0] && textFits(y + h + 30, 20))
        fontDrawClipped(FONT_REGULAR, 15, x + 2, y + h + 30, w - 4, on_now, COLOR_FAINT);
}

// Under a wide tile: a film's year, an episode's number and title, a series' seasons.
static void wideLine(const Library *lib, LibraryRef ref, char *out, size_t size)
{
    out[0] = '\0';
    if (ref.is_show) {
        const LibraryShow *s = &lib->shows[ref.index];
        if (s->seasons > 1)
            snprintf(out, size, T("%d seasons \xC2\xB7 %d episodes"), s->seasons, s->count);
        else
            snprintf(out, size, s->count == 1 ? T("%d episode") : T("%d episodes"), s->count);
        return;
    }
    const LibraryItem *it = &lib->items[ref.index];
    if (it->show >= 0)
        uiLibEpisodeLabel(it, out, size);
    else if (it->name.year)
        snprintf(out, size, "%d", it->name.year);
}

void uiDrawWideTile(const Library *lib, LibraryRef ref, int x, int y, bool focused, double progress, bool seen,
                    const char *tag)
{
    int w = TILE_WIDE_W, h = TILE_WIDE_H;
    if (focused)
        drawShadow(x, y, w, h);
    const uint32_t *picture = uiLibPicture(lib, ref);
    if (picture) {
        drawFilled(picture, THUMB_W, THUMB_H, x, y, w, h, WIDE_R);
    } else {
        Color top, bottom;
        uiLibCardColors(uiTileName(lib, ref), &top, &bottom);
        if (clipTo(x, y, w, h, WIDE_R)) {
            gfxGradientV(x, y, w, h, top, bottom);
            unclip();
        }
    }
    if ((tag || progress >= 0) && clipTo(x, y, w, h, WIDE_R)) {
        fadeBottom(x, y + h / 2, w, y + h, NAME_FADE_ALPHA * 3 / 4);
        unclip();
    }
    if (tag && textFits(y + h - 36, 18))
        fontDraw(FONT_BOLD, 13, x + 12, y + h - 36, tag, COLOR_TEXT);
    if (progress >= 0)
        drawProgress(x + 12, y + h - 14, w - 24, progress);
    if (seen && textFits(y, 36))
        drawSeen(x + w - 18.0f, y + 18.0f);
    if (focused)
        uiDrawTileFocus(x, y, w, h);
    char below[160];
    wideLine(lib, ref, below, sizeof(below));
    if (textFits(y + h + 8, 22))
        fontDrawClipped(FONT_BOLD, 17, x + 2, y + h + 8, w - 4, uiTileName(lib, ref), focused ? COLOR_TEXT : COLOR_DIM);
    if (textFits(y + h + 30, 20))
        fontDrawClipped(FONT_REGULAR, 15, x + 2, y + h + 30, w - 4, below, COLOR_FAINT);
}
