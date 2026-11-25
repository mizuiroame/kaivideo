// Home's Showcase: genres read from the servers' answers and kept by series,
// its rows (genre grouping included), its tiles, its layout and its settings.
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "genres.h"
#include "home.h"
#include "jellyfin.h"
#include "libview.h"
#include "plex.h"
#include "settings.h"
#include "stremio.h"
#include "thumbgen.h"

static void testJoin(void)
{
    char out[GENRES_TEXT_MAX];
    json_t *strings = json_loads("[\"Drama\",\"Comedy\",\"\",\"Thriller\",\"Western\"]", 0, NULL);
    genresJoin(strings, NULL, out, sizeof(out));
    CHECK(strcmp(out, "Drama, Comedy, Thriller") == 0);  // Empty left out, at most three.
    json_decref(strings);
    json_t *tags = json_loads("[{\"tag\":\"Animation\"},{\"tag\":\"Bad, Name\"},{\"id\":3}]", 0, NULL);
    genresJoin(tags, "tag", out, sizeof(out));
    CHECK(strcmp(out, "Animation") == 0);  // A comma would split it; one without a tag is skipped.
    json_decref(tags);
    genresJoin(NULL, NULL, out, sizeof(out));
    CHECK(out[0] == '\0');
    char small[12];
    json_t *two = json_loads("[\"Drama\",\"Documentary\"]", 0, NULL);
    genresJoin(two, NULL, small, sizeof(small));
    CHECK(strcmp(small, "Drama") == 0);  // The one that doesn't fit whole is left out.
    json_decref(two);

    char one[32];
    CHECK(genresAt("Drama, Science Fiction", 1, one, sizeof(one)) && strcmp(one, "Science Fiction") == 0);
    CHECK(genresAt(" Drama ,Comedy", 0, one, sizeof(one)) && strcmp(one, "Drama") == 0);
    CHECK(!genresAt("Drama", 1, one, sizeof(one)) && !genresAt(NULL, 0, one, sizeof(one)));
}

static void testTable(void)
{
    GenreTable t = {0};
    char id[16];
    for (int i = 200; i > 0; i--) {  // Out of order, and growing past its first room.
        snprintf(id, sizeof(id), "s%03d", i);
        CHECK(genreTableAdd(&t, id, i % 2 ? "Drama" : "", i % 2 ? NULL : "A summary"));
    }
    CHECK(genreTableAdd(&t, "empty", "", ""));  // Neither: skipped, not an error.
    CHECK(t.count == 200);
    int at = genreTableFind(&t, "s007");
    CHECK(at >= 0 && strcmp(genreTableGenres(&t, at), "Drama") == 0 && genreTableTakeOverview(&t, at) == NULL);
    at = genreTableFind(&t, "s008");
    CHECK(at >= 0 && genreTableGenres(&t, at) == NULL);
    const char *summary = genreTableTakeOverview(&t, at);
    CHECK(summary && strcmp(summary, "A summary") == 0);
    CHECK(genreTableTakeOverview(&t, at) == NULL);  // Once: its first episode keeps it.
    CHECK(genreTableFind(&t, "s999") == -1 && genreTableFind(&t, "empty") == -1 && genreTableFind(&t, NULL) == -1);
    CHECK(!genreTableAdd(&t, "an-id-far-too-long-for-the-table-to-keep-it-whole", "Drama", NULL));
    genreTableFree(&t);
    CHECK(t.count == 0 && genreTableFind(&t, "s007") == -1);
}

static const char SERIES[] =
    "{\"Items\":[{\"Id\":\"aaaa1111bbbb2222cccc3333dddd4444\",\"Genres\":[\"Comedy\",\"Family\"],"
    "\"Overview\":\"A radio host.\"}],\"TotalRecordCount\":1}";
static const char ITEMS[] =
    "{\"Items\":["
    "{\"Id\":\"1111aaaa2222bbbb3333cccc4444dddd\",\"Type\":\"Episode\",\"Name\":\"Pilot\",\"SeriesName\":\"Rook\","
    "\"SeriesId\":\"aaaa1111bbbb2222cccc3333dddd4444\",\"ParentIndexNumber\":1,\"IndexNumber\":1,\"Genres\":[],"
    "\"MediaSources\":[{\"Id\":\"1111aaaa2222bbbb3333cccc4444dddd\",\"MediaStreams\":[{\"Type\":\"Video\","
    "\"Codec\":\"h264\"}]}]},"
    "{\"Id\":\"5555aaaa2222bbbb3333cccc4444dddd\",\"Type\":\"Episode\",\"Name\":\"Two\",\"SeriesName\":\"Rook\","
    "\"SeriesId\":\"aaaa1111bbbb2222cccc3333dddd4444\",\"ParentIndexNumber\":1,\"IndexNumber\":2,"
    "\"MediaSources\":[{\"Id\":\"5555aaaa2222bbbb3333cccc4444dddd\",\"MediaStreams\":[{\"Type\":\"Video\","
    "\"Codec\":\"h264\"}]}]},"
    "{\"Id\":\"9999aaaa2222bbbb3333cccc4444dddd\",\"Type\":\"Movie\",\"Name\":\"Ashfall\",\"ProductionYear\":2010,"
    "\"Genres\":[\"Drama\",\"Thriller\"],"
    "\"MediaSources\":[{\"Id\":\"9999aaaa2222bbbb3333cccc4444dddd\",\"MediaStreams\":[{\"Type\":\"Video\","
    "\"Codec\":\"h264\"}]}]}],\"TotalRecordCount\":3}";

static void testJellyfinGenres(void)
{
    JfCatalog c;
    jfCatalogInit(&c);
    CHECK(jfCatalogAddSeriesGenres(&c, SERIES) == 1);
    CHECK(jfCatalogAddSeriesGenres(&c, "nonsense") == -1);
    CHECK(jfCatalogAddPage(&c, ITEMS, NULL) == 3 && c.library.count == 3);
    const LibraryItem *pilot = &c.library.items[0], *two = &c.library.items[1], *film = &c.library.items[2];
    CHECK(pilot->genres && strcmp(pilot->genres, "Comedy, Family") == 0);  // Its series'.
    CHECK(pilot->series_overview && strcmp(pilot->series_overview, "A radio host.") == 0);
    CHECK(two->genres && !two->series_overview);  // The summary is kept once.
    CHECK(film->genres && strcmp(film->genres, "Drama, Thriller") == 0 && !film->series_overview);

    // Kept between runs, and an older cache line without them still reads.
    const char *file = TMP_ROOT "/showcase_catalog.txt";
    system("mkdir -p " TMP_ROOT);
    CHECK(jfCatalogSave(&c, file));
    JfCatalog back;
    jfCatalogInit(&back);
    CHECK(jfCatalogLoad(&back, file) && back.library.count == 3);
    int found = 0;
    for (size_t i = 0; i < back.library.count; i++) {
        const LibraryItem *it = &back.library.items[i];
        if (it->series_overview && strcmp(it->series_overview, "A radio host.") == 0 && it->genres &&
            strcmp(it->genres, "Comedy, Family") == 0)
            found++;
        if (!strcmp(it->name.title, "Ashfall") && it->genres && !strcmp(it->genres, "Drama, Thriller"))
            found++;
    }
    CHECK(found == 2);
    jfCatalogFree(&back);
    jfCatalogFree(&c);

    FILE *f = fopen(file, "w");
    CHECK(f != NULL);
    if (f) {
        fprintf(f, "kaivideo-jellyfin-3\n1\t0\t0\t0\t0\t0\t0\t2001\t0\t0\t0\t"
                   "jellyfin:/9999aaaa2222bbbb3333cccc4444dddd/9999aaaa2222bbbb3333cccc4444dddd\tOld\t\t\t\t0\t0\n");
        fclose(f);
    }
    jfCatalogInit(&back);
    CHECK(jfCatalogLoad(&back, file) && back.library.count == 1 && !back.library.items[0].genres &&
          !back.library.items[0].series_overview);
    jfCatalogFree(&back);
}

static void testPlexGenres(void)
{
    JfCatalog c;
    jfCatalogInit(&c);
    static const char SHOWS[] = "{\"MediaContainer\":{\"size\":1,\"Metadata\":[{\"type\":\"show\",\"ratingKey\":\"77\","
                                "\"summary\":\"Two cooks.\",\"Genre\":[{\"tag\":\"Comedy\"}]}]}}";
    static const char EPISODE[] =
        "{\"MediaContainer\":{\"size\":1,\"Metadata\":[{\"type\":\"episode\",\"ratingKey\":\"501\","
        "\"title\":\"Pilot\",\"grandparentTitle\":\"Kitchen\",\"grandparentRatingKey\":\"77\",\"parentIndex\":1,"
        "\"index\":1,\"Media\":[{\"Part\":[{\"id\":9001,\"size\":10}]}]}]}}";
    CHECK(plexCatalogAddPage(&c, SHOWS, NULL) == 1 && c.library.count == 0);  // Series give only their genres.
    CHECK(plexCatalogAddPage(&c, EPISODE, NULL) == 1 && c.library.count == 1);
    CHECK(c.library.items[0].genres && strcmp(c.library.items[0].genres, "Comedy") == 0);
    CHECK(c.library.items[0].series_overview && strcmp(c.library.items[0].series_overview, "Two cooks.") == 0);
    jfCatalogFree(&c);
}

static void testStremioGenres(void)
{
    StremioMeta *m = NULL;
    int n = 0;
    CHECK(stremioParseMetas("{\"metas\":[{\"id\":\"kt1\",\"type\":\"movie\",\"name\":\"Paper Boats\","
                            "\"genres\":[\"Animation\",\"Family\"]},{\"id\":\"kt2\",\"type\":\"series\","
                            "\"name\":\"Old\",\"genre\":[\"Drama\"]}]}",
                            &m, &n));
    CHECK(n == 2 && strcmp(m[0].genres, "Animation, Family") == 0 && strcmp(m[1].genres, "Drama") == 0);
    stremioMetasFree(m, n);
}

// A server's library: films and series with genres, the newest added last.
static void addVideo(Library *lib, const char *path, const char *show, int episode, const char *genres,
                     int64_t added)
{
    VideoName name = {.is_episode = show != NULL, .season = 1, .episode = episode};
    snprintf(name.title, sizeof(name.title), "%s", show ? "Ep" : strrchr(path, '/') + 1);
    if (show)
        snprintf(name.show, sizeof(name.show), "%s", show);
    LibraryRemote remote = {.genres = genres};
    libraryAddRemote(lib, path, &name, 1, added, &remote);
}

static void testRows(void)
{
    static Library lib;
    libraryInit(&lib, "jellyfin:");
    addVideo(&lib, "jellyfin:/f/Ashfall", NULL, 0, "Drama, Thriller", 10);
    addVideo(&lib, "jellyfin:/f/Bramble", NULL, 0, "Comedy", 20);
    addVideo(&lib, "jellyfin:/f/Cinder", NULL, 0, "Drama", 30);
    addVideo(&lib, "jellyfin:/f/Dune Sea", NULL, 0, "Western", 40);
    addVideo(&lib, "jellyfin:/s/Rook/1", "Rook", 1, NULL, 50);  // Its first episode has none,
    addVideo(&lib, "jellyfin:/s/Rook/2", "Rook", 2, "Comedy, Drama", 60);  // its second does.
    libraryBuild(&lib);
    CHECK(homeShowGenres(&lib, 0) && strcmp(homeShowGenres(&lib, 0), "Comedy, Drama") == 0);

    static Home home;
    static ResumeList resume;
    memset(&resume, 0, sizeof(resume));
    homeComputeShowcase(&home, &lib, &resume, NULL);
    // Nothing started, no favorites: Films, Series, then Drama (3) and Comedy (2); Thriller and Western
    // have one each: no row.
    CHECK(home.count == 4);
    CHECK(home.rows[0].kind == HOME_FILMS && home.rows[0].count == 4 && !home.rows[0].items[0].is_show);
    CHECK(lib.items[home.rows[0].items[0].index].added == 40);  // The newest first.
    CHECK(home.rows[1].kind == HOME_SERIES && home.rows[1].count == 1 && home.rows[1].items[0].is_show);
    CHECK(home.rows[2].kind == HOME_GENRE && strcmp(home.rows[2].genre, "Drama") == 0 && home.rows[2].count == 3);
    CHECK(home.rows[2].items[0].is_show);  // The series, newest, first.
    CHECK(strcmp(homeRowName(&home.rows[2]), "Drama") == 0 && strcmp(homeRowName(&home.rows[0]), "Films") == 0);
    CHECK(home.rows[3].kind == HOME_GENRE && strcmp(home.rows[3].genre, "Comedy") == 0 && home.rows[3].count == 2);

    // Started and favorites go first.
    const int64_t S = 1000000000;
    resumeRemember(&resume, "jellyfin:/f/Bramble", 60 * S, 600 * S);
    librarySetFavorite(&lib, (LibraryRef){true, 0}, true);
    homeComputeShowcase(&home, &lib, &resume, NULL);
    CHECK(home.count == 6 && home.rows[0].kind == HOME_CONTINUE && home.rows[1].kind == HOME_FAVORITES);

    // Tiles: Mixed makes only Continue watching wide.
    homeSetTiles(&home, HOME_TILES_MIXED);
    CHECK(home.rows[0].wide && !home.rows[1].wide && !home.rows[5].wide);
    homeSetTiles(&home, HOME_TILES_POSTERS);
    CHECK(!home.rows[0].wide);
    homeSetTiles(&home, HOME_TILES_LANDSCAPE);
    CHECK(home.rows[0].wide && home.rows[3].wide);

    // The layout: wide rows hold fewer tiles; a finger moves each by its own size.
    static LibView v;
    libviewInit(&v);
    CHECK(!v.showcase && v.home_layout.rows == LIBVIEW_HOME_ROWS);
    libviewSetHomeShowcase(&v, true);
    homeSetTiles(&home, HOME_TILES_MIXED);
    int col_px;
    CHECK(libviewHomeCols(&v, &home, 0, &col_px) == LIBVIEW_SHOWCASE_WIDE_COLS &&
          col_px == LIBVIEW_SHOWCASE_WIDE_COL_PX);
    CHECK(libviewHomeCols(&v, &home, 1, &col_px) == LIBVIEW_SHOWCASE_COLS && col_px == LIBVIEW_SHOWCASE_COL_PX);
    // One row in view: moving down puts the next one first.
    libviewHomeMove(&v, &home, 0, 2);
    CHECK(v.home_row == 2 && v.home_first_row == 2);
    libviewHomeMove(&v, &home, 0, 99);
    CHECK(v.home_row == home.count - 1 && v.home_first_row == home.count - 1);
    libviewHomeMove(&v, &home, 0, -99);
    CHECK(v.home_row == 0 && v.home_first_row == 0);
    // A finger up by more than half a row: the cursor goes to the row mostly in view.
    CHECK(libviewHomeScrollPx(&v, &home, LIBVIEW_SHOWCASE_ROW_PX * 3 / 4));
    CHECK(v.home_row == 1 && v.home_first_row == 0 && v.home_offset == LIBVIEW_SHOWCASE_ROW_PX * 3 / 4);
    libviewSetHomeShowcase(&v, false);  // Back to the classic rows: in view again, on a row.
    CHECK(!v.showcase && v.home_offset == 0 && v.home_first_row == v.home_row);
    libraryFree(&lib);
}

static void testSettingsAndShapes(void)
{
    Config c;
    configDefaults(&c);
    CHECK(!c.home_classic && c.home_tiles == HOME_TILES_MIXED);  // The Showcase, mixed, unless chosen.
    settingStep(&c, SETTING_HOME_STYLE, 1);
    settingStep(&c, SETTING_HOME_TILES, -1);
    CHECK(c.home_classic && c.home_tiles == HOME_TILES_LANDSCAPE);
    char value[64];
    settingValue(&c, SETTING_HOME_STYLE, value, sizeof(value));
    CHECK(strcmp(value, "Classic") == 0);
    settingValue(&c, SETTING_HOME_TILES, value, sizeof(value));
    CHECK(strcmp(value, "Landscape") == 0);
    CHECK(settingIsChoice(SETTING_HOME_STYLE) && settingIsChoice(SETTING_HOME_TILES));
    CHECK(strcmp(settingLabel(SETTING_HOME_STYLE), "Home style") == 0);

    char text[4096];
    FILE *f = fmemopen(text, sizeof(text), "w");
    configWrite(&c, f);
    fclose(f);
    Config back;
    configDefaults(&back);
    for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n"))
        configReadLine(&back, line);
    CHECK(back.home_classic && back.home_tiles == HOME_TILES_LANDSCAPE);
    CHECK(configReadLine(&back, "home_tiles=9") && back.home_tiles == HOME_TILES_MIXED);  // Out of range.
    CHECK(configReadLine(&back, "home_style=showcase") && !back.home_classic);

    int w, h;
    thumbShapeSize(THUMB_SHAPE_POSTER, &w, &h);
    CHECK(w == THUMB_POSTER_W && h == THUMB_POSTER_H && h * 2 == w * 3);
    thumbShapeSize(THUMB_SHAPE_LARGE, &w, &h);
    CHECK(w == THUMB_LARGE_W && h == THUMB_LARGE_H);
    thumbShapeSize(THUMB_SHAPE_CARD, &w, &h);
    CHECK(w == THUMB_W && h == THUMB_H);
}

void testShowcase(void)
{
    testJoin();
    testTable();
    testJellyfinGenres();
    testPlexGenres();
    testStremioGenres();
    testRows();
    testSettingsAndShapes();
}
