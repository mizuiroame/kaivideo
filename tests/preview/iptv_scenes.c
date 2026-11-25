// The IPTV list's Showcase in the preview: made-up channels with logos and a
// guide (an M3U list's), an Xtream account's with films and series, and X
// going between it and the other sources, a frame or two each (run it with
// sanitizers: the large picture must never keep a place of another list).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "home.h"
#include "iptvhome.h"
#include "lang.h"
#include "libview.h"
#include "thumbs.h"
#include "ui_library.h"
#include "ui_showcase.h"
#include "ui_style.h"
#include "xtreamvod.h"
#include "showcase_scenes.h"

#define DRAWS 40
#define LOGO_PREFIX "https://logo.invalid/"
#define NOW_S 1795600800  // 2026-11-25 13:20 UTC: the guide's programs are around it.
#define LOGOS 6

static uint32_t g_logos[LOGOS][THUMB_W * THUMB_H];
static bool g_logos_made;

static unsigned hashOf(const char *text)
{
    unsigned h = 0;
    for (const char *p = text; *p; p++)
        h = h * 31 + (unsigned char)*p;
    return h;
}

// A logo as thumbgen.c makes one: on the cards' dark color, a brand's colored badge with a white
// mark and two bars of "letters", in the middle 80%.
static void makeLogos(void)
{
    static const Color FILL[LOGOS] = {GFX_RGB(0xD6, 0x28, 0x3C), GFX_RGB(0x1F, 0x6F, 0xD8), GFX_RGB(0x1E, 0xA3, 0x5A),
                                      GFX_RGB(0xF2, 0x9E, 0x1C), GFX_RGB(0x7A, 0x3C, 0xC8), GFX_RGB(0x0F, 0xA8, 0xB8)};
    for (int l = 0; l < LOGOS; l++) {
        uint32_t *px = g_logos[l];
        int bx = 70 + (l % 3) * 10, bw = THUMB_W - 2 * bx, by = 52, bh = THUMB_H - 2 * by;
        for (int y = 0; y < THUMB_H; y++)
            for (int x = 0; x < THUMB_W; x++) {
                uint32_t p = GFX_RGB(0x1C, 0x1C, 0x21);
                bool badge = x >= bx && x < bx + bw && y >= by && y < by + bh;
                int cx = bx + bh / 2, cy = by + bh / 2, dx = x - cx, dy = y - cy;
                if (badge)
                    p = l % 2 && x > bx + bw * 2 / 3 ? GFX_RGB(0xF5, 0xF5, 0xF8) : FILL[l];
                if (badge && dx * dx + dy * dy < (bh / 3) * (bh / 3))
                    p = GFX_RGB(0xFF, 0xFF, 0xFF);
                bool bar1 = y >= cy - 14 && y < cy - 2 && x >= cx + bh / 2 && x < bx + bw * 2 / 3 - 10;
                bool bar2 = y >= cy + 4 && y < cy + 12 && x >= cx + bh / 2 && x < bx + bw / 2 + 10;
                if (badge && (bar1 || bar2))
                    p = GFX_RGB(0xFF, 0xFF, 0xFF);
                px[y * THUMB_W + x] = p;
            }
    }
    g_logos_made = true;
}

const uint32_t *fakeLogo(const char *path)
{
    if (strncmp(path, LOGO_PREFIX, sizeof(LOGO_PREFIX) - 1))
        return NULL;
    if (!g_logos_made)
        makeLogos();
    return g_logos[hashOf(path) % LOGOS];
}

// --- The channels and their guide ---

static const char *const NAMES[] = {"Canal Uno HD",   "Noticias 24",    "Deportes Max", "Kids Planet",
                                    "Musica Viva",    "Cine Clasico",   "Docu World",   "Local Siete",
                                    "Noticias Norte", "Gol TV",         "Teleaventura", "Nature One",
                                    "Comedia Central de Toda la Ciudad y Alrededores", "Retro TV"};
#define CHANNELS (int)(sizeof(NAMES) / sizeof(NAMES[0]))
static const int GROUP_OF[CHANNELS] = {0, 0, 1, 2, 3, 4, 5, 0, 0, 1, 2, 5, 4, 4};
static const char *const SHOWS[] = {"Noticiero Matutino", "Resumen Deportivo", "El Gran Partido", "Aventuras en el Bosque",
                                    "Top 40 de la Semana", "Casablanca", "Oceanos Profundos", "Hoy en la Ciudad"};
static const char DESC[] =
    "Las noticias de la mañana: lo que pasó en el país y el mundo, el clima y el tránsito, con entrevistas "
    "en el estudio y reportes en vivo desde la calle.";

static void programs(const void *ctx, int channel, UiProgram *now, UiProgram *next)
{
    (void)ctx;
    memset(now, 0, sizeof(*now));
    memset(next, 0, sizeof(*next));
    if (channel % 5 == 4)
        return;  // No guide for it.
    now->title = SHOWS[channel % 8];
    now->desc = channel % 3 == 1 ? NULL : DESC;
    now->start = NOW_S - 600 - 300 * (channel % 4);
    now->end = now->start + 3600;
    next->title = SHOWS[(channel + 3) % 8];
    next->start = now->end;
    next->end = now->end + 1800;
}

static bool g_favorites[CHANNELS];

static bool favorite(const void *ctx, int channel)
{
    (void)ctx;
    return channel >= 0 && channel < CHANNELS && g_favorites[channel];
}

static IptvGroup GROUPS[] = {{"Noticias", "", 4}, {"Deportes", "", 2}, {"Infantil", "", 2},
                             {"Música", "", 1},   {"Cine", "", 3},     {"Documentales", "", 2}};

// A list of count channels (malloc'd: freed after it, as a list read again is).
static IptvList makeList(int count)
{
    IptvList list;
    memset(&list, 0, sizeof(list));
    list.channels = calloc((size_t)count, sizeof(IptvChannel));
    list.count = count;
    list.groups = GROUPS;
    list.group_count = 6;
    for (int i = 0; i < count; i++) {
        IptvChannel *ch = &list.channels[i];
        snprintf(ch->name, sizeof(ch->name), "%s", NAMES[i % CHANNELS]);
        if (i % 7 != 6)  // Some have no logo: their name on its colors.
            snprintf(ch->logo, sizeof(ch->logo), "%s%d.png", LOGO_PREFIX, i);
        snprintf(ch->number, sizeof(ch->number), "%d", i + 1);
        ch->group = GROUP_OF[i % CHANNELS];
    }
    return list;
}

static void useChannels(const IptvList *list)
{
    UiShowcaseChannels ch = {list, programs, favorite, NULL, NOW_S, -6 * 3600, true};
    uiShowcaseSetChannels(list ? &ch : NULL);
}

// --- An Xtream account's films and series ---

static void makeCatalog(XtreamCatalog *c, Library *lib)
{
    memset(c, 0, sizeof(*c));
    xtreamParseVodCategories("[{\"category_id\":\"1\",\"category_name\":\"Acción\"},"
                             "{\"category_id\":\"2\",\"category_name\":\"Comedia\"},"
                             "{\"category_id\":\"3\",\"category_name\":\"Drama\"}]",
                             c, false);
    char films[8192];
    size_t n = (size_t)snprintf(films, sizeof(films), "[");
    static const char *const TITLES[] = {"Mi Suegra es Astronauta", "Tacos a Domicilio", "La Última Frontera",
                                         "Noche de Tormenta (2019)", "El Faro del Norte", "Código Rojo",
                                         "Doble Categoría", "Viaje al Centro"};
    for (int i = 0; i < 8; i++)
        n += (size_t)snprintf(films + n, sizeof(films) - n,
                              "%s{\"stream_id\":%d,\"name\":\"%s\",\"stream_icon\":\"https://posters.invalid/f%d.jpg\","
                              "\"added\":\"%d\",\"category_id\":\"%d\",\"container_extension\":\"mkv\"}",
                              i ? "," : "", 100 + i, TITLES[i], i, 1700000000 + i * 1000, i % 3 + 1);
    snprintf(films + n, sizeof(films) - n, "]");
    xtreamParseFilms(films, c);
    xtreamParseShows(
        "[{\"series_id\":1001,\"name\":\"La Casa del Faro\",\"cover\":\"https://posters.invalid/s1.jpg\","
        "\"plot\":\"Dos hermanas heredan un faro en una isla y descubren que el último farero nunca se fue.\","
        "\"genre\":\"Drama, Misterio\",\"releaseDate\":\"2024-03-01\",\"last_modified\":\"1735693200\"},"
        "{\"series_id\":1002,\"name\":\"Detectives de Barrio\",\"cover\":\"https://posters.invalid/s2.jpg\","
        "\"plot\":\"Comedia policial.\",\"genre\":\"Comedia\",\"last_modified\":\"1735693100\"}]",
        c);
    XtreamFilmInfo info;  // The film the large picture rested on: its details were read.
    xtreamParseFilmInfo("{\"info\":{\"plot\":\"Una suegra muy decidida se cuela en la primera misión tripulada a "
                        "Marte, y la familia tiene que traerla de vuelta antes de la cena de Navidad.\","
                        "\"genre\":\"Comedia, Familia\",\"duration_secs\":5820}}",
                        &info);
    xtreamFilmSetInfo(c, 100, &info);
    xtreamParseFilmInfo("{\"info\":{\"plot\":\"Un detective y una tormenta.\",\"genre\":\"Thriller\","
                        "\"duration_secs\":6300,\"backdrop_path\":[\"https://backdrops.invalid/f3.jpg\"]}}",
                        &info);
    xtreamFilmSetInfo(c, 103, &info);  // One with a wide picture.
    libraryInit(lib, "iptv:");
    xtreamFillLibrary(lib, c, T("Reading the episodes..."), T("The episodes couldn't be read."));
    libraryBuild(lib);
}

static int rowOf(const Home *home, HomeRowKind kind)
{
    for (int r = 0; r < home->count; r++)
        if (home->rows[r].kind == kind)
            return r;
    return 0;
}

static void checkFavoriteTap(const ShowcaseCtx *c, const char *name)
{
    bool found = false;
    for (int y = 160; y < 356 && !found; y += 4)
        for (int x = MARGIN; x < 640 && !found; x += 8)
            found = uiLibraryTap(x, y).kind == LIBHIT_FAVORITE;
    if (!found)
        showcaseProblem(c, name, "the channel's favorite button doesn't answer a tap");
}

static void drawOnce(const ShowcaseCtx *c, const LibView *v, const Library *lib, const Home *home)
{
    c->begin();
    uiDrawLibrary(v, lib, home, c->resume, true, NULL);
}

void iptvShowcaseScenes(const ShowcaseCtx *c, const Library *sd, const Library *server, const LibView *sd_view,
                        const Home *sd_home)
{
    uiLibrarySetSource("IPTV", UI_IPTV_COLOR);
    static IptvMarks marks;
    iptvMarksInit(&marks);
    iptvMarksWatched(&marks, "Gol TV");
    iptvMarksWatched(&marks, "Kids Planet");
    iptvMarksWatched(&marks, "Noticias 24");
    iptvMarksToggleFavorite(&marks, "Noticias 24");
    iptvMarksToggleFavorite(&marks, "Docu World");
    g_favorites[1] = g_favorites[6] = true;

    // An M3U list: channels only.
    IptvList list = makeList(CHANNELS);
    static IptvHome channels;
    iptvHomeBuild(&channels, &list, &marks);
    static Library empty;
    libraryInit(&empty, "iptv:");
    libraryBuild(&empty);
    static Home home;
    static LibView view;
    libviewInit(&view);
    libviewSetIptvTabs(&view, &empty);
    libviewSetHomeShowcase(&view, true);
    homeComputeIptv(&home, &channels, &list, &empty, NULL, 0);
    homeSetTiles(&home, HOME_TILES_MIXED);
    libviewClamp(&view, &empty, &home);
    useChannels(&list);
    showcaseDraw(c, "showcase_iptv_m3u", &view, &empty, &home, DRAWS);
    showcaseCheckTaps(c, "showcase_iptv_m3u", &view, &home);
    checkFavoriteTap(c, "showcase_iptv_m3u");
    if (libviewSelectedChannel(&view, &home) != 1)
        showcaseProblem(c, "showcase_iptv_m3u", "A isn't on Noticias 24, first in Recently watched");
    // A group's row, on a channel without a logo; then one with no guide.
    showcaseMoveTo(&view, &home, rowOf(&home, HOME_CHANNELS), 3);
    showcaseDraw(c, "showcase_iptv_m3u_group", &view, &empty, &home, DRAWS);
    showcaseCheckTaps(c, "showcase_iptv_m3u_group", &view, &home);
    showcaseMoveTo(&view, &home, rowOf(&home, HOME_CHANNELS_FAVORITES), 1);
    showcaseDraw(c, "showcase_iptv_m3u_favorite", &view, &empty, &home, DRAWS);
    for (int r = 0; r < home.count; r++)  // Every channel under the cursor in turn: a long name, no logo, no guide.
        for (int col = 0; col < home.rows[r].count; col++) {
            showcaseMoveTo(&view, &home, r, col);
            drawOnce(c, &view, &empty, &home);
        }
    showcaseMoveTo(&view, &home, rowOf(&home, HOME_CHANNELS) + 4, 0);
    showcaseDraw(c, "showcase_iptv_m3u_long", &view, &empty, &home, DRAWS);

    // An Xtream account: channels, then films (posters whole on their colors), series and categories.
    static XtreamCatalog catalog;
    static Library vod;
    makeCatalog(&catalog, &vod);
    static Home xhome;
    static LibView xview;
    libviewInit(&xview);
    libviewSetIptvTabs(&xview, &vod);
    libviewSetHomeShowcase(&xview, true);
    homeComputeIptv(&xhome, &channels, &list, &vod, catalog.film_groups, catalog.film_group_count);
    homeSetTiles(&xhome, HOME_TILES_MIXED);
    libviewClamp(&xview, &vod, &xhome);
    showcaseMoveTo(&xview, &xhome, rowOf(&xhome, HOME_FILMS), 0);
    while (strcmp(vod.items[xhome.rows[xview.home_row].items[xview.home_cols[xview.home_row].cursor].index].name.title,
                  "Mi Suegra es Astronauta") &&
           xview.home_cols[xview.home_row].cursor + 1 < xhome.rows[xview.home_row].count)
        showcaseMoveTo(&xview, &xhome, xview.home_row, xview.home_cols[xview.home_row].cursor + 1);
    showcaseDraw(c, "showcase_iptv_xtream_film", &xview, &vod, &xhome, DRAWS);
    showcaseCheckTaps(c, "showcase_iptv_xtream_film", &xview, &xhome);
    // Just after the cursor moved to the next film: one poster fading into the next.
    showcaseMoveTo(&xview, &xhome, xview.home_row, xview.home_cols[xview.home_row].cursor + 1);
    showcaseDraw(c, "showcase_iptv_xtream_fading", &xview, &vod, &xhome, 6);
    for (int i = 0; i < xhome.rows[xview.home_row].count; i++) {  // The one with a wide picture.
        LibraryRef ref = xhome.rows[xview.home_row].items[i];
        if (vod.items[ref.index].backdrop)
            showcaseMoveTo(&xview, &xhome, xview.home_row, i);
    }
    showcaseDraw(c, "showcase_iptv_xtream_backdrop", &xview, &vod, &xhome, DRAWS);
    showcaseMoveTo(&xview, &xhome, rowOf(&xhome, HOME_SERIES), 0);
    showcaseDraw(c, "showcase_iptv_xtream_series", &xview, &vod, &xhome, DRAWS);
    showcaseCheckTaps(c, "showcase_iptv_xtream_series", &xview, &xhome);
    homeSetTiles(&xhome, HOME_TILES_LANDSCAPE);
    libviewClamp(&xview, &vod, &xhome);
    showcaseMoveTo(&xview, &xhome, rowOf(&xhome, HOME_CATEGORY), 1);
    showcaseDraw(c, "showcase_iptv_xtream_category", &xview, &vod, &xhome, DRAWS);
    showcaseCheckTaps(c, "showcase_iptv_xtream_category", &xview, &xhome);
    // From a channel to a film: the logo's blur fading into the poster's.
    showcaseMoveTo(&xview, &xhome, rowOf(&xhome, HOME_CHANNELS), 1);
    showcaseDraw(c, "showcase_iptv_xtream_channel", &xview, &vod, &xhome, DRAWS);
    showcaseMoveTo(&xview, &xhome, rowOf(&xhome, HOME_FILMS), 1);
    showcaseDraw(c, "showcase_iptv_xtream_to_film", &xview, &vod, &xhome, 5);

    // X again and again: the IPTV view on its last channel, the server, the card, and the list read again
    // shorter (the channel shown is past its end), the films gone; each a frame or two, mid-fade.
    static Home server_home;
    static LibView server_view;
    libviewInit(&server_view);
    libviewSetTabs(&server_view, server, true, false, false);
    libviewSetHomeShowcase(&server_view, true);
    homeComputeShowcase(&server_home, server, c->resume, c->watched);
    homeSetTiles(&server_home, HOME_TILES_MIXED);
    for (int round = 0; round < 8; round++) {
        int row = rowOf(&xhome, round % 2 ? HOME_SERIES : HOME_CHANNELS);
        showcaseMoveTo(&xview, &xhome, row, xhome.rows[row].count - 1);
        useChannels(&list);
        for (int f = 0; f < 1 + round % 3; f++)
            drawOnce(c, &xview, &vod, &xhome);
        useChannels(NULL);
        drawOnce(c, &server_view, server, &server_home);
        drawOnce(c, sd_view, sd, sd_home);
        // The list read again, shorter; Home made again with it.
        iptvHomeFree(&channels);
        free(list.channels);
        list = makeList(round % 2 ? CHANNELS : 3);
        iptvHomeBuild(&channels, &list, &marks);
        useChannels(&list);
        homeComputeIptv(&home, &channels, &list, &empty, NULL, 0);
        libviewClamp(&view, &empty, &home);
        drawOnce(c, &view, &empty, &home);
        homeComputeIptv(&xhome, &channels, &list, &vod, catalog.film_groups, catalog.film_group_count);
        homeSetTiles(&xhome, HOME_TILES_MIXED);
        libviewClamp(&xview, &vod, &xhome);
    }
    // The films and series read again (a film's details came): the same one stays in the large picture.
    libraryFree(&vod);
    libraryInit(&vod, "iptv:");
    xtreamFillLibrary(&vod, &catalog, T("Reading the episodes..."), T("The episodes couldn't be read."));
    libraryBuild(&vod);
    drawOnce(c, &xview, &vod, &xhome);

    useChannels(NULL);
    iptvHomeFree(&channels);
    free(list.channels);
    libraryFree(&vod);
    libraryFree(&empty);
    xtreamCatalogFree(&catalog);
}
