// An Xtream account's films and series (made-up ones only).
#include <stdlib.h>
#include <string.h>

#include "check.h"
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

void testXtreamVod(void)
{
    testParse();
    testLocations();
}
