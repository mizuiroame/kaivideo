// Plex without a server: locations, the sign-in code, the account's servers,
// libraries turned into a catalog, and what tells the server how far a video went.
#include <stdio.h>
#include <string.h>

#include "check.h"
#include "plex.h"

#define MS_TO_NS 1000000ll

static const char RESOURCES[] =
    "[{\"name\":\"Shared one\",\"provides\":\"server\",\"clientIdentifier\":\"bbb\",\"accessToken\":\"t2\",\"owned\":false,"
    "\"connections\":[{\"protocol\":\"https\",\"address\":\"10.0.0.9\",\"port\":32400,\"uri\":\"https://10-0-0-9.x.plex.direct:32400\",\"local\":false,\"relay\":false}]},"
    "{\"name\":\"A phone\",\"provides\":\"client,player\",\"clientIdentifier\":\"ccc\",\"owned\":true,\"connections\":[]},"
    "{\"name\":\"Home\",\"provides\":\"server\",\"clientIdentifier\":\"aaa\",\"accessToken\":\"t1\",\"owned\":true,"
    "\"connections\":["
    "{\"protocol\":\"https\",\"address\":\"203.0.113.5\",\"port\":32400,\"uri\":\"https://203-0-113-5.x.plex.direct:32400\",\"local\":false,\"relay\":false},"
    "{\"protocol\":\"https\",\"address\":\"192.168.0.168\",\"port\":32400,\"uri\":\"https://192-168-0-168.x.plex.direct:32400\",\"local\":true,\"relay\":false},"
    "{\"protocol\":\"https\",\"address\":\"fd00::1\",\"port\":32400,\"uri\":\"https://fd00-1.x.plex.direct:32400\",\"local\":true,\"relay\":false,\"IPv6\":true},"
    "{\"protocol\":\"https\",\"address\":\"1.2.3.4\",\"port\":8443,\"uri\":\"https://1-2-3-4.x.plex.direct:8443\",\"local\":false,\"relay\":true}]}]";

static const char SECTIONS[] =
    "{\"MediaContainer\":{\"size\":3,\"Directory\":["
    "{\"key\":\"1\",\"type\":\"movie\",\"title\":\"Movies\"},"
    "{\"key\":\"2\",\"type\":\"artist\",\"title\":\"Music\"},"
    "{\"key\":\"3\",\"type\":\"show\",\"title\":\"TV Shows\"}]}}";

static const char MOVIES[] =
    "{\"MediaContainer\":{\"size\":2,\"totalSize\":7,\"Metadata\":["
    "{\"ratingKey\":\"4\",\"type\":\"movie\",\"title\":\"Paper Lanterns\",\"year\":1998,\"summary\":\"A quiet story.\","
    "\"duration\":120000,\"addedAt\":1763500000,\"viewOffset\":30000,\"lastViewedAt\":1763600000,"
    "\"Media\":[{\"videoCodec\":\"h264\",\"width\":1920,\"bitrate\":6000,\"container\":\"mkv\",\"videoProfile\":\"high\","
    "\"Part\":[{\"id\":7,\"key\":\"/library/parts/7/1/file.mkv\",\"size\":41424247}]}]},"
    "{\"ratingKey\":\"5\",\"type\":\"movie\",\"title\":\"Morning Lanterns\",\"year\":2023,\"viewCount\":2,"
    "\"Media\":[{\"videoCodec\":\"vp9\",\"width\":3840,\"bitrate\":20000,\"container\":\"webm\","
    "\"Part\":[{\"id\":8,\"size\":5}]}]},"
    "{\"ratingKey\":\"6\",\"type\":\"movie\",\"title\":\"No file\"}]}}";

static const char EPISODES[] =
    "{\"MediaContainer\":{\"size\":1,\"Metadata\":["
    "{\"ratingKey\":\"9\",\"type\":\"episode\",\"title\":\"Episode 4\",\"grandparentTitle\":\"Rook\","
    "\"grandparentRatingKey\":\"16\",\"parentIndex\":1,\"index\":4,"
    "\"Media\":[{\"videoCodec\":\"hevc\",\"width\":1920,\"bitrate\":3000,\"container\":\"mkv\",\"videoProfile\":\"main 10\","
    "\"Part\":[{\"id\":12,\"size\":100}]}]}]}}";

static const char META[] =
    "{\"MediaContainer\":{\"Metadata\":[{\"ratingKey\":\"20\",\"Media\":[{\"videoCodec\":\"h264\",\"width\":1280,"
    "\"bitrate\":2784,\"container\":\"mkv\",\"videoProfile\":\"high\",\"Part\":[{\"id\":21,\"Stream\":["
    "{\"id\":6,\"streamType\":1,\"codec\":\"h264\",\"bitDepth\":8},"
    "{\"id\":7,\"streamType\":2,\"codec\":\"aac\",\"displayTitle\":\"Español (AAC Mono)\","
    "\"extendedDisplayTitle\":\"Low tone (Español AAC Mono)\"},"
    "{\"id\":8,\"streamType\":2,\"codec\":\"aac\",\"displayTitle\":\"English (AAC Mono)\",\"selected\":true},"
    "{\"id\":9,\"streamType\":3,\"codec\":\"vobsub\",\"displayTitle\":\"Español\"},"
    "{\"id\":11,\"streamType\":3,\"codec\":\"ass\",\"displayTitle\":\"English\"},"
    "{\"id\":19,\"streamType\":3,\"codec\":\"srt\",\"displayTitle\":\"Español\",\"key\":\"/library/streams/19\","
    "\"selected\":true}]}]}]}]}}";

static const char META_HDR[] =
    "{\"MediaContainer\":{\"Metadata\":[{\"Media\":[{\"videoCodec\":\"hevc\",\"width\":1920,\"bitrate\":9000,"
    "\"container\":\"mkv\",\"videoProfile\":\"main 10\",\"Part\":[{\"id\":3,\"Stream\":["
    "{\"id\":1,\"streamType\":1,\"codec\":\"hevc\",\"colorTrc\":\"smpte2084\"}]}]}]}]}}";

static void testPlans(void)
{
    JfPlan plan;
    CHECK(plexPlanParse(META, 25000000, -1, &plan));
    CHECK(plan.direct && plan.audio_count == 2 && plan.audio_current == 1);
    CHECK(plan.audio[0].index == 7 && !strcmp(plan.audio[0].name, "Low tone (Español AAC Mono)"));
    CHECK(!strcmp(plan.audio[1].name, "English (AAC Mono)"));
    // Direct: the file holds its own text subtitles; the external one is fetched.
    CHECK(plan.sub_count == 1 && plan.subs[0].index == 19 && plan.subs[0].external && plan.sub_default == 0);
    CHECK(plan.picture_count == 1 && plan.pictures[0].index == 9 && plan.picture_burned == -1);

    CHECK(plexPlanParse(META, 2000000, -1, &plan) && !plan.direct);  // Over the quality chosen.
    CHECK(plan.sub_count == 1 && plan.picture_count == 2 && plan.pictures[1].index == 11);

    CHECK(plexPlanParse(META, 25000000, 9, &plan) && !plan.direct && plan.picture_burned == 0);
    CHECK(plexPlanParse(META_HDR, 25000000, -1, &plan) && !plan.direct);
    CHECK(!plexPlanParse("{\"MediaContainer\":{}}", 0, -1, &plan));

    char path[1024];
    plexTranscodePath(path, sizeof(path), "20", "s1", 8000000, true, false, "X-Plex-Product=KaiVideo");
    CHECK(strstr(path, "/video/:/transcode/universal/start.m3u8?path=%2Flibrary%2Fmetadata%2F20&") == path);
    CHECK(strstr(path, "&maxVideoBitrate=8000&") && strstr(path, "&subtitles=burn&") && strstr(path, "&session=s1&"));
    CHECK(strstr(path, "videoResolution=1920x1080") && strstr(path, "add-transcode-target"));
    CHECK(strstr(path, "&X-Plex-Product=KaiVideo") && !strstr(path, "Token"));
    plexTranscodePath(path, sizeof(path), "20", "s2", 8000000, false, true, "");
    CHECK(strstr(path, "videoResolution=320x180") && strstr(path, "&subtitles=none&"));
}

static void testLocations(void)
{
    char item[PLEX_ID_MAX], part[PLEX_ID_MAX], location[64];
    plexLocationMake(location, sizeof(location), "4", "7");
    CHECK(!strcmp(location, "plex:4/7"));
    CHECK(plexIsLocation(location) && jfIsRemoteLocation(location) && !jfIsLocation(location));
    CHECK(plexLocationIds(location, item, part) && !strcmp(item, "4") && !strcmp(part, "7"));
    CHECK(!plexLocationIds("plex:4/x", item, part));
    CHECK(!plexLocationIds("plex:/7", item, part));
    CHECK(!plexLocationIds("jellyfin:4/7", item, part));
    CHECK(jfIsRemoteLocation("jellyfin:aa/bb") && !jfIsRemoteLocation("/movies/a.mkv"));
}

static void testSignIn(void)
{
    long long id = 0;
    char code[8], token[PLEX_TOKEN_MAX] = "";
    CHECK(plexPinParse("{\"id\":123,\"code\":\"AB12\",\"authToken\":null}", &id, code, sizeof(code)));
    CHECK(id == 123 && !strcmp(code, "AB12"));
    CHECK(!plexPinParse("{\"errors\":[]}", &id, code, sizeof(code)));
    CHECK(!plexPinParse("not json", &id, code, sizeof(code)));
    CHECK(!plexPinToken("{\"id\":123,\"authToken\":null}", token, sizeof(token)));
    CHECK(plexPinToken("{\"id\":123,\"authToken\":\"secret\"}", token, sizeof(token)) && !strcmp(token, "secret"));

    char query[PLEX_QUERY_MAX];
    plexClientQuery("console1", "1.9.0", NULL, query, sizeof(query));
    CHECK(strstr(query, "X-Plex-Product=KaiVideo") && strstr(query, "X-Plex-Client-Identifier=console1") &&
          strstr(query, "X-Plex-Version=1.9.0") && !strstr(query, "Token"));
    plexClientQuery("console1", "1.9.0", "tok", query, sizeof(query));
    CHECK(strstr(query, "&X-Plex-Token=tok"));
}

static void testServers(void)
{
    PlexServer s[4];
    int n = plexResourcesParse(RESOURCES, s, 4);
    CHECK(n == 2);
    CHECK(!strcmp(s[0].name, "Home") && s[0].owned && !strcmp(s[0].token, "t1") && !strcmp(s[0].id, "aaa"));
    CHECK(s[0].uri_count == 4);
    CHECK(!strcmp(s[0].uris[0], "https://192-168-0-168.x.plex.direct:32400"));
    CHECK(!strcmp(s[0].uris[1], "http://192.168.0.168:32400"));
    CHECK(!strcmp(s[0].uris[2], "https://203-0-113-5.x.plex.direct:32400"));
    CHECK(!strcmp(s[0].uris[3], "https://1-2-3-4.x.plex.direct:8443"));
    CHECK(!strcmp(s[1].name, "Shared one") && !s[1].owned && s[1].uri_count == 1);
    CHECK(plexResourcesParse("{}", s, 4) == -1);
    CHECK(plexResourcesParse(RESOURCES, s, 1) == 1 && !strcmp(s[0].name, "Home"));
}

static void testCatalog(void)
{
    JfCatalog c;
    jfCatalogInit(&c);
    CHECK(plexCatalogAddSections(&c, SECTIONS) == 2);
    CHECK(c.library.collection_count == 2 && !strcmp(c.view_ids[0], "1") && !strcmp(c.view_ids[1], "3"));
    CHECK(c.library.collections[1].kind == COLLECTION_SERIES);

    int total = 0;
    c.adding_collection = 0;
    CHECK(plexCatalogAddPage(&c, MOVIES, &total) == 3 && total == 7);
    c.adding_collection = 1;
    CHECK(plexCatalogAddPage(&c, EPISODES, NULL) == 1);
    CHECK(c.library.count == 3);

    const LibraryItem *film = &c.library.items[0];
    CHECK(!strcmp(film->path, "plex:4/7") && !strcmp(film->name.title, "Paper Lanterns") && film->name.year == 1998);
    CHECK(film->size == 41424247 && film->added == 1763500000 && film->runtime_ns == 120000 * MS_TO_NS);
    CHECK(film->blocked == JF_PLAYABLE && film->collection == 0 && !strcmp(film->overview, "A quiet story."));
    CHECK(c.users[0].position_ns == 30000 * MS_TO_NS && c.users[0].last_played == 1763600000 && !c.users[0].played);
    CHECK(c.library.items[1].blocked == JF_TOO_LARGE && c.users[1].played);

    const LibraryItem *episode = &c.library.items[2];
    CHECK(episode->name.is_episode && !strcmp(episode->name.show, "Rook") && episode->name.season == 1 &&
          episode->name.episode == 4);
    CHECK(episode->blocked == JF_PLAYABLE && episode->collection == 1);
    CHECK(episode->series_art && !strcmp(episode->series_art, "plex:16/series"));
    CHECK(plexCatalogAddPage(&c, "[]", NULL) == -1);
    jfCatalogFree(&c);
}

static void testReports(void)
{
    char path[256];
    plexTimelinePath(path, sizeof(path), "4", "playing", 30500 * MS_TO_NS, 120000 * MS_TO_NS);
    CHECK(!strcmp(path, "/:/timeline?ratingKey=4&key=%2Flibrary%2Fmetadata%2F4&state=playing&time=30500&duration=120000"));
    plexPlayedPath(path, sizeof(path), "4", true);
    CHECK(!strcmp(path, "/:/scrobble?identifier=com.plexapp.plugins.library&key=4"));
    plexPlayedPath(path, sizeof(path), "4", false);
    CHECK(strstr(path, "/:/unscrobble?"));
}

static void testMarkers(void)
{
    JfSegment s[4];
    const char *json = "{\"MediaContainer\":{\"Metadata\":[{\"Marker\":["
                       "{\"type\":\"credits\",\"startTimeOffset\":1300000,\"endTimeOffset\":1400000},"
                       "{\"type\":\"intro\",\"startTimeOffset\":5000,\"endTimeOffset\":65000},"
                       "{\"type\":\"commercial\",\"startTimeOffset\":1,\"endTimeOffset\":2},"
                       "{\"type\":\"intro\",\"startTimeOffset\":9000,\"endTimeOffset\":9000}]}]}}";
    CHECK(plexMarkersParse(json, s, 4) == 2);
    CHECK(s[0].kind == JF_SEGMENT_INTRO && s[0].start_ns == 5000 * MS_TO_NS && s[0].end_ns == 65000 * MS_TO_NS);
    CHECK(s[1].kind == JF_SEGMENT_OUTRO && s[1].start_ns == 1300000 * MS_TO_NS);
    CHECK(plexMarkersParse(json, s, 1) == 1 && plexMarkersParse("{}", s, 4) == 0);
}

static void testFavorites(void)
{
    const char *file = TMP_ROOT "_plex_favorites.txt";
    remove(file);
    CHECK(plexFavoritesSet(file, "plex:4/7", true));
    CHECK(plexFavoritesSet(file, "plex:16/series", true));
    CHECK(plexFavoritesSet(file, "plex:99/1", true) && plexFavoritesSet(file, "plex:99/1", false));
    CHECK(!plexFavoritesSet(file, "/movies/a.mkv", true));
    JfCatalog c;
    jfCatalogInit(&c);
    plexCatalogAddPage(&c, MOVIES, NULL);
    plexCatalogAddPage(&c, EPISODES, NULL);
    plexFavoritesApply(&c, file);
    CHECK(c.library.items[0].favorite && !c.library.items[1].favorite && c.library.items[2].favorite);
    libraryBuild(&c.library);
    CHECK(c.library.show_count == 1 && c.library.shows[0].favorite);
    jfCatalogFree(&c);
    remove(file);
}

// Several servers' libraries in one: each keeps its videos, users and libraries.
static void testAppend(void)
{
    JfCatalog plex, all;
    jfCatalogInit(&plex);
    jfCatalogInit(&all);
    plexCatalogAddSections(&plex, SECTIONS);
    plex.adding_collection = 0;
    plexCatalogAddPage(&plex, MOVIES, NULL);
    plex.adding_collection = 1;
    plexCatalogAddPage(&plex, EPISODES, NULL);
    libraryAddCollection(&all.library, "Movies", COLLECTION_FILMS);
    CHECK(jfCatalogAppend(&all, &plex, " (Plex)"));
    CHECK(all.library.collection_count == 3 && !strcmp(all.library.collections[1].name, "Movies (Plex)") &&
          !strcmp(all.library.collections[2].name, "TV Shows (Plex)"));
    CHECK(all.library.count == 3 && all.library.items[0].collection == 1 && all.library.items[2].collection == 2);
    CHECK(!strcmp(all.library.items[0].path, "plex:4/7") && all.users[0].position_ns == 30000 * MS_TO_NS);
    CHECK(!strcmp(all.library.items[2].series_art, "plex:16/series") && all.library.items[2].name.is_episode);
    libraryBuild(&all.library);
    CHECK(all.library.film_count == 2 && all.library.show_count == 1);
    jfCatalogFree(&plex);
    jfCatalogFree(&all);

    // The same film and series on two servers are told apart; the rest keep their names.
    JfCatalog parts[SERVER_KIND_COUNT];
    bool have[SERVER_KIND_COUNT] = {true, false, true};
    static const char *const SUFFIXES[SERVER_KIND_COUNT] = {" (Jellyfin)", " (Emby)", " (Plex)"};
    jfCatalogInit(&parts[0]);
    jfCatalogInit(&parts[2]);
    plexCatalogAddPage(&parts[2], MOVIES, NULL);
    plexCatalogAddPage(&parts[2], EPISODES, NULL);
    VideoName film = {.year = 1998}, episode = {.is_episode = true, .season = 1, .episode = 1};
    snprintf(film.title, sizeof(film.title), "Paper Lanterns");
    snprintf(episode.show, sizeof(episode.show), "rook");  // Compared as names are: case and marks aside.
    LibraryRemote remote = {0};
    JfUserData user = {0};
    jfCatalogAddVideo(&parts[0], "jellyfin:a/a", &film, 1, 1, &remote, &user);
    jfCatalogAddVideo(&parts[0], "jellyfin:b/b", &episode, 1, 1, &remote, &user);
    jfCatalogTagShared(parts, have, SERVER_KIND_COUNT, SUFFIXES);
    CHECK(!strcmp(parts[0].library.items[0].name.title, "Paper Lanterns (Jellyfin)"));
    CHECK(!strcmp(parts[0].library.items[1].name.show, "rook (Jellyfin)"));
    CHECK(!strcmp(parts[2].library.items[0].name.title, "Paper Lanterns (Plex)"));
    CHECK(!strcmp(parts[2].library.items[1].name.title, "Morning Lanterns"));  // Only on Plex.
    CHECK(!strcmp(parts[2].library.items[2].name.show, "Rook (Plex)"));
    jfCatalogFree(&parts[0]);
    jfCatalogFree(&parts[2]);

    char item[JF_ID_MAX], source[JF_ID_MAX], location[64];
    jfLocationMakeWith(location, sizeof(location), EMBY_PREFIX, "17", "mediasource_17");
    CHECK(!strcmp(location, "emby:17/mediasource_17") && jfIsLocation(location) && jfIsEmbyLocation(location));
    CHECK(jfLocationIds(location, item, source) && !strcmp(item, "17") && !strcmp(source, "mediasource_17"));
    CHECK(jfIsRemoteLocation(location) && !jfIsEmbyLocation("jellyfin:a/b") && !plexIsLocation(location));
}

static void testSession(void)
{
    PlexSession s = {0}, back;
    snprintf(s.client_id, sizeof(s.client_id), "console1");
    snprintf(s.token, sizeof(s.token), "tok");
    snprintf(s.server_name, sizeof(s.server_name), "Home");
    snprintf(s.server_uri, sizeof(s.server_uri), "http://192.168.0.168:32400");
    CHECK(plexSessionSave(&s, TMP_ROOT "_plex_session.txt"));
    CHECK(plexSessionLoad(&back, TMP_ROOT "_plex_session.txt"));
    CHECK(!strcmp(back.client_id, "console1") && !strcmp(back.token, "tok") && !strcmp(back.server_name, "Home") &&
          !strcmp(back.server_uri, "http://192.168.0.168:32400") && !back.server_token[0]);
    remove(TMP_ROOT "_plex_session.txt");
    CHECK(!plexSessionLoad(&back, TMP_ROOT "_no_such_session.txt") && !back.token[0]);
}

void testPlex(void)
{
    testLocations();
    testSignIn();
    testServers();
    testCatalog();
    testReports();
    testPlans();
    testMarkers();
    testFavorites();
    testAppend();
    testSession();
}
