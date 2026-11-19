// Jellyfin without a server: addresses, what can be played, reading its
// answers, keeping them, merging where videos were left, and the read-ahead blocks.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "blockcache.h"
#include "check.h"
#include "hlslist.h"
#include "jellyfin.h"
#include "thumbgen.h"

#define ITEM_A "aaaa1111"
#define SOURCE_A "aaaa2222"
#define LOCATION_A JF_PREFIX ITEM_A "/" SOURCE_A
#define LOCATION_B JF_PREFIX "bbbb1111/bbbb2222"
#define S_TO_NS 1000000000ll

static const char PAGE[] =
    "{\"TotalRecordCount\":4,\"Items\":["
    "{\"Id\":\"aaaa1111\",\"Name\":\"Paper Lanterns\",\"Type\":\"Movie\",\"ProductionYear\":1998,"
    "\"Overview\":\"A quiet\\tstory.\\nTwo lines.\",\"RunTimeTicks\":72000000000,"
    "\"DateCreated\":\"2024-03-09T21:15:00.1234567Z\","
    "\"UserData\":{\"PlaybackPositionTicks\":6000000000,\"Played\":false,\"LastPlayedDate\":\"2024-05-01T00:00:00Z\"},"
    "\"MediaSources\":[{\"Id\":\"aaaa2222\",\"Size\":1000,\"Bitrate\":2000000,\"Container\":\"mkv\",\"MediaStreams\":["
    "{\"Type\":\"Audio\",\"Codec\":\"aac\"},"
    "{\"Type\":\"Video\",\"Codec\":\"hevc\",\"Width\":1920,\"Height\":1080,\"BitDepth\":10,\"VideoRange\":\"SDR\"}]}]},"
    "{\"Id\":\"bbbb1111\",\"Name\":\"Dry Creek\",\"Type\":\"Episode\",\"SeriesName\":\"Prairie Road\",\"SeriesId\":\"eeee1111\","
    "\"ParentIndexNumber\":2,\"IndexNumber\":5,\"RunTimeTicks\":12000000000,"
    "\"UserData\":{\"Played\":true,\"LastPlayedDate\":\"2024-06-01T00:00:00Z\"},"
    "\"MediaSources\":[{\"Id\":\"bbbb2222\",\"Size\":2000,\"Bitrate\":1000000,\"Container\":\"mp4\",\"MediaStreams\":["
    "{\"Type\":\"Video\",\"Codec\":\"h264\",\"Width\":1280,\"BitDepth\":8,\"VideoRange\":\"SDR\"}]}]},"
    "{\"Id\":\"cccc1111\",\"Name\":\"Big Sky\",\"Type\":\"Movie\","
    "\"MediaSources\":[{\"Id\":\"cccc2222\",\"Size\":3000,\"Bitrate\":60000000,\"Container\":\"mkv\",\"MediaStreams\":["
    "{\"Type\":\"Video\",\"Codec\":\"hevc\",\"Width\":3840,\"BitDepth\":10,\"VideoRange\":\"HDR\"}]}]},"
    "{\"Id\":\"dddd1111\",\"Name\":\"Not on disk\",\"Type\":\"Episode\",\"SeriesName\":\"Prairie Road\"}"
    "]}";

static void testAddresses(void)
{
    char out[JF_SERVER_MAX];
    CHECK(jfServerNormalize("192.168.0.2", out, sizeof(out)) && !strcmp(out, "http://192.168.0.2:8096"));
    CHECK(jfServerNormalize(" 192.168.0.2:9000/ ", out, sizeof(out)) && !strcmp(out, "http://192.168.0.2:9000"));
    CHECK(jfServerNormalize("https://media.example.org", out, sizeof(out)) && !strcmp(out, "https://media.example.org"));
    CHECK(jfServerNormalize("http://nas.local/jellyfin/", out, sizeof(out)) && !strcmp(out, "http://nas.local/jellyfin"));
    CHECK(!jfServerNormalize("", out, sizeof(out)));
    CHECK(!jfServerNormalize("two words", out, sizeof(out)));
    CHECK(!jfServerNormalize("ftp://host", out, sizeof(out)));

    char item[JF_ID_MAX], source[JF_ID_MAX];
    CHECK(jfIsLocation(LOCATION_A) && !jfIsLocation("sdmc:/movies/a.mkv"));
    CHECK(jfLocationIds(LOCATION_A, item, source) && !strcmp(item, ITEM_A) && !strcmp(source, SOURCE_A));
    CHECK(!jfLocationIds(JF_PREFIX "only", item, source));
    CHECK(!jfLocationIds(JF_PREFIX "../x/y", item, source));

    JfSession s = {.device_id = "dev"};
    char header[JF_AUTH_MAX];
    jfAuthHeader(&s, "1.4.0", header, sizeof(header));
    CHECK(strstr(header, "DeviceId=\"dev\"") && !strstr(header, "Token"));
    snprintf(s.token, sizeof(s.token), "abc");
    jfAuthHeader(&s, "1.4.0", header, sizeof(header));
    CHECK(strstr(header, "Token=\"abc\""));
}

static void testPlayability(void)
{
    JfVideoFacts v = {"hevc", "SDR", "mkv", 1920, 10, 3000000};
    CHECK(jfPlayability(&v) == JF_PLAYABLE);
    v.width = 3840;
    CHECK(jfPlayability(&v) == JF_TOO_LARGE);
    v.width = 1920;
    v.range = "HDR";
    CHECK(jfPlayability(&v) == JF_HDR);
    v.range = "";
    CHECK(jfPlayability(&v) == JF_PLAYABLE);  // Not said: as usual.
    v.bitrate = JF_MAX_BITRATE + 1;
    CHECK(jfPlayability(&v) == JF_TOO_HEAVY);
    v.bitrate = 1;
    v.bits = 12;
    CHECK(jfPlayability(&v) == JF_FORMAT);
    v = (JfVideoFacts){"av1", "SDR", "mkv", 1920, 10, 1};
    CHECK(jfPlayability(&v) == JF_FORMAT);  // The hardware decoder takes 8 bits.
    v.bits = 8;
    CHECK(jfPlayability(&v) == JF_PLAYABLE);
    v = (JfVideoFacts){"mpeg2video", "SDR", "ts", 720, 8, 1};
    CHECK(jfPlayability(&v) == JF_FORMAT);
    CHECK(jfPlayabilityMessage(JF_HDR)[0] && !jfPlayabilityMessage(JF_PLAYABLE)[0]);

    CHECK(jfParseDate("1970-01-02T00:00:00Z") == 0);  // Too old to be a real date here.
    CHECK(jfParseDate("2024-03-09T21:15:00.5Z") == 1710018900);
    CHECK(jfParseDate("") == 0 && jfParseDate(NULL) == 0);
}

static void checkCatalog(const JfCatalog *c)
{
    const Library *lib = &c->library;
    CHECK(lib->count == 3 && lib->film_count == 2 && lib->show_count == 1);
    const LibraryItem *film = &lib->items[0], *episode = &lib->items[1];
    CHECK(!strcmp(film->path, LOCATION_A) && !strcmp(film->name.title, "Paper Lanterns") && film->name.year == 1998);
    CHECK(film->size == 1000 && film->added == 1710018900 && film->runtime_ns == 7200 * S_TO_NS && !film->blocked);
    CHECK(film->overview && strstr(film->overview, "quiet"));
    CHECK(c->users[0].position_ns == 600 * S_TO_NS && !c->users[0].played);
    CHECK(episode->name.is_episode && episode->name.season == 2 && episode->name.episode == 5);
    CHECK(!strcmp(lib->shows[0].name, "Prairie Road") && c->users[1].played);
    CHECK(episode->series_art && !strcmp(episode->series_art, JF_PREFIX "eeee1111/" JF_SERIES_SOURCE));
    CHECK(!film->series_art);
    CHECK(lib->items[2].blocked == JF_TOO_LARGE);
}

static void testCatalog(void)
{
    JfCatalog c;
    jfCatalogInit(&c);
    int total = 0;
    CHECK(jfCatalogAddPage(&c, PAGE, &total) == 4 && total == 4);
    CHECK(jfCatalogAddPage(&c, "not json", NULL) == -1);
    libraryBuild(&c.library);
    checkCatalog(&c);

    const char *file = TMP_ROOT "/jellyfin_catalog.txt";
    CHECK(jfCatalogSave(&c, file));
    JfCatalog back;
    jfCatalogInit(&back);
    CHECK(jfCatalogLoad(&back, file));
    checkCatalog(&back);
    CHECK(!strchr(back.library.items[0].overview, '\t'));
    jfCatalogFree(&back);
    remove(file);
    jfCatalogInit(&back);
    CHECK(!jfCatalogLoad(&back, file));
    jfCatalogFree(&back);

    static ResumeList resume;
    Watched watched;
    watchedInit(&watched);
    resumeRemember(&resume, "sdmc:/film.mkv", 100 * S_TO_NS, 1000 * S_TO_NS);
    resumeRemember(&resume, JF_PREFIX "gone/gone", 100 * S_TO_NS, 1000 * S_TO_NS);
    watchedSet(&watched, "sdmc:/seen.mkv", true);
    watchedSet(&watched, JF_PREFIX "old/old", true);
    jfMergeUserData(&c, &resume, &watched);
    int64_t at = 0;
    CHECK(resumeFind(&resume, LOCATION_A, &at, NULL) && at == 600 * S_TO_NS);
    CHECK(resumeFind(&resume, "sdmc:/film.mkv", &at, NULL));   // The card's stay.
    CHECK(!resumeFind(&resume, JF_PREFIX "gone/gone", &at, NULL));
    CHECK(watchedHas(&watched, LOCATION_B) && watchedHas(&watched, "sdmc:/seen.mkv"));
    CHECK(!watchedHas(&watched, JF_PREFIX "old/old"));
    watchedFree(&watched);
    jfCatalogFree(&c);
}

// The server's libraries become tabs; favorites come from the item, or from its series.
static void testViewsAndFavorites(void)
{
    static const char VIEWS[] =
        "{\"Items\":[{\"Id\":\"v1\",\"Name\":\"Películas\",\"CollectionType\":\"movies\"},"
        "{\"Id\":\"v2\",\"Name\":\"Música\",\"CollectionType\":\"music\"},"
        "{\"Id\":\"v3\",\"Name\":\"Programas\",\"CollectionType\":\"tvshows\"},"
        "{\"Id\":\"v4\",\"Name\":\"Mixed\"}]}";
    static const char FAVORITE_SERIES[] = "{\"Items\":[{\"Id\":\"eeee1111\",\"Name\":\"Prairie Road\"}]}";
    static const char ONE_FILM[] =
        "{\"Items\":[{\"Id\":\"ffff1111\",\"Name\":\"Kite Hill\",\"Type\":\"Movie\","
        "\"UserData\":{\"IsFavorite\":true},"
        "\"MediaSources\":[{\"Id\":\"ffff2222\",\"Size\":1,\"Container\":\"mp4\",\"MediaStreams\":["
        "{\"Type\":\"Video\",\"Codec\":\"h264\",\"Width\":1280,\"BitDepth\":8,\"VideoRange\":\"SDR\"}]}]}]}";
    JfCatalog c;
    jfCatalogInit(&c);
    CHECK(jfCatalogAddViews(&c, VIEWS) == 3);  // Music isn't videos.
    CHECK(c.library.collection_count == 3 && !strcmp(c.library.collections[1].name, "Programas"));
    CHECK(c.library.collections[0].kind == COLLECTION_FILMS && c.library.collections[1].kind == COLLECTION_SERIES &&
          c.library.collections[2].kind == COLLECTION_MIXED);
    CHECK(!strcmp(c.view_ids[1], "v3"));
    CHECK(jfCatalogAddViews(&c, "nope") == -1);
    CHECK(jfCatalogSetFavoriteSeries(&c, FAVORITE_SERIES) == 1);

    c.adding_collection = 1;
    CHECK(jfCatalogAddPage(&c, PAGE, NULL) == 4);  // Dry Creek's series, Prairie Road, is a favorite.
    c.adding_collection = 0;
    CHECK(jfCatalogAddPage(&c, ONE_FILM, NULL) == 1);
    libraryBuild(&c.library);
    int kite = libraryFind(&c.library, JF_PREFIX "ffff1111/ffff2222");
    int dry = libraryFind(&c.library, LOCATION_B), paper = libraryFind(&c.library, LOCATION_A);
    CHECK(kite >= 0 && c.library.items[kite].favorite && c.library.items[kite].collection == 0);
    CHECK(dry >= 0 && c.library.items[dry].favorite && c.library.items[dry].collection == 1);
    CHECK(paper >= 0 && !c.library.items[paper].favorite);

    // All of it is kept between runs.
    const char *file = TMP_ROOT "/jellyfin_views.txt";
    CHECK(jfCatalogSave(&c, file));
    JfCatalog back;
    jfCatalogInit(&back);
    CHECK(jfCatalogLoad(&back, file));
    CHECK(back.library.collection_count == 3 && !strcmp(back.library.collections[0].name, "Películas") &&
          back.library.collections[1].kind == COLLECTION_SERIES);
    kite = libraryFind(&back.library, JF_PREFIX "ffff1111/ffff2222");
    dry = libraryFind(&back.library, LOCATION_B);
    CHECK(kite >= 0 && back.library.items[kite].favorite && back.library.items[kite].collection == 0);
    CHECK(dry >= 0 && back.library.items[dry].collection == 1 && back.library.shows[back.library.items[dry].show].favorite);
    jfCatalogFree(&back);
    jfCatalogFree(&c);
    remove(file);
}

// The parts of a video the server found: intro, recap, credits... in order of start.
static void testSegments(void)
{
    static const char ANSWER[] =
        "{\"Items\":[{\"Type\":\"Outro\",\"StartTicks\":13000000000,\"EndTicks\":14000000000},"
        "{\"Type\":\"Intro\",\"StartTicks\":50000000,\"EndTicks\":900000000},"
        "{\"Type\":\"Commercial\",\"StartTicks\":1,\"EndTicks\":2},"
        "{\"Type\":\"Intro\",\"StartTicks\":900000000,\"EndTicks\":900000000}],\"TotalRecordCount\":4}";
    JfSegment s[JF_SEGMENTS_MAX];
    int n = jfSegmentsParse(ANSWER, s, JF_SEGMENTS_MAX);
    CHECK(n == 2);  // Commercials aren't offered; an empty one is dropped.
    CHECK(s[0].kind == JF_SEGMENT_INTRO && s[0].start_ns == 5 * S_TO_NS && s[0].end_ns == 90 * S_TO_NS);
    CHECK(s[1].kind == JF_SEGMENT_OUTRO && s[1].start_ns == 1300 * S_TO_NS);
    CHECK(jfSegmentsParse("nope", s, JF_SEGMENTS_MAX) == 0);
    CHECK(jfSegmentsParse(ANSWER, s, 1) == 1 && s[0].kind == JF_SEGMENT_INTRO);

    // Which one to offer at a moment: from its start until a few seconds before its end.
    CHECK(jfSegmentAt(s, n, 4 * S_TO_NS) < 0);
    CHECK(jfSegmentAt(s, n, 5 * S_TO_NS) == 0 && jfSegmentAt(s, n, 80 * S_TO_NS) == 0);
    CHECK(jfSegmentAt(s, n, 89 * S_TO_NS) < 0);  // Almost over: not worth a jump.
    CHECK(jfSegmentAt(s, n, 1350 * S_TO_NS) == 1);
    CHECK(!strcmp(jfSegmentLabel(JF_SEGMENT_INTRO), "Skip intro") && !strcmp(jfSegmentLabel(JF_SEGMENT_OUTRO), "Skip credits"));
}

static void testSession(void)
{
    const char *file = TMP_ROOT "/jellyfin_session.txt";
    JfSession s = {.server = "http://h:8096", .user_id = "u1", .user_name = "Ana", .token = "t", .device_id = "d"}, back;
    CHECK(jfSessionSave(&s, file) && jfSessionLoad(&back, file));
    CHECK(!strcmp(back.server, s.server) && !strcmp(back.user_id, "u1") && !strcmp(back.user_name, "Ana"));
    CHECK(!strcmp(back.token, "t") && !strcmp(back.device_id, "d"));
    remove(file);
    CHECK(!jfSessionLoad(&back, file) && !back.server[0]);
}

static void testBlocks(void)
{
    BlockSlot s[BLOCK_SLOTS] = {{0}};
    int64_t at = 0;
    CHECK(blockNextWanted(s, BLOCK_SLOTS, &at, 1, -1) == 0);   // Length unknown: the first.
    s[0] = (BlockSlot){.index = 0, .state = BLOCK_READY};
    CHECK(blockNextWanted(s, BLOCK_SLOTS, &at, 1, -1) == -1);  // Only that until the length is known.
    CHECK(blockNextWanted(s, BLOCK_SLOTS, &at, 1, 100) == 1);
    CHECK(blockNextWanted(s, BLOCK_SLOTS, &at, 1, 1) == -1);   // The file ends there.
    CHECK(blockFind(s, BLOCK_SLOTS, 0) == 0 && blockFind(s, BLOCK_SLOTS, 5) < 0);
    for (int i = 0; i < BLOCK_SLOTS; i++)
        s[i] = (BlockSlot){.index = i + 10, .state = BLOCK_READY};
    at = 12;
    CHECK(blockNextWanted(s, BLOCK_SLOTS, &at, 1, 100) == -1);
    CHECK(blockVictim(s, BLOCK_SLOTS, &at, 1) == 0);          // Furthest behind.
    at = 10;
    CHECK(blockVictim(s, BLOCK_SLOTS, &at, 1) == BLOCK_SLOTS - 1);  // Nothing behind: furthest past the window.
    s[BLOCK_SLOTS - 1].state = BLOCK_LOADING;
    CHECK(blockVictim(s, BLOCK_SLOTS, &at, 1) == BLOCK_SLOTS - 2);  // Not one on its way.
    // Two readers: what either needs stays, and what's missing nearest to one comes first.
    int64_t two[2] = {10, 12 + BLOCK_SLOTS};
    CHECK(blockNextWanted(s, BLOCK_SLOTS, two, 2, 1000) == two[1]);
    CHECK(blockIsWanted(11, two, 2) && blockIsWanted(two[1] + 1, two, 2) && !blockIsWanted(20, two, 2));
    for (int i = 0; i < BLOCK_SLOTS; i++)
        s[i] = (BlockSlot){.index = i < BLOCK_AHEAD ? i + 20 : 0, .state = i < BLOCK_AHEAD ? BLOCK_READY : BLOCK_LOADING};
    at = 20;
    CHECK(blockVictim(s, BLOCK_SLOTS, &at, 1) == -1);         // All needed.
}

static void testPlan(void)
{
    static const char ANSWER[] =
        "{\"PlaySessionId\":\"ps1\",\"MediaSources\":[{\"SupportsDirectPlay\":false,"
        "\"TranscodingUrl\":\"/videos/a/master.m3u8?x=1\",\"DefaultAudioStreamIndex\":2,"
        "\"DefaultSubtitleStreamIndex\":5,\"MediaStreams\":["
        "{\"Type\":\"Video\",\"Index\":0},"
        "{\"Type\":\"Audio\",\"Index\":1,\"DisplayTitle\":\"English\"},"
        "{\"Type\":\"Audio\",\"Index\":2,\"DisplayTitle\":\"French\"},"
        "{\"Type\":\"Subtitle\",\"Index\":4,\"IsTextSubtitleStream\":false,\"DisplayTitle\":\"Picture\"},"
        "{\"Type\":\"Subtitle\",\"Index\":5,\"IsTextSubtitleStream\":true,\"IsExternal\":true,"
        "\"DisplayTitle\":\"Spanish\"}]}]}";
    JfPlan p;
    CHECK(jfPlanParse(ANSWER, &p) && !p.direct && !strcmp(p.transcode_url, "/videos/a/master.m3u8?x=1"));
    CHECK(!strcmp(p.play_session, "ps1") && p.audio_count == 2 && p.audio_current == 1);
    CHECK(p.sub_count == 1 && p.sub_default == 0 && p.subs[0].index == 5 && p.subs[0].external);
    CHECK(!jfPlanParse("{\"MediaSources\":[{\"SupportsDirectPlay\":false}]}", &p));
    CHECK(!jfPlanParse("nonsense", &p));

    char body[4096];
    jfPlaybackBody(body, sizeof(body), "u1", 8000000, 3);
    CHECK(strstr(body, "\"AudioStreamIndex\":3") && strstr(body, "\"MaxStreamingBitrate\":8000000"));
    jfPlaybackBody(body, sizeof(body), "u1", 60000000, -1);
    CHECK(!strstr(body, "AudioStreamIndex") && strstr(body, "\"Value\":\"25000000\""));  // Never more directly.
    CHECK(strlen(body) < sizeof(body) - 1);
}

static void testHlsLists(void)
{
    char out[256];
    CHECK(hlsResolveUrl("http://h:1/videos/a/master.m3u8?x=1", "main.m3u8?y=2", out, sizeof(out)) &&
          !strcmp(out, "http://h:1/videos/a/main.m3u8?y=2"));
    CHECK(hlsResolveUrl("http://h:1/videos/a/m.m3u8", "/other/b.ts", out, sizeof(out)) &&
          !strcmp(out, "http://h:1/other/b.ts"));
    CHECK(hlsResolveUrl("http://h:1/a", "http://x/y", out, sizeof(out)) && !strcmp(out, "http://x/y"));
    CHECK(hlsFirstEntry("#EXTM3U\r\n#TAG\r\nmain.m3u8?a=1\r\n", out, sizeof(out)) && !strcmp(out, "main.m3u8?a=1"));
    CHECK(!hlsFirstEntry("#EXTM3U\n", out, sizeof(out)));

    HlsPlaylist p;
    static const char LIST[] = "#EXTM3U\n#EXTINF:3.0,\nhls/0.ts?k=1\n#EXTINF:3.0,\nhls/1.ts?k=1\n#EXT-X-ENDLIST\n";
    CHECK(hlsPlaylistParse(LIST, "http://h:1/videos/a/main.m3u8?z", 3500000000ll, &p));
    CHECK(p.base_ns == 3000000000ll && !strstr(p.text, HLS_LOCAL_PREFIX "0.ts") && p.count == 2);
    CHECK(strstr(p.text, "#EXTINF:3.0,\n" HLS_LOCAL_PREFIX "1.ts\n") != NULL);
    hlsPlaylistFree(&p);
    CHECK(hlsPlaylistParse(LIST, "http://h:1/videos/a/main.m3u8?z", 999000000000ll, &p));  // Past the end.
    CHECK(p.base_ns == 0 && strstr(p.text, HLS_LOCAL_PREFIX "0.ts"));
    hlsPlaylistFree(&p);
    CHECK(hlsPlaylistParse(LIST, "http://h:1/videos/a/main.m3u8?z", 0, &p) && p.base_ns == 0);
    CHECK(p.count == 2 && !strcmp(p.urls[1], "http://h:1/videos/a/hls/1.ts?k=1"));
    CHECK(strstr(p.text, HLS_LOCAL_PREFIX "1.ts\n") && strstr(p.text, "#EXT-X-ENDLIST") && !strstr(p.text, "k=1"));
    hlsPlaylistFree(&p);
    CHECK(!hlsPlaylistParse("#EXTM3U\n", "http://h/a", 0, &p));
    CHECK(hlsPlaylistParse(LIST, "http://h:1/a.m3u8", 0, &p) && p.ended);
    CHECK(p.end_ns == 6000000000ll && p.longest_ns == 3000000000ll);
    hlsPlaylistFree(&p);
    // Live TV: no end yet, so it's read again as it grows.
    CHECK(hlsPlaylistParse("#EXTM3U\n#EXT-X-PLAYLIST-TYPE:EVENT\n#EXTINF:8.3,\nhls/0.ts\n", "http://h/a.m3u8", 0, &p) &&
          !p.ended && p.count == 1);
    hlsPlaylistFree(&p);

    static const char THREE[] = "#EXTM3U\n#EXTINF:3.0,\n0.ts\n#EXTINF:3.0,\n1.ts\n#EXTINF:3.0,\n2.ts\n#EXT-X-ENDLIST\n";
    int first = -1, last = -1;
    CHECK(hlsPlaylistParse(THREE, "http://h/a.m3u8", 0, &p));
    hlsPlaylistSpan(&p, 3500000000ll, 4000000000ll, &first, &last);  // 3.5 to 7.5 s.
    CHECK(first == 1 && last == 2);
    hlsPlaylistSpan(&p, 0, 3000000000ll, &first, &last);  // Exactly the first segment.
    CHECK(first == 0 && last == 0);
    hlsPlaylistSpan(&p, 8000000000ll, 4000000000ll, &first, &last);  // Runs past the end.
    CHECK(first == 2 && last == 2);
    hlsPlaylistFree(&p);

    CHECK(hlsLocalSegment(HLS_LOCAL_PREFIX "12.ts") == 12);
    CHECK(hlsLocalSegment(HLS_LOCAL_PREFIX "x.ts") == -1 && hlsLocalSegment("http://h/1.ts") == -1);
}

static void testFit(void)
{
    // 4x2, left half black and right half white: to 2x1, each half its own pixel.
    uint8_t rgb[4 * 2 * 3] = {0}, out[2 * 3];
    for (int y = 0; y < 2; y++)
        memset(rgb + (size_t)(y * 4 + 2) * 3, 255, 2 * 3);
    thumbgenFit(rgb, 4, 2, out, 2, 1);
    CHECK(out[0] == 0 && out[3] == 255);
    // A tall picture keeps its middle: a white band in the middle row fills the output.
    uint8_t tall[1 * 3 * 3] = {0, 0, 0, 255, 255, 255, 0, 0, 0}, one[3];
    thumbgenFit(tall, 1, 3, one, 1, 1);
    CHECK(one[0] == 255);
}

// Each way a connection can fail says what to check; a good answer says who answered.
static void testConnectionVerdict(void)
{
    char out[JF_VERDICT_MAX];
    static const char INFO[] = "{\"ServerName\":\"den\",\"Version\":\"10.11.11\",\"ProductName\":\"Jellyfin Server\"}";
    CHECK(jfConnectionVerdict(NET_OK, 200, INFO, "http://192.168.0.20:8096", out, sizeof(out)));
    CHECK(strstr(out, "den") && strstr(out, "10.11.11") && !strstr(out, "encrypted"));
    // Over the internet without https: it works, and says it isn't private.
    CHECK(jfConnectionVerdict(NET_OK, 200, INFO, "http://media.example.org:8096", out, sizeof(out)));
    CHECK(strstr(out, "isn't encrypted") != NULL);
    CHECK(jfConnectionVerdict(NET_OK, 200, INFO, "https://media.example.org", out, sizeof(out)));
    CHECK(!strstr(out, "encrypted"));

    CHECK(!jfConnectionVerdict(NET_OK, 200, "<html>router</html>", "http://192.168.0.1", out, sizeof(out)));
    CHECK(strstr(out, "isn't Jellyfin") != NULL);
    CHECK(!jfConnectionVerdict(NET_STATUS, 404, "", "http://192.168.0.20:80", out, sizeof(out)));
    CHECK(strstr(out, "404") != NULL && !strstr(out, "https://"));
    // Plain http to a port that wants https: say how to write it.
    CHECK(!jfConnectionVerdict(NET_STATUS, 400, "", "http://192.168.0.20:8921", out, sizeof(out)));
    CHECK(strstr(out, "https://") != NULL);
    CHECK(!jfConnectionVerdict(NET_NO_NAME, 0, NULL, "http://nas.lan:8096", out, sizeof(out)));
    CHECK(strstr(out, "name") != NULL);
    CHECK(!jfConnectionVerdict(NET_NO_CONNECT, 0, NULL, "http://192.168.0.20:8096", out, sizeof(out)));
    CHECK(strstr(out, "port") != NULL);
    CHECK(!jfConnectionVerdict(NET_TIMEOUT, 0, NULL, "http://192.168.0.20:8096", out, sizeof(out)));
    CHECK(strstr(out, "in time") != NULL);
    CHECK(!jfConnectionVerdict(NET_CERTIFICATE, 0, NULL, "https://den.lan", out, sizeof(out)));
    CHECK(strstr(out, "Certificate check") != NULL);
    CHECK(!jfConnectionVerdict(NET_SECURE, 0, NULL, "https://192.168.0.20:8096", out, sizeof(out)));
    CHECK(strstr(out, "https") != NULL);

    CHECK(jfAddressIsLocal("http://192.168.0.20:8096") && jfAddressIsLocal("http://10.0.0.5"));
    CHECK(jfAddressIsLocal("http://172.20.1.1:8096") && !jfAddressIsLocal("http://172.40.1.1:8096"));
    CHECK(jfAddressIsLocal("http://den.local:8096") && jfAddressIsLocal("http://den:8096"));
    CHECK(!jfAddressIsLocal("http://media.example.org") && !jfAddressIsLocal("https://8.8.8.8"));
}

// A server's answer to the network-wide question: its port and scheme, at the
// address the answer came from (in Docker it gives its own inner address).
static void testDiscovery(void)
{
    char server[JF_SERVER_MAX], name[JF_NAME_MAX];
    CHECK(jfDiscoveryParse("{\"Address\":\"http://172.18.0.11:8096\",\"Id\":\"d477\",\"Name\":\"den\"}",
                           "192.168.0.20", server, sizeof(server), name, sizeof(name)));
    CHECK(!strcmp(server, "http://192.168.0.20:8096") && !strcmp(name, "den"));
    CHECK(jfDiscoveryParse("{\"Address\":\"https://nas.lan:8920\",\"Name\":\"\"}", "10.0.0.9", server,
                           sizeof(server), name, sizeof(name)));
    CHECK(!strcmp(server, "https://10.0.0.9:8920") && !strcmp(name, "Jellyfin"));
    CHECK(jfDiscoveryParse("{\"Address\":\"http://172.18.0.11\",\"Name\":\"x\"}", "10.0.0.9", server, sizeof(server),
                           name, sizeof(name)));
    CHECK(!strcmp(server, "http://10.0.0.9:8096"));  // No port: Jellyfin's usual.
    CHECK(!jfDiscoveryParse("not json", "10.0.0.9", server, sizeof(server), name, sizeof(name)));
    CHECK(!jfDiscoveryParse("{\"Name\":\"x\"}", "10.0.0.9", server, sizeof(server), name, sizeof(name)));
    CHECK(!jfDiscoveryParse("{\"Address\":\"ftp://x:1\"}", "10.0.0.9", server, sizeof(server), name, sizeof(name)));
}

void testJellyfin(void)
{
    testConnectionVerdict();
    testDiscovery();
    testViewsAndFavorites();
    testSegments();
    testPlan();
    testHlsLists();
    testFit();
    testAddresses();
    testPlayability();
    testCatalog();
    testSession();
    testBlocks();
}
