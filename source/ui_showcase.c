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
#include "ui_showcase_parts.h"
#include "ui_style.h"
#include "ui_tiles.h"
#include "xtreamvod.h"

#define HERO_FADE_LEFT 380    // How far the large picture's left edge fades in,
#define HERO_FADE_BOTTOM 230  // and its bottom.
#define HERO_TOP_SHADE 96     // The header reads over its top.
#define HERO_TOP_ALPHA 150
#define HERO_VEIL_ALPHA 36    // A little darker overall, for the text beside it.
#define FADE_STEP 3
#define FADE_FRAMES 14        // A picture fades in over this many frames,
#define SETTLE_FRAMES 9       // and the sharp one is asked for once the cursor rests this long.
#define ROW_TITLE_PX 19
#define TILES_DY 30           // From a row's title to its tiles.
#define DIM_ALPHA 110         // Rows away from the cursor are dimmed this much.
#define CLIP_ABOVE 10
#define CARD_BACK GFX_RGB(0x1C, 0x1C, 0x21)  // What a channel's logo card is around its logo (thumbgen.c).

// What the large picture shows: a film or series of the library, or a channel of the IPTV list.
typedef struct {
    LibraryRef ref;
    bool channel;  // ref.index is a channel.
} HeroRef;

// How it shows: a wide picture, or a picture whole on a blur of its colors (a logo, a poster).
typedef enum { LOOK_PICTURE, LOOK_PLATE } HeroLook;

typedef struct {
    const uint32_t *pixels;  // NULL: the name on its colors, or (waiting) on its way.
    int w, h;
    bool upright;
    bool waiting;
    const char *name;
} Plate;

enum { SHOWN, FROM };

// What the large picture shows, and what it fades from.
static struct {
    HeroRef shown, from;
    bool has_shown, has_from;
    bool from_sharp;  // The cursor rested on the one it fades from: its sharp picture was asked for.
    int still;        // Frames the cursor has rested on it.
    int in;           // Frames its picture has been in, to FADE_FRAMES.
    int sharp_in;     // The same for its sharp (large) picture over the card's.
    char key[LIBRARY_PATH_MAX];  // Its path or name, to find it again when the library is made again.
    ArtColors colors[2];         // The blur of SHOWN's and FROM's picture,
    const void *colors_of[2];    // and what each was made from (NULL: not made).
} g_hero;

static UiShowcaseChannels g_ch;
static bool g_has_ch;

void uiShowcaseSetChannels(const UiShowcaseChannels *channels)
{
    g_has_ch = channels && channels->list;
    if (g_has_ch)
        g_ch = *channels;
    else
        memset(&g_ch, 0, sizeof(g_ch));
}

bool uiShowcaseShown(const LibView *v, const Library *lib, const Home *home, bool folder_set)
{
    (void)lib;
    return folder_set && v->showcase && v->tab == TAB_HOME && v->show < 0 && home->count > 0;
}

static bool selected(const LibView *v, const Home *home, HeroRef *out)
{
    if (v->home_row < 0 || v->home_row >= home->count)
        return false;
    const HomeRow *row = &home->rows[v->home_row];
    int c = v->home_cols[v->home_row].cursor;
    if (c < 0 || c >= row->count)
        return false;
    out->ref = row->items[c];
    out->channel = row->channels;
    return true;
}

static bool sameRef(HeroRef a, HeroRef b)
{
    return a.channel == b.channel && a.ref.is_show == b.ref.is_show && a.ref.index == b.ref.index;
}

// A ref that this library (or list) has: one kept from another (the source changed) may point past its
// end, or at series it doesn't have.
static bool refIn(const Library *lib, HeroRef h)
{
    LibraryRef ref = h.ref;
    if (ref.index < 0)
        return false;
    if (h.channel)
        return g_has_ch && ref.index < g_ch.list->count;
    if (!ref.is_show)
        return (size_t)ref.index < lib->count;
    return (size_t)ref.index < lib->show_count && lib->shows[ref.index].count > 0;
}

static void keyOf(const Library *lib, HeroRef h, char *out, size_t size)
{
    const char *key = h.channel       ? g_ch.list->channels[h.ref.index].name
                      : h.ref.is_show ? lib->shows[h.ref.index].name
                                      : lib->items[h.ref.index].path;
    snprintf(out, size, "%s", key);
}

// What the large picture showed, in a library (or list) made again: by its path or name.
static bool findAgain(const Library *lib, HeroRef *h)
{
    if (h->channel) {
        for (int i = 0; g_has_ch && i < g_ch.list->count; i++)
            if (!strcmp(g_ch.list->channels[i].name, g_hero.key)) {
                h->ref.index = i;
                return true;
            }
        return false;
    }
    if (h->ref.is_show) {
        for (size_t i = 0; i < lib->show_count; i++)
            if (lib->shows[i].count > 0 && !strcmp(lib->shows[i].name, g_hero.key)) {
                h->ref.index = (int)i;
                return true;
            }
        return false;
    }
    h->ref.index = libraryFind(lib, g_hero.key);
    return h->ref.index >= 0;
}

// The library and list the large picture's refs are of: another one (a new source, or the same read
// again) and they're found again by path or name (it goes on as it was), or let go, so nothing fades
// from a video or channel of the one before.
static void sameSource(const Library *lib)
{
    static const LibraryItem *items;
    static size_t count, show_count;
    static const IptvChannel *channels;
    static int channel_count;
    const IptvChannel *now_channels = g_has_ch ? g_ch.list->channels : NULL;
    int now_channel_count = g_has_ch ? g_ch.list->count : 0;
    if (lib->items == items && lib->count == count && lib->show_count == show_count &&
        now_channels == channels && now_channel_count == channel_count)
        return;
    items = lib->items;
    count = lib->count;
    show_count = lib->show_count;
    channels = now_channels;
    channel_count = now_channel_count;
    HeroRef found = g_hero.shown;
    if (g_hero.has_shown && findAgain(lib, &found)) {
        g_hero.shown = found;
        g_hero.has_from = false;
        g_hero.colors_of[SHOWN] = g_hero.colors_of[FROM] = NULL;  // Made again from what's there now.
        return;
    }
    memset(&g_hero, 0, sizeof(g_hero));
}

// --- The large picture ---

// The art of a film or series: its first item (a series' first episode).
static const LibraryItem *itemOf(const Library *lib, LibraryRef ref)
{
    return ref.is_show ? &lib->items[lib->shows[ref.index].episodes[0]] : &lib->items[ref.index];
}

// An Xtream film's or series' poster when it has no wide picture: shown whole, on a blur of it.
static const char *plateArt(const Library *lib, HeroRef h)
{
    const LibraryItem *it = itemOf(lib, h.ref);
    return !it->backdrop && xtreamIsLocation(it->path) ? uiTilePosterArt(lib, h.ref) : NULL;
}

static HeroLook lookOf(const Library *lib, HeroRef h)
{
    return h.channel || plateArt(lib, h) ? LOOK_PLATE : LOOK_PICTURE;
}

static Plate plateOf(const Library *lib, HeroRef h)
{
    Plate p = {NULL, THUMB_W, THUMB_H, false, false, NULL};
    if (h.channel) {
        const IptvChannel *c = &g_ch.list->channels[h.ref.index];
        p.name = c->name;
        p.pixels = c->logo[0] ? thumbGet(c->logo, 0) : NULL;
        p.waiting = !p.pixels && c->logo[0] && !thumbIsNone(c->logo, 0);
        return p;
    }
    const char *art = plateArt(lib, h);
    p.name = uiTileName(lib, h.ref);
    p.upright = true;
    p.w = THUMB_POSTER_W;
    p.h = THUMB_POSTER_H;
    p.pixels = art ? thumbGetPoster(art) : NULL;
    p.waiting = !p.pixels && art && !thumbPosterIsNone(art);
    return p;
}

// The blur's colors for a plate, made once for each picture (slot: SHOWN or FROM).
static const ArtColors *plateColors(const Plate *p, int slot)
{
    static ArtColors waiting;
    if (p->waiting) {
        for (int y = 0; y < ART_GRID_H; y++)
            for (int x = 0; x < ART_GRID_W; x++)
                waiting.cells[y][x] = COLOR_BG;
        return &waiting;
    }
    const void *of = p->pixels ? (const void *)p->pixels : (const void *)p->name;
    if (g_hero.colors_of[slot] != of) {
        Color top, bottom;
        uiLibCardColors(p->name, &top, &bottom);
        if (!p->pixels || !artColorsFrom(p->pixels, p->w, p->h, p->upright ? ART_NO_IGNORE : CARD_BACK,
                                         &g_hero.colors[slot]))
            artColorsOfTwo(top, bottom, &g_hero.colors[slot]);
        g_hero.colors_of[slot] = of;
    }
    return &g_hero.colors[slot];
}

// The file or art a library ref's pictures are of: its wide picture, a film's or episode's own, a
// series' (or its first episode's).
static void artOf(const Library *lib, LibraryRef ref, const char **key, int64_t *size)
{
    const LibraryItem *it = itemOf(lib, ref);
    if (it->backdrop)
        *key = it->backdrop, *size = 0;
    else if (ref.is_show && it->series_art)
        *key = it->series_art, *size = 0;
    else if (!ref.is_show && it->art)
        *key = it->art, *size = 0;
    else
        *key = it->path, *size = it->size;
}

// Its sharp picture (asked for only when sharp), or the card's while that comes.
static const uint32_t *heroPicture(const Library *lib, LibraryRef ref, bool sharp, bool *is_large)
{
    const char *key;
    int64_t size;
    artOf(lib, ref, &key, &size);
    const uint32_t *large = sharp ? thumbGetLarge(key, size) : NULL;
    *is_large = large != NULL;
    if (large)
        return large;
    return itemOf(lib, ref)->backdrop ? thumbGet(key, 0) : uiLibPicture(lib, ref);
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

float uiShowcaseSmooth(float t)
{
    return t * t * (3 - 2 * t);
}

// The edges melt into the background: left, bottom, and darker under the header.
static void drawHeroFades(void)
{
    gfxBlendRect(HERO_X, 0, HERO_W, HERO_H, GFX_RGB(0, 0, 0), HERO_VEIL_ALPHA);
    for (int i = 0; i < HERO_FADE_LEFT; i += FADE_STEP)
        gfxBlendRect(HERO_X + i, 0, FADE_STEP, HERO_H, COLOR_BG,
                     (unsigned)(255 * uiShowcaseSmooth(1.0f - (float)i / HERO_FADE_LEFT)));
    for (int i = 0; i < HERO_FADE_BOTTOM; i += FADE_STEP)
        gfxBlendRect(HERO_X, HERO_H - HERO_FADE_BOTTOM + i, HERO_W, FADE_STEP, COLOR_BG,
                     (unsigned)(255 * uiShowcaseSmooth((float)i / HERO_FADE_BOTTOM)));
    for (int i = 0; i < HERO_TOP_SHADE; i += FADE_STEP)
        gfxBlendRect(HERO_X, i, HERO_W, FADE_STEP, COLOR_BG,
                     (unsigned)(HERO_TOP_ALPHA * uiShowcaseSmooth(1.0f - (float)i / HERO_TOP_SHADE)));
}

// The cursor moved: the picture shown becomes the one it fades from.
static void follow(const Library *lib, HeroRef ref)
{
    if (g_hero.has_shown && sameRef(g_hero.shown, ref)) {
        g_hero.still++;
        return;
    }
    g_hero.from = g_hero.shown;
    g_hero.has_from = g_hero.has_shown && g_hero.in > 0;
    g_hero.from_sharp = g_hero.still >= SETTLE_FRAMES;  // Passed over quickly: not asked for now either.
    g_hero.colors[FROM] = g_hero.colors[SHOWN];
    g_hero.colors_of[FROM] = g_hero.colors_of[SHOWN];
    g_hero.colors_of[SHOWN] = NULL;
    g_hero.shown = ref;
    g_hero.has_shown = true;
    g_hero.still = 0;
    g_hero.in = 0;
    g_hero.sharp_in = 0;
    keyOf(lib, ref, g_hero.key, sizeof(g_hero.key));
}

// A wide picture (the library's): the card's, then the sharp one over it.
static void drawPicture(const Library *lib, LibraryRef ref, unsigned in)
{
    bool settled = g_hero.still >= SETTLE_FRAMES, large = false;
    const uint32_t *pixels = heroPicture(lib, ref, settled, &large);
    if (!pixels)
        return;
    if (g_hero.in == 0 && large)
        g_hero.sharp_in = FADE_FRAMES;  // Sharp already: no second fade.
    if (large) {  // The card's picture under it until the sharp one is fully in.
        bool card_large;
        if (g_hero.sharp_in < FADE_FRAMES)
            drawHeroImage(heroPicture(lib, ref, false, &card_large), false, in);
        drawHeroImage(pixels, true,
                      in * (unsigned)(255 * uiShowcaseSmooth((float)g_hero.sharp_in / FADE_FRAMES)) / 255);
        if (g_hero.sharp_in < FADE_FRAMES)
            g_hero.sharp_in++;
    } else {
        drawHeroImage(pixels, false, in);
    }
}

void uiShowcaseDrawBackdrop(const LibView *v, const Library *lib, const Home *home)
{
    HeroRef ref;
    sameSource(lib);
    if (!selected(v, home, &ref) || !refIn(lib, ref))
        return;
    follow(lib, ref);
    if (g_hero.has_from && !refIn(lib, g_hero.from))
        g_hero.has_from = false;
    if (!uiLibThumbnailsOn())
        return;
    unsigned in = (unsigned)(255 * uiShowcaseSmooth((float)g_hero.in / FADE_FRAMES));
    bool fading = g_hero.has_from && g_hero.in < FADE_FRAMES;
    HeroLook look = lookOf(lib, ref), from_look = fading ? lookOf(lib, g_hero.from) : LOOK_PICTURE;
    Plate from_plate = {0};
    const ArtColors *from_colors = NULL;
    if (fading && from_look == LOOK_PLATE) {
        from_plate = plateOf(lib, g_hero.from);
        from_colors = plateColors(&from_plate, FROM);
    }
    if (look == LOOK_PICTURE) {
        bool from_large;
        if (fading && from_look == LOOK_PICTURE)
            drawHeroImage(heroPicture(lib, g_hero.from.ref, g_hero.from_sharp, &from_large), from_large, 255);
        else if (from_colors)
            uiShowcaseDrawBlur(from_colors);
        drawPicture(lib, ref.ref, in);
        drawHeroFades();
        if (from_colors)  // The picture it fades from goes as this one comes.
            uiShowcaseDrawPlate(from_plate.pixels, from_plate.w, from_plate.h, from_plate.upright,
                                from_plate.name, 255 - in);
    } else {
        Plate plate = plateOf(lib, ref);
        ArtColors base, blur;
        if (from_colors) {
            base = *from_colors;
        } else {  // From a wide picture, or nothing: out of the background.
            for (int y = 0; y < ART_GRID_H; y++)
                for (int x = 0; x < ART_GRID_W; x++)
                    base.cells[y][x] = COLOR_BG;
        }
        artColorsMix(&base, plateColors(&plate, SHOWN), in, &blur);
        uiShowcaseDrawBlur(&blur);
        drawHeroFades();
        if (from_colors)
            uiShowcaseDrawPlate(from_plate.pixels, from_plate.w, from_plate.h, from_plate.upright,
                                from_plate.name, 255 - in);
        if (!plate.waiting)
            uiShowcaseDrawPlate(plate.pixels, plate.w, plate.h, plate.upright, plate.name, in);
    }
    if (g_hero.in < FADE_FRAMES && (look == LOOK_PLATE || heroPicture(lib, ref.ref, false, &(bool){0})))
        g_hero.in++;
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

void uiShowcaseAddPart(char *out, size_t size, const char *part)
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
        uiShowcaseAddPart(out, size, part);
    }
    if (ref.is_show) {
        const LibraryShow *s = &lib->shows[ref.index];
        bool stand_in = s->count == 1 && xtreamStandInShow(lib->items[s->episodes[0]].path) > 0;
        if (stand_in)  // An Xtream series whose episodes aren't read yet: how many isn't known.
            part[0] = '\0';
        else if (s->seasons > 1)
            snprintf(part, sizeof(part), T("%d seasons \xC2\xB7 %d episodes"), s->seasons, s->count);
        else
            snprintf(part, sizeof(part), s->count == 1 ? T("%d episode") : T("%d episodes"), s->count);
        uiShowcaseAddPart(out, size, part);
        uiShowcaseAddPart(out, size, homeShowGenres(lib, ref.index));
        return;
    }
    const LibraryItem *it = &lib->items[ref.index];
    int minutes = (int)(it->runtime_ns / 60000000000ll);
    if (minutes >= 60) {
        snprintf(part, sizeof(part), T("%d h %d min"), minutes / 60, minutes % 60);
        uiShowcaseAddPart(out, size, part);
    } else if (minutes > 0) {
        snprintf(part, sizeof(part), T("%d min"), minutes);
        uiShowcaseAddPart(out, size, part);
    }
    uiShowcaseAddPart(out, size, it->genres);
}

int uiShowcaseDrawPill(int x, int y, const char *button, const char *label, bool primary, LibHitKind hit)
{
    int w = uiButtonWidth(button) + fontTextWidth(FONT_BOLD, 19, label) + 40;
    Color fill = primary ? COLOR_TEXT : COLOR_SURFACE_HI, text = primary ? COLOR_BG : COLOR_TEXT;
    gfxFillRoundRect(x, y, w, BUTTON_H, BUTTON_H / 2, fill);
    uiDrawButton(x + 12, y + BUTTON_H / 2.0f, button, primary ? COLOR_BG : COLOR_SURFACE, COLOR_TEXT);
    fontDrawMiddle(FONT_BOLD, 19, x + 24 + uiButtonWidth(button), y + BUTTON_H / 2.0f, label, text);
    uiLibraryAddHit(x, y, w, BUTTON_H, hit, 0, 0);
    return w;
}

// The A button's pill under the text: it opens what's shown, with how far it was watched.
static void drawOpenButton(const Library *lib, const ResumeList *resume, LibraryRef ref, int y)
{
    double progress = ref.is_show ? -1 : uiLibWatched(lib, resume, ref.index);
    const char *label = ref.is_show ? T("Episodes") : progress >= 0 ? T("Resume") : T("Play");
    int right = MARGIN + uiShowcaseDrawPill(MARGIN, y, "A", label, true, LIBHIT_HERO);
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
}

int uiShowcaseSummaryLines(const char *text, int max)
{
    int w = fontTextWidth(FONT_REGULAR, SUMMARY_PX, text), lines = (w + SUMMARY_W * 9 / 10 - 1) / (SUMMARY_W * 9 / 10);
    return lines < 1 ? 1 : lines > max ? max : lines;
}

int uiShowcaseDrawTitle(const char *kicker, const char *title)
{
    gfxFillRect(MARGIN, KICKER_Y + 9, 22, 3, COLOR_ACCENT);  // The brand's short line before it.
    fontDrawClipped(FONT_BOLD, 15, MARGIN + 32, KICKER_Y, TEXT_W - 32, kicker, COLOR_ACCENT);
    bool two = fontTextWidth(FONT_BOLD, TITLE_PX, title) > TEXT_W;
    uiLibDrawWrapped(FONT_BOLD, TITLE_PX, MARGIN, TITLE_Y, TEXT_W, TITLE_LINE, 2, title, COLOR_TEXT);
    return TITLE_Y + (two ? 2 : 1) * TITLE_LINE + 8;
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
    int y = uiShowcaseDrawTitle(kicker, uiTileName(lib, ref));
    char info[256];
    infoLine(lib, ref, info, sizeof(info));
    fontDrawClipped(FONT_REGULAR, INFO_PX, MARGIN, y, TEXT_W, info, COLOR_DIM);
    y += 34;
    const char *summary = ref.is_show ? homeShowOverview(lib, ref.index) : it->overview;
    int room = (BUTTON_Y - BUTTON_GAP + 6 - y) / SUMMARY_LINE, lines = 0;
    if (summary && summary[0] && room > 0) {
        lines = uiShowcaseSummaryLines(summary, room < SUMMARY_LINES ? room : SUMMARY_LINES);
        uiLibDrawWrapped(FONT_REGULAR, SUMMARY_PX, MARGIN, y, SUMMARY_W, SUMMARY_LINE, lines, summary, SUMMARY_COLOR);
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

// A channel's tile: its logo, its name, what's on and how far into it.
static void drawChannelTile(int channel, int x, int y, bool focused)
{
    UiProgram now = {0}, next = {0};
    g_ch.programs(g_ch.ctx, channel, &now, &next);
    double progress = now.title && now.title[0] && now.end > now.start && g_ch.now_s >= now.start
                          ? (double)(g_ch.now_s - now.start) / (double)(now.end - now.start)
                          : -1;
    uiDrawChannelTile(&g_ch.list->channels[channel], now.title, progress, x, y, focused);
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
        HeroRef h = {row->items[c], row->channels};
        if (!refIn(lib, h))
            continue;
        bool focused = current && c == cols->cursor;
        if (row->channels) {
            drawChannelTile(h.ref.index, x, tile_y, focused);
        } else {
            LibraryRef ref = h.ref;
            double progress = ref.is_show ? -1 : uiLibWatched(lib, resume, ref.index);
            bool seen = !ref.is_show && progress < 0 && uiLibSeen(lib, ref.index);
            if (row->wide)
                uiDrawWideTile(lib, ref, x, tile_y, focused, progress, seen, row->up_next[c] ? T("UP NEXT") : NULL);
            else
                uiDrawCaseTile(lib, ref, x, tile_y, focused, progress, seen);
        }
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
    HeroRef ref;
    if (selected(v, home, &ref) && refIn(lib, ref)) {
        if (ref.channel)
            uiShowcaseDrawChannelText(&g_ch, ref.ref.index);
        else
            drawHeroText(lib, resume, ref.ref);
    }
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
