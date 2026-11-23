#include "ui_live.h"

#include <stdio.h>

#include "font.h"
#include "gfx.h"
#include "lang.h"
#include "ui_buttons.h"
#include "thumbs.h"
#include "ui_library.h"
#include "ui_library_shared.h"
#include "ui_style.h"

// The guide: times along the top, a row a channel, its programs across two
// hours, and what the cursor is on in a strip at the bottom.
#define TIMES_Y 84
#define ROWS_Y 116
#define LIVE_ROW_H LIBVIEW_LIVE_ROW_PX
#define CHANNEL_W 224          // The channel's number and name, on the left.
#define NUMBER_W 52
#define LOGO_W 72              // An IPTV channel's logo, 16:9.
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
    int name_x = MARGIN + NUMBER_W;
    const uint32_t *logo = c->logo ? thumbGet(c->logo, 0) : NULL;
    if (c->logo) {  // An IPTV channel: its logo (a dark card while it comes) where the number was.
        int lw = LOGO_W, lh = LOGO_W * THUMB_H / THUMB_W, ly = (int)cy - lh / 2;
        if (logo)
            uiLibDrawPicture(logo, MARGIN - 4, ly, lw, lh, 6);
        else
            gfxFillRoundRect(MARGIN - 4, ly, lw, lh, 6, COLOR_SURFACE);
        name_x = MARGIN - 4 + lw + 10;
    } else {
        fontDrawMiddle(FONT_BOLD, 20, MARGIN, cy, c->number[0] ? c->number : "-", COLOR_FAINT);
    }
    fontDrawClipped(FONT_BOLD, 19, name_x, (int)cy - 12, MARGIN + CHANNEL_W - 8 - name_x, c->name,
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
        fontDraw(FONT_REGULAR, 16, GRID_X + 12, y + LIVE_ROW_H / 2 - 10, T("No guide for these hours"), COLOR_FAINT);
        uiLibraryAddHit(GRID_X, y, GRID_W, LIVE_ROW_H, LIBHIT_LIVE_ROW, index, 0);
    }
}

// What the cursor is on: on now or later, its times, and its channel.
#define PILL_H 30

// R: the IPTV channel in the favorites, or out of them, at the strip's bottom right. Returns its width.
static int drawFavoritePill(int right, int y, bool favorite)
{
    const char *label = favorite ? T("In Favorites") : T("Add to Favorites");
    int w = uiButtonWidth("R") + fontTextWidth(FONT_BOLD, 16, label) + 32, x = right - w;
    gfxFillRoundRect(x, y, w, PILL_H, PILL_H / 2, favorite ? COLOR_ACCENT : COLOR_SURFACE_HI);
    uiDrawButton(x + 10, y + PILL_H / 2.0f, "R", COLOR_SURFACE, COLOR_TEXT);
    fontDrawMiddle(FONT_BOLD, 16, x + 20 + uiButtonWidth("R"), y + PILL_H / 2.0f, label,
                   favorite ? COLOR_BG : COLOR_TEXT);
    uiLibraryAddHit(x, y - 4, w, PILL_H + 8, LIBHIT_FAVORITE, 0, 0);
    return w;
}

static void drawStrip(const LiveChannel *c, int focus, const GuideTime *t, int favorite)
{
    int x = MARGIN - 12, w = GFX_WIDTH - 2 * x;
    gfxFillRoundRect(x, STRIP_Y, w, STRIP_H, 12, COLOR_SURFACE);
    int tx = x + 20, right = x + w - 20;
    int title_right = right;
    if (favorite >= 0)
        title_right -= drawFavoritePill(right, STRIP_Y + STRIP_H - PILL_H - 10, favorite == 1) + 16;
    int channel_w = fontTextWidth(FONT_REGULAR, 16, c->name);
    fontDraw(FONT_REGULAR, 16, right - channel_w, STRIP_Y + 14, c->name, COLOR_FAINT);
    if (focus < 0) {
        fontDraw(FONT_BOLD, 20, tx, STRIP_Y + 26, T("No guide for now"), COLOR_DIM);
        return;
    }
    const LiveProgram *p = &c->programs[focus];
    bool on_now = p->start <= t->now_s;
    const char *label = on_now ? T("ON NOW") : T("LATER");
    char when[48];
    liveTimeRange(p, t->offset, t->h24, when, sizeof(when));
    fontDraw(FONT_BOLD, 14, tx, STRIP_Y + 14, label, COLOR_ACCENT);
    fontDraw(FONT_REGULAR, 15, tx + fontTextWidth(FONT_BOLD, 14, label) + 10, STRIP_Y + 14, when, COLOR_FAINT);
    fontDrawClipped(FONT_BOLD, 22, tx, STRIP_Y + 38, title_right - tx, p->title, COLOR_TEXT);
}

// The IPTV group shown, as a pill over the channels' names: Y or a tap lists the groups.
static void drawGroupPill(const char *group)
{
    int x = MARGIN - 4, y = TIMES_Y - 2;
    uiDrawButton(x, y + 13.0f, "Y", COLOR_SURFACE_HI, COLOR_DIM);
    int bx = x + uiButtonWidth("Y") + 8, w = CHANNEL_W - (bx - MARGIN);
    gfxFillRoundRect(bx, y, w, 26, 13, COLOR_SURFACE_HI);
    fontDrawClipped(FONT_BOLD, 15, bx + 12, y + 4, w - 34, group, COLOR_TEXT);
    float cx = bx + w - 16.0f, cy = y + 13.0f;  // A chevron down: it opens a list.
    gfxLine(cx - 5, cy - 2, cx, cy + 3, 2.5f, COLOR_DIM);
    gfxLine(cx, cy + 3, cx + 5, cy - 2, 2.5f, COLOR_DIM);
    uiLibraryAddHit(x - 8, y - 8, CHANNEL_W + 12, 42, LIBHIT_LIVE_GROUPS, 0, 0);
}

void uiDrawLiveTab(const LibView *v, const LiveGuide *guide, int64_t now_s, int64_t utc_offset_s, bool clock_24h,
                   bool loading, const char *group, bool iptv, int favorite)
{
    if (group)
        drawGroupPill(group);
    if (guide->count == 0) {
        if (loading)
            drawCentered(T("Reading the guide..."), T("The channels come in a moment."));
        else if (iptv)
            drawCentered(T("No channels in this group"), T("Press Y to choose another group."));
        else
            drawCentered(T("No channels"), T("Live TV comes from your Jellyfin server, or an IPTV list (Settings > IPTV)."));
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
        drawStrip(c, liveProgramAt(c, at), &t, favorite);
    }
}

#define GROUP_ROW_H 52
#define GROUPS_Y (UI_TAB_BAR_H + 56)
#define GROUP_ROWS ((UI_FOOTER_Y - 8 - GROUPS_Y) / GROUP_ROW_H)

void uiDrawLiveGroups(const IptvList *list, int cursor, int chosen)
{
    int x = MARGIN - 12, w = GFX_WIDTH - 2 * x, top = UI_TAB_BAR_H + 8;
    gfxFillRoundRect(x, top, w, UI_FOOTER_Y - 8 - top, 14, COLOR_SURFACE);
    uiLibraryAddHit(x, top, w, UI_FOOTER_Y - 8 - top, LIBHIT_PANEL, 0, 0);
    // "< Groups": back to the guide.
    int bx = MARGIN + 4, by = top + 14;
    gfxLine(bx + 8, by + 4, bx, by + 13, 3, COLOR_TEXT);
    gfxLine(bx, by + 13, bx + 8, by + 22, 3, COLOR_TEXT);
    int tw = fontDraw(FONT_BOLD, 20, bx + 20, by + 2, T("Groups"), COLOR_TEXT);
    uiLibraryAddHit(x, top, tw + 60, 50, LIBHIT_LIVE_GROUPS, 0, 0);
    int rows = list->group_count + 1;
    int first = cursor - GROUP_ROWS / 2;
    first = first > rows - GROUP_ROWS ? rows - GROUP_ROWS : first;
    first = first < 0 ? 0 : first;
    for (int r = first; r < rows && r < first + GROUP_ROWS; r++) {
        int y = GROUPS_Y + (r - first) * GROUP_ROW_H;
        if (r == cursor) {
            gfxFillRoundRect(x + 12, y + 2, w - 24, GROUP_ROW_H - 4, 10, COLOR_SURFACE_HI);
            gfxFillRect(x + 12, y + 12, 3, GROUP_ROW_H - 24, COLOR_ACCENT);
        }
        char count[32];
        snprintf(count, sizeof(count), "%d", r == 0 ? list->count : list->groups[r - 1].count);
        const char *name = r == 0 ? T("All channels") : list->groups[r - 1].name;
        bool on = r == chosen;
        fontDrawClipped(on ? FONT_BOLD : FONT_REGULAR, 19, MARGIN + 16, y + 14, w - 200, name,
                        on ? COLOR_ACCENT : COLOR_TEXT);
        int cw = fontTextWidth(FONT_REGULAR, 16, count);
        fontDraw(FONT_REGULAR, 16, x + w - 40 - cw, y + 16, count, COLOR_FAINT);
        uiLibraryAddHit(x + 12, y, w - 24, GROUP_ROW_H, LIBHIT_LIVE_GROUP_ROW, r, 0);
    }
}
