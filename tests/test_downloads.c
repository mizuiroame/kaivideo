// Downloads to the card: their list, their names on the card, and what was
// watched of them without the network, waiting to be told to the server.
#include <stdio.h>
#include <string.h>

#include "check.h"
#include "downloads.h"
#include "jellyfin.h"
#include "plex.h"

static VideoName film(const char *title, int year)
{
    VideoName n = {0};
    snprintf(n.title, sizeof(n.title), "%s", title);
    n.year = year;
    return n;
}

static VideoName episode(const char *show, int season, int number, const char *title)
{
    VideoName n = {.is_episode = true, .season = season, .episode = number};
    snprintf(n.show, sizeof(n.show), "%s", show);
    snprintf(n.title, sizeof(n.title), "%s", title);
    return n;
}

static void testSafeNames(void)
{
    char out[128];
    dlSafeName("Pokémon: Mewtwo's Return", out, sizeof(out));
    CHECK(strcmp(out, "Pokemon - Mewtwo's Return") == 0);
    dlSafeName("ÉLITE  Señor/Niño?*", out, sizeof(out));  // Signs FAT refuses go; spaces fold.
    CHECK(strcmp(out, "ELITE Senor Nino") == 0);
    dlSafeName("Dad’s Café...", out, sizeof(out));  // A curly quote is a plain one; no dots at the end.
    CHECK(strcmp(out, "Dad's Cafe") == 0);
    dlSafeName("Straße Æon Œuvre", out, sizeof(out));
    CHECK(strcmp(out, "Strasse AEon OEuvre") == 0);
    dlSafeName("東京", out, sizeof(out));  // Nothing ASCII is left.
    CHECK(out[0] == '\0');
    dlSafeName("  .hidden ", out, sizeof(out));
    CHECK(strcmp(out, "hidden") == 0);
    char tiny[6];
    dlSafeName("Paper Lanterns", tiny, sizeof(tiny));
    CHECK(strcmp(tiny, "Paper") == 0);  // Cut, with no space at the end.
}

static void testPaths(void)
{
    char out[LIBRARY_PATH_MAX];
    VideoName f = film("Paper Lanterns", 1998);
    CHECK(dlPathFor(&f, "mkv", "Video 41", "sdmc:/V/" DL_FOLDER, out, sizeof(out)));
    CHECK(strcmp(out, "sdmc:/V/" DL_FOLDER "/Films/Paper Lanterns (1998)/Paper Lanterns (1998).mkv") == 0);
    VideoName e = episode("Prairie Road", 9, 6, "Dry Creek: Part 2");
    CHECK(dlPathFor(&e, "mp4", "Video 42", "sdmc:/D", out, sizeof(out)));
    CHECK(strcmp(out, "sdmc:/D/Series/Prairie Road/Season 09/Prairie Road S09E06 Dry Creek - Part 2.mp4") == 0);
    VideoName named = film("Kite Hill (2019)", 2019);  // The server's name has its year already.
    CHECK(dlPathFor(&named, "mkv", "Video", "sdmc:/D", out, sizeof(out)));
    CHECK(strcmp(out, "sdmc:/D/Films/Kite Hill (2019)/Kite Hill (2019).mkv") == 0);
    VideoName blank = film("東京", 0);  // A name with nothing ASCII takes the fallback.
    CHECK(dlPathFor(&blank, "mkv", "Video 43", "sdmc:/D", out, sizeof(out)));
    CHECK(strcmp(out, "sdmc:/D/Films/Video 43/Video 43.mkv") == 0);
    VideoName untitled = episode("Rook", 1, 2, "");
    CHECK(dlPathFor(&untitled, "", "Video 44", "sdmc:/D", out, sizeof(out)));  // No extension: mkv.
    CHECK(strcmp(out, "sdmc:/D/Series/Rook/Season 01/Rook S01E02.mkv") == 0);
    char small[24];
    CHECK(!dlPathFor(&f, "mkv", "Video 41", "sdmc:/D", small, sizeof(small)));
    char ext[8];
    dlExtensionOf("Some.Film.2020.MKV", ext, sizeof(ext));
    CHECK(strcmp(ext, "mkv") == 0);
    dlExtensionOf("no extension", ext, sizeof(ext));
    CHECK(ext[0] == '\0');
    dlExtensionOf("weird.tool0ngextension", ext, sizeof(ext));
    CHECK(ext[0] == '\0');
    dlExtensionOf("x.m/p4", ext, sizeof(ext));
    CHECK(ext[0] == '\0');
}

static void testAccounts(void)
{
    char a[DL_ACCOUNT_MAX];
    dlAccountMake("jellyfin", "http://10.0.0.2:8096", "u1", a, sizeof(a));
    CHECK(strcmp(a, "jellyfin|http://10.0.0.2:8096|u1") == 0);
    dlAccountMake("plex", "srv\tid", "Kid\n", a, sizeof(a));  // No tabs or line breaks reach the files.
    CHECK(strcmp(a, "plex|srv id|Kid ") == 0);
    CHECK(dlAccountIsKind(a, "plex") && !dlAccountIsKind(a, "ple") && !dlAccountIsKind(a, "jellyfin"));
}

static void testList(void)
{
    static DownloadList list;
    dlListInit(&list);
    const char *file = TMP_ROOT "/downloads.txt";
    remove(file);
    CHECK(dlListLoad(&list, file) && list.count == 0);

    VideoName f = film("Paper Lanterns", 1998), e = episode("Rook", 1, 2, "Pawns");
    int a = dlListAdd(&list, "jellyfin:i1/s1", "jellyfin|h|u1", &f, 5400 * 1000000000ll, "sdmc:/D/a.mkv", 100);
    int b = dlListAdd(&list, "plex:7/8", "plex|s|Kid", &e, 0, "sdmc:/D/b.mkv", 101);
    CHECK(a == 0 && b == 1 && list.items[a].state == DL_QUEUED && list.items[a].size == -1);
    // The same video for the same user is the one already there; for another user, a second one.
    CHECK(dlListAdd(&list, "jellyfin:i1/s1", "jellyfin|h|u1", &f, 0, "sdmc:/D/other.mkv", 102) == a);
    CHECK(list.count == 2);
    CHECK(dlListFindOrigin(&list, "plex:7/8", "plex|s|Kid") == b);
    CHECK(dlListFindOrigin(&list, "plex:7/8", "plex|s|Adult") == -1);
    CHECK(dlListFindOrigin(&list, "plex:7/8", NULL) == b);  // Anyone's.
    CHECK(dlListFindPath(&list, "sdmc:/D/b.mkv") == b && dlListFindPath(&list, "sdmc:/D/c.mkv") == -1);
    CHECK(dlListAdd(&list, "bad\norigin", "x", &f, 0, "sdmc:/D/c.mkv", 1) == -1);  // Would break the file.
    CHECK(dlListAdd(&list, "jellyfin:i9/s9", "x", &f, 0, "sdmc:/D/a.mkv", 1) == -1);  // Its file is taken.

    // The next to fetch: the one going on, else the oldest waiting; none paused or done.
    CHECK(dlListNext(&list) == a);
    list.items[b].state = DL_ACTIVE;
    CHECK(dlListNext(&list) == b);
    list.items[b].state = DL_PAUSED;
    list.items[a].state = DL_DONE;
    CHECK(dlListNext(&list) == -1);

    // Kept on the card; one that was being fetched waits again when read back.
    list.items[a].size = list.items[a].done = 123456789012ll;
    list.items[b].state = DL_ACTIVE;
    list.items[b].size = 1000;
    list.items[b].done = 400;
    snprintf(list.items[b].error, sizeof(list.items[b].error), "The server stopped answering.");
    CHECK(dlListSave(&list, file));
    static DownloadList back;
    dlListInit(&back);
    CHECK(dlListLoad(&back, file) && back.count == 2);
    const Download *ra = &back.items[0], *rb = &back.items[1];
    CHECK(ra->state == DL_DONE && ra->size == 123456789012ll && ra->runtime_ns == 5400 * 1000000000ll);
    CHECK(strcmp(ra->name.title, "Paper Lanterns") == 0 && ra->name.year == 1998 && !ra->name.is_episode);
    CHECK(rb->state == DL_QUEUED && rb->done == 400 && rb->name.is_episode && rb->name.season == 1);
    CHECK(rb->name.episode == 2 && strcmp(rb->name.show, "Rook") == 0 && strcmp(rb->name.title, "Pawns") == 0);
    CHECK(strcmp(rb->origin, "plex:7/8") == 0 && strcmp(rb->account, "plex|s|Kid") == 0);
    CHECK(strcmp(rb->path, "sdmc:/D/b.mkv") == 0 && strcmp(rb->error, "The server stopped answering.") == 0);
    CHECK(rb->added == 101);

    dlListRemove(&back, 0);
    CHECK(back.count == 1 && strcmp(back.items[0].origin, "plex:7/8") == 0);
    dlListFree(&back);

    // A file of another kind, or broken lines, aren't taken.
    FILE *bad = fopen(file, "w");
    if (bad) {
        fputs("not downloads\n", bad);
        fclose(bad);
    }
    CHECK(!dlListLoad(&back, file) && back.count == 0);
    bad = fopen(file, "w");
    if (bad) {
        fprintf(bad, "%s\nbroken line\n9\t1\t1\t1\t1\t0\t0\t0\t0\tx\ty\tz\t\tt\t\n", DL_LIST_HEADER);
        fclose(bad);
    }
    CHECK(dlListLoad(&back, file) && back.count == 0);  // State 9 doesn't exist.
    dlListFree(&back);
    dlListFree(&list);
}

static void testNames(void)
{
    static DownloadList list;
    dlListInit(&list);
    VideoName e = episode("Pokémon", 1, 3, "Ash’s Day");
    dlListAdd(&list, "jellyfin:i/s", "a", &e, 0, "sdmc:/V/D/Series/Pokemon/Season 01/Pokemon S01E03 Ash's Day.mkv", 1);
    list.items[0].state = DL_DONE;
    VideoName f = film("Paper Lanterns", 1998);
    dlListAdd(&list, "jellyfin:j/s", "a", &f, 0, "sdmc:/V/D/Films/P/P.mkv", 1);  // Not done: not named.
    static Library lib;
    libraryInit(&lib, "sdmc:/V");
    libraryAdd(&lib, list.items[0].path, 10, 1);
    libraryAdd(&lib, "sdmc:/V/D/Films/P/P.mkv", 10, 1);
    libraryAdd(&lib, "sdmc:/V/Own.Film.2001.mkv", 10, 1);
    dlApplyNames(&lib, &list);
    libraryBuild(&lib);
    int ep = libraryFind(&lib, list.items[0].path);
    CHECK(ep >= 0 && lib.items[ep].named && strcmp(lib.items[ep].name.show, "Pokémon") == 0);
    CHECK(strcmp(lib.items[ep].name.title, "Ash’s Day") == 0 && lib.items[ep].show >= 0);
    CHECK(!lib.items[libraryFind(&lib, "sdmc:/V/D/Films/P/P.mkv")].named);
    CHECK(!lib.items[libraryFind(&lib, "sdmc:/V/Own.Film.2001.mkv")].named);
    libraryFree(&lib);
    dlListFree(&list);
}

static void testSyncQueue(void)
{
    static DlSyncQueue q;
    dlSyncInit(&q);
    const char *file = TMP_ROOT "/offline_sync.txt";
    remove(file);
    CHECK(dlSyncLoad(&q, file) && q.count == 0);
    const int64_t S = 1000000000;
    dlSyncRecord(&q, "jellyfin|h|u1", "jellyfin:i1/s1", 300 * S, false, 1000);
    dlSyncRecord(&q, "plex|s|Kid", "plex:7/8", 0, true, 1001);
    dlSyncRecord(&q, "jellyfin|h|u1", "jellyfin:i1/s1", 900 * S, false, 1002);  // Replaces the first.
    CHECK(q.count == 2);
    int i = dlSyncFind(&q, "jellyfin|h|u1", "jellyfin:i1/s1");
    CHECK(i >= 0 && q.items[i].position_ns == 900 * S && q.items[i].when == 1002 && !q.items[i].played);
    CHECK(dlSyncFind(&q, "plex|s|Adult", "plex:7/8") == -1);
    dlSyncRecord(&q, "x", "bad\tone", 0, true, 1);  // Would break the file.
    CHECK(q.count == 2);
    CHECK(dlSyncSave(&q, file));
    static DlSyncQueue back;
    dlSyncInit(&back);
    CHECK(dlSyncLoad(&back, file) && back.count == 2);
    int p = dlSyncFind(&back, "plex|s|Kid", "plex:7/8");
    CHECK(p >= 0 && back.items[p].played && back.items[p].when == 1001);
    int j = dlSyncFind(&back, "jellyfin|h|u1", "jellyfin:i1/s1");
    CHECK(j >= 0 && back.items[j].position_ns == 900 * S);
    dlSyncRemove(&back, p);
    CHECK(back.count == 1 && dlSyncFind(&back, "plex|s|Kid", "plex:7/8") == -1);

    // What was watched here wins over the server only if it came later.
    CHECK(dlSyncIsNewer(2000, 1999) && !dlSyncIsNewer(2000, 2000) && dlSyncIsNewer(5, 0));
    dlSyncFree(&back);
    dlSyncFree(&q);
}

// What the servers say of where a video was left, and what sets it later.
static void testServerUserData(void)
{
    const int64_t S = 1000000000;
    JfUserData u = {0};
    CHECK(jfUserDataParse("{\"Id\":\"i1\",\"UserData\":{\"PlaybackPositionTicks\":3000000000,\"Played\":false,"
                          "\"LastPlayedDate\":\"2026-11-20T10:00:00.0000000Z\"}}",
                          &u));
    CHECK(u.position_ns == 300 * S && !u.played && u.last_played == jfParseDate("2026-11-20T10:00:00Z"));
    CHECK(!jfUserDataParse("{\"Id\":\"i1\"}", &u) && !jfUserDataParse("not json", &u));
    char body[256];
    jfUserDataBody(body, sizeof(body), 300 * S, true, 1763632800);  // 2025-11-20 10:00:00 UTC.
    CHECK(strcmp(body, "{\"PlaybackPositionTicks\":3000000000,\"Played\":true,"
                       "\"LastPlayedDate\":\"2025-11-20T10:00:00.0000000Z\"}") == 0);
    jfUserDataBody(body, sizeof(body), -5, false, 0);
    CHECK(strstr(body, "\"PlaybackPositionTicks\":0,\"Played\":false") != NULL);

    CHECK(plexUserDataParse("{\"MediaContainer\":{\"Metadata\":[{\"ratingKey\":\"7\",\"viewOffset\":61000,"
                            "\"viewCount\":2,\"lastViewedAt\":1763632800}]}}",
                            &u));
    CHECK(u.position_ns == 61 * S && u.played && u.last_played == 1763632800);
    CHECK(plexUserDataParse("{\"MediaContainer\":{\"Metadata\":[{\"ratingKey\":\"7\"}]}}", &u));
    CHECK(u.position_ns == 0 && !u.played && u.last_played == 0);
    CHECK(!plexUserDataParse("{\"MediaContainer\":{}}", &u));
    char path[256];
    plexProgressPath(path, sizeof(path), "7", 61 * S + 500000);
    CHECK(strcmp(path, "/:/progress?key=7&identifier=com.plexapp.plugins.library&time=61000&state=stopped") == 0);
}

void testDownloads(void)
{
    testServerUserData();
    testSafeNames();
    testPaths();
    testAccounts();
    testList();
    testNames();
    testSyncQueue();
}
