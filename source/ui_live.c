#include "ui_live.h"

#include <stdio.h>

#include "font.h"
#include "gfx.h"
#include "ui_library.h"
#include "ui_style.h"

#define LIVE_Y 84
#define LIVE_ROW_H 84
#define NUMBER_W 64
#define NEXT_W 380           // What's next, on the right.
#define PROGRESS_H 4

static void drawCentered(const char *title, const char *line)
{
    int cy = (UI_TAB_BAR_H + UI_FOOTER_Y) / 2;
    fontDrawCentered(FONT_BOLD, 26, GFX_WIDTH / 2.0f, cy - 18.0f, title, COLOR_TEXT);
    fontDrawCentered(FONT_REGULAR, 19, GFX_WIDTH / 2.0f, cy + 20.0f, line, COLOR_DIM);
}

static void drawRow(const LiveChannel *c, int y, bool focused, int64_t now_s, int64_t offset, bool h24)
{
    if (focused) {
        gfxFillRoundRect(MARGIN - 12, y + 2, GFX_WIDTH - 2 * (MARGIN - 12), LIVE_ROW_H - 4, 10, COLOR_SURFACE_HI);
        gfxFillRect(MARGIN - 12, y + 18, 3, LIVE_ROW_H - 36, COLOR_ACCENT);
    }
    int x = MARGIN, right = GFX_WIDTH - MARGIN;
    fontDrawMiddle(FONT_BOLD, 22, x, y + 30.0f, c->number[0] ? c->number : "-", COLOR_FAINT);
    x += NUMBER_W;
    int main_w = right - NEXT_W - 24 - x;
    fontDrawClipped(FONT_BOLD, 21, x, y + 12, main_w, c->name, COLOR_TEXT);
    char when[48];
    if (c->now.title[0]) {
        liveTimeRange(&c->now, offset, h24, when, sizeof(when));
        int when_w = fontTextWidth(FONT_REGULAR, 16, when);
        fontDrawClipped(FONT_REGULAR, 18, x, y + 42, main_w - when_w - 16, c->now.title, COLOR_DIM);
        fontDraw(FONT_REGULAR, 16, x + main_w - when_w, y + 44, when, COLOR_FAINT);
        double done = liveProgress(&c->now, now_s);
        gfxFillRoundRect(x, y + LIVE_ROW_H - 14, main_w, PROGRESS_H, 2, COLOR_TRACK);
        gfxFillRoundRect(x, y + LIVE_ROW_H - 14, (int)(main_w * done) > 4 ? (int)(main_w * done) : 4, PROGRESS_H, 2,
                         COLOR_ACCENT);
    } else {
        fontDraw(FONT_REGULAR, 18, x, y + 42, "No guide for now", COLOR_FAINT);
    }
    if (c->next.title[0]) {
        liveTimeRange(&c->next, offset, h24, when, sizeof(when));
        int nx = right - NEXT_W;
        fontDraw(FONT_BOLD, 14, nx, y + 16, "NEXT", COLOR_ACCENT);
        fontDraw(FONT_REGULAR, 15, nx + fontTextWidth(FONT_BOLD, 14, "NEXT") + 10, y + 16, when, COLOR_FAINT);
        fontDrawClipped(FONT_REGULAR, 18, nx, y + 42, NEXT_W, c->next.title, COLOR_DIM);
    }
}

void uiDrawLiveTab(const LibView *v, const LiveGuide *guide, int64_t now_s, int64_t utc_offset_s, bool clock_24h,
                   bool loading)
{
    if (guide->count == 0) {
        drawCentered(loading ? "Reading the guide..." : "No channels",
                     "Live TV comes from your Jellyfin server's tuners and guide.");
        return;
    }
    const Scroll *s = &v->live;
    for (int i = s->first; i < guide->count && i < s->first + LIBVIEW_LIVE_ROWS; i++) {
        int y = LIVE_Y + (i - s->first) * LIVE_ROW_H;
        drawRow(&guide->channels[i], y, i == s->cursor, now_s, utc_offset_s, clock_24h);
        uiLibraryAddHit(0, y, GFX_WIDTH, LIVE_ROW_H, LIBHIT_LIVE_ROW, i, 0);
    }
}
