#include "ui_library.h"

#include <stdio.h>
#include <string.h>

#include "background.h"
#include "font.h"
#include "gfx.h"
#include "thumbs.h"
#include "ui_buttons.h"
#include "ui_style.h"
#include "util.h"

#define TAB_GAP 28             // The header, as KaiPlayer's and KaiRadio's.
#define TAB_TEXT_MIDDLE 28.0f
#define HINT_GAP 9
#define BATTERY_LOW 15
#define COLOR_CHARGING GFX_RGB(0x5B, 0xD6, 0x7A)
#define MENU_PANEL_W 640       // The + menu.
#define MENU_PANEL_ROW_H 48
#define MENU_PANEL_PAD 32
#define GAP 16
#define HOME_Y 76              // Home's first row; three fit above the help line.
#define HOME_TITLE_H 34
#define HOME_CARD_W 206
#define HOME_CARD_H 116
#define HOME_ROW_H 196
#define GRID_Y 84
#define GRID_CARD_W ((GFX_WIDTH - 2 * MARGIN - (LIBVIEW_COLS - 1) * GAP) / LIBVIEW_COLS)
#define GRID_CARD_H 128
#define GRID_ROW_H 186
#define LIST_ROW_H 58
#define SHOW_LIST_Y 196        // A series' episodes.
#define SEARCH_BOX_Y 84
#define SEARCH_BOX_H 56
#define SEARCH_LIST_Y 160
#define MAX_HITS 96
#define SERIES_PICTURE_TRIES 8  // Episodes tried for a series' picture.

// --- Touch targets, kept as they're drawn ---

typedef struct {
    int x, y, w, h;
    LibHit hit;
} HitRegion;

static HitRegion g_hits[MAX_HITS];
static int g_hit_count;

void uiLibraryAddHit(int x, int y, int w, int h, LibHitKind kind, int a, int b)
{
    if (g_hit_count < MAX_HITS)
        g_hits[g_hit_count++] = (HitRegion){x, y, w, h, {kind, a, b}};
}

LibHit uiLibraryTap(int x, int y)
{
    for (int i = g_hit_count - 1; i >= 0; i--) {  // The last drawn is on top.
        const HitRegion *r = &g_hits[i];
        if (x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h)
            return r->hit;
    }
    return (LibHit){LIBHIT_NONE, 0, 0};
}

// --- Pieces ---

static bool g_thumbnails = true;

void uiLibrarySetThumbnails(bool on)
{
    g_thumbnails = on;
}

void uiDrawBackground(void)
{
    const uint32_t *pixels = backgroundPixels();
    if (pixels)
        gfxCopyFrame(pixels);
    else
        gfxClear(COLOR_BG);
}

// The picture for a film or episode (item), or a series (its first episode).
static const uint32_t *pictureOf(const Library *lib, LibraryRef ref)
{
    if (!g_thumbnails)
        return NULL;
    if (!ref.is_show)
        return thumbGet(lib->items[ref.index].path, lib->items[ref.index].size);
    // A series: its first episode that has one (an HEVC one has none: the next).
    const LibraryShow *show = &lib->shows[ref.index];
    for (int i = 0; i < show->count && i < SERIES_PICTURE_TRIES; i++) {
        const LibraryItem *item = &lib->items[show->episodes[i]];
        const uint32_t *pixels = thumbGet(item->path, item->size);
        if (pixels || !thumbIsNone(item->path, item->size))
            return pixels;  // Its picture, or NULL while it's made.
    }
    return NULL;
}

// Draws a picture into a layout box with round corners.
static void drawPicture(const uint32_t *pixels, int x, int y, int w, int h, int radius)
{
    gfxSetClipRound(x, y, w, h, radius);
    gfxDrawImageScaledFb((float)gfxToFb(x), (float)gfxToFb(y), (float)gfxToFb(w), (float)gfxToFb(h), THUMB_W, THUMB_H,
                         pixels);
    gfxResetClip();
}

static const char *const TAB_NAMES[TAB_COUNT] = {"Home", "Films", "Series", "Search", "Files"};

static SysInfo g_sys;
static bool g_clock_24h;

void uiLibrarySetStatus(const SysInfo *sys, bool clock_24h)
{
    g_sys = *sys;
    g_clock_24h = clock_24h;
}

void uiLibraryResetHits(void)
{
    g_hit_count = 0;
}

// The tabs in the middle, between the ZL and ZR that change them.
static void drawTabs(LibTab active)
{
    int widths[TAB_COUNT], total = 0;
    for (int i = 0; i < TAB_COUNT; i++) {
        widths[i] = fontTextWidth((int)active == i ? FONT_BOLD : FONT_REGULAR, 20, TAB_NAMES[i]);
        total += widths[i] + (i > 0 ? TAB_GAP : 0);
    }
    int x = (GFX_WIDTH - total) / 2;
    uiDrawButton(x - HINT_GAP - uiButtonWidth("ZL"), TAB_TEXT_MIDDLE, "ZL", COLOR_SURFACE_HI, COLOR_DIM);
    for (int i = 0; i < TAB_COUNT; i++) {
        bool on = (int)active == i;
        fontDraw(on ? FONT_BOLD : FONT_REGULAR, 20, x, 18, TAB_NAMES[i], on ? COLOR_TEXT : COLOR_DIM);
        uiLibraryAddHit(x - TAB_GAP / 2, 4, widths[i] + TAB_GAP, UI_TAB_BAR_H - 8, LIBHIT_TAB, i, 0);
        if (on)
            gfxFillRoundRect(x, 48, widths[i], 3, 1, COLOR_ACCENT);
        x += widths[i] + TAB_GAP;
    }
    uiDrawButton(x - TAB_GAP + HINT_GAP, TAB_TEXT_MIDDLE, "ZR", COLOR_SURFACE_HI, COLOR_DIM);
}

static void drawBattery(int x, int y, int percent, bool charging)
{
    Color fill = charging ? COLOR_CHARGING : percent <= BATTERY_LOW ? COLOR_ERROR : COLOR_TEXT;
    gfxFillRoundRect(x, y, 30, 16, 4, COLOR_DIM);
    gfxFillRoundRect(x + 2, y + 2, 26, 12, 3, COLOR_BG);
    gfxFillRoundRect(x + 31, y + 5, 3, 6, 1, COLOR_DIM);
    int inner = 22 * (percent < 0 ? 0 : percent > 100 ? 100 : percent) / 100;
    if (inner > 0)
        gfxFillRoundRect(x + 4, y + 4, inner, 8, 2, fill);
    if (charging) {  // A lightning bolt over the battery.
        float cx = (float)x + 15.0f, cy = (float)y + 8.0f;
        // Two halves that overlap in the middle, so no seam shows between them.
        gfxTriangle(cx + 2.0f, cy - 8.0f, cx - 5.0f, cy + 1.5f, cx + 1.5f, cy + 1.5f, COLOR_BG);
        gfxTriangle(cx - 2.0f, cy + 8.0f, cx + 5.0f, cy - 1.5f, cx - 1.5f, cy - 1.5f, COLOR_BG);
        gfxTriangle(cx + 1.5f, cy - 6.0f, cx - 3.5f, cy + 1.0f, cx + 1.0f, cy + 1.0f, COLOR_TEXT);
        gfxTriangle(cx - 1.5f, cy + 6.0f, cx + 3.5f, cy - 1.0f, cx - 1.0f, cy - 1.0f, COLOR_TEXT);
    }
}

// The top right: a scan's progress, the time and the battery.
static void drawStatus(const char *status)
{
    int x = GFX_WIDTH - MARGIN;
    if (g_sys.has_battery) {
        char percent[8];
        snprintf(percent, sizeof(percent), "%d%%", g_sys.battery_percent);
        x -= 34;
        drawBattery(x, 20, g_sys.battery_percent, g_sys.charging);
        x -= fontTextWidth(FONT_REGULAR, 17, percent) + 8;
        fontDraw(FONT_REGULAR, 17, x, 19, percent, g_sys.charging ? COLOR_CHARGING : COLOR_DIM);
        x -= 20;
    }
    if (g_sys.has_time) {
        char clock[16];
        utilFormatClock(clock, sizeof(clock), g_sys.hour, g_sys.minute, g_clock_24h);
        x -= fontTextWidth(FONT_BOLD, 18, clock);
        fontDraw(FONT_BOLD, 18, x, 18, clock, COLOR_TEXT);
        x -= 24;
    }
    if (status && status[0]) {
        x -= fontTextWidth(FONT_BOLD, 16, status);
        fontDraw(FONT_BOLD, 16, x, 20, status, COLOR_ACCENT);
    }
}

// Three bars before the name: the touch way into the + menu.
static void drawMenuButton(void)
{
    for (int i = 0; i < 3; i++)
        gfxFillRoundRect(MARGIN, 22 + i * 7, 22, 3, 1, COLOR_ACCENT);
}

void uiDrawTabBar(LibTab active, const char *status)
{
    drawMenuButton();
    int title_w = fontDraw(FONT_BOLD, 26, MARGIN + 36, 16, "KaiVideo", COLOR_ACCENT);
    uiLibraryAddHit(0, 0, MARGIN + 36 + title_w + 12, UI_TAB_BAR_H, LIBHIT_MENU, 0, 0);
    drawTabs(active);
    drawStatus(status);
}

void uiDrawFooterStrip(void)
{
    if (!backgroundPixels())
        gfxFillRect(0, UI_FOOTER_Y, GFX_WIDTH, GFX_HEIGHT - UI_FOOTER_Y, COLOR_BG);
}

void uiDrawFooterHelp(const char *help)
{
    uiDrawHelp(MARGIN, UI_FOOTER_Y + 28.0f, GFX_WIDTH - 2 * MARGIN, help);
}

// --- The + menu ---

static void drawMenuRow(int x, int row_y, int i, int cursor, const char *label)
{
    bool selected = i == cursor;
    if (selected) {
        gfxFillRoundRect(x + 16, row_y, MENU_PANEL_W - 32, MENU_PANEL_ROW_H - 6, 10, COLOR_SURFACE_HI);
        gfxFillRect(x + 16, row_y + 10, 3, MENU_PANEL_ROW_H - 26, COLOR_ACCENT);
    }
    fontDrawClipped(selected ? FONT_BOLD : FONT_REGULAR, 21, x + MENU_PANEL_PAD, row_y + 9,
                    MENU_PANEL_W - 2 * MENU_PANEL_PAD, label, COLOR_TEXT);
    uiLibraryAddHit(x + 16, row_y, MENU_PANEL_W - 32, MENU_PANEL_ROW_H - 6, LIBHIT_MENU_ITEM, i, 0);
}

void uiDrawMenu(const char *title, const char *version, const char *const *items, int count, int cursor)
{
    int h = 76 + count * MENU_PANEL_ROW_H + 20;
    int x = (GFX_WIDTH - MENU_PANEL_W) / 2, y = (GFX_HEIGHT - h) / 2;
    gfxBlendRect(0, 0, GFX_WIDTH, GFX_HEIGHT, GFX_RGB(0, 0, 0), UI_DIM_ALPHA);
    uiLibraryAddHit(0, 0, GFX_WIDTH, GFX_HEIGHT, LIBHIT_CLOSE, 0, 0);
    gfxFillRoundRect(x, y, MENU_PANEL_W, h, 16, COLOR_SURFACE);
    uiLibraryAddHit(x, y, MENU_PANEL_W, h, LIBHIT_PANEL, 0, 0);
    // The app's name and version (as KaiRadio's), or a question.
    int title_w = fontDraw(FONT_BOLD, 22, x + MENU_PANEL_PAD, y + 24, title, version ? COLOR_ACCENT : COLOR_TEXT);
    if (version)
        fontDraw(FONT_REGULAR, 15, x + MENU_PANEL_PAD + title_w + 12, y + 31, version, COLOR_FAINT);
    for (int i = 0; i < count; i++)
        drawMenuRow(x, y + 76 + i * MENU_PANEL_ROW_H, i, cursor, items[i]);
}

// A card's colors, the same for the same name every time.
static void cardColors(const char *name, Color *top, Color *bottom)
{
    static const uint32_t PALETTE[][2] = {
        {0x3B2A6B, 0x17112E}, {0x1F4E79, 0x0C1E30}, {0x6B2A3B, 0x2B1018}, {0x2A6B5A, 0x0F2A23},
        {0x7A4A1F, 0x301C0B}, {0x4A6B2A, 0x1C2A10}, {0x2A4A6B, 0x101C2B}, {0x6B2A6B, 0x2A102A},
        {0x5A3A2A, 0x241710}, {0x2A5A6B, 0x10232B},
    };
    unsigned hash = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++)
        hash = (hash ^ *p) * 16777619u;
    const uint32_t *pair = PALETTE[hash % (sizeof(PALETTE) / sizeof(PALETTE[0]))];
    *top = GFX_RGB(pair[0] >> 16, (pair[0] >> 8) & 0xFF, pair[0] & 0xFF);
    *bottom = GFX_RGB(pair[1] >> 16, (pair[1] >> 8) & 0xFF, pair[1] & 0xFF);
}

// Draws text wrapped at spaces into at most max_lines lines of width w,
// the last cut with an ellipsis, bottom-aligned at bottom. Returns the top.
static int drawWrappedUp(FontFace face, int px, int x, int bottom, int w, int line_h, int max_lines, const char *text,
                         Color color)
{
    char lines[4][160];
    int count = 0;
    const char *p = text;
    while (*p && count < max_lines) {
        // As many words as fit; the last line takes the rest.
        char line[160];
        size_t best = 0, len = 0;
        while (p[len] && len + 1 < sizeof(line)) {
            size_t next = len;
            while (p[next] == ' ')
                next++;
            while (p[next] && p[next] != ' ')
                next++;
            if (next >= sizeof(line))
                break;
            snprintf(line, sizeof(line), "%.*s", (int)next, p);
            if (fontTextWidth(face, px, line) > w && best > 0)
                break;
            best = next;
            len = next;
            if (!p[len])
                break;
        }
        if (best == 0)
            best = strlen(p) < sizeof(line) - 1 ? strlen(p) : sizeof(line) - 1;
        if (count == max_lines - 1)
            best = strlen(p) < sizeof(line) - 1 ? strlen(p) : sizeof(line) - 1;
        snprintf(lines[count++], sizeof(lines[0]), "%.*s", (int)best, p);
        p += best;
        while (*p == ' ')
            p++;
    }
    int top = bottom - count * line_h;
    for (int i = 0; i < count; i++)
        fontDrawClipped(face, px, x, top + i * line_h, w, lines[i], color);
    return top;
}

// A card: a colored tile with the name in it (and a tag above it, like
// "SERIES"), how far it was watched along the bottom, and a white ring when
// the cursor is on it.
static void drawCard(int x, int y, int w, int h, const char *name, const char *tag, double progress, bool focused,
                     const uint32_t *picture)
{
    if (focused)
        gfxFillRoundRect(x - 4, y - 4, w + 8, h + 8, 14, COLOR_TEXT);
    if (picture) {
        // The frame, darkened toward the bottom so the name reads over it.
        drawPicture(picture, x, y, w, h, 10);
        gfxSetClipRound(x, y, w, h, 10);
        int fade_y = y + h / 3, fade_h = h - h / 3;  // Two pixels a step: no bands show.
        for (int i = 0; i < fade_h; i += 2)
            gfxBlendRect(x, fade_y + i, w, 2, GFX_RGB(0, 0, 0), (unsigned)(20 + 200 * i / fade_h));
        gfxResetClip();
    } else {
        Color top, bottom;
        cardColors(name, &top, &bottom);
        gfxSetClipRound(x, y, w, h, 10);
        gfxGradientV(x, y, w, h, top, bottom);
        gfxResetClip();
    }
    int text_bottom = y + h - (progress >= 0 ? 18 : 12);
    int name_top = drawWrappedUp(FONT_BOLD, 21, x + 14, text_bottom, w - 28, 26, 3, name, COLOR_TEXT);
    if (tag && tag[0])
        fontDraw(FONT_BOLD, 13, x + 14, name_top - 20 > y + 8 ? name_top - 20 : y + 8, tag,
                 GFX_RGB(0xD8, 0xD8, 0xE0));
    if (progress >= 0) {
        int bar_w = w - 28, done = (int)(bar_w * (progress > 1 ? 1 : progress));
        gfxFillRoundRect(x + 14, y + h - 12, bar_w, 4, 2, GFX_RGB(0x00, 0x00, 0x00));
        gfxFillRoundRect(x + 14, y + h - 12, done > 4 ? done : 4, 4, 2, COLOR_ACCENT);
    }
}

// How far a video was watched (0-1), or -1 if it wasn't started.
static double watched(const Library *lib, const ResumeList *resume, int item)
{
    int64_t at, len;
    if (!resume || item < 0 || !resumeFind(resume, lib->items[item].path, &at, &len) || len <= 0)
        return -1;
    return (double)at / (double)len;
}

static void episodeLabel(const LibraryItem *item, char *out, size_t size)
{
    if (item->name.title[0])
        snprintf(out, size, "S%02dE%02d \xC2\xB7 %s", item->name.season, item->name.episode, item->name.title);
    else
        snprintf(out, size, "Season %d, episode %d", item->name.season, item->name.episode);
}

static void showLabel(const LibraryShow *show, char *out, size_t size)
{
    if (show->seasons > 1)
        snprintf(out, size, "%d seasons \xC2\xB7 %d episodes", show->seasons, show->count);
    else
        snprintf(out, size, "%d episode%s", show->count, show->count == 1 ? "" : "s");
}

// What a card shows for ref: its name in the tile, a tag, and a line under it.
static void describe(const Library *lib, LibraryRef ref, const char **name, const char **tag, char *below,
                     size_t size)
{
    below[0] = '\0';
    if (ref.is_show) {
        *name = lib->shows[ref.index].name;
        *tag = "SERIES";
        showLabel(&lib->shows[ref.index], below, size);
        return;
    }
    const LibraryItem *item = &lib->items[ref.index];
    if (item->show >= 0) {
        *name = lib->shows[item->show].name;
        *tag = "EPISODE";
        episodeLabel(item, below, size);
    } else {
        *name = item->name.title;
        *tag = "FILM";
        if (item->name.year)
            snprintf(below, size, "%d", item->name.year);
    }
}

static void drawRefCard(const Library *lib, const ResumeList *resume, LibraryRef ref, int x, int y, int w, int h,
                        bool focused)
{
    const char *name, *tag;
    char below[200];
    describe(lib, ref, &name, &tag, below, sizeof(below));
    drawCard(x, y, w, h, name, tag, ref.is_show ? -1 : watched(lib, resume, ref.index), focused, pictureOf(lib, ref));
    fontDrawClipped(FONT_REGULAR, 17, x + 2, y + h + 10, w - 4, below, focused ? COLOR_TEXT : COLOR_DIM);
}

static void drawCentered(const char *title, const char *line)
{
    fontDrawCentered(FONT_BOLD, 26, GFX_WIDTH / 2.0f, 320, title, COLOR_TEXT);
    if (line)
        fontDrawCentered(FONT_REGULAR, 19, GFX_WIDTH / 2.0f, 364, line, COLOR_DIM);
}

static void drawScrollbar(int y, int h, int first, int visible, int count)
{
    if (count <= visible)
        return;
    int thumb_h = h * visible / count;
    if (thumb_h < 24)
        thumb_h = 24;
    int thumb_y = y + (h - thumb_h) * first / (count - visible);
    gfxFillRoundRect(GFX_WIDTH - 12, y, 4, h, 2, COLOR_TRACK);
    gfxFillRoundRect(GFX_WIDTH - 12, thumb_y, 4, thumb_h, 2, COLOR_FAINT);
}

// --- Home ---

static void drawHome(const LibView *v, const Library *lib, const Home *home, const ResumeList *resume)
{
    for (int r = v->home_first_row; r < home->count && r < v->home_first_row + LIBVIEW_HOME_ROWS; r++) {
        const HomeRow *row = &home->rows[r];
        int y = HOME_Y + (r - v->home_first_row) * HOME_ROW_H;
        fontDraw(FONT_BOLD, 22, MARGIN, y, homeRowTitle(row->kind), r == v->home_row ? COLOR_TEXT : COLOR_DIM);
        const Scroll *cols = &v->home_cols[r];
        // One card more than fit, cut by the screen's edge: there's more.
        for (int c = cols->first; c < row->count && c <= cols->first + LIBVIEW_HOME_COLS; c++) {
            int x = MARGIN + (c - cols->first) * (HOME_CARD_W + GAP);
            drawRefCard(lib, resume, row->items[c], x, y + HOME_TITLE_H, HOME_CARD_W, HOME_CARD_H,
                        r == v->home_row && c == cols->cursor);
            int w = x + HOME_CARD_W > GFX_WIDTH ? GFX_WIDTH - x : HOME_CARD_W;
            uiLibraryAddHit(x, y + HOME_TITLE_H, w, HOME_CARD_H + 40, LIBHIT_HOME_CARD, r, c);
        }
    }
}

// --- Films and Series ---

static void drawGrid(const LibView *v, LibGrid grid, const Library *lib, const ResumeList *resume)
{
    int count = grid == GRID_FILMS ? (int)lib->film_count : (int)lib->show_count;
    const Scroll *s = &v->grid[grid];
    int first = s->first * LIBVIEW_COLS;
    // A row more than fit, cut by the screen's bottom: there's more.
    for (int i = first; i < count && i < first + (LIBVIEW_GRID_ROWS + 1) * LIBVIEW_COLS; i++) {
        int col = i % LIBVIEW_COLS, row = (i - first) / LIBVIEW_COLS;
        int x = MARGIN + col * (GRID_CARD_W + GAP), y = GRID_Y + row * GRID_ROW_H;
        LibraryRef ref = grid == GRID_FILMS ? (LibraryRef){false, lib->films[i]} : (LibraryRef){true, i};
        drawRefCard(lib, resume, ref, x, y, GRID_CARD_W, GRID_CARD_H, i == s->cursor);
        if (y < GFX_HEIGHT)
            uiLibraryAddHit(x, y, GRID_CARD_W, GRID_CARD_H + 40, LIBHIT_GRID_CARD, i, 0);
    }
    int rows = (count + LIBVIEW_COLS - 1) / LIBVIEW_COLS;
    drawScrollbar(GRID_Y, LIBVIEW_GRID_ROWS * GRID_ROW_H - 20, s->first, LIBVIEW_GRID_ROWS, rows);
}

// --- A series' page ---

static void drawListRow(int y, bool focused, const char *number, const char *title, const char *detail,
                        double progress, const uint32_t *picture)
{
    if (focused) {
        gfxFillRoundRect(MARGIN - 12, y + 2, GFX_WIDTH - 2 * (MARGIN - 12), LIST_ROW_H - 4, 10, COLOR_SURFACE_HI);
        gfxFillRect(MARGIN - 12, y + 14, 3, LIST_ROW_H - 28, COLOR_ACCENT);
    }
    int x = MARGIN;
    if (number) {
        fontDrawMiddle(FONT_BOLD, 19, x, y + LIST_ROW_H / 2.0f, number, COLOR_FAINT);
        x += 56;
    }
    if (g_thumbnails) {  // A picture, or its place while it's made (or if there's none).
        int pic_w = (LIST_ROW_H - 12) * 16 / 9;
        if (picture)
            drawPicture(picture, x, y + 6, pic_w, LIST_ROW_H - 12, 6);
        else
            gfxFillRoundRect(x, y + 6, pic_w, LIST_ROW_H - 12, 6, COLOR_SURFACE_HI);
        x += pic_w + 18;
    }
    int detail_w = detail && detail[0] ? fontTextWidth(FONT_REGULAR, 17, detail) : 0;
    int title_w = GFX_WIDTH - MARGIN - x - (detail_w ? detail_w + 24 : 0);
    fontDrawClipped(FONT_BOLD, 21, x, y + (LIST_ROW_H - 30) / 2, title_w, title, COLOR_TEXT);
    if (detail_w)
        fontDrawMiddle(FONT_REGULAR, 17, GFX_WIDTH - MARGIN - detail_w, y + LIST_ROW_H / 2.0f, detail, COLOR_DIM);
    if (progress >= 0) {
        int bar_w = title_w < 220 ? title_w : 220, done = (int)(bar_w * progress);
        gfxFillRoundRect(x, y + LIST_ROW_H - 12, bar_w, 4, 2, COLOR_TRACK);
        gfxFillRoundRect(x, y + LIST_ROW_H - 12, done > 4 ? done : 4, 4, 2, COLOR_ACCENT);
    }
}

static void formatSize(int64_t bytes, char *out, size_t size)
{
    if (bytes >= (int64_t)1 << 30)
        snprintf(out, size, "%.1f GB", bytes / 1073741824.0);
    else
        snprintf(out, size, "%.0f MB", bytes / 1048576.0);
}

static void drawSeasonChips(const LibView *v, const Library *lib)
{
    int seasons[LIBVIEW_SEASONS];
    int n = libviewSeasons(lib, v->show, seasons, LIBVIEW_SEASONS);
    if (n <= 1 && (n == 0 || seasons[0] <= 1))
        return;  // One season: nothing to choose.
    char label[32];
    int widths[LIBVIEW_SEASONS];
    for (int i = 0; i < n; i++) {
        snprintf(label, sizeof(label), "Season %d", seasons[i]);
        widths[i] = fontTextWidth(FONT_BOLD, 18, label) + 28;
    }
    // From the first chip, or later so that the current one fits.
    int current = v->season < n ? v->season : n - 1;
    int first = 0, right = MARGIN;
    for (int i = 0; i <= current; i++)
        right += widths[i] + 8;
    for (; right > GFX_WIDTH - MARGIN && first < current; first++)
        right -= widths[first] + 8;
    int x = MARGIN, y = 140;
    for (int i = first; i < n && x < GFX_WIDTH; i++) {
        snprintf(label, sizeof(label), "Season %d", seasons[i]);
        bool on = i == v->season;
        gfxFillRoundRect(x, y, widths[i], 38, 19, on ? COLOR_TEXT : COLOR_SURFACE_HI);
        fontDrawCentered(FONT_BOLD, 18, x + widths[i] / 2.0f, y + 19, label, on ? COLOR_BG : COLOR_DIM);
        uiLibraryAddHit(x, y - 6, widths[i], 50, LIBHIT_SEASON, i, 0);
        x += widths[i] + 8;
    }
}

static void drawShow(const LibView *v, const Library *lib, const ResumeList *resume)
{
    const LibraryShow *show = &lib->shows[v->show];
    // Back, and the series' name.
    gfxLine(MARGIN + 12, 92, MARGIN + 2, 103, 3, COLOR_TEXT);
    gfxLine(MARGIN + 2, 103, MARGIN + 12, 114, 3, COLOR_TEXT);
    uiLibraryAddHit(0, UI_TAB_BAR_H, 90, 64, LIBHIT_BACK, 0, 0);
    int name_w = fontTextWidth(FONT_BOLD, 30, show->name);
    fontDrawClipped(FONT_BOLD, 30, MARGIN + 30, 84, GFX_WIDTH - 2 * MARGIN - 330, show->name, COLOR_TEXT);
    char info[64];
    showLabel(show, info, sizeof(info));
    int info_x = MARGIN + 30 + (name_w < GFX_WIDTH - 2 * MARGIN - 330 ? name_w : GFX_WIDTH - 2 * MARGIN - 330) + 20;
    fontDrawMiddle(FONT_REGULAR, 18, info_x, 104, info, COLOR_DIM);
    drawSeasonChips(v, lib);

    int first, count;
    libviewSeasonRange(lib, v->show, v->season, &first, &count);
    for (int e = v->episodes.first; e < count && e < v->episodes.first + LIBVIEW_LIST_ROWS; e++) {
        int item = show->episodes[first + e];
        const LibraryItem *it = &lib->items[item];
        int y = SHOW_LIST_Y + (e - v->episodes.first) * LIST_ROW_H;
        char number[16], title[160], size[24];
        snprintf(number, sizeof(number), "%d", it->name.episode);
        if (it->name.title[0])
            snprintf(title, sizeof(title), "%s", it->name.title);
        else
            snprintf(title, sizeof(title), "Episode %d", it->name.episode);
        formatSize(it->size, size, sizeof(size));
        drawListRow(y, e == v->episodes.cursor, number, title, size, watched(lib, resume, item),
                    pictureOf(lib, (LibraryRef){false, item}));
        uiLibraryAddHit(0, y, GFX_WIDTH, LIST_ROW_H, LIBHIT_EPISODE, e, 0);
    }
    drawScrollbar(SHOW_LIST_Y, LIBVIEW_LIST_ROWS * LIST_ROW_H, v->episodes.first, LIBVIEW_LIST_ROWS, count);
}

// --- Search ---

static void drawSearchIcon(float x, float y, Color color)
{
    gfxArc(x + 9, y + 9, 8, 3, 0, 6.2832f, color);
    gfxLine(x + 15, y + 15, x + 22, y + 22, 3, color);
}

static void drawSearch(const LibView *v, const Library *lib, const ResumeList *resume)
{
    int w = GFX_WIDTH - 2 * MARGIN;
    gfxFillRoundRect(MARGIN, SEARCH_BOX_Y, w, SEARCH_BOX_H, SEARCH_BOX_H / 2, COLOR_SURFACE_HI);
    drawSearchIcon(MARGIN + 22, SEARCH_BOX_Y + 16, COLOR_DIM);
    bool typed = v->query[0] != '\0';
    fontDrawClipped(FONT_REGULAR, 21, MARGIN + 62, SEARCH_BOX_Y + 13, w - 90,
                    typed ? v->query : "Search films, series and episodes  (A or Y)", typed ? COLOR_TEXT : COLOR_FAINT);
    uiLibraryAddHit(MARGIN, SEARCH_BOX_Y - 6, w, SEARCH_BOX_H + 12, LIBHIT_SEARCH_BOX, 0, 0);
    if (!typed)
        return;
    if (v->result_count == 0) {
        char line[120];
        snprintf(line, sizeof(line), "Nothing in the library is called \"%s\".", v->query);
        drawCentered("Nothing found", line);
        return;
    }
    const Scroll *s = &v->results_scroll;
    for (int i = s->first; i < v->result_count && i < s->first + LIBVIEW_LIST_ROWS; i++) {
        LibraryRef ref = v->results[i];
        const char *name, *tag;
        char below[200], title[300];
        describe(lib, ref, &name, &tag, below, sizeof(below));
        bool episode = !ref.is_show && lib->items[ref.index].show >= 0;
        if (episode)  // "Rook", "S01E03 · Lamentia" -> "S01E03 · Lamentia", "Rook".
            snprintf(title, sizeof(title), "%s", below);
        else
            snprintf(title, sizeof(title), "%s", name);
        const char *detail = episode ? name : below;
        int y = SEARCH_LIST_Y + (i - s->first) * LIST_ROW_H;
        drawListRow(y, i == s->cursor, NULL, title, detail, ref.is_show ? -1 : watched(lib, resume, ref.index),
                    pictureOf(lib, ref));
        uiLibraryAddHit(0, y, GFX_WIDTH, LIST_ROW_H, LIBHIT_RESULT, i, 0);
    }
    drawScrollbar(SEARCH_LIST_Y, LIBVIEW_LIST_ROWS * LIST_ROW_H, s->first, LIBVIEW_LIST_ROWS, v->result_count);
}

void uiDrawToast(const char *message)
{
    if (!message || !message[0])
        return;
    int w = fontTextWidth(FONT_BOLD, 18, message) + 48;
    if (w > GFX_WIDTH - 2 * MARGIN)
        w = GFX_WIDTH - 2 * MARGIN;
    int x = (GFX_WIDTH - w) / 2, y = UI_FOOTER_Y - 58;
    gfxFillRoundRect(x, y, w, 46, 23, COLOR_SURFACE_HI);
    fontDrawClipped(FONT_BOLD, 18, x + 24, y + 10, w - 48, message, COLOR_ERROR);
}

// --- The screen ---

void uiDrawLibrary(const LibView *v, const Library *lib, const Home *home, const ResumeList *resume,
                   bool folder_set, const char *status, const char *message)
{
    uiDrawBackground();
    uiDrawTabBar(v->tab, status);
    gfxSetClip(0, UI_TAB_BAR_H, GFX_WIDTH, UI_FOOTER_Y - UI_TAB_BAR_H);
    if (!folder_set) {
        drawCentered("Choose the folder with your videos",
                     "KaiVideo finds the films and series in it and its folders: - or Settings > Library folder.");
    } else if (v->show >= 0 && (size_t)v->show < lib->show_count) {
        drawShow(v, lib, resume);
    } else if (v->tab == TAB_SEARCH) {
        drawSearch(v, lib, resume);
    } else if (lib->count == 0) {
        drawCentered(status && status[0] ? "Looking for videos..." : "No videos here yet",
                     "KaiVideo plays MP4, MKV and WebM files (H.264, VP9, AV1, VP8).");
    } else if (v->tab == TAB_HOME) {
        drawHome(v, lib, home, resume);
    } else if (v->tab == TAB_FILMS && lib->film_count == 0) {
        drawCentered("No films", "Every video here is an episode of a series.");
    } else if (v->tab == TAB_SERIES && lib->show_count == 0) {
        drawCentered("No series", "Episodes are found by names like \"Show S01E02\".");
    } else if (v->tab == TAB_FILMS || v->tab == TAB_SERIES) {
        drawGrid(v, v->tab == TAB_FILMS ? GRID_FILMS : GRID_SERIES, lib, resume);
    }
    gfxResetClip();
    uiDrawFooterStrip();
    uiDrawToast(message);
}
