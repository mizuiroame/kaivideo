#include "ui_library.h"

#include <stdio.h>
#include <string.h>

#include "font.h"
#include "gfx.h"
#include "ui_style.h"

#define TAB_X 210              // The first tab, right of the name.
#define TAB_H 40
#define TAB_Y 14
#define FOLDER_W 112           // The Folder button, top right.
#define FOLDER_X (GFX_WIDTH - MARGIN - FOLDER_W)
#define GAP 16
#define HOME_Y 80              // Home's first row.
#define HOME_TITLE_H 36
#define HOME_CARD_W 216
#define HOME_CARD_H 122
#define HOME_ROW_H 202
#define GRID_Y 88
#define GRID_CARD_W ((GFX_WIDTH - 2 * MARGIN - (LIBVIEW_COLS - 1) * GAP) / LIBVIEW_COLS)
#define GRID_CARD_H 128
#define GRID_ROW_H 186
#define LIST_ROW_H 58
#define SHOW_LIST_Y 196        // A series' episodes.
#define SEARCH_BOX_Y 84
#define SEARCH_BOX_H 56
#define SEARCH_LIST_Y 160
#define MAX_HITS 96

// --- Touch targets, kept as they're drawn ---

typedef struct {
    int x, y, w, h;
    LibHit hit;
} HitRegion;

static HitRegion g_hits[MAX_HITS];
static int g_hit_count;

static void addHit(int x, int y, int w, int h, LibHitKind kind, int a, int b)
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

static const char *const TAB_NAMES[TAB_COUNT] = {"Home", "Films", "Series", "Search", "Files"};

static void drawFolderIcon(int x, int y, Color color)
{
    gfxFillRoundRect(x, y, 9, 5, 2, color);
    gfxFillRoundRect(x, y + 3, 22, 14, 3, color);
}

void uiDrawTabBar(LibTab active, const char *status)
{
    g_hit_count = 0;
    fontDraw(FONT_BOLD, 26, MARGIN, 18, "KaiVideo", COLOR_ACCENT);
    int x = TAB_X;
    for (int t = 0; t < TAB_COUNT; t++) {
        int w = fontTextWidth(FONT_BOLD, 19, TAB_NAMES[t]) + 32;
        bool on = t == (int)active;
        if (on)
            gfxFillRoundRect(x, TAB_Y, w, TAB_H, TAB_H / 2, COLOR_SURFACE_HI);
        fontDrawCentered(FONT_BOLD, 19, x + w / 2.0f, TAB_Y + TAB_H / 2.0f, TAB_NAMES[t], on ? COLOR_TEXT : COLOR_DIM);
        if (on)
            gfxFillRoundRect(x + w / 2 - 12, TAB_Y + TAB_H + 4, 24, 3, 1, COLOR_ACCENT);
        addHit(x, 0, w, UI_TAB_BAR_H, LIBHIT_TAB, t, 0);
        x += w + 6;
    }
    // L and R change tab: said once, left of the Folder button.
    const char *hint = status && status[0] ? status : "L R  tabs     Y  search";
    int hint_w = fontTextWidth(FONT_REGULAR, 16, hint);
    fontDrawMiddle(FONT_REGULAR, 16, FOLDER_X - 20 - hint_w, TAB_Y + TAB_H / 2.0f, hint,
                   status && status[0] ? COLOR_ACCENT : COLOR_FAINT);
    gfxFillRoundRect(FOLDER_X, TAB_Y, FOLDER_W, TAB_H, TAB_H / 2, COLOR_SURFACE);
    drawFolderIcon(FOLDER_X + 16, TAB_Y + 11, COLOR_DIM);
    fontDrawMiddle(FONT_BOLD, 17, FOLDER_X + 46, TAB_Y + TAB_H / 2.0f, "Folder", COLOR_DIM);
    addHit(FOLDER_X, 0, FOLDER_W, UI_TAB_BAR_H, LIBHIT_FOLDER, 0, 0);
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
static void drawCard(int x, int y, int w, int h, const char *name, const char *tag, double progress, bool focused)
{
    if (focused)
        gfxFillRoundRect(x - 4, y - 4, w + 8, h + 8, 14, COLOR_TEXT);
    Color top, bottom;
    cardColors(name, &top, &bottom);
    gfxSetClipRound(x, y, w, h, 10);
    gfxGradientV(x, y, w, h, top, bottom);
    gfxResetClip();
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
    drawCard(x, y, w, h, name, tag, ref.is_show ? -1 : watched(lib, resume, ref.index), focused);
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
            addHit(x, y + HOME_TITLE_H, w, HOME_CARD_H + 40, LIBHIT_HOME_CARD, r, c);
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
            addHit(x, y, GRID_CARD_W, GRID_CARD_H + 40, LIBHIT_GRID_CARD, i, 0);
    }
    int rows = (count + LIBVIEW_COLS - 1) / LIBVIEW_COLS;
    drawScrollbar(GRID_Y, LIBVIEW_GRID_ROWS * GRID_ROW_H - 20, s->first, LIBVIEW_GRID_ROWS, rows);
}

// --- A series' page ---

static void drawListRow(int y, bool focused, const char *number, const char *title, const char *detail,
                        double progress)
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
        addHit(x, y - 6, widths[i], 50, LIBHIT_SEASON, i, 0);
        x += widths[i] + 8;
    }
}

static void drawShow(const LibView *v, const Library *lib, const ResumeList *resume)
{
    const LibraryShow *show = &lib->shows[v->show];
    // Back, and the series' name.
    gfxLine(MARGIN + 12, 92, MARGIN + 2, 103, 3, COLOR_TEXT);
    gfxLine(MARGIN + 2, 103, MARGIN + 12, 114, 3, COLOR_TEXT);
    addHit(0, UI_TAB_BAR_H, 90, 64, LIBHIT_BACK, 0, 0);
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
        drawListRow(y, e == v->episodes.cursor, number, title, size, watched(lib, resume, item));
        addHit(0, y, GFX_WIDTH, LIST_ROW_H, LIBHIT_EPISODE, e, 0);
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
    addHit(MARGIN, SEARCH_BOX_Y - 6, w, SEARCH_BOX_H + 12, LIBHIT_SEARCH_BOX, 0, 0);
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
        drawListRow(y, i == s->cursor, NULL, title, detail, ref.is_show ? -1 : watched(lib, resume, ref.index));
        addHit(0, y, GFX_WIDTH, LIST_ROW_H, LIBHIT_RESULT, i, 0);
    }
    drawScrollbar(SEARCH_LIST_Y, LIBVIEW_LIST_ROWS * LIST_ROW_H, s->first, LIBVIEW_LIST_ROWS, v->result_count);
}

// --- The screen ---

void uiDrawLibrary(const LibView *v, const Library *lib, const Home *home, const ResumeList *resume,
                   bool folder_set, const char *status, const char *message)
{
    gfxClear(COLOR_BG);
    uiDrawTabBar(v->tab, status);
    if (!folder_set) {
        drawCentered("Choose the folder with your videos",
                     "KaiVideo finds the films and series in it and in its folders. Press - or tap Folder.");
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
    if (message && message[0]) {
        int w = fontTextWidth(FONT_BOLD, 18, message) + 48;
        if (w > GFX_WIDTH - 2 * MARGIN)
            w = GFX_WIDTH - 2 * MARGIN;
        int x = (GFX_WIDTH - w) / 2, y = GFX_HEIGHT - 70;
        gfxFillRoundRect(x, y, w, 46, 23, COLOR_SURFACE_HI);
        fontDrawClipped(FONT_BOLD, 18, x + 24, y + 10, w - 48, message, COLOR_ERROR);
    }
}
