// An Xtream account's films and series (made-up ones only).
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "libview.h"
#include "xtreamvod.h"

static const char *const FILMS =
    "[{\"num\":1,\"name\":\"Paper Boats (2019)\",\"stream_id\":101,\"stream_icon\":\"http://img.example/p.jpg\","
    "\"added\":\"1600000000\",\"container_extension\":\"mkv\"},"
    "{\"name\":\" Night Desk \",\"stream_id\":\"102\",\"stream_icon\":\"javascript:x\",\"container_extension\":\"m p4\"},"
    "{\"name\":\"(2020)\",\"stream_id\":103},"
    "{\"name\":\"No id\"},{\"stream_id\":104}]";

static const char *const SHOWS =
    "[{\"name\":\"Kite Hill\",\"series_id\":7,\"cover\":\"https://img.example/k.jpg\",\"plot\":\"Kites.\","
    "\"last_modified\":\"1700000000\"},{\"name\":\"Harbor Weather\",\"series_id\":8}]";

static const char *const EPISODES =
    "{\"info\":{},\"episodes\":{\"2\":[{\"id\":\"902\",\"episode_num\":1,\"title\":\"Return\",\"season\":2,"
    "\"container_extension\":\"mp4\"}],\"1\":[{\"id\":\"802\",\"episode_num\":\"2\",\"title\":\"Second\","
    "\"container_extension\":\"mkv\",\"info\":{\"duration_secs\":1500,\"plot\":\"More kites.\"}},"
    "{\"id\":\"801\",\"episode_num\":1,\"title\":\"First\",\"container_extension\":\"mkv\"}]}}";

static void testParse(void)
{
    XtreamCatalog c;
    memset(&c, 0, sizeof(c));
    CHECK(xtreamParseFilms(FILMS, &c));
    CHECK(c.film_count == 3);
    CHECK(c.films[0].id == 101 && !strcmp(c.films[0].name, "Paper Boats") && c.films[0].year == 2019);
    CHECK(!strcmp(c.films[0].ext, "mkv") && !strcmp(c.films[0].poster, "http://img.example/p.jpg"));
    CHECK(c.films[0].added == 1600000000);
    CHECK(c.films[1].id == 102 && !strcmp(c.films[1].name, "Night Desk") && !c.films[1].poster);
    CHECK(!strcmp(c.films[1].ext, "mp4"));  // A strange extension: mp4.
    CHECK(!strcmp(c.films[2].name, "(2020)") && c.films[2].year == 0);
    CHECK(!xtreamParseFilms("{}", &c) && !xtreamParseFilms(NULL, &c) && c.film_count == 3);

    CHECK(xtreamParseShows(SHOWS, &c));
    CHECK(c.show_count == 2 && c.shows[0].id == 7 && !strcmp(c.shows[0].plot, "Kites."));
    CHECK(c.shows[0].state == XTREAM_EPISODES_UNREAD && !c.shows[1].cover);

    XtreamEpisode *e;
    int n;
    CHECK(xtreamParseEpisodes(EPISODES, &e, &n));
    CHECK(n == 3);
    CHECK(e[0].id == 801 && e[0].season == 1 && e[0].episode == 1 && !strcmp(e[0].title, "First"));
    CHECK(e[1].id == 802 && e[1].runtime_s == 1500 && !strcmp(e[1].plot, "More kites."));
    CHECK(e[2].id == 902 && e[2].season == 2);
    xtreamShowSetEpisodes(&c, 7, e, n);
    CHECK(c.shows[0].state == XTREAM_EPISODES_READ && c.shows[0].episode_count == 3);
    xtreamShowSetEpisodes(&c, 8, NULL, 0);
    CHECK(c.shows[1].state == XTREAM_EPISODES_FAILED);
    CHECK(!xtreamParseEpisodes("[]", &e, &n) && !e);
    CHECK(xtreamParseEpisodes("{\"episodes\":[[{\"id\":5}],[{\"id\":6,\"episode_num\":3}]]}", &e, &n));
    CHECK(n == 2 && e[0].season == 1 && e[0].episode == 1 && e[1].season == 2 && e[1].episode == 3);
    CHECK(!strcmp(e[0].ext, "mp4"));
    xtreamEpisodesFree(e, n);
    xtreamShowSetEpisodes(&c, 99, NULL, 0);  // Not one of them: nothing.

    Library lib;
    libraryInit(&lib, "iptv:");
    xtreamFillLibrary(&lib, &c, "Reading", "Failed");
    libraryBuild(&lib);
    CHECK(lib.film_count == 3 && lib.show_count == 2);
    int film = libraryFind(&lib, "xtream:movie/101.mkv");
    CHECK(film >= 0 && !strcmp(lib.items[film].art, "poster:http://img.example/p.jpg"));
    CHECK(lib.items[film].name.year == 2019);
    CHECK(!lib.items[libraryFind(&lib, "xtream:movie/102.mp4")].art);
    int episode = libraryFind(&lib, "xtream:series/802.mkv");
    CHECK(episode >= 0 && lib.items[episode].runtime_ns == 1500000000000ll);
    CHECK(!strcmp(lib.items[episode].series_art, "poster:https://img.example/k.jpg"));
    int stand_in = libraryFind(&lib, "xtream:show/8");
    CHECK(stand_in >= 0 && !strcmp(lib.items[stand_in].name.title, "Failed"));
    libraryFree(&lib);
    xtreamCatalogFree(&c);
    CHECK(c.film_count == 0 && c.show_count == 0 && !c.films);
}

static void testLocations(void)
{
    char loc[64], kind[16], ext[8];
    long long id;
    CHECK(xtreamLocation(loc, sizeof(loc), "movie", 12, "mkv") && !strcmp(loc, "xtream:movie/12.mkv"));
    CHECK(xtreamParseLocation(loc, kind, sizeof(kind), &id, ext, sizeof(ext)));
    CHECK(!strcmp(kind, "movie") && id == 12 && !strcmp(ext, "mkv"));
    CHECK(xtreamLocation(loc, sizeof(loc), "show", 5, NULL) && xtreamStandInShow(loc) == 5);
    CHECK(xtreamStandInShow("xtream:movie/5.mp4") == -1 && xtreamStandInShow("iptv:5") == -1);
    CHECK(!xtreamParseLocation("xtream:movie/x.mkv", kind, sizeof(kind), &id, ext, sizeof(ext)));
    CHECK(!xtreamParseLocation("xtream:movie/1.m/k", kind, sizeof(kind), &id, ext, sizeof(ext)));
    CHECK(!xtreamParseLocation("xtream:/1.mkv", kind, sizeof(kind), &id, ext, sizeof(ext)));
    CHECK(!xtreamLocation(loc, 8, "movie", 12, "mkv"));

    char url[512], auth[64];
    xtreamVodSetAccount(NULL, false);
    CHECK(!xtreamVodStreamRequest("xtream:movie/12.mkv", url, sizeof(url), auth, sizeof(auth)));
    XtreamAccount a = {"http://tv.example:8080", "user", "p&ss"};
    xtreamVodSetAccount(&a, true);
    CHECK(xtreamVodAnyCertificate());
    CHECK(xtreamVodStreamRequest("xtream:series/9.mp4", url, sizeof(url), auth, sizeof(auth)));
    CHECK(!strcmp(url, "http://tv.example:8080/series/user/p%26ss/9.mp4") && auth[0] == '\0');
    CHECK(!xtreamVodStreamRequest("xtream:show/9", url, sizeof(url), auth, sizeof(auth)));
    xtreamVodSetAccount(NULL, false);
}

// The provider's categories: films and series in them, the empty ones dropped, and Films or Series
// showing one of them.
static void testCategories(void)
{
    XtreamCatalog c;
    memset(&c, 0, sizeof(c));
    CHECK(xtreamParseVodCategories("[{\"category_id\":\"3\",\"category_name\":\"Drama\"},"
                                   "{\"category_id\":4,\"category_name\":\" Comedy \"},"
                                   "{\"category_id\":\"5\",\"category_name\":\"Empty\"},"
                                   "{\"category_name\":\"No id\"},{\"category_id\":\"6\"}]",
                                   &c, false));
    CHECK(c.film_group_count == 3 && !strcmp(c.film_groups[1].name, "Comedy") && !strcmp(c.film_groups[1].id, "4"));
    CHECK(!xtreamParseVodCategories("{}", &c, false) && c.film_group_count == 3);
    CHECK(xtreamParseFilms("[{\"name\":\"Paper Boats\",\"stream_id\":1,\"category_id\":\"4\"},"
                           "{\"name\":\"Night Desk\",\"stream_id\":2,\"category_id\":\"3\"},"
                           "{\"name\":\"Kite Hill\",\"stream_id\":3,\"category_ids\":[4]},"
                           "{\"name\":\"Harbor Lights\",\"stream_id\":4,\"category_id\":\"99\"}]",
                           &c));
    CHECK(c.film_group_count == 2);  // "Empty" had none.
    CHECK(!strcmp(c.film_groups[0].name, "Drama") && c.film_groups[0].count == 1);
    CHECK(!strcmp(c.film_groups[1].name, "Comedy") && c.film_groups[1].count == 2);
    CHECK(c.films[0].group == 1 && c.films[1].group == 0 && c.films[2].group == 1 && c.films[3].group == -1);
    CHECK(xtreamParseVodCategories("[{\"category_id\":\"8\",\"category_name\":\"Kids\"}]", &c, true));
    CHECK(xtreamParseShows("[{\"name\":\"Evening Report\",\"series_id\":7,\"category_id\":\"8\"},"
                           "{\"name\":\"Low Tide\",\"series_id\":9}]",
                           &c));
    CHECK(c.show_group_count == 1 && c.show_groups[0].count == 1);
    CHECK(c.shows[0].group == 0 && c.shows[1].group == -1);

    Library lib;
    libraryInit(&lib, "iptv:");
    xtreamFillLibrary(&lib, &c, "Reading", "Failed");
    libraryBuild(&lib);
    int refs[8];
    CHECK(libraryGroupRefs(&lib, false, 1, refs, 8) == 2);  // Comedy, by title: Kite Hill, Paper Boats.
    CHECK(!strcmp(lib.items[refs[0]].name.title, "Kite Hill") && !strcmp(lib.items[refs[1]].name.title, "Paper Boats"));
    CHECK(libraryGroupRefs(&lib, false, 1, refs, 1) == 1);
    CHECK(libraryGroupRefs(&lib, true, 0, refs, 8) == 1 && !strcmp(lib.shows[refs[0]].name, "Evening Report"));
    CHECK(libraryGroupRefs(&lib, true, 1, refs, 8) == 0);

    LibView v;
    libviewInit(&v);
    libviewSetIptvTabs(&v, &lib);
    CHECK(libviewGridCount(&v, &lib, GRID_FILMS) == 4);
    libraryGroupRefs(&lib, false, 1, refs, 8);
    libviewSetGridOnly(&v, GRID_FILMS, refs, 2);
    CHECK(libviewGridCount(&v, &lib, GRID_FILMS) == 2 && libviewGridCount(&v, &lib, GRID_SERIES) == 2);
    LibraryRef r = libviewGridRef(&v, &lib, GRID_FILMS, 1);
    CHECK(!r.is_show && r.index == refs[1]);
    v.grid[GRID_FILMS].cursor = 3;
    libviewClamp(&v, &lib, &(Home){0});
    CHECK(v.grid[GRID_FILMS].cursor == 1);
    libviewSetGridOnly(&v, GRID_COLLECTIONS, refs, 2);  // Not Films or Series: nothing.
    CHECK(v.only[GRID_FILMS] == refs);
    libviewSetTabs(&v, &lib, false, false, false);  // Another view: all of them again.
    CHECK(libviewGridCount(&v, &lib, GRID_FILMS) == 4 && !v.only[GRID_FILMS]);
    libraryFree(&lib);
    xtreamCatalogFree(&c);
    CHECK(!c.film_groups && c.film_group_count == 0 && !c.show_groups);
}

// A film's details (get_vod_info), read when they're opened, and kept when the list loads again.
static void testFilmInfo(void)
{
    XtreamFilmInfo info;
    CHECK(xtreamParseFilmInfo("{\"info\":{\"plot\":\" Boats at night. \",\"duration_secs\":240,\"duration\":\"00:04:00\","
                              "\"genre\":\"Drama, Comedy\"},\"movie_data\":{\"stream_id\":101}}", &info));
    CHECK(!strcmp(info.plot, "Boats at night.") && !strcmp(info.genre, "Drama, Comedy") && info.runtime_s == 240);
    xtreamFilmInfoFree(&info);
    CHECK(!info.plot && !info.genre);
    // No duration_secs: the clock text; a description instead of a plot.
    CHECK(xtreamParseFilmInfo("{\"info\":{\"description\":\"Desk.\",\"duration\":\"01:30:05\"}}", &info));
    CHECK(!strcmp(info.plot, "Desk.") && info.runtime_s == 5405 && !info.genre);
    xtreamFilmInfoFree(&info);
    CHECK(xtreamParseFilmInfo("{\"info\":{\"duration\":\"95:10\"}}", &info) && info.runtime_s == 5710);
    CHECK(xtreamParseFilmInfo("{\"info\":{\"duration\":\"1h 30m\",\"duration_secs\":\"0\"}}", &info) &&
          info.runtime_s == 0);
    // Some servers have nothing for a film: an answer, with nothing in it.
    CHECK(xtreamParseFilmInfo("{\"info\":[],\"movie_data\":{\"stream_id\":101}}", &info) && !info.plot);
    CHECK(!xtreamParseFilmInfo("[]", &info) && !xtreamParseFilmInfo("<html>", &info) &&
          !xtreamParseFilmInfo(NULL, &info));

    XtreamCatalog c;
    memset(&c, 0, sizeof(c));
    CHECK(xtreamParseFilms(FILMS, &c));
    CHECK(c.films[0].info_state == XTREAM_INFO_UNREAD);
    CHECK(xtreamFilmOf("xtream:movie/101.mkv") == 101 && xtreamFilmOf("xtream:show/7") == -1 &&
          xtreamFilmOf("xtream:series/5.mp4") == -1 && xtreamFilmOf("/videos/a.mkv") == -1);
    CHECK(xtreamParseFilmInfo("{\"info\":{\"plot\":\"Boats.\",\"duration_secs\":5400,\"genre\":\"Drama\"}}", &info));
    xtreamFilmSetInfo(&c, 101, &info);
    CHECK(!info.plot && c.films[0].info_state == XTREAM_INFO_READ && !strcmp(c.films[0].info.plot, "Boats."));
    xtreamFilmSetInfo(&c, 102, NULL);
    CHECK(c.films[1].info_state == XTREAM_INFO_FAILED);
    CHECK(xtreamParseFilmInfo("{\"info\":{\"plot\":\"Lost.\"}}", &info));
    xtreamFilmSetInfo(&c, 999, &info);  // Not one of them: let go.
    CHECK(!info.plot);

    Library lib;
    libraryInit(&lib, "iptv:");
    xtreamFillLibrary(&lib, &c, "Reading", "Failed");
    libraryBuild(&lib);
    int film = libraryFind(&lib, "xtream:movie/101.mkv");
    CHECK(film >= 0 && !strcmp(lib.items[film].overview, "Boats.") && lib.items[film].runtime_ns == 5400000000000ll);
    CHECK(!strcmp(lib.items[film].genres, "Drama"));
    CHECK(!lib.items[libraryFind(&lib, "xtream:movie/102.mp4")].overview);
    libraryFree(&lib);

    XtreamCatalog again;  // The list loaded again: what was read stays.
    memset(&again, 0, sizeof(again));
    CHECK(xtreamParseFilms(FILMS, &again));
    xtreamCatalogKeepInfo(&again, &c);
    CHECK(again.films[0].info_state == XTREAM_INFO_READ && !strcmp(again.films[0].info.plot, "Boats."));
    CHECK(again.films[1].info_state == XTREAM_INFO_UNREAD);  // A failure is asked again.
    CHECK(c.films[0].info_state == XTREAM_INFO_UNREAD && !c.films[0].info.plot);
    xtreamCatalogFree(&again);
    xtreamCatalogFree(&c);
}

void testXtreamVod(void)
{
    testParse();
    testFilmInfo();
    testLocations();
    testCategories();
}
