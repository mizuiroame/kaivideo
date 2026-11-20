#include "ui_library.h"

#include <stdio.h>

#include "font.h"
#include "gfx.h"
#include "thumbs.h"
#include "ui.h"
#include "ui_buttons.h"
#include "ui_library_shared.h"
#include "ui_style.h"

#define UP_NEXT_W 300          // The next episode's card, in the player.
#define UP_NEXT_PAD 12
#define UP_NEXT_PIC_H ((UP_NEXT_W - 2 * UP_NEXT_PAD) * 9 / 16)
#define UP_NEXT_TEXT_H 96

// --- The next episode, in the player's last seconds ---

void uiDrawUpNextCard(const Library *lib, int item, double fraction_left, int seconds_left, bool bar_shown)
{
    const LibraryItem *it = &lib->items[item];
    int h = UP_NEXT_PAD + UP_NEXT_PIC_H + UP_NEXT_TEXT_H;
    int x = GFX_WIDTH - MARGIN - UP_NEXT_W;
    int y = (bar_shown ? GFX_HEIGHT - UI_PLAYER_BAR_H - 76 : GFX_HEIGHT - 40) - h;
    gfxFillRoundRect(x - 2, y - 2, UP_NEXT_W + 4, h + 4, 16, COLOR_SURFACE_HI);  // An edge against the picture.
    gfxFillRoundRect(x, y, UP_NEXT_W, h, 14, COLOR_SURFACE);
    uiLibraryAddHit(x, y, UP_NEXT_W, h, LIBHIT_UP_NEXT, 0, 0);  // The whole card plays it.

    int px = x + UP_NEXT_PAD, py = y + UP_NEXT_PAD, pw = UP_NEXT_W - 2 * UP_NEXT_PAD;
    const uint32_t *picture = uiLibPicture(lib, (LibraryRef){false, item});
    if (picture) {
        uiLibDrawPicture(picture, px, py, pw, UP_NEXT_PIC_H, 8);
    } else {
        Color top, bottom;
        uiLibCardColors(lib->shows[it->show].name, &top, &bottom);
        gfxSetClipRound(px, py, pw, UP_NEXT_PIC_H, 8);
        gfxGradientV(px, py, pw, UP_NEXT_PIC_H, top, bottom);
        gfxResetClip();
    }
    float cx = px + pw / 2.0f, cy = py + UP_NEXT_PIC_H / 2.0f;
    gfxFillCircle((int)cx, (int)cy, 24, GFX_RGB(0, 0, 0));
    gfxTriangle(cx - 7, cy - 12, cx - 7, cy + 12, cx + 14, cy, COLOR_TEXT);
    // The time left, as a bar along the picture's bottom.
    int done = (int)(pw * (fraction_left < 0 ? 0 : fraction_left > 1 ? 1 : fraction_left));
    gfxFillRect(px, py + UP_NEXT_PIC_H - 4, pw, 4, GFX_RGB(0, 0, 0));
    gfxFillRect(px, py + UP_NEXT_PIC_H - 4, done, 4, COLOR_ACCENT);

    // Close: no next episode this time (it stays on the credits).
    float kx = x + UP_NEXT_W - 26.0f, ky = y + 26.0f;
    gfxFillCircle((int)kx, (int)ky, 15, GFX_RGB(0, 0, 0));
    gfxLine(kx - 5, ky - 5, kx + 5, ky + 5, 2, COLOR_TEXT);
    gfxLine(kx - 5, ky + 5, kx + 5, ky - 5, 2, COLOR_TEXT);
    uiLibraryAddHit((int)kx - 26, (int)ky - 26, 52, 52, LIBHIT_CLOSE, 0, 0);

    int ty = py + UP_NEXT_PIC_H + 12;
    char when[48];
    snprintf(when, sizeof(when), "UP NEXT  \xC2\xB7  %d s", seconds_left);
    fontDraw(FONT_BOLD, 14, px, ty, when, COLOR_ACCENT);
    char label[200];
    uiLibEpisodeLabel(it, label, sizeof(label));
    fontDrawClipped(FONT_BOLD, 19, px, ty + 22, pw, label, COLOR_TEXT);
    uiDrawButton(px, ty + 68.0f, "ZR", COLOR_SURFACE_HI, COLOR_TEXT);
    fontDrawMiddle(FONT_REGULAR, 17, px + uiButtonWidth("ZR") + 10, ty + 68.0f, "play now", COLOR_DIM);
}

// --- A video's details ---

#define DETAILS_W 1040
#define DETAILS_H 393
#define DETAILS_PAD 32
#define DETAILS_PIC_W 460
#define DETAILS_PIC_H 259
#define DETAILS_BUTTON_H 46
#define OVERVIEW_LINES 8
#define DETAILS_BUTTON_GAP 12
#define OVERVIEW_LINE_H 27

// "1998  ·  1 h 52 min", or "Season 2, episode 5  ·  22 min".
static void detailsLine(const LibraryItem *it, char *out, size_t size)
{
    char first[48] = "", length[32] = "";
    if (it->show >= 0)
        snprintf(first, sizeof(first), "Season %d, episode %d", it->name.season, it->name.episode);
    else if (it->name.year)
        snprintf(first, sizeof(first), "%d", it->name.year);
    int minutes = (int)(it->runtime_ns / 60000000000ll);
    if (minutes >= 60)
        snprintf(length, sizeof(length), "%d h %d min", minutes / 60, minutes % 60);
    else if (minutes > 0)
        snprintf(length, sizeof(length), "%d min", minutes);
    snprintf(out, size, "%s%s%s", first, first[0] && length[0] ? "  \xC2\xB7  " : "", length);
}

// A pill with a button's letter and its label. Returns its width.
static int detailsButton(int x, int y, const char *key, const char *label, LibHitKind hit)
{
    int w = uiButtonWidth(key) + fontTextWidth(FONT_BOLD, 19, label) + 38;
    gfxFillRoundRect(x, y, w, DETAILS_BUTTON_H, DETAILS_BUTTON_H / 2, COLOR_SURFACE_HI);
    uiDrawButton(x + 14, y + DETAILS_BUTTON_H / 2.0f, key, COLOR_SURFACE, COLOR_TEXT);
    fontDrawMiddle(FONT_BOLD, 19, x + 24 + uiButtonWidth(key), y + DETAILS_BUTTON_H / 2.0f, label, COLOR_TEXT);
    uiLibraryAddHit(x, y, w, DETAILS_BUTTON_H, hit, 0, 0);
    return w;
}

#define PREVIEW_W 256          // The picture above the bar while dragging it.
#define PREVIEW_ABOVE_BAR 62
#define PREVIEW_EDGE 3

void uiDrawSeekPreview(const uint32_t *pixels, int w, int h, double fraction)
{
    int box_h = PREVIEW_W * h / w;
    int track_w = GFX_WIDTH - 2 * MARGIN;
    int x = MARGIN + (int)(track_w * fraction) - PREVIEW_W / 2;
    x = x < MARGIN ? MARGIN : x > GFX_WIDTH - MARGIN - PREVIEW_W ? GFX_WIDTH - MARGIN - PREVIEW_W : x;
    int y = GFX_HEIGHT - UI_PLAYER_BAR_H - PREVIEW_ABOVE_BAR - box_h;
    gfxFillRoundRect(x - PREVIEW_EDGE, y - PREVIEW_EDGE, PREVIEW_W + 2 * PREVIEW_EDGE, box_h + 2 * PREVIEW_EDGE, 10,
                     COLOR_TEXT);
    uiLibDrawImage(pixels, w, h, x, y, PREVIEW_W, box_h, 8);
}

void uiDrawPreviewBehind(const uint32_t *pixels, int w, int h)
{
    int box_h = GFX_WIDTH * h / w;
    uiLibDrawImage(pixels, w, h, 0, (GFX_HEIGHT - box_h) / 2, GFX_WIDTH, box_h, 0);
}

#define SKIP_H 52
#define SKIP_ABOVE_BAR 70  // Its bottom, over the player's bar.
#define SKIP_ABOVE_EDGE 44

void uiDrawSkipButton(const char *label, bool bar_shown)
{
    int w = uiButtonWidth("ZR") + fontTextWidth(FONT_BOLD, 21, label) + 44;
    int x = GFX_WIDTH - MARGIN - w;
    int y = GFX_HEIGHT - (bar_shown ? UI_PLAYER_BAR_H + SKIP_ABOVE_BAR : SKIP_ABOVE_EDGE) - SKIP_H;
    gfxFillRoundRect(x - 2, y - 2, w + 4, SKIP_H + 4, SKIP_H / 2 + 2, COLOR_SURFACE_HI);  // An edge against the picture.
    gfxFillRoundRect(x, y, w, SKIP_H, SKIP_H / 2, COLOR_SURFACE);
    uiDrawButton(x + 16, y + SKIP_H / 2.0f, "ZR", COLOR_SURFACE_HI, COLOR_TEXT);
    fontDrawMiddle(FONT_BOLD, 21, x + 28 + uiButtonWidth("ZR"), y + SKIP_H / 2.0f, label, COLOR_TEXT);
    uiLibraryAddHit(x, y, w, SKIP_H, LIBHIT_SKIP, 0, 0);
}

// A series page's pill at the top right, ending at right. Returns its width.
static int showPill(int right, const char *key, const char *label, bool lit, LibHitKind hit)
{
    int w = uiButtonWidth(key) + fontTextWidth(FONT_BOLD, 18, label) + 34;
    int x = right - w, y = UI_TAB_BAR_H + 18;
    gfxFillRoundRect(x, y, w, 40, 20, lit ? COLOR_ACCENT : COLOR_SURFACE_HI);
    uiDrawButton(x + 12, y + 20.0f, key, COLOR_SURFACE, COLOR_TEXT);
    fontDrawMiddle(FONT_BOLD, 18, x + 22 + uiButtonWidth(key), y + 20.0f, label, lit ? COLOR_BG : COLOR_TEXT);
    uiLibraryAddHit(x, y, w, 40, hit, 0, 0);
    return w;
}

static const char *favoriteLabel(bool favorite)
{
    return favorite ? "In Favorites" : "Add to Favorites";
}

void uiDrawShowFavorite(bool favorite)
{
    showPill(GFX_WIDTH - MARGIN, "R", favoriteLabel(favorite), favorite, LIBHIT_FAVORITE);
}

void uiDrawShowDownload(const char *label, bool done, bool favorite_shown, bool favorite)
{
    int right = GFX_WIDTH - MARGIN;
    if (favorite_shown)
        right -= uiButtonWidth("R") + fontTextWidth(FONT_BOLD, 18, favoriteLabel(favorite)) + 34 + 12;
    showPill(right, "L", label, done, LIBHIT_SHOW_DOWNLOAD);
}

void uiDrawDetails(const Library *lib, int item, const ResumeList *resume, const char *note, bool favorites,
                   const char *download)
{
    const LibraryItem *it = &lib->items[item];
    int x = (GFX_WIDTH - DETAILS_W) / 2, y = (UI_FOOTER_Y + UI_TAB_BAR_H - DETAILS_H) / 2;
    gfxBlendRect(0, 0, GFX_WIDTH, GFX_HEIGHT, GFX_RGB(0, 0, 0), UI_DIM_ALPHA);
    uiLibraryAddHit(0, 0, GFX_WIDTH, GFX_HEIGHT, LIBHIT_CLOSE, 0, 0);
    gfxFillRoundRect(x, y, DETAILS_W, DETAILS_H, 16, COLOR_SURFACE);
    uiLibraryAddHit(x, y, DETAILS_W, DETAILS_H, LIBHIT_PANEL, 0, 0);

    int px = x + DETAILS_PAD, py = y + DETAILS_PAD;
    // The sharp picture, and the card's until it comes.
    const uint32_t *large = uiLibThumbnailsOn() ? thumbGetLarge(it->path, it->size) : NULL;
    const uint32_t *picture = large ? NULL : uiLibPicture(lib, (LibraryRef){false, item});
    if (large)
        uiLibDrawImage(large, THUMB_LARGE_W, THUMB_LARGE_H, px, py, DETAILS_PIC_W, DETAILS_PIC_H, 10);
    else if (picture)
        uiLibDrawPicture(picture, px, py, DETAILS_PIC_W, DETAILS_PIC_H, 10);
    else
        gfxFillRoundRect(px, py, DETAILS_PIC_W, DETAILS_PIC_H, 10, COLOR_SURFACE_HI);
    double progress = uiLibWatched(lib, resume, item);
    if (progress >= 0) {
        int done = (int)((DETAILS_PIC_W - 28) * (progress > 1 ? 1 : progress));
        gfxFillRoundRect(px + 14, py + DETAILS_PIC_H - 14, DETAILS_PIC_W - 28, 4, 2, GFX_RGB(0, 0, 0));
        gfxFillRoundRect(px + 14, py + DETAILS_PIC_H - 14, done > 4 ? done : 4, 4, 2, COLOR_ACCENT);
    }
    int tx = px + DETAILS_PIC_W + 28, tw = x + DETAILS_W - DETAILS_PAD - tx, ty = py;
    if (it->show >= 0) {
        fontDrawClipped(FONT_BOLD, 18, tx, ty, tw, lib->shows[it->show].name, COLOR_ACCENT);
        ty += 28;
    }
    int title_lines_top = ty;
    uiLibDrawWrapped(FONT_BOLD, 28, tx, title_lines_top, tw, 36, 2, it->name.title, COLOR_TEXT);
    ty += fontTextWidth(FONT_BOLD, 28, it->name.title) > tw ? 76 : 42;
    char line[96];
    detailsLine(it, line, sizeof(line));
    fontDrawClipped(FONT_REGULAR, 18, tx, ty, tw, line, COLOR_DIM);
    int text_top = ty + 36, bottom = y + DETAILS_H - DETAILS_PAD;
    if (note[0]) {
        fontDrawClipped(FONT_BOLD, 18, tx, text_top, tw, note, COLOR_ACCENT);
        text_top += 32;
    }
    int lines = (bottom - text_top) / OVERVIEW_LINE_H;
    uiLibDrawWrapped(FONT_REGULAR, 19, tx, text_top, tw, OVERVIEW_LINE_H, lines < OVERVIEW_LINES ? lines : OVERVIEW_LINES,
                    it->overview ? it->overview : "", COLOR_TEXT);

    int bx = px, by = py + DETAILS_PIC_H + 24;
    bx += detailsButton(bx, by, "A", progress >= 0 ? "Resume" : "Play", LIBHIT_DETAILS_PLAY) + DETAILS_BUTTON_GAP;
    if (progress >= 0)
        bx += detailsButton(bx, by, "Y", "From start", LIBHIT_DETAILS_START) + DETAILS_BUTTON_GAP;
    bx += detailsButton(bx, by, "X", uiLibSeen(lib, item) ? "Unwatched" : "Watched", LIBHIT_DETAILS_WATCHED) +
          DETAILS_BUTTON_GAP;
    if (favorites) {
        bool favorite = it->favorite;
        const char *label = it->show >= 0 ? (favorite ? "Series in Favorites" : "Favorite series")
                                          : (favorite ? "In Favorites" : "Favorite");
        bx += detailsButton(bx, by, "R", label, LIBHIT_FAVORITE) + DETAILS_BUTTON_GAP;
    }
    if (download)
        detailsButton(bx, by, "L", download, LIBHIT_DETAILS_DOWNLOAD);
}
