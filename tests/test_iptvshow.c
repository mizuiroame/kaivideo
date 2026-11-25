// The IPTV list's Showcase: its rows (channels, films, series, categories),
// the cursor keeping its row when rows come or go, the blur's colors, the
// guide's descriptions, and an Xtream series' genres, year and pictures.
#include <stdlib.h>
#include <string.h>

#include "artcolors.h"
#include "check.h"
#include "home.h"
#include "iptvhome.h"
#include "libview.h"
#include "xmltv.h"
#include "xtreamvod.h"

static const char M3U[] = "#EXTM3U\n"
                          "#EXTINF:-1 group-title=\"News\",Alpha\nhttps://s.example/a.m3u8\n"
                          "#EXTINF:-1 group-title=\"Sports\",Bravo\nhttps://s.example/b.m3u8\n"
                          "#EXTINF:-1 group-title=\"News\",Charlie\nhttps://s.example/c.m3u8\n";

static const char CATEGORIES[] = "[{\"category_id\":\"1\",\"category_name\":\"Action\"},"
                                 "{\"category_id\":\"2\",\"category_name\":\"Comedy\"},"
                                 "{\"category_id\":\"3\",\"category_name\":\"Empty\"}]";
static const char FILMS[] =
    "[{\"stream_id\":11,\"name\":\"Old Action (2001)\",\"added\":\"100\",\"category_id\":\"1\"},"
    "{\"stream_id\":12,\"name\":\"New Action\",\"added\":\"300\",\"category_id\":\"1\"},"
    "{\"stream_id\":13,\"name\":\"A Comedy\",\"added\":\"200\",\"category_id\":\"2\"}]";
static const char SHOWS[] =
    "[{\"series_id\":21,\"name\":\"Lighthouse\",\"plot\":\"Keepers.\",\"genre\":\"Drama, Mystery\","
    "\"releaseDate\":\"2024-03-01\",\"backdrop_path\":[\"ftp://no\",\"https://img.example/b.jpg\"],"
    "\"last_modified\":\"400\",\"category_id\":\"5\"}]";

static void makeCatalog(XtreamCatalog *c, Library *lib)
{
    memset(c, 0, sizeof(*c));
    CHECK(xtreamParseVodCategories(CATEGORIES, c, false));
    CHECK(xtreamParseFilms(FILMS, c));
    CHECK(xtreamParseShows(SHOWS, c));
    libraryInit(lib, "iptv:");
    xtreamFillLibrary(lib, c, "Reading", "Failed");
    libraryBuild(lib);
}

static int rowOf(const Home *h, HomeRowKind kind, const char *name)
{
    for (int r = 0; r < h->count; r++)
        if (h->rows[r].kind == kind && (!name || !strcmp(homeRowName(&h->rows[r]), name)))
            return r;
    return -1;
}

static void testRows(void)
{
    IptvList list;
    memset(&list, 0, sizeof(list));
    CHECK(iptvParseM3u(M3U, strlen(M3U), &list));
    IptvMarks *marks = malloc(sizeof(*marks));
    iptvMarksInit(marks);
    iptvMarksWatched(marks, "Charlie");
    IptvHome channels = {0};
    iptvHomeBuild(&channels, &list, marks);
    XtreamCatalog c;
    static Library lib;
    makeCatalog(&c, &lib);
    static Home home;
    homeComputeIptv(&home, &channels, &list, &lib, c.film_groups, c.film_group_count);

    // Recently watched, News, Sports; Films, Series; Action, Comedy (not Empty: no films in it).
    CHECK(home.count == 7);
    CHECK(home.rows[0].kind == HOME_CHANNELS_RECENT && home.rows[0].channels && home.rows[0].count == 1 &&
          home.rows[0].items[0].index == 2);
    int news = rowOf(&home, HOME_CHANNELS, "News");
    CHECK(news == 1 && home.rows[news].count == 2 && home.rows[news].channels);
    CHECK(rowOf(&home, HOME_CHANNELS, "Sports") == 2);
    int films = rowOf(&home, HOME_FILMS, NULL), series = rowOf(&home, HOME_SERIES, NULL);
    CHECK(films == 3 && !home.rows[films].channels && home.rows[films].count == 3);
    CHECK(!strcmp(lib.items[home.rows[films].items[0].index].name.title, "New Action"));  // The newest first.
    CHECK(series == 4 && home.rows[series].count == 1 && home.rows[series].items[0].is_show);
    int action = rowOf(&home, HOME_CATEGORY, "Action");
    CHECK(action == 5 && home.rows[action].count == 2 && rowOf(&home, HOME_CATEGORY, "Comedy") == 6);
    CHECK(rowOf(&home, HOME_CATEGORY, "Empty") < 0);

    // Channels' rows are wide whatever the tiles; the films' follow the setting.
    homeSetTiles(&home, HOME_TILES_POSTERS);
    CHECK(home.rows[news].wide && !home.rows[films].wide);
    homeSetTiles(&home, HOME_TILES_LANDSCAPE);
    CHECK(home.rows[films].wide);

    // A on a channel's row is a channel, not a library item.
    static LibView v;
    libviewInit(&v);
    libviewSetIptvTabs(&v, &lib);
    libviewSetHomeShowcase(&v, true);
    v.home_row = news;
    v.home_cols[news].cursor = 1;
    LibraryRef ref;
    CHECK(libviewSelectedChannel(&v, &home) == 2 && !libviewSelected(&v, &lib, &home, &ref));
    v.home_row = films;
    v.home_cols[films].cursor = 0;
    CHECK(libviewSelectedChannel(&v, &home) == -1 && libviewSelected(&v, &lib, &home, &ref) && !ref.is_show);

    // Favorite channels come above the groups: the cursor stays on Sports, where it was in it.
    v.home_row = 2;
    v.home_cols[2].cursor = 0;
    v.home_cols[films].cursor = 2;
    static Home before;
    before = home;
    iptvMarksToggleFavorite(marks, "Bravo");
    iptvHomeBuild(&channels, &list, marks);
    homeComputeIptv(&home, &channels, &list, &lib, c.film_groups, c.film_group_count);
    CHECK(home.count == 8 && home.rows[1].kind == HOME_CHANNELS_FAVORITES);
    libviewHomeRebuilt(&v, &before, &home);
    CHECK(v.home_row == 3 && !strcmp(homeRowName(&home.rows[3]), "Sports"));
    CHECK(v.home_cols[films + 1].cursor == 2);  // Films moved down one, its place with it.
    CHECK(homeFindRow(&home, &before.rows[0]) == 0 && homeFindRow(&before, &home.rows[1]) == -1);

    // No list: only the films and series.
    IptvList none;
    memset(&none, 0, sizeof(none));
    IptvHome no_channels = {0};
    iptvHomeBuild(&no_channels, &none, marks);
    homeComputeIptv(&home, &no_channels, &none, &lib, NULL, 0);
    CHECK(home.count == 2 && home.rows[0].kind == HOME_FILMS);

    iptvHomeFree(&channels);
    iptvHomeFree(&no_channels);
    iptvListFree(&list);
    libraryFree(&lib);
    xtreamCatalogFree(&c);
    free(marks);
}

static void testSeries(void)
{
    XtreamCatalog c;
    static Library lib;
    makeCatalog(&c, &lib);
    const XtreamShow *s = &c.shows[0];
    CHECK(s->year == 2024 && !strcmp(s->genre, "Drama, Mystery") && !strcmp(s->backdrop, "https://img.example/b.jpg"));
    CHECK(lib.show_count == 1);
    const LibraryItem *stand_in = &lib.items[lib.shows[0].episodes[0]];
    CHECK(!strcmp(homeShowGenres(&lib, 0), "Drama, Mystery") && !strcmp(homeShowOverview(&lib, 0), "Keepers."));
    CHECK(stand_in->name.year == 2024 && !strcmp(stand_in->backdrop, "poster:https://img.example/b.jpg"));
    XtreamFilmInfo info;
    CHECK(xtreamParseFilmInfo("{\"info\":{\"plot\":\"P\",\"backdrop_path\":\"https://img.example/w.jpg\"}}", &info));
    CHECK(info.backdrop && !strcmp(info.backdrop, "https://img.example/w.jpg"));
    xtreamFilmInfoFree(&info);
    CHECK(xtreamParseFilmInfo("{\"info\":{\"backdrop_path\":[]}}", &info) && !info.backdrop);
    xtreamFilmInfoFree(&info);
    libraryFree(&lib);
    xtreamCatalogFree(&c);
}

static void testColors(void)
{
    enum { W = 60, H = 40 };
    static uint32_t px[W * H];
    const uint32_t back = 0xFF211C1Cu, red = 0xFF2020E0u, blue = 0xFFE02020u;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)  // A logo: red on the left, blue on the right, on the cards' color.
            px[y * W + x] = y < 8 || y >= 32 ? back : x < W / 2 ? red : blue;
    ArtColors a;
    CHECK(artColorsFrom(px, W, H, back, &a));
    uint32_t left = a.cells[ART_GRID_H / 2][0], right = a.cells[ART_GRID_H / 2][ART_GRID_W - 1];
    CHECK((left & 0xFF) > ((left >> 16) & 0xFF) && ((right >> 16) & 0xFF) > (right & 0xFF));  // Red, blue.
    CHECK((left & 0xFF) <= 150);  // Darkened.
    uint32_t top = a.cells[0][0];  // Only the card's color there: leans on the logo's colors, not gray.
    CHECK(top != 0xFF211C1Cu && ((top & 0xFF) > 0x30 || ((top >> 16) & 0xFF) > 0x30));
    for (int i = 0; i < W * H; i++)
        px[i] = back;
    CHECK(!artColorsFrom(px, W, H, back, &a));  // Nothing but the card.
    CHECK(!artColorsFrom(NULL, 0, 0, ART_NO_IGNORE, &a));
    ArtColors two, mixed;
    artColorsOfTwo(red, blue, &two);
    artColorsMix(&a, &two, 255, &mixed);
    CHECK(mixed.cells[0][0] == two.cells[0][0]);
    artColorsMix(&a, &two, 0, &mixed);
    CHECK(mixed.cells[0][0] == a.cells[0][0]);
}

static void testDescriptions(void)
{
    static const char *const IDS[] = {"one", "two"};
    static const char GUIDE[] =
        "<tv><programme start=\"20261122090000 +0000\" stop=\"20261122100000 +0000\" channel=\"one\">"
        "<title>Morning</title><desc lang=\"es\">The news &amp; weather.</desc><desc>Second</desc></programme>"
        "<programme start=\"20261122100000 +0000\" stop=\"20261122110000 +0000\" channel=\"one\">"
        "<desc>Desc before title</desc><title>Cooking</title></programme>"
        "<programme start=\"20261122200000 +0000\" stop=\"20261122210000 +0000\" channel=\"one\">"
        "<title>Late</title><desc>Too far ahead</desc></programme></tv>";
    const int64_t nine = 1795338000;  // 2026-11-22 09:00 UTC.
    XmltvReader *r = xmltvBegin(IDS, 2, nine - 3600, nine + 24 * 3600);
    xmltvKeepDescriptions(r, nine + 6 * 3600);
    size_t length = strlen(GUIDE);
    for (size_t at = 0; at < length; at += 11)
        CHECK(xmltvFeed(r, GUIDE + at, length - at < 11 ? length - at : 11));
    int count;
    const LiveProgram *p = xmltvPrograms(r, 0, &count);
    CHECK(count == 3 && p[0].start == nine && !strcmp(p[1].title, "Cooking"));
    CHECK(!strcmp(xmltvDescription(r, 0, 0), "The news & weather."));
    CHECK(!strcmp(xmltvDescription(r, 0, 1), "Desc before title"));
    CHECK(xmltvDescription(r, 0, 2) == NULL);  // Past the window kept.
    CHECK(xmltvDescription(r, 1, 0) == NULL && xmltvDescription(r, 0, 9) == NULL);
    xmltvFree(r);
    r = xmltvBegin(IDS, 2, nine - 3600, nine + 24 * 3600);  // Not asked: none kept.
    xmltvFeed(r, GUIDE, length);
    CHECK(xmltvDescription(r, 0, 0) == NULL);
    xmltvFree(r);
}

void testIptvShowcase(void)
{
    testRows();
    testSeries();
    testColors();
    testDescriptions();
}
