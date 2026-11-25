#include "ui_showcase.h"

#include <stdio.h>
#include <string.h>

#include "font.h"
#include "gfx.h"
#include "lang.h"
#include "thumbs.h"
#include "ui_buttons.h"
#include "ui_library.h"
#include "ui_library_shared.h"
#include "ui_style.h"
#include "ui_tiles.h"

// The large picture: 16:9 at the top right, behind the header, fading into the background.
#define HERO_X 440
#define HERO_W (GFX_WIDTH - HERO_X)
#define HERO_H (HERO_W * 9 / 16)
#define HERO_FADE_LEFT 380    // How far its left edge fades in,
#define HERO_FADE_BOTTOM 230  // and its bottom.
#define HERO_TOP_SHADE 96     // The header reads over its top.
#define HERO_TOP_ALPHA 150
#define HERO_VEIL_ALPHA 36    // A little darker overall, for the text beside it.
#define FADE_STEP 3
#define FADE_FRAMES 14        // A picture fades in over this many frames,
#define SETTLE_FRAMES 9       // and the sharp one is asked for once the cursor rests this long.
// The text beside it.
#define TEXT_W 560
#define SUMMARY_W 520
#define KICKER_Y 84
#define TITLE_Y 104
#define TITLE_PX 40
#define TITLE_LINE 46
#define INFO_PX 18
#define SUMMARY_PX 18
#define SUMMARY_LINE 26
#define SUMMARY_LINES 3
#define BUTTON_H 40
#define BUTTON_Y 296          // At the lowest: higher when the text above is short.
#define BUTTON_GAP 22
// The rows under it: one in view, the next peeking under it.
#define ROWS_Y 356
#define ROW_TITLE_PX 19
#define TILES_DY 30           // From a row's title to its tiles.
#define DIM_ALPHA 110         // Rows away from the cursor are dimmed this much.
#define CLIP_ABOVE 10

// What the large picture shows, and what it fades from.
static struct {
    LibraryRef shown, from;
    bool has_shown, has_from;
    bool from_sharp;  // The cursor rested on the one it fades from: its sharp picture was asked for.
    int still;       // Frames the cursor has rested on it.
    int in;          // Frames its picture has been in, to FADE_FRAMES.
    int sharp_in;    // The same for its sharp (large) picture over the card's.
} g_hero;

bool uiShowcaseShown(const LibView *v, const Library *lib, const Home *home, bool folder_set)
{
    return folder_set && v->showcase && v->tab == TAB_HOME && v->show < 0 && !v->iptv && lib->count > 0 &&
           home->count > 0;
}

static bool selected(const LibView *v, const Home *home, LibraryRef *out)
{
    if (v->home_row < 0 || v->home_row >= home->count)
        return false;
    const HomeRow *row = &home->rows[v->home_row];
    int c = v->home_cols[v->home_row].cursor;
    if (c < 0 || c >= row->count)
        return false;
    *out = row->items[c];
    return true;
}

static bool sameRef(LibraryRef a, LibraryRef b)
{
    return a.is_show == b.is_show && a.index == b.index;
}

// A ref that this library has: one kept from another library (the source changed) may point past its
// end, or at series it doesn't have.
static bool refIn(const Library *lib, LibraryRef ref)
{
    if (ref.index < 0)
        return false;
    if (!ref.is_show)
        return (size_t)ref.index < lib->count;
    return (size_t)ref.index < lib->show_count && lib->shows[ref.index].count > 0;
}

// The library the large picture's refs are of: another one (a new source, or the same read again) and
// they're let go, so nothing fades from a video of the one before.
static void sameLibrary(const Library *lib)
{
    static const LibraryItem *items;
    static size_t count, show_count;
    if (lib->items == items && lib->count == count && lib->show_count == show_count)
        return;
    items = lib->items;
    count = lib->count;
    show_count = lib->show_count;
    memset(&g_hero, 0, sizeof(g_hero));
}

// --- The large picture ---

// The file or art its pictures are of: a film's or episode's own, a series' (or its first episode's).
static void artOf(const Library *lib, LibraryRef ref, const char **key, int64_t *size)
{
    const LibraryItem *it = ref.is_show ? &lib->items[lib->shows[ref.index].episodes[0]] : &lib->items[ref.index];
    if (ref.is_show && it->series_art)
        *key = it->series_art, *size = 0;
    else if (!ref.is_show && it->art)
        *key = it->art, *size = 0;
    else
        *key = it->path, *size = it->size;
}

// Its sharp picture (asked for only when sharp), or the card's while that comes. w, h: its size.
static const uint32_t *heroPicture(const Library *lib, LibraryRef ref, bool sharp, bool *is_large)
{
    const char *key;
    int64_t size;
    artOf(lib, ref, &key, &size);
    const uint32_t *large = sharp ? thumbGetLarge(key, size) : NULL;
    *is_large = large != NULL;
    return large ? large : uiLibPicture(lib, ref);
}

static void drawHeroImage(const uint32_t *pixels, bool large, unsigned alpha)
{
    if (!pixels || alpha == 0)
        return;
    gfxSetImageAlpha(alpha);
    int iw = large ? THUMB_LARGE_W : THUMB_W, ih = large ? THUMB_LARGE_H : THUMB_H;
    gfxDrawImageScaledFb(HERO_X * GFX_SCALE, 0, HERO_W * GFX_SCALE, HERO_H * GFX_SCALE, iw, ih, pixels);
    gfxSetImageAlpha(255);
}

static float smooth(float t)
{
    return t * t * (3 - 2 * t);
}

// The edges melt into the background: left, bottom, and darker under the header.
static void drawHeroFades(void)
{
    gfxBlendRect(HERO_X, 0, HERO_W, HERO_H, GFX_RGB(0, 0, 0), HERO_VEIL_ALPHA);
    for (int i = 0; i < HERO_FADE_LEFT; i += FADE_STEP)
        gfxBlendRect(HERO_X + i, 0, FADE_STEP, HERO_H, COLOR_BG,
                     (unsigned)(255 * smooth(1.0f - (float)i / HERO_FADE_LEFT)));
    for (int i = 0; i < HERO_FADE_BOTTOM; i += FADE_STEP)
        gfxBlendRect(HERO_X, HERO_H - HERO_FADE_BOTTOM + i, HERO_W, FADE_STEP, COLOR_BG,
                     (unsigned)(255 * smooth((float)i / HERO_FADE_BOTTOM)));
    for (int i = 0; i < HERO_TOP_SHADE; i += FADE_STEP)
        gfxBlendRect(HERO_X, i, HERO_W, FADE_STEP, COLOR_BG,
                     (unsigned)(HERO_TOP_ALPHA * smooth(1.0f - (float)i / HERO_TOP_SHADE)));
}

// The cursor moved: the picture shown becomes the one it fades from.
static void follow(LibraryRef ref)
{
    if (g_hero.has_shown && sameRef(g_hero.shown, ref)) {
        g_hero.still++;
        return;
    }
    g_hero.from = g_hero.shown;
    g_hero.has_from = g_hero.has_shown && g_hero.in > 0;
    g_hero.from_sharp = g_hero.still >= SETTLE_FRAMES;  // Passed over quickly: not asked for now either.
    g_hero.shown = ref;
    g_hero.has_shown = true;
    g_hero.still = 0;
    g_hero.in = 0;
    g_hero.sharp_in = 0;
}

void uiShowcaseDrawBackdrop(const LibView *v, const Library *lib, const Home *home)
{
    LibraryRef ref;
    sameLibrary(lib);
    if (!selected(v, home, &ref) || !refIn(lib, ref))
        return;
    follow(ref);
    if (g_hero.has_from && !refIn(lib, g_hero.from))
        g_hero.has_from = false;
    if (!uiLibThumbnailsOn())
        return;
    bool settled = g_hero.still >= SETTLE_FRAMES, large = false, from_large = false;
    const uint32_t *pixels = heroPicture(lib, ref, settled, &large);
    if (pixels && g_hero.in == 0 && large)
        g_hero.sharp_in = FADE_FRAMES;  // Sharp already: no second fade.
    if (g_hero.has_from && g_hero.in < FADE_FRAMES) {
        const uint32_t *from = heroPicture(lib, g_hero.from, g_hero.from_sharp, &from_large);
        drawHeroImage(from, from_large, 255);
    }
    if (pixels) {
        unsigned in = (unsigned)(255 * smooth((float)g_hero.in / FADE_FRAMES));
        if (large) {  // The card's picture under it until the sharp one is fully in.
            bool card_large;
            if (g_hero.sharp_in < FADE_FRAMES)
                drawHeroImage(heroPicture(lib, ref, false, &card_large), false, in);
            drawHeroImage(pixels, true, in * (unsigned)(255 * smooth((float)g_hero.sharp_in / FADE_FRAMES)) / 255);
            if (g_hero.sharp_in < FADE_FRAMES)
                g_hero.sharp_in++;
        } else {
            drawHeroImage(pixels, false, in);
        }
        if (g_hero.in < FADE_FRAMES)
            g_hero.in++;
    }
    drawHeroFades();
}

// --- The text beside it ---

static int yearOf(const Library *lib, LibraryRef ref)
{
    if (!ref.is_show)
        return lib->items[ref.index].show >= 0 ? 0 : lib->items[ref.index].name.year;
    const LibraryShow *s = &lib->shows[ref.index];
    int year = 0;
    for (int i = 0; i < s->count; i++) {
        int y = lib->items[s->episodes[i]].name.year;
        if (y && (!year || y < year))
            year = y;
    }
    return year;
}

static void addPart(char *out, size_t size, const char *part)
{
    if (!part || !part[0])
        return;
    size_t used = strlen(out);
    snprintf(out + used, size - used, "%s%s", used ? "  \xC2\xB7  " : "", part);
}

// "2019  ·  1 h 52 min  ·  Drama, Thriller", or with seasons and episodes for a series.
static void infoLine(const Library *lib, LibraryRef ref, char *out, size_t size)
{
    out[0] = '\0';
    char part[96];
    int year = yearOf(lib, ref);
    if (year) {
        snprintf(part, sizeof(part), "%d", year);
        addPart(out, size, part);
    }
    if (ref.is_show) {
        const LibraryShow *s = &lib->shows[ref.index];
        if (s->seasons > 1)
            snprintf(part, sizeof(part), T("%d seasons \xC2\xB7 %d episodes"), s->seasons, s->count);
        else
            snprintf(part, sizeof(part), s->count == 1 ? T("%d episode") : T("%d episodes"), s->count);
        addPart(out, size, part);
        addPart(out, size, homeShowGenres(lib, ref.index));
        return;
    }
    const LibraryItem *it = &lib->items[ref.index];
    int minutes = (int)(it->runtime_ns / 60000000000ll);
    if (minutes >= 60) {
        snprintf(part, sizeof(part), T("%d h %d min"), minutes / 60, minutes % 60);
        addPart(out, size, part);
    } else if (minutes > 0) {
        snprintf(part, sizeof(part), T("%d min"), minutes);
        addPart(out, size, part);
    }
    addPart(out, size, it->genres);
}

// The A button's pill under the text: it opens what's shown. Returns its right edge.
static int drawOpenButton(const Library *lib, const ResumeList *resume, LibraryRef ref, int y)
{
    double progress = ref.is_show ? -1 : uiLibWatched(lib, resume, ref.index);
    const char *label = ref.is_show ? T("Episodes") : progress >= 0 ? T("Resume") : T("Play");
    int x = MARGIN, w = uiButtonWidth("A") + fontTextWidth(FONT_BOLD, 19, label) + 40;
    gfxFillRoundRect(x, y, w, BUTTON_H, BUTTON_H / 2, COLOR_TEXT);
    uiDrawButton(x + 12, y + BUTTON_H / 2.0f, "A", COLOR_BG, COLOR_TEXT);
    fontDrawMiddle(FONT_BOLD, 19, x + 24 + uiButtonWidth("A"), y + BUTTON_H / 2.0f, label, COLOR_BG);
    uiLibraryAddHit(x, y, w, BUTTON_H, LIBHIT_HERO, 0, 0);
    int right = x + w;
    if (progress >= 0 && progress < 1) {  // How far it was watched, and how much is left.
        const LibraryItem *it = &lib->items[ref.index];
        int bar_x = right + 20, bar_w = 120, cy = y + BUTTON_H / 2;
        gfxFillRoundRect(bar_x, cy - 2, bar_w, 4, 2, COLOR_TRACK);
        gfxFillRoundRect(bar_x, cy - 2, (int)(bar_w * progress) > 4 ? (int)(bar_w * progress) : 4, 4, 2,
                         COLOR_ACCENT);
        int left = (int)((1 - progress) * (double)it->runtime_ns / 60000000000.0);
        if (left > 0) {
            char text[48];
            snprintf(text, sizeof(text), T("%d min left"), left);
            fontDrawMiddle(FONT_REGULAR, 16, bar_x + bar_w + 12, (float)cy, text, COLOR_DIM);
        }
    }
    return right;
}

// How many lines of SUMMARY_W the summary takes, up to max.
static int summaryLines(const char *text, int max)
{
    int w = fontTextWidth(FONT_REGULAR, SUMMARY_PX, text), lines = (w + SUMMARY_W * 9 / 10 - 1) / (SUMMARY_W * 9 / 10);
    return lines < 1 ? 1 : lines > max ? max : lines;
}

static void drawHeroText(const Library *lib, const ResumeList *resume, LibraryRef ref)
{
    const LibraryItem *it = ref.is_show ? NULL : &lib->items[ref.index];
    char kicker[160];
    if (ref.is_show)
        snprintf(kicker, sizeof(kicker), "%s", T("SERIES"));
    else if (it->show >= 0)
        uiLibEpisodeLabel(it, kicker, sizeof(kicker));
    else
        snprintf(kicker, sizeof(kicker), "%s", T("FILM"));
    gfxFillRect(MARGIN, KICKER_Y + 9, 22, 3, COLOR_ACCENT);  // The brand's short line before it.
    fontDrawClipped(FONT_BOLD, 15, MARGIN + 32, KICKER_Y, TEXT_W - 32, kicker, COLOR_ACCENT);

    const char *title = uiTileName(lib, ref);
    bool two = fontTextWidth(FONT_BOLD, TITLE_PX, title) > TEXT_W;
    uiLibDrawWrapped(FONT_BOLD, TITLE_PX, MARGIN, TITLE_Y, TEXT_W, TITLE_LINE, 2, title, COLOR_TEXT);
    int y = TITLE_Y + (two ? 2 : 1) * TITLE_LINE + 8;
    char info[256];
    infoLine(lib, ref, info, sizeof(info));
    fontDrawClipped(FONT_REGULAR, INFO_PX, MARGIN, y, TEXT_W, info, COLOR_DIM);
    y += 34;
    const char *summary = ref.is_show ? homeShowOverview(lib, ref.index) : it->overview;
    int room = (BUTTON_Y - BUTTON_GAP + 6 - y) / SUMMARY_LINE, lines = 0;
    if (summary && summary[0] && room > 0) {
        lines = summaryLines(summary, room < SUMMARY_LINES ? room : SUMMARY_LINES);
        uiLibDrawWrapped(FONT_REGULAR, SUMMARY_PX, MARGIN, y, SUMMARY_W, SUMMARY_LINE, lines, summary,
                         GFX_RGB(0xD4, 0xD6, 0xDE));
    }
    int button_y = y + lines * SUMMARY_LINE + (lines ? BUTTON_GAP - 6 : 0);
    drawOpenButton(lib, resume, ref, button_y < BUTTON_Y ? button_y : BUTTON_Y);
}

// --- The rows ---

// A row's title, with the brand's two lines under it: a short one in the accent where the cursor
// is, and a thin one on to the edge. count: "3 / 16" at its right, where the cursor is.
static void drawRowTitle(const HomeRow *row, int y, bool current, int cursor)
{
    Color color = current ? COLOR_TEXT : COLOR_DIM;
    int w = fontDraw(FONT_BOLD, ROW_TITLE_PX, MARGIN, y, homeRowName(row), color);
    int line_y = y + 12, line_x = MARGIN + w + 16;
    gfxFillRect(line_x, line_y, GFX_WIDTH - MARGIN - line_x - (current ? 72 : 0), 1, COLOR_TRACK);
    if (current) {
        gfxFillRect(line_x, line_y - 1, 28, 3, COLOR_ACCENT);
        char count[32];
        snprintf(count, sizeof(count), "%d / %d", cursor + 1, row->count);
        int cw = fontTextWidth(FONT_REGULAR, 15, count);
        fontDraw(FONT_REGULAR, 15, GFX_WIDTH - MARGIN - cw, y + 2, count, COLOR_FAINT);
    }
}

static void drawRow(const LibView *v, const Library *lib, const Home *home, const ResumeList *resume, int r, int y)
{
    const HomeRow *row = &home->rows[r];
    bool current = r == v->home_row;
    const Scroll *cols = &v->home_cols[r];
    if (y >= ROWS_Y - TILES_DY)
        drawRowTitle(row, y, current, cols->cursor);
    int col_px, in_view = libviewHomeCols(v, home, r, &col_px);
    int tile_y = y + TILES_DY, tile_w = row->wide ? TILE_WIDE_W : TILE_CASE_W;
    int tile_h = row->wide ? TILE_WIDE_H + TILE_WIDE_TEXT_H : TILE_CASE_H;
    // One more than fit, cut by the screen's edge: there's more.
    for (int c = cols->first; c < row->count && c <= cols->first + in_view + 1; c++) {
        int x = MARGIN + (c - cols->first) * col_px - cols->offset;
        if (x >= GFX_WIDTH)
            break;
        LibraryRef ref = row->items[c];
        bool focused = current && c == cols->cursor;
        double progress = ref.is_show ? -1 : uiLibWatched(lib, resume, ref.index);
        bool seen = !ref.is_show && progress < 0 && uiLibSeen(lib, ref.index);
        if (row->wide)
            uiDrawWideTile(lib, ref, x, tile_y, focused, progress, seen, row->up_next[c] ? T("UP NEXT") : NULL);
        else
            uiDrawCaseTile(lib, ref, x, tile_y, focused, progress, seen);
        int left = x < 0 ? 0 : x, right = x + tile_w > GFX_WIDTH ? GFX_WIDTH : x + tile_w;
        int top = tile_y < ROWS_Y - CLIP_ABOVE ? ROWS_Y - CLIP_ABOVE : tile_y;
        int bottom = tile_y + tile_h > UI_FOOTER_Y ? UI_FOOTER_Y : tile_y + tile_h;
        if (right > left && bottom > top)
            uiLibraryAddHit(left, top, right - left, bottom - top, LIBHIT_HOME_CARD, r, c);
    }
    if (!current)  // Quieter than the row the cursor is in.
        gfxBlendRect(0, tile_y - 8, GFX_WIDTH, tile_h + 16, COLOR_BG, DIM_ALPHA);
}

void uiShowcaseDraw(const LibView *v, const Library *lib, const Home *home, const ResumeList *resume)
{
    LibraryRef ref;
    if (selected(v, home, &ref) && refIn(lib, ref))
        drawHeroText(lib, resume, ref);
    uiTilesSetRegion(ROWS_Y - CLIP_ABOVE, UI_FOOTER_Y);
    // The row in view, and the one under it (and above it, while a finger moves them).
    for (int r = v->home_first_row - 1; r < home->count; r++) {
        if (r < 0)
            continue;
        int y = ROWS_Y + (r - v->home_first_row) * v->home_layout.row_px - v->home_offset;
        if (y >= UI_FOOTER_Y)
            break;
        if (y + v->home_layout.row_px <= ROWS_Y - CLIP_ABOVE)
            continue;
        drawRow(v, lib, home, resume, r, y);
    }
    gfxResetClip();
}
