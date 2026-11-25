// Home's Showcase in the preview: made-up posters and large pictures, a
// server's library (with genres and summaries) and the card's, in each tile
// style, and checks that what's drawn answers a tap.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "font.h"
#include "gfx.h"
#include "home.h"
#include "lang.h"
#include "library.h"
#include "libview.h"
#include "scroll.h"
#include "thumbs.h"
#include "ui_library.h"
#include "ui_style.h"
#include "showcase_scenes.h"

#define FAKE_POSTERS 8
#define DRAWS 40  // Frames drawn before a picture is saved: the large picture is settled and in.

static uint32_t g_posters[FAKE_POSTERS][THUMB_POSTER_W * THUMB_POSTER_H];
static uint32_t g_large[THUMB_LARGE_W * THUMB_LARGE_H];
static bool g_large_on;
static unsigned g_large_made = ~0u;

static unsigned hashOf(const char *text)
{
    unsigned h = 0;
    for (const char *p = text; *p; p++)
        h = h * 31 + (unsigned char)*p;
    return h;
}

static uint32_t mix(uint32_t a, uint32_t b, unsigned k)
{
    unsigned r = (((a >> 16) & 0xFF) * (255 - k) + ((b >> 16) & 0xFF) * k) / 255;
    unsigned g = (((a >> 8) & 0xFF) * (255 - k) + ((b >> 8) & 0xFF) * k) / 255;
    unsigned bl = ((a & 0xFF) * (255 - k) + (b & 0xFF) * k) / 255;
    return GFX_RGB(r, g, bl);
}

static const uint32_t SKIES[FAKE_POSTERS][3] = {
    {0x1B2A4A, 0xE07A5F, 0xF2CC8F}, {0x0B1D2C, 0x3D7EA6, 0xA8DADC}, {0x2B1B2E, 0xB5537A, 0xF6BD60},
    {0x0F2A23, 0x4E9F7A, 0xD8F3DC}, {0x1A1423, 0x6D4C9A, 0xE4C1F9}, {0x2E1A12, 0xC1663B, 0xFFD6A5},
    {0x101820, 0x4A6FA5, 0xF1FAEE}, {0x231A0E, 0x9C6B30, 0xF4E1C1},
};

// A scene w x h: a sky, a low sun or moon, and two ridges of hills; style picks the colors.
static void paintScene(uint32_t *px, int w, int h, unsigned style, bool upright)
{
    const uint32_t *c = SKIES[style % FAKE_POSTERS];
    int sun_x = (int)(w * (0.25 + (style % 5) * 0.12)), sun_y = (int)(h * (upright ? 0.38 : 0.42));
    int sun_r = upright ? w / 6 : h / 9;
    for (int y = 0; y < h; y++) {
        unsigned k = (unsigned)(y * 255 / h);
        uint32_t sky = k < 160 ? mix(c[0], c[1], k * 255 / 160) : mix(c[1], c[2], (k - 160) * 255 / 95);
        for (int x = 0; x < w; x++) {
            int dx = x - sun_x, dy = y - sun_y;
            float t = (float)x / w;
            int far = (int)(h * (0.62 + 0.06 * sinf(t * 7.0f + style)));
            int near = (int)(h * (0.74 + 0.05 * sinf(t * 4.3f + style * 2)));
            uint32_t p = sky;
            if (dx * dx + dy * dy < sun_r * sun_r)
                p = mix(c[2], 0xFFFFFF, 120);
            if (y > far)
                p = mix(c[0], 0x000000, 90);
            if (y > near)
                p = mix(c[0], 0x000000, 170);
            px[y * w + x] = p;
        }
    }
}

// Posters: the scene, a dark band at the bottom where a poster's title would be, and a thin line.
static void makePosters(void)
{
    for (int i = 0; i < FAKE_POSTERS; i++) {
        uint32_t *p = g_posters[i];
        paintScene(p, THUMB_POSTER_W, THUMB_POSTER_H, (unsigned)i, true);
        for (int y = THUMB_POSTER_H * 3 / 4; y < THUMB_POSTER_H; y++)
            for (int x = 0; x < THUMB_POSTER_W; x++)
                p[y * THUMB_POSTER_W + x] = mix(p[y * THUMB_POSTER_W + x] & 0xFFFFFF, 0x000000, 150) | 0xFF000000u;
        for (int y = 0; y < 6; y++)  // Block letters' stand-in: bars of a title.
            for (int x = 24; x < THUMB_POSTER_W - 24 - (i % 3) * 20; x++)
                p[(THUMB_POSTER_H - 52 + y) * THUMB_POSTER_W + x] = GFX_RGB(0xF2, 0xF2, 0xF5);
        for (int y = 0; y < 3; y++)
            for (int x = 24; x < 110; x++)
                p[(THUMB_POSTER_H - 36 + y) * THUMB_POSTER_W + x] = GFX_RGB(0xB0, 0xB0, 0xBA);
    }
}

const uint32_t *thumbGetPoster(const char *art)
{
    if (strncmp(art, LIBRARY_POSTER_PREFIX, sizeof(LIBRARY_POSTER_PREFIX) - 1))
        return NULL;  // Only the made-up server's have posters.
    return g_posters[hashOf(art) % FAKE_POSTERS];
}

bool thumbPosterIsNone(const char *art)
{
    return thumbGetPoster(art) == NULL;
}

const uint32_t *fakeLarge(const char *path)
{
    if (!g_large_on)
        return NULL;
    unsigned style = hashOf(path) % FAKE_POSTERS;
    if (style != g_large_made) {
        paintScene(g_large, THUMB_LARGE_W, THUMB_LARGE_H, style, false);
        g_large_made = style;
    }
    return g_large;
}

// --- Libraries ---

static const char *const GENRES[] = {"Drama, Mystery", "Comedy", "Drama", "Science Fiction, Drama",
                                     "Comedy, Family", "Thriller, Mystery", "Documentary", "Western, Drama"};
static const char SUMMARY[] =
    "When the last road out of the valley closes, a fire lookout and a retired surveyor have one week to find "
    "the family who stayed behind, before the ash reaches the river.";
static const char SERIES_SUMMARY[] =
    "A small-town radio host keeps the lights on for a county that time forgot, one late-night call at a time.";

// The sample library as a server gives it: posters, genres, summaries, lengths and favorites.
static void makeServerLibrary(Library *lib, const Library *from)
{
    libraryInit(lib, "jellyfin:");
    for (size_t i = 0; i < from->count; i++) {
        const LibraryItem *it = &from->items[i];
        char art[160];
        const char *name = it->show >= 0 ? from->shows[it->show].name : it->name.title;
        snprintf(art, sizeof(art), "%shttps://posters.invalid/%s", LIBRARY_POSTER_PREFIX, name);
        bool first = it->show >= 0 && from->shows[it->show].episodes[0] == (int)i;
        LibraryRemote remote = {.art = it->show >= 0 ? NULL : art, .series_art = it->show >= 0 ? art : NULL,
                                .overview = SUMMARY, .genres = GENRES[hashOf(name) % 8],
                                .series_overview = first ? SERIES_SUMMARY : NULL,
                                .runtime_ns = (it->show >= 0 ? 44ll : 112ll) * 60000000000ll,
                                .favorite = i % 7 == 0};
        VideoName named = it->name;
        if (!named.year)
            named.year = 2010 + (int)(hashOf(name) % 14);
        libraryAddRemote(lib, it->path, &named, it->size, it->added, &remote);
    }
    libraryBuild(lib);
}

// --- Scenes ---

static void draw(const ShowcaseCtx *c, const char *name, const LibView *v, const Library *lib, const Home *home,
                 int frames)
{
    for (int i = 0; i < frames; i++) {
        c->begin();
        uiDrawLibrary(v, lib, home, c->resume, true, NULL);
        uiDrawFooterHelp(T("[A] open  [Y] search  [ZL][ZR] tabs  [-] settings  [+] menu"));
    }
    c->save(name);
}

static void problem(const ShowcaseCtx *c, const char *name, const char *what)
{
    printf("TOUCH: %s: %s\n", name, what);
    (*c->problems)++;
}

// Every tile drawn in the row under the cursor answers a tap with its own place; so does the A button.
static void checkTaps(const ShowcaseCtx *c, const char *name, const LibView *v, const Home *home)
{
    int row = v->home_row, found = 0;
    for (int y = 360; y < UI_FOOTER_Y; y += 6)
        for (int x = 0; x < GFX_WIDTH; x += 6) {
            LibHit hit = uiLibraryTap(x, y);
            if (hit.kind == LIBHIT_HOME_CARD && hit.a == row && hit.b >= 0 && hit.b < 32)
                found |= 1 << hit.b;
        }
    const Scroll *cols = &v->home_cols[row];
    int in_view = libviewHomeCols(v, home, row, NULL);
    for (int i = cols->first; i < home->rows[row].count && i < cols->first + in_view; i++)
        if (!(found & (1 << i)))
            problem(c, name, "a tile in view doesn't answer a tap");
    bool hero = false;
    for (int y = 160; y < 356 && !hero; y += 4)
        hero = uiLibraryTap(MARGIN + 30, y).kind == LIBHIT_HERO;
    if (!hero)
        problem(c, name, "the large picture's button doesn't answer a tap");
    if (uiLibraryTap(700, 200).kind == LIBHIT_HOME_CARD)
        problem(c, name, "a tile answers a tap over the large picture");
}

static int rowOf(const Home *home, HomeRowKind kind)
{
    for (int r = 0; r < home->count; r++)
        if (home->rows[r].kind == kind)
            return r;
    return 0;
}

static void moveTo(LibView *v, const Home *home, int row, int col)
{
    libviewHomeMove(v, home, 0, row - v->home_row);
    libviewHomeMove(v, home, col - v->home_cols[v->home_row].cursor, 0);
}

void showcaseScenes(const ShowcaseCtx *c)
{
    makePosters();
    uiLibrarySetThumbnails(true);
    g_large_on = true;
    static Library server;
    static Home home;
    static LibView view;
    makeServerLibrary(&server, c->sd);
    libviewInit(&view);
    libviewSetTabs(&view, &server, true, false, false);
    libviewSetHomeShowcase(&view, true);
    uiLibrarySetSource("Jellyfin", uiServerColor(SERVER_JELLYFIN));
    Color chosen = g_colors.accent;
    g_colors.accent = uiServerColor(SERVER_JELLYFIN);

    homeComputeShowcase(&home, &server, c->resume, c->watched);
    homeSetTiles(&home, HOME_TILES_MIXED);
    draw(c, "showcase_server", &view, &server, &home, DRAWS);
    checkTaps(c, "showcase_server", &view, &home);

    homeSetTiles(&home, HOME_TILES_POSTERS);
    moveTo(&view, &home, rowOf(&home, HOME_FILMS), 2);
    draw(c, "showcase_server_posters", &view, &server, &home, DRAWS);
    checkTaps(c, "showcase_server_posters", &view, &home);

    homeSetTiles(&home, HOME_TILES_LANDSCAPE);
    libviewClamp(&view, &server, &home);
    moveTo(&view, &home, rowOf(&home, HOME_GENRE), 1);
    draw(c, "showcase_server_landscape", &view, &server, &home, DRAWS);
    checkTaps(c, "showcase_server_landscape", &view, &home);

    homeSetTiles(&home, HOME_TILES_MIXED);
    libviewClamp(&view, &server, &home);
    moveTo(&view, &home, rowOf(&home, HOME_SERIES), 4);
    draw(c, "showcase_server_series", &view, &server, &home, DRAWS);
    // Just after the cursor moved: the picture fading from the last one.
    moveTo(&view, &home, rowOf(&home, HOME_SERIES), 5);
    draw(c, "showcase_server_fading", &view, &server, &home, 6);
    // Left between rows by a finger: nothing drawn over the large picture's text.
    libviewHomeScrollPx(&view, &home, 120);
    draw(c, "showcase_server_between", &view, &server, &home, DRAWS);
    if (uiLibraryTap(MARGIN + 40, 330).kind == LIBHIT_HOME_CARD)
        problem(c, "showcase_server_between", "a tile answers a tap above the rows");
    g_colors.accent = chosen;

    // The card's library: frames from the videos, no genres.
    static Home sd_home;
    static LibView sd_view;
    libviewInit(&sd_view);
    libviewSetHomeShowcase(&sd_view, true);
    uiLibrarySetSource(T("SD card"), COLOR_ACCENT);
    homeComputeShowcase(&sd_home, c->sd, c->resume, c->watched);
    homeSetTiles(&sd_home, HOME_TILES_MIXED);
    draw(c, "showcase_sd", &sd_view, c->sd, &sd_home, DRAWS);
    checkTaps(c, "showcase_sd", &sd_view, &sd_home);
    moveTo(&sd_view, &sd_home, rowOf(&sd_home, HOME_FILMS), 1);
    draw(c, "showcase_sd_films", &sd_view, c->sd, &sd_home, DRAWS);
    checkTaps(c, "showcase_sd_films", &sd_view, &sd_home);

    uiLibrarySetSource("", COLOR_ACCENT);
    g_large_on = false;
    libraryFree(&server);
}
