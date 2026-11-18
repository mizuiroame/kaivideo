// Jellyfin without a server: addresses, what can be played, reading its
// answers, keeping them, merging where videos were left, and the read-ahead blocks.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "blockcache.h"
#include "check.h"
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
    CHECK(blockNextWanted(s, BLOCK_SLOTS, 0, -1) == 0);   // Length unknown: the first.
    s[0] = (BlockSlot){.index = 0, .state = BLOCK_READY};
    CHECK(blockNextWanted(s, BLOCK_SLOTS, 0, -1) == -1);  // Only that until the length is known.
    CHECK(blockNextWanted(s, BLOCK_SLOTS, 0, 100) == 1);
    CHECK(blockNextWanted(s, BLOCK_SLOTS, 0, 1) == -1);   // The file ends there.
    CHECK(blockFind(s, BLOCK_SLOTS, 0) == 0 && blockFind(s, BLOCK_SLOTS, 5) < 0);
    for (int i = 0; i < BLOCK_SLOTS; i++)
        s[i] = (BlockSlot){.index = i + 10, .state = BLOCK_READY};
    CHECK(blockNextWanted(s, BLOCK_SLOTS, 12, 100) == -1);
    CHECK(blockVictim(s, BLOCK_SLOTS, 12) == 0);          // Furthest behind.
    CHECK(blockVictim(s, BLOCK_SLOTS, 10) == BLOCK_SLOTS - 1);  // Nothing behind: furthest past the window.
    s[BLOCK_SLOTS - 1].state = BLOCK_LOADING;
    CHECK(blockVictim(s, BLOCK_SLOTS, 10) == BLOCK_SLOTS - 2);  // Not one on its way.
    for (int i = 0; i < BLOCK_SLOTS; i++)
        s[i] = (BlockSlot){.index = i < BLOCK_AHEAD ? i + 20 : 0, .state = i < BLOCK_AHEAD ? BLOCK_READY : BLOCK_LOADING};
    CHECK(blockVictim(s, BLOCK_SLOTS, 20) == -1);         // All needed.
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

void testJellyfin(void)
{
    testFit();
    testAddresses();
    testPlayability();
    testCatalog();
    testSession();
    testBlocks();
}
