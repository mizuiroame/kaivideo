#include "ui_live.h"

#include <stdio.h>

#include "font.h"
#include "gfx.h"
#include "ui_library.h"
#include "ui_style.h"

// The guide: times along the top, a row a channel, its programs across two
// hours, and what the cursor is on in a strip at the bottom.
#define TIMES_Y 84
#define ROWS_Y 116
#define LIVE_ROW_H LIBVIEW_LIVE_ROW_PX
#define CHANNEL_W 224          // The channel's number and name, on the left.
#define NUMBER_W 52
#define GRID_X (MARGIN + CHANNEL_W + 12)
#define GRID_W (GFX_WIDTH - MARGIN - GRID_X)
#define CELL_GAP 3
#define CELL_RADIUS 8
#define CELL_TEXT_MIN_W 36     // Narrower programs show no title.
#define CELL_TIME_MIN_W 120    // Nor their times.
#define PROGRESS_H 3
#define NOW_LINE_ALPHA 110
#define STRIP_Y (ROWS_Y + LIBVIEW_LIVE_ROWS * LIVE_ROW_H + 8)
#define STRIP_H (UI_FOOTER_Y - 8 - STRIP_Y)

typedef struct {
    int64_t now_s, from, offset;  // from: where the view starts.
    bool h24;
} GuideTime;

static void drawCentered(const char *title, const char *line)
{
    int cy = (UI_TAB_BAR_H + UI_FOOTER_Y) / 2;
    fontDrawCentered(FONT_BOLD, 26, GFX_WIDTH / 2.0f, cy - 18.0f, title, COLOR_TEXT);
    fontDrawCentered(FONT_REGULAR, 19, GFX_WIDTH / 2.0f, cy + 20.0f, line, COLOR_DIM);
}

static void drawTimes(const GuideTime *t, int rows)
{
    int columns = LIVE_GRID_SPAN_S / LIVE_GRID_STEP_S;
    for (int i = 0; i < columns; i++) {
        char clock[16];
        int x = GRID_X + i * GRID_W / columns;
        liveClock(t->from + (int64_t)i * LIVE_GRID_STEP_S, t->offset, t->h24, clock, sizeof(clock));
        gfxFillRect(x, TIMES_Y + 4, 1, 18, COLOR_TRACK);
        fontDraw(FONT_REGULAR, 15, x + 8, TIMES_Y + 4, clock, COLOR_FAINT);
        gfxFillRect(x, ROWS_Y, 1, rows * LIVE_ROW_H, COLOR_TRACK);
    }
}

// The line at the time now, over the rows.
static void drawNowLine(const GuideTime *t, int rows)
{
    if (t->now_s < t->from || t->now_s >= t->from + LIVE_GRID_SPAN_S)
        return;
    int x = GRID_X + (int)((t->now_s - t->from) * GRID_W / LIVE_GRID_SPAN_S);
    gfxFillCircle(x, ROWS_Y - 4, 4, COLOR_ACCENT);
    gfxBlendRect(x - 1, ROWS_Y - 4, 2, rows * LIVE_ROW_H + 4, COLOR_ACCENT, NOW_LINE_ALPHA);  // Titles show through.
}

static void drawChannel(const LiveChannel *c, int index, int y, bool focused)
{
    if (focused) {
        gfxFillRoundRect(MARGIN - 12, y + CELL_GAP, CHANNEL_W + 12, LIVE_ROW_H - 2 * CELL_GAP, CELL_RADIUS,
                         COLOR_SURFACE_HI);
        gfxFillRect(MARGIN - 12, y + 18, 3, LIVE_ROW_H - 36, COLOR_ACCENT);
    }
    float cy = y + LIVE_ROW_H / 2.0f;
    fontDrawMiddle(FONT_BOLD, 20, MARGIN, cy, c->number[0] ? c->number : "-", COLOR_FAINT);
    fontDrawClipped(FONT_BOLD, 19, MARGIN + NUMBER_W, (int)cy - 12, CHANNEL_W - NUMBER_W - 8, c->name,
                    focused ? COLOR_TEXT : COLOR_DIM);
    uiLibraryAddHit(0, y, GRID_X - CELL_GAP, LIVE_ROW_H, LIBHIT_LIVE_ROW, index, 0);
}

static void drawCell(const LiveProgram *p, int x, int y, int w, bool focused, const GuideTime *t)
{
    int cx = x + CELL_GAP, cy = y + CELL_GAP, cw = w - 2 * CELL_GAP, ch = LIVE_ROW_H - 2 * CELL_GAP;
    if (cw <= 0)
        return;
    if (focused) {
        gfxFillRoundRect(cx, cy, cw, ch, CELL_RADIUS, COLOR_ACCENT);
        gfxFillRoundRect(cx + 2, cy + 2, cw - 4, ch - 4, CELL_RADIUS - 2, COLOR_SURFACE_HI);
    } else {
        gfxFillRoundRect(cx, cy, cw, ch, CELL_RADIUS, COLOR_SURFACE);
    }
    bool on_now = p->start <= t->now_s && p->end > t->now_s;
    if (cw >= CELL_TEXT_MIN_W)
        fontDrawClipped(FONT_REGULAR, 17, cx + 12, cy + 12, cw - 24, p->title, focused ? COLOR_TEXT : COLOR_DIM);
    if (cw >= CELL_TIME_MIN_W) {
        char when[48];
        liveTimeRange(p, t->offset, t->h24, when, sizeof(when));
        fontDrawClipped(FONT_REGULAR, 14, cx + 12, cy + 38, cw - 24, when, COLOR_FAINT);
    }
    if (on_now && cw > 24) {  // How far into it: the whole program, not only the part in view.
        int bar_w = cw - 24, done = (int)(bar_w * liveProgress(p, t->now_s));
        gfxFillRoundRect(cx + 12, cy + ch - 10, bar_w, PROGRESS_H, 1, COLOR_TRACK);
        gfxFillRoundRect(cx + 12, cy + ch - 10, done > 3 ? done : 3, PROGRESS_H, 1, COLOR_ACCENT);
    }
}

static void drawPrograms(const LiveChannel *c, int index, int y, int focus, const GuideTime *t)
{
    bool any = false;
    for (int j = 0; j < c->program_count; j++) {
        int x, w;
        if (!liveGridSpan(&c->programs[j], t->from, GRID_X, GRID_W, &x, &w))
            continue;
        drawCell(&c->programs[j], x, y, w, j == focus, t);
        uiLibraryAddHit(x, y, w, LIVE_ROW_H, LIBHIT_LIVE_PROGRAM, index, j);
        any = true;
    }
    if (!any) {
        fontDraw(FONT_REGULAR, 16, GRID_X + 12, y + LIVE_ROW_H / 2 - 10, "No guide for these hours", COLOR_FAINT);
        uiLibraryAddHit(GRID_X, y, GRID_W, LIVE_ROW_H, LIBHIT_LIVE_ROW, index, 0);
    }
}

// What the cursor is on: on now or later, its times, and its channel.
static void drawStrip(const LiveChannel *c, int focus, const GuideTime *t)
{
    int x = MARGIN - 12, w = GFX_WIDTH - 2 * x;
    gfxFillRoundRect(x, STRIP_Y, w, STRIP_H, 12, COLOR_SURFACE);
    int tx = x + 20, right = x + w - 20;
    int channel_w = fontTextWidth(FONT_REGULAR, 16, c->name);
    fontDraw(FONT_REGULAR, 16, right - channel_w, STRIP_Y + 14, c->name, COLOR_FAINT);
    if (focus < 0) {
        fontDraw(FONT_BOLD, 20, tx, STRIP_Y + 26, "No guide for now", COLOR_DIM);
        return;
    }
    const LiveProgram *p = &c->programs[focus];
    bool on_now = p->start <= t->now_s;
    const char *label = on_now ? "ON NOW" : "LATER";
    char when[48];
    liveTimeRange(p, t->offset, t->h24, when, sizeof(when));
    fontDraw(FONT_BOLD, 14, tx, STRIP_Y + 14, label, COLOR_ACCENT);
    fontDraw(FONT_REGULAR, 15, tx + fontTextWidth(FONT_BOLD, 14, label) + 10, STRIP_Y + 14, when, COLOR_FAINT);
    fontDrawClipped(FONT_BOLD, 22, tx, STRIP_Y + 38, right - tx, p->title, COLOR_TEXT);
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
    int64_t at = v->live_at > now_s ? v->live_at : now_s;
    GuideTime t = {now_s, liveGridStart(now_s, v->live_at, utc_offset_s), utc_offset_s, clock_24h};
    int rows = guide->count - s->first < LIBVIEW_LIVE_ROWS ? guide->count - s->first : LIBVIEW_LIVE_ROWS;
    drawTimes(&t, rows);
    // Between rows, a finger left it: a row more, cut to the guide's place.
    int top = ROWS_Y, bottom = ROWS_Y + LIBVIEW_LIVE_ROWS * LIVE_ROW_H;
    gfxSetClip(0, top, GFX_WIDTH, bottom - top);
    uiLibraryClipHits(top, bottom);
    for (int i = s->first; i < guide->count && i <= s->first + rows; i++) {
        const LiveChannel *c = &guide->channels[i];
        int y = ROWS_Y + (i - s->first) * LIVE_ROW_H - s->offset;
        bool focused = i == s->cursor;
        drawChannel(c, i, y, focused);
        drawPrograms(c, i, y, focused ? liveProgramAt(c, at) : -1, &t);
    }
    uiLibraryClipHits(0, GFX_HEIGHT);
    gfxResetClip();
    drawNowLine(&t, rows);
    if (s->cursor >= 0 && s->cursor < guide->count) {
        const LiveChannel *c = &guide->channels[s->cursor];
        drawStrip(c, liveProgramAt(c, at), &t);
    }
}
