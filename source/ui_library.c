#include "ui_library.h"

#include <stdio.h>
#include <string.h>

#include "background.h"
#include "font.h"
#include "gfx.h"
#include "thumbs.h"
#include "ui.h"
#include "ui_buttons.h"
#include "ui_style.h"
#include "ui_library_shared.h"
#include "util.h"

#define TAB_GAP 28             // The header, as KaiPlayer's and KaiRadio's.
#define TAB_TEXT_MIDDLE 28.0f
#define HINT_GAP 9
#define SOURCE_DOT 12         // The source's color, before its name.
#define GLOW_BANDS 12         // The header's glow: this many bands,
#define GLOW_BAND_H 10        // each this tall,
#define GLOW_ALPHA 34         // from this strong at the top to nothing.
#define TAB_SIZE 20
#define TAB_SIZE_MIN 17       // Smaller, before the tabs slide.
#define TAB_SIDE_GAP 16       // Between the tabs' ZL and ZR and what the header shows at its sides.
#define TAB_FADE_STEPS 12     // More tabs past an edge: it fades out over this many steps,
#define TAB_FADE_SOLID 5      // the first ones solid,
#define TAB_FADE_STEP 6       // each this wide,
#define TAB_ARROW_W 10        // with an arrow this long
#define TAB_ARROW_H 8         // and half this tall,
#define TAB_ARROW_INSET 16    // its tip this far in from the edge.
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
#define HOME_ROW_H LIBVIEW_HOME_ROW_PX
#define GRID_Y 84
#define GRID_CARD_W ((GFX_WIDTH - 2 * MARGIN - (LIBVIEW_COLS - 1) * GAP) / LIBVIEW_COLS)
#define GRID_CARD_H 128
#define GRID_ROW_H LIBVIEW_GRID_ROW_PX
#define LIST_ROW_H LIBVIEW_LIST_ROW_PX
#define CLIP_ABOVE 8          // A list drawn between rows: room above its first for a focused card's ring.
#define SHOW_LIST_Y 196        // A series' episodes.
#define SEARCH_BOX_Y 84
#define SEARCH_BOX_H 56
#define SEARCH_LIST_Y 160
#define MAX_HITS 160  // The Live TV guide has one for each program in view.
#define SERIES_PICTURE_TRIES 8  // Episodes tried for a series' picture.
#define PLAYER_FADE_BANDS 12
#define PLAYER_FADE_BAND_H 6
#define PLAYER_FADE_ALPHA 190
#define CHECK_R 13             // A list row's watched circle.

// --- Touch targets, kept as they're drawn ---

typedef struct {
    int x, y, w, h;
    LibHit hit;
} HitRegion;

static HitRegion g_hits[MAX_HITS];
static int g_hit_count;

static int g_hit_top = 0, g_hit_bottom = GFX_HEIGHT;

void uiLibraryClipHits(int top, int bottom)
{
    g_hit_top = top;
    g_hit_bottom = bottom;
}

void uiLibraryAddHit(int x, int y, int w, int h, LibHitKind kind, int a, int b)
{
    int y0 = y < g_hit_top ? g_hit_top : y, y1 = y + h > g_hit_bottom ? g_hit_bottom : y + h;
    if (y1 <= y0)
        return;
    y = y0;
    h = y1 - y0;
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

bool uiLibThumbnailsOn(void)
{
    return g_thumbnails;
}
static const Watched *g_watched;
static const char *g_source_label = "";

void uiLibrarySetThumbnails(bool on)
{
    g_thumbnails = on;
}

void uiLibrarySetWatched(const Watched *watched)
{
    g_watched = watched;
}

bool uiLibSeen(const Library *lib, int item)
{
    return g_watched && item >= 0 && watchedHas(g_watched, lib->items[item].path);
}

// A check mark centered at cx, cy, size across.
static void drawCheckMark(float cx, float cy, float size, Color color)
{
    gfxLine(cx - size * 0.45f, cy, cx - size * 0.12f, cy + size * 0.32f, 3, color);
    gfxLine(cx - size * 0.12f, cy + size * 0.32f, cx + size * 0.45f, cy - size * 0.3f, 3, color);
}

// Watched: a filled circle with a check; not, an empty ring (a list row's toggle).
static void drawSeenCircle(float cx, float cy, float r, bool on, bool ring)
{
    if (on) {
        gfxFillRoundRect((int)(cx - r), (int)(cy - r), (int)(2 * r), (int)(2 * r), (int)r, COLOR_ACCENT);
        drawCheckMark(cx, cy, r * 1.1f, COLOR_BG);
    } else if (ring) {
        gfxArc(cx, cy, r - 1.5f, 2, 0, 6.2832f, COLOR_FAINT);
    }
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
const uint32_t *uiLibPicture(const Library *lib, LibraryRef ref)
{
    if (!g_thumbnails)
        return NULL;
    if (!ref.is_show)
        return thumbGet(lib->items[ref.index].path, lib->items[ref.index].size);
    // A series: the server's picture for it, or its first episode that has one
    // (one that can't be decoded has none: the next).
    const LibraryShow *show = &lib->shows[ref.index];
    const char *art = show->count ? lib->items[show->episodes[0]].series_art : NULL;
    if (art) {
        const uint32_t *pixels = thumbGet(art, 0);
        if (pixels || !thumbIsNone(art, 0))
            return pixels;
    }
    for (int i = 0; i < show->count && i < SERIES_PICTURE_TRIES; i++) {
        const LibraryItem *item = &lib->items[show->episodes[i]];
        const uint32_t *pixels = thumbGet(item->path, item->size);
        if (pixels || !thumbIsNone(item->path, item->size))
            return pixels;  // Its picture, or NULL while it's made.
    }
    return NULL;
}

// Draws a picture into a layout box with round corners.
void uiLibDrawImage(const uint32_t *pixels, int image_w, int image_h, int x, int y, int w, int h, int radius)
{
    gfxSetClipRound(x, y, w, h, radius);
    gfxDrawImageScaledFb((float)gfxToFb(x), (float)gfxToFb(y), (float)gfxToFb(w), (float)gfxToFb(h), image_w, image_h,
                         pixels);
    gfxResetClip();
}

void uiLibDrawPicture(const uint32_t *pixels, int x, int y, int w, int h, int radius)
{
    uiLibDrawImage(pixels, THUMB_W, THUMB_H, x, y, w, h, radius);
}

static const char *const TAB_NAMES[TAB_COUNT] = {"Home", "Films", "Series", "Search", "Files", "", "Live TV"};

static SysInfo g_sys;
static bool g_clock_24h;

void uiLibrarySetStatus(const SysInfo *sys, bool clock_24h)
{
    g_sys = *sys;
    g_clock_24h = clock_24h;
}

static Color g_source_color;

void uiLibrarySetSource(const char *label, Color color)
{
    g_source_label = label;
    g_source_color = color;
}

Color uiServerColor(ServerKind kind)
{
    switch (kind) {
    case SERVER_EMBY: return GFX_RGB(0x52, 0xB5, 0x4B);
    case SERVER_PLEX: return GFX_RGB(0xE5, 0xA0, 0x0D);
    default: return GFX_RGB(0xAA, 0x5C, 0xC3);
    }
}

void uiLibraryResetHits(void)
{
    g_hit_count = 0;
}

// The tabs in the middle, between the ZL and ZR that change them.
// A tab's name: its kind's, or the server library's own.
static const char *tabName(const LibView *v, const Library *lib, int index)
{
    LibTabRef t = v->tabs[index];
    if (t.kind == TAB_COLLECTION)
        return t.collection >= 0 && t.collection < lib->collection_count ? lib->collections[t.collection].name : "";
    return t.kind >= 0 && t.kind < TAB_COUNT ? TAB_NAMES[t.kind] : "";
}

// The tabs' widths at a size; their total, gaps included.
static int tabWidths(const LibView *v, const Library *lib, int size, int widths[LIBVIEW_TABS])
{
    int total = 0, active = libviewTabIndex(v);
    for (int i = 0; i < v->tab_count; i++) {
        widths[i] = fontTextWidth(active == i ? FONT_BOLD : FONT_REGULAR, size, tabName(v, lib, i));
        total += widths[i] + (i > 0 ? TAB_GAP : 0);
    }
    return total;
}

// Where the first tab starts: centered on the screen if they fit there, else
// in the room they have; too many (several servers' libraries) slide so the
// one chosen shows.
static int tabsStart(const int widths[LIBVIEW_TABS], int count, int active, int total, int left, int right)
{
    int x = (GFX_WIDTH - total) / 2;
    if (total <= right - left)
        return x < left ? left : x + total > right ? right - total : x;
    int active_x = 0;
    for (int i = 0; i < active && i < count; i++)
        active_x += widths[i] + TAB_GAP;
    int active_w = active >= 0 && active < count ? widths[active] : 0;
    x = (left + right) / 2 - active_x - active_w / 2;
    if (x > left)
        x = left;
    if (x + total < right)
        x = right - total;
    return x;
}

// More tabs past an edge: the edge fades out, with an arrow that way.
static void drawMoreMark(int edge, int direction)
{
    for (int i = 0; i < TAB_FADE_STEPS; i++) {
        int x = direction < 0 ? edge + i * TAB_FADE_STEP : edge - (i + 1) * TAB_FADE_STEP;
        int fading = i - TAB_FADE_SOLID;  // Solid under the arrow, then fading out.
        gfxBlendRect(x, 8, TAB_FADE_STEP, UI_TAB_BAR_H - 16, COLOR_BG,
                     fading < 0 ? 255u : (unsigned)(255 - fading * 255 / (TAB_FADE_STEPS - TAB_FADE_SOLID)));
    }
    float tip = (float)(edge - direction * TAB_ARROW_INSET), back = tip - (float)(direction * TAB_ARROW_W);
    gfxTriangle(tip, TAB_TEXT_MIDDLE, back, TAB_TEXT_MIDDLE - TAB_ARROW_H, back, TAB_TEXT_MIDDLE + TAB_ARROW_H,
                COLOR_ACCENT);
}

// The tabs, between left and right (what the header shows at its sides).
static void drawTabs(const LibView *v, const Library *lib, int left, int right)
{
    int widths[LIBVIEW_TABS], active = libviewTabIndex(v), size = TAB_SIZE;
    left += uiButtonWidth("ZL") + 2 * HINT_GAP;
    right -= uiButtonWidth("ZR") + 2 * HINT_GAP;
    int total = tabWidths(v, lib, size, widths);
    while (total > right - left && size > TAB_SIZE_MIN)
        total = tabWidths(v, lib, --size, widths);
    int x = tabsStart(widths, v->tab_count, active, total, left, right);
    bool slides = total > right - left;
    int first_x = x;
    if (slides)
        gfxSetClip(left - TAB_GAP / 2, 0, right - left + TAB_GAP, UI_TAB_BAR_H);
    for (int i = 0; i < v->tab_count; i++) {
        bool on = active == i;
        fontDraw(on ? FONT_BOLD : FONT_REGULAR, size, x, 18 + (TAB_SIZE - size) / 2, tabName(v, lib, i),
                 on ? COLOR_TEXT : COLOR_DIM);
        int hit_l = x - TAB_GAP / 2, hit_r = x + widths[i] + TAB_GAP / 2;  // Only what shows answers a tap.
        if (hit_l < left - TAB_GAP / 2)
            hit_l = left - TAB_GAP / 2;
        if (hit_r > right + TAB_GAP / 2)
            hit_r = right + TAB_GAP / 2;
        if (hit_r > hit_l)
            uiLibraryAddHit(hit_l, 4, hit_r - hit_l, UI_TAB_BAR_H - 8, LIBHIT_TAB, i, 0);
        if (on)
            gfxFillRoundRect(x, 48, widths[i], 3, 1, COLOR_ACCENT);
        x += widths[i] + TAB_GAP;
    }
    if (slides) {
        gfxResetClip();
        if (first_x < left)  // Some before: ZL goes to them.
            drawMoreMark(left - TAB_GAP / 2, -1);
        if (x - TAB_GAP > right)  // Some after: ZR.
            drawMoreMark(right + TAB_GAP / 2, 1);
    }
    int zl_x = slides ? left - HINT_GAP - uiButtonWidth("ZL") : first_x - HINT_GAP - uiButtonWidth("ZL");
    int zr_x = slides ? right + HINT_GAP : x - TAB_GAP + HINT_GAP;
    uiDrawButton(zl_x, TAB_TEXT_MIDDLE, "ZL", COLOR_SURFACE_HI, COLOR_DIM);
    uiDrawButton(zr_x, TAB_TEXT_MIDDLE, "ZR", COLOR_SURFACE_HI, COLOR_DIM);
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

// The top right: a scan's progress, the time and the battery. Returns where
// it begins on the left.
static int drawStatus(const char *status)
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
    return x;
}

void uiDrawPlayerStatus(void)
{
    for (int i = 0; i < PLAYER_FADE_BANDS; i++)  // A fade from the top, so they read over any picture.
        gfxBlendRect(0, i * PLAYER_FADE_BAND_H, GFX_WIDTH, PLAYER_FADE_BAND_H, GFX_RGB(0, 0, 0),
                     (unsigned)(PLAYER_FADE_ALPHA - i * PLAYER_FADE_ALPHA / PLAYER_FADE_BANDS));
    drawStatus(NULL);
}

// Three bars before the name: the touch way into the + menu.
static void drawMenuButton(void)
{
    for (int i = 0; i < 3; i++)
        gfxFillRoundRect(MARGIN, 22 + i * 7, 22, 3, 1, COLOR_ACCENT);
}

// A soft glow of the accent from the top: the source's color sets the mood.
static void drawHeaderGlow(void)
{
    for (int i = 0; i < GLOW_BANDS; i++)
        gfxBlendRect(0, i * GLOW_BAND_H, GFX_WIDTH, GLOW_BAND_H, COLOR_ACCENT,
                     (unsigned)(GLOW_ALPHA - i * GLOW_ALPHA / GLOW_BANDS));
}

void uiDrawTabBar(const LibView *v, const Library *lib, const char *status)
{
    drawHeaderGlow();
    drawMenuButton();
    int title_w = fontDraw(FONT_BOLD, 26, MARGIN + 36, 16, "KaiVideo", COLOR_ACCENT);
    uiLibraryAddHit(0, 0, MARGIN + 36 + title_w + 12, UI_TAB_BAR_H, LIBHIT_MENU, 0, 0);
    int left = MARGIN + 36 + title_w;
    if (g_source_label[0]) {  // Where the library comes from: X changes it.
        int x = MARGIN + 36 + title_w + 18, key_w = uiButtonWidth("X");
        int w = key_w + SOURCE_DOT + 8 + fontTextWidth(FONT_BOLD, 17, g_source_label) + 28;
        gfxFillRoundRect(x, 15, w, 32, 16, COLOR_SURFACE_HI);
        uiDrawButton(x + 8, 31.0f, "X", COLOR_SURFACE, COLOR_TEXT);
        int dot_x = x + 16 + key_w;  // The source's color.
        gfxFillRoundRect(dot_x, 31 - SOURCE_DOT / 2, SOURCE_DOT, SOURCE_DOT, SOURCE_DOT / 2, g_source_color);
        fontDrawMiddle(FONT_BOLD, 17, dot_x + SOURCE_DOT + 8, 31.0f, g_source_label, COLOR_TEXT);
        uiLibraryAddHit(x - 4, 4, w + 8, UI_TAB_BAR_H - 8, LIBHIT_SOURCE, 0, 0);
        left = x + w;
    }
    int right = drawStatus(status);
    drawTabs(v, lib, left + TAB_SIDE_GAP, right - TAB_SIDE_GAP);
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
void uiLibCardColors(const char *name, Color *top, Color *bottom)
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

#define WRAP_LINE_MAX 160

// How much of p goes on a line of width w: as many words as fit; a last line
// takes the rest. The line goes in out (WRAP_LINE_MAX bytes).
static size_t wrapLine(FontFace face, int px, int w, const char *p, bool last, char *out)
{
    char line[WRAP_LINE_MAX];
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
    if (best == 0 || last)
        best = strlen(p) < sizeof(line) - 1 ? strlen(p) : sizeof(line) - 1;
    snprintf(out, WRAP_LINE_MAX, "%.*s", (int)best, p);
    return best;
}

// Draws text wrapped at spaces from top, at most max_lines lines.
void uiLibDrawWrapped(FontFace face, int px, int x, int top, int w, int line_h, int max_lines, const char *text,
                      Color color)
{
    const char *p = text;
    for (int i = 0; *p && i < max_lines; i++) {
        char line[WRAP_LINE_MAX];
        p += wrapLine(face, px, w, p, i == max_lines - 1, line);
        fontDrawClipped(face, px, x, top + i * line_h, w, line, color);
        while (*p == ' ')
            p++;
    }
}

// Draws text wrapped at spaces into at most max_lines lines of width w,
// the last cut with an ellipsis, bottom-aligned at bottom. Returns the top.
static int drawWrappedUp(FontFace face, int px, int x, int bottom, int w, int line_h, int max_lines, const char *text,
                         Color color)
{
    char lines[4][WRAP_LINE_MAX];
    int count = 0;
    const char *p = text;
    while (*p && count < max_lines) {
        p += wrapLine(face, px, w, p, count == max_lines - 1, lines[count]);
        count++;
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
                     const uint32_t *picture, bool watched_badge)
{
    if (focused)
        gfxFillRoundRect(x - 4, y - 4, w + 8, h + 8, 14, COLOR_TEXT);
    if (picture) {
        // The frame, darkened toward the bottom so the name reads over it.
        uiLibDrawPicture(picture, x, y, w, h, 10);
        gfxSetClipRound(x, y, w, h, 10);
        int fade_y = y + h / 3, fade_h = h - h / 3;  // Two pixels a step: no bands show.
        for (int i = 0; i < fade_h; i += 2)
            gfxBlendRect(x, fade_y + i, w, 2, GFX_RGB(0, 0, 0), (unsigned)(20 + 200 * i / fade_h));
        gfxResetClip();
    } else {
        Color top, bottom;
        uiLibCardColors(name, &top, &bottom);
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
    if (watched_badge)
        drawSeenCircle(x + w - 22.0f, y + 22.0f, 12, true, false);
}

// How far a video was watched (0-1), or -1 if it wasn't started.
double uiLibWatched(const Library *lib, const ResumeList *resume, int item)
{
    int64_t at, len;
    if (!resume || item < 0 || !resumeFind(resume, lib->items[item].path, &at, &len) || len <= 0)
        return -1;
    return (double)at / (double)len;
}

void uiLibEpisodeLabel(const LibraryItem *item, char *out, size_t size)
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
        uiLibEpisodeLabel(item, below, size);
    } else {
        *name = item->name.title;
        *tag = "FILM";
        if (item->name.year)
            snprintf(below, size, "%d", item->name.year);
    }
}

// tag_over: a tag in place of its own ("UP NEXT"), or NULL.
static void drawRefCard(const Library *lib, const ResumeList *resume, LibraryRef ref, int x, int y, int w, int h,
                        bool focused, const char *tag_over)
{
    const char *name, *tag;
    char below[200];
    describe(lib, ref, &name, &tag, below, sizeof(below));
    double progress = ref.is_show ? -1 : uiLibWatched(lib, resume, ref.index);
    drawCard(x, y, w, h, name, tag_over ? tag_over : tag, progress, focused, uiLibPicture(lib, ref),
             !ref.is_show && progress < 0 && uiLibSeen(lib, ref.index));
    fontDrawClipped(FONT_REGULAR, 17, x + 2, y + h + 10, w - 4, below, focused ? COLOR_TEXT : COLOR_DIM);
}

static void drawCentered(const char *title, const char *line)
{
    fontDrawCentered(FONT_BOLD, 26, GFX_WIDTH / 2.0f, 320, title, COLOR_TEXT);
    if (line)
        fontDrawCentered(FONT_REGULAR, 19, GFX_WIDTH / 2.0f, 364, line, COLOR_DIM);
}

// first and offset: where the view is (offset pixels into row first, rows row_h tall).
static void drawScrollbar(int y, int h, int first, int offset, int row_h, int visible, int count)
{
    if (count <= visible)
        return;
    int thumb_h = h * visible / count;
    if (thumb_h < 24)
        thumb_h = 24;
    long long at = (long long)first * row_h + offset, end = (long long)(count - visible) * row_h;
    int thumb_y = y + (int)((h - thumb_h) * (at < end ? at : end) / end);
    gfxFillRoundRect(GFX_WIDTH - 12, y, 4, h, 2, COLOR_TRACK);
    gfxFillRoundRect(GFX_WIDTH - 12, thumb_y, 4, thumb_h, 2, COLOR_FAINT);
}

// --- Home ---

// A tap target, only where its list shows (between top and bottom).
static void addListHit(int x, int y, int w, int h, int top, int bottom, LibHitKind kind, int a, int b)
{
    int y0 = y < top ? top : y, y1 = y + h > bottom ? bottom : y + h;
    if (y1 > y0)
        uiLibraryAddHit(x, y0, w, y1 - y0, kind, a, b);
}

static void drawHome(const LibView *v, const Library *lib, const Home *home, const ResumeList *resume)
{
    int top = HOME_Y - CLIP_ABOVE, bottom = HOME_Y + LIBVIEW_HOME_ROWS * HOME_ROW_H;
    gfxSetClip(0, top, GFX_WIDTH, bottom - top);
    // One row more than fit when a finger left them between rows.
    for (int r = v->home_first_row; r < home->count && r <= v->home_first_row + LIBVIEW_HOME_ROWS; r++) {
        const HomeRow *row = &home->rows[r];
        int y = HOME_Y + (r - v->home_first_row) * HOME_ROW_H - v->home_offset;
        if (y >= bottom)
            break;
        fontDraw(FONT_BOLD, 22, MARGIN, y, homeRowTitle(row->kind), r == v->home_row ? COLOR_TEXT : COLOR_DIM);
        const Scroll *cols = &v->home_cols[r];
        // One card more than fit, cut by the screen's edge: there's more.
        for (int c = cols->first; c < row->count && c <= cols->first + LIBVIEW_HOME_COLS + 1; c++) {
            int x = MARGIN + (c - cols->first) * LIBVIEW_HOME_COL_PX - cols->offset;
            if (x >= GFX_WIDTH)
                break;
            drawRefCard(lib, resume, row->items[c], x, y + HOME_TITLE_H, HOME_CARD_W, HOME_CARD_H,
                        r == v->home_row && c == cols->cursor, row->up_next[c] ? "UP NEXT" : NULL);
            int left = x < 0 ? 0 : x, right = x + HOME_CARD_W > GFX_WIDTH ? GFX_WIDTH : x + HOME_CARD_W;
            if (right > left)
                addListHit(left, y + HOME_TITLE_H, right - left, HOME_CARD_H + 40, top, bottom, LIBHIT_HOME_CARD, r, c);
        }
    }
    gfxResetClip();
}

// --- Films and Series ---

static void drawGrid(const LibView *v, int grid, const Library *lib, const ResumeList *resume)
{
    int count = libviewGridCount(v, lib, grid);
    const Scroll *s = &v->grid[grid];
    int first = s->first * LIBVIEW_COLS, top = GRID_Y - CLIP_ABOVE;
    gfxSetClip(0, top, GFX_WIDTH, GFX_HEIGHT - top);
    // A row more than fit, cut by the screen's bottom: there's more (two between rows).
    for (int i = first; i < count && i < first + (LIBVIEW_GRID_ROWS + 2) * LIBVIEW_COLS; i++) {
        int col = i % LIBVIEW_COLS, row = (i - first) / LIBVIEW_COLS;
        int x = MARGIN + col * (GRID_CARD_W + GAP), y = GRID_Y + row * GRID_ROW_H - s->offset;
        if (y >= GFX_HEIGHT)
            break;
        LibraryRef ref = libviewGridRef(v, lib, grid, i);
        drawRefCard(lib, resume, ref, x, y, GRID_CARD_W, GRID_CARD_H, i == s->cursor, NULL);
        addListHit(x, y, GRID_CARD_W, GRID_CARD_H + 40, top, GFX_HEIGHT, LIBHIT_GRID_CARD, i, 0);
    }
    gfxResetClip();
    int rows = (count + LIBVIEW_COLS - 1) / LIBVIEW_COLS;
    drawScrollbar(GRID_Y, LIBVIEW_GRID_ROWS * GRID_ROW_H - 20, s->first, s->offset, GRID_ROW_H, LIBVIEW_GRID_ROWS,
                  rows);
}

// --- A series' page ---

// watched: 1 or 0 draws the row's watched circle (filled, or a ring), -1 none.
static void drawListRow(int y, bool focused, const char *number, const char *title, const char *detail,
                        double progress, const uint32_t *picture, int watched_mark)
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
            uiLibDrawPicture(picture, x, y + 6, pic_w, LIST_ROW_H - 12, 6);
        else
            gfxFillRoundRect(x, y + 6, pic_w, LIST_ROW_H - 12, 6, COLOR_SURFACE_HI);
        x += pic_w + 18;
    }
    int right = GFX_WIDTH - MARGIN;
    if (watched_mark >= 0) {
        drawSeenCircle(right - CHECK_R, y + LIST_ROW_H / 2.0f, CHECK_R, watched_mark == 1, true);
        right -= 2 * CHECK_R + 20;
    }
    int detail_w = detail && detail[0] ? fontTextWidth(FONT_REGULAR, 17, detail) : 0;
    int title_w = right - x - (detail_w ? detail_w + 24 : 0);
    fontDrawClipped(FONT_BOLD, 21, x, y + (LIST_ROW_H - 30) / 2, title_w, title,
                    watched_mark == 1 && !focused ? COLOR_DIM : COLOR_TEXT);
    if (detail_w)
        fontDrawMiddle(FONT_REGULAR, 17, right - detail_w, y + LIST_ROW_H / 2.0f, detail, COLOR_DIM);
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
    int list_bottom = SHOW_LIST_Y + LIBVIEW_LIST_ROWS * LIST_ROW_H;
    gfxSetClip(0, SHOW_LIST_Y, GFX_WIDTH, list_bottom - SHOW_LIST_Y);
    for (int e = v->episodes.first; e < count && e <= v->episodes.first + LIBVIEW_LIST_ROWS; e++) {
        int item = show->episodes[first + e];
        const LibraryItem *it = &lib->items[item];
        int y = SHOW_LIST_Y + (e - v->episodes.first) * LIST_ROW_H - v->episodes.offset;
        char number[16], title[160], size[24];
        snprintf(number, sizeof(number), "%d", it->name.episode);
        if (it->name.title[0])
            snprintf(title, sizeof(title), "%s", it->name.title);
        else
            snprintf(title, sizeof(title), "Episode %d", it->name.episode);
        formatSize(it->size, size, sizeof(size));
        drawListRow(y, e == v->episodes.cursor, number, title, size, uiLibWatched(lib, resume, item),
                    uiLibPicture(lib, (LibraryRef){false, item}), uiLibSeen(lib, item) ? 1 : 0);
        addListHit(0, y, GFX_WIDTH, LIST_ROW_H, SHOW_LIST_Y, list_bottom, LIBHIT_EPISODE, e, 0);
        // The circle on the right marks it watched or not (X).
        addListHit(GFX_WIDTH - MARGIN - 2 * CHECK_R - 16, y, 2 * CHECK_R + 16 + MARGIN, LIST_ROW_H, SHOW_LIST_Y,
                   list_bottom, LIBHIT_WATCHED, e, 0);
    }
    gfxResetClip();
    drawScrollbar(SHOW_LIST_Y, LIBVIEW_LIST_ROWS * LIST_ROW_H, v->episodes.first, v->episodes.offset, LIST_ROW_H,
                  LIBVIEW_LIST_ROWS, count);
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
    int list_bottom = SEARCH_LIST_Y + LIBVIEW_LIST_ROWS * LIST_ROW_H;
    gfxSetClip(0, SEARCH_LIST_Y, GFX_WIDTH, list_bottom - SEARCH_LIST_Y);
    for (int i = s->first; i < v->result_count && i <= s->first + LIBVIEW_LIST_ROWS; i++) {
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
        int y = SEARCH_LIST_Y + (i - s->first) * LIST_ROW_H - s->offset;
        drawListRow(y, i == s->cursor, NULL, title, detail, ref.is_show ? -1 : uiLibWatched(lib, resume, ref.index),
                    uiLibPicture(lib, ref), !ref.is_show && uiLibSeen(lib, ref.index) ? 1 : -1);
        addListHit(0, y, GFX_WIDTH, LIST_ROW_H, SEARCH_LIST_Y, list_bottom, LIBHIT_RESULT, i, 0);
    }
    gfxResetClip();
    drawScrollbar(SEARCH_LIST_Y, LIBVIEW_LIST_ROWS * LIST_ROW_H, s->first, s->offset, LIST_ROW_H, LIBVIEW_LIST_ROWS,
                  v->result_count);
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
    uiDrawTabBar(v, lib, status);
    gfxSetClip(0, UI_TAB_BAR_H, GFX_WIDTH, UI_FOOTER_Y - UI_TAB_BAR_H);
    if (!folder_set) {
        drawCentered("Choose the folder with your videos",
                     "KaiVideo finds the films and series in it and its folders: - or Settings > Library folder.");
    } else if (v->show >= 0 && (size_t)v->show < lib->show_count) {
        drawShow(v, lib, resume);
    } else if (v->tab == TAB_SEARCH) {
        drawSearch(v, lib, resume);
    } else if (v->tab == TAB_LIVE) {
        // Drawn by the app over this (app_live.c): it holds the guide.
    } else if (lib->count == 0) {
        drawCentered(status && status[0] ? "Looking for videos..." : "No videos here yet",
                     "KaiVideo plays MP4, MKV and WebM files (H.264, HEVC, VP9, AV1, VP8).");
    } else if (v->tab == TAB_HOME) {
        drawHome(v, lib, home, resume);
    } else if (v->tab == TAB_FILMS && lib->film_count == 0) {
        drawCentered("No films", "Every video here is an episode of a series.");
    } else if (v->tab == TAB_SERIES && lib->show_count == 0) {
        drawCentered("No series", "Episodes are found by names like \"Show S01E02\".");
    } else if (v->tab == TAB_COLLECTION && libviewGridCount(v, lib, libviewGrid(v)) == 0) {
        drawCentered("Nothing here yet", "This library on the server has no films or episodes KaiVideo found.");
    } else if (libviewGrid(v) >= 0) {
        drawGrid(v, libviewGrid(v), lib, resume);
    }
    gfxResetClip();
    uiDrawFooterStrip();
    uiDrawToast(message);
}
