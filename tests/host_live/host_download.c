// Runs the download thread on a PC against the Emby test server (and Plex's,
// with PLEX_TOKEN and PLEX_URL), as the console does: downloads a film whole,
// pauses one and lets it go on, deletes one, and tells the server where a
// download was left while offline (and not when the server heard of it later).
//   EMBY_USER=... EMBY_PASSWORD=... [PLEX_TOKEN=... PLEX_URL=...] host_download http://192.168.0.10:8097
// Only for the test servers with made-up videos: it changes where their user
// left them (and puts it back). The password and token are never printed.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "dlworker.h"
#include "jfsync.h"
#include "jfsync_internal.h"
#include "plex.h"
#include "plexsync.h"

#define WAIT_SECONDS 120
#define STEP_US 100000
#define ROOT "/tmp/kvdl/" DL_FOLDER
#define S 1000000000ll

static int g_failures;

static void expect(bool ok, const char *what)
{
    printf("%s %s\n", ok ? "  ok  " : "  FAIL", what);
    g_failures += !ok;
}

static bool waitSignedIn(void)
{
    JfStatus st;
    for (int i = 0; i < WAIT_SECONDS * 10; i++) {
        jfSyncGetStatus(&st);
        if (st.state == JF_SIGNED_IN || (st.state == JF_SIGNED_OUT && st.error[0]))
            break;
        usleep(STEP_US);
    }
    if (st.error[0])
        printf("        error: %s\n", st.error);
    return st.state == JF_SIGNED_IN;
}

static bool waitCatalog(ServerKind kind, JfCatalog *c)
{
    for (int i = 0; i < WAIT_SECONDS * 10; i++) {
        if (jfSyncTakeCatalog(kind, c))
            return true;
        usleep(STEP_US);
    }
    return false;
}

// The films of a server, smallest and largest (by file size).
static void pickFilms(const JfCatalog *c, const char *prefix, int *smallest, int *largest)
{
    *smallest = *largest = -1;
    for (size_t i = 0; i < c->library.count; i++) {
        const LibraryItem *it = &c->library.items[i];
        if (it->name.is_episode || it->blocked || strncmp(it->path, prefix, strlen(prefix)) || it->size <= 0)
            continue;
        if (*smallest < 0 || it->size < c->library.items[*smallest].size)
            *smallest = (int)i;
        if (*largest < 0 || it->size > c->library.items[*largest].size)
            *largest = (int)i;
    }
}

// The download of path, as the thread has it now.
static bool stateOf(const char *path, Download *out)
{
    DownloadList list;
    dlWorkerList(&list);
    int i = dlListFindPath(&list, path);
    if (i >= 0)
        *out = list.items[i];
    dlListFree(&list);
    return i >= 0;
}

static bool waitState(const char *path, DlState want, Download *out)
{
    for (int i = 0; i < WAIT_SECONDS * 10; i++) {
        if (stateOf(path, out) && (out->state == want || out->state == DL_FAILED))
            return out->state == want;
        usleep(STEP_US);
    }
    return false;
}

static long long fileSize(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 ? (long long)st.st_size : -1;
}

// Downloads item whole: the file on the card is the server's, byte for byte in length.
static void downloadWhole(const JfCatalog *c, int item, const char *account)
{
    const LibraryItem *it = &c->library.items[item];
    char path[LIBRARY_PATH_MAX];
    expect(dlPathFor(&it->name, "mkv", "Video", ROOT, path, sizeof(path)), "a place on the card");
    printf("        %s (%lld bytes) -> %s\n", it->path, (long long)it->size, path);
    expect(dlWorkerAdd(it->path, account, &it->name, it->runtime_ns, path, 0) >= 0, "asked for");
    Download d;
    bool done = waitState(path, DL_DONE, &d);
    if (!done)
        printf("        state %d: %s\n", d.state, d.error);
    expect(done, "it came whole");
    expect(fileSize(path) == it->size && d.size == it->size, "as long as the server's file");
    char part[LIBRARY_PATH_MAX + 8];
    snprintf(part, sizeof(part), "%s" DL_PART_SUFFIX, path);
    expect(fileSize(part) < 0, "no part left behind");
}

// Converted by the server at a low bitrate (so it must convert), paused after
// a few parts and let go on: the parts join into one MPEG-TS file.
// With NETWORK_TEST set: the server goes away mid-download (whoever runs this
// stops it when told, and starts it again when told). The download waits for
// the network instead of failing, and goes on by itself once it's back.
static void waitsForNetwork(const JfCatalog *c, int item, const char *account)
{
    const LibraryItem *it = &c->library.items[item];
    char path[LIBRARY_PATH_MAX];
    VideoName name = it->name;
    snprintf(name.title, sizeof(name.title), "%.100s offline", it->name.title);
    dlPathFor(&name, "mkv", "Video", ROOT, path, sizeof(path));
    expect(dlWorkerAdd(it->path, account, &name, it->runtime_ns, path, 400000) >= 0, "a conversion to cut off");
    Download d;
    for (int i = 0; i < WAIT_SECONDS * 10 && !(stateOf(path, &d) && d.parts_done >= 2); i++)
        usleep(STEP_US);
    printf("STOP THE SERVER NOW (at part %d of %d)\n", d.parts_done, d.part_count);
    fflush(stdout);
    bool waiting = false;
    for (int i = 0; i < 300 * 10 && !waiting; i++) {
        waiting = stateOf(path, &d) && d.state == DL_QUEUED && !strncmp(d.error, "Waiting for the network", 23);
        usleep(STEP_US);
    }
    printf("        %s\n", d.error);
    expect(waiting, "without the server, it waits for the network (not failed)");
    printf("START THE SERVER NOW\n");
    fflush(stdout);
    bool done = false;
    for (int i = 0; i < 600 * 10 && !done; i++) {
        done = stateOf(path, &d) && d.state == DL_DONE;
        if (stateOf(path, &d) && d.state == DL_FAILED)
            break;
        usleep(STEP_US);
    }
    if (!done)
        printf("        state %d: %s\n", d.state, d.error);
    expect(done && d.parts_done == d.part_count, "once it's back, it went on by itself and came whole");
}

static bool serverState(const char *location, JfUserData *out);
static bool waitTold(const char *account, const char *location);

// With SYNC_TEST set: what's watched offline reaches the server while a long
// conversion is still on its way (between its parts), not after it.
static void toldDuringConversion(const JfCatalog *c, int item, const char *account)
{
    const LibraryItem *it = &c->library.items[item];
    char path[LIBRARY_PATH_MAX];
    VideoName name = it->name;
    snprintf(name.title, sizeof(name.title), "%.100s sync", it->name.title);
    dlPathFor(&name, "mkv", "Video", ROOT, path, sizeof(path));
    dlWorkerAdd(it->path, account, &name, it->runtime_ns, path, 400000);
    Download d;
    for (int i = 0; i < WAIT_SECONDS * 10 && !(stateOf(path, &d) && d.parts_done >= 2); i++)
        usleep(STEP_US);
    JfUserData before, after;
    expect(serverState(it->path, &before), "what the server says before");
    dlWorkerRecord(account, it->path, 77 * S, false, time(NULL) + 2);
    bool told = waitTold(account, it->path);
    bool still = stateOf(path, &d) && d.state == DL_ACTIVE;
    printf("        told at part %d of %d\n", d.parts_done, d.part_count);
    expect(told && still && serverState(it->path, &after) && after.position_ns / S == 77,
           "told while the conversion was still on its way");
    dlWorkerRecord(account, it->path, before.position_ns, before.played, time(NULL) + 4);  // As it was.
    expect(waitTold(account, it->path), "put back as it was");
    dlWorkerRemove(path);
}

static void convertWhole(const JfCatalog *c, int item, const char *account)
{
    const LibraryItem *it = &c->library.items[item];
    char path[LIBRARY_PATH_MAX], part[LIBRARY_PATH_MAX + 8];
    VideoName name = it->name;
    snprintf(name.title, sizeof(name.title), "%.100s converted", it->name.title);
    dlPathFor(&name, "mkv", "Video", ROOT, path, sizeof(path));
    snprintf(part, sizeof(part), "%s" DL_PART_SUFFIX, path);
    printf("        %s -> %s\n", it->path, path);
    expect(dlWorkerAdd(it->path, account, &name, it->runtime_ns, path, 400000) >= 0, "a conversion asked for");
    Download d;
    for (int i = 0; i < WAIT_SECONDS * 10 && !(stateOf(path, &d) && (d.parts_done >= 2 || d.state != DL_ACTIVE &&
                                                                      d.state != DL_QUEUED));
         i++)
        usleep(STEP_US);
    if (d.state == DL_ACTIVE && d.part_count > d.parts_done) {
        dlWorkerPause(path, true);
        usleep(5 * STEP_US);
        expect(stateOf(path, &d) && d.state == DL_PAUSED && fileSize(part) == d.done && d.done > 0,
               "paused after whole parts, the file cut back to them");
        printf("        paused at part %d of %d (%lld bytes)\n", d.parts_done, d.part_count, (long long)d.done);
        dlWorkerPause(path, false);
    } else {
        printf("        (state %d, %d of %d parts: not paused) %s\n", d.state, d.parts_done, d.part_count, d.error);
    }
    bool done = waitState(path, DL_DONE, &d);
    if (!done)
        printf("        state %d: %s\n", d.state, d.error);
    expect(done && d.part_count > 0 && d.parts_done == d.part_count, "every part came");
    unsigned char magic[4] = {0};  // EBML: an MKV.
    FILE *f = fopen(path, "rb");
    bool mkv = f && fread(magic, 1, 4, f) == 4 && magic[0] == 0x1A && magic[1] == 0x45 && magic[2] == 0xDF &&
               magic[3] == 0xA3;
    if (f)
        fclose(f);
    printf("        %lld bytes in %d parts\n", fileSize(path), d.part_count);
    expect(mkv && d.size == fileSize(path) && fileSize(part) < 0, "rewrapped into one MKV, as long as the list says");
}

// Pauses a download on its way, lets it go on from there, then deletes it.
static void pauseAndGoOn(const JfCatalog *c, int item, const char *account)
{
    const LibraryItem *it = &c->library.items[item];
    char path[LIBRARY_PATH_MAX], part[LIBRARY_PATH_MAX + 8];
    VideoName name = it->name;
    snprintf(name.title, sizeof(name.title), "%.100s paused", it->name.title);
    dlPathFor(&name, "mkv", "Video", ROOT, path, sizeof(path));
    snprintf(part, sizeof(part), "%s" DL_PART_SUFFIX, path);
    dlWorkerAdd(it->path, account, &name, 0, path, 0);
    Download d;
    for (int i = 0; i < WAIT_SECONDS * 100 && fileSize(part) <= 0 && !(stateOf(path, &d) && d.state == DL_DONE); i++)
        usleep(STEP_US / 10);
    dlWorkerPause(path, true);
    usleep(5 * STEP_US);
    if (stateOf(path, &d) && d.state == DL_DONE) {
        printf("        (it came whole before it could be paused)\n");
    } else {
        long long kept = fileSize(part);
        expect(stateOf(path, &d) && d.state == DL_PAUSED && kept > 0 && kept < it->size, "paused part of the way");
        printf("        paused at %lld of %lld bytes\n", kept, (long long)it->size);
        dlWorkerPause(path, false);
        expect(waitState(path, DL_DONE, &d) && fileSize(path) == it->size, "it went on from there and came whole");
    }
    dlWorkerRemove(path);
    usleep(5 * STEP_US);
    expect(fileSize(path) < 0 && fileSize(part) < 0 && !stateOf(path, &d), "deleted, from the list and the card");
}

// Where the server says its user left location (from its own answer).
static bool serverState(const char *location, JfUserData *out)
{
    NetClient *net = netClientOpen();
    char url[2048], auth[JF_AUTH_MAX] = "", path[256], item[JF_ID_MAX], source[JF_ID_MAX];
    bool plex = plexIsLocation(location);
    if (plex) {
        PlexSession s = jfSyncPlexSession();
        plexLocationIds(location, item, source);
        snprintf(path, sizeof(path), "/library/metadata/%s", item);
        plexServerUrl(&s, "host", path, url, sizeof(url));
    } else if (jfIsEmbyLocation(location)) {
        JfSession s = jfSyncSessionFor(SERVER_EMBY);
        jfLocationIds(location, item, source);
        snprintf(url, sizeof(url), "%s/Users/%s/Items/%s", s.server, s.user_id, item);
        jfAuthHeader(&s, "host", auth, sizeof(auth));
    } else {
        JfSession s = jfSyncSessionFor(SERVER_JELLYFIN);
        jfLocationIds(location, item, source);
        snprintf(url, sizeof(url), "%s/Items/%s?userId=%s", s.server, item, s.user_id);
        jfAuthHeader(&s, "host", auth, sizeof(auth));
    }
    NetRequest request = {.url = url, .authorization = auth[0] ? auth : NULL, .timeout_s = 20, .accept_json = true};
    NetResponse r;
    bool ok = netFetchText(net, &request, 1 << 20, &r);
    if (ok) {
        ok = plex ? plexUserDataParse(r.data, out) : jfUserDataParse(r.data, out);
        netResponseFree(&r);
    }
    netClientClose(net);
    return ok;
}

// Waits until what waits for location (as account) has been told.
static bool waitTold(const char *account, const char *location)
{
    DlSyncEvent e;
    dlWorkerSyncSoon();
    for (int i = 0; i < WAIT_SECONDS * 10 && dlWorkerSyncFind(account, location, &e); i++)
        usleep(STEP_US);
    return !dlWorkerSyncFind(account, location, &e);
}

// What was watched offline reaches the server; older news doesn't overwrite it.
static void tellServer(const char *location, const char *account)
{
    JfUserData u;
    time_t now = time(NULL);
    dlWorkerRecord(account, location, 123 * S, false, now + 2);
    expect(waitTold(account, location) && serverState(location, &u) && u.position_ns / S == 123 && !u.played,
           "the server took where it was left offline (2:03)");
    dlWorkerRecord(account, location, 456 * S, false, 1000);  // Heard of long ago.
    expect(waitTold(account, location) && serverState(location, &u) && u.position_ns / S == 123,
           "older news doesn't replace what the server heard later");
    DlSyncEvent e;
    dlWorkerRecord("other|http://elsewhere|nobody", location, 789 * S, false, now + 4);
    dlWorkerSyncSoon();
    usleep(20 * STEP_US);
    expect(dlWorkerSyncFind("other|http://elsewhere|nobody", location, &e), "another user's news waits for that user");
    dlWorkerRecord(account, location, 0, false, now + 6);  // Back as it was.
    expect(waitTold(account, location) && serverState(location, &u) && u.position_ns == 0, "put back at the start");
}

static void keepPlexSession(const char *file, const char *token, const char *url)
{
    PlexSession p = {0};
    snprintf(p.client_id, sizeof(p.client_id), "kaivideo-host-test");
    snprintf(p.token, sizeof(p.token), "%s", token);
    snprintf(p.server_token, sizeof(p.server_token), "%s", token);
    snprintf(p.server_uri, sizeof(p.server_uri), "%s", url);
    snprintf(p.server_name, sizeof(p.server_name), "Test");
    snprintf(p.server_id, sizeof(p.server_id), "test-server");
    snprintf(p.user_name, sizeof(p.user_name), "tester");
    plexSessionSave(&p, file);
}

static void tryServer(ServerKind kind, const char *prefix)
{
    JfCatalog c;
    bool got = waitCatalog(kind, &c);
    expect(got, kind == SERVER_PLEX ? "Plex's library came" : "Emby's library came");
    if (!got)
        return;
    int small, large;
    pickFilms(&c, prefix, &small, &large);
    expect(small >= 0, "a film to download");
    if (small >= 0) {
        char account[DL_ACCOUNT_MAX];
        expect(jfSyncAccountOf(c.library.items[small].path, account, sizeof(account)) &&
                   dlAccountIsKind(account, kind == SERVER_PLEX ? "plex" : "emby"),
               "who it's downloaded as");
        downloadWhole(&c, small, account);
        if (getenv("SYNC_TEST")) {
            toldDuringConversion(&c, large, account);
            jfCatalogFree(&c);
            return;
        }
        if (getenv("NETWORK_TEST")) {
            waitsForNetwork(&c, large, account);
            jfCatalogFree(&c);
            return;
        }
        pauseAndGoOn(&c, large, account);
        convertWhole(&c, large, account);
        tellServer(c.library.items[small].path, account);
    }
    jfCatalogFree(&c);
}

// Jellyfin (JF_URL, JF_USER_ID, JF_TOKEN: a session made elsewhere): only the
// test film "KaiVideo Subtitle Test" is touched, and left as it was found.
static int jellyfinOnly(const char *url, const char *user_id, const char *token)
{
    JfSyncFiles files = {"/tmp/kv_jf.txt", "/tmp/kv_jf_lib.txt", "/tmp/kv_emby.txt", "/tmp/kv_emby_lib.txt",
                         "/tmp/kv_plex.txt", "/tmp/kv_plex_lib.txt"};
    remove(files.jellyfin_library);
    JfSession session = {0};
    snprintf(session.server, sizeof(session.server), "%s", url);
    snprintf(session.user_id, sizeof(session.user_id), "%s", user_id);
    snprintf(session.user_name, sizeof(session.user_name), "dev");
    snprintf(session.token, sizeof(session.token), "%s", token);
    snprintf(session.device_id, sizeof(session.device_id), "kaivideo-dev");
    jfSessionSave(&session, files.jellyfin);
    expect(jfSyncStart(&files, "host", true, SERVER_JELLYFIN), "start");
    jfSyncRefresh();
    expect(dlWorkerStart("/tmp/kv_dl.txt", "/tmp/kv_sync.txt", NULL), "the download thread starts");
    JfCatalog c;
    bool got = waitCatalog(SERVER_JELLYFIN, &c);
    expect(got, "Jellyfin's library came");
    int film = -1;
    for (size_t i = 0; got && i < c.library.count; i++)
        if (!strncmp(c.library.items[i].name.title, "KaiVideo Subtitle Test", 22))
            film = (int)i;
    expect(film >= 0, "the test film is there");
    if (film >= 0) {
        const char *location = c.library.items[film].path;
        char account[DL_ACCOUNT_MAX];
        JfUserData before;
        expect(jfSyncAccountOf(location, account, sizeof(account)) && dlAccountIsKind(account, "jellyfin"),
               "who it's downloaded as");
        expect(serverState(location, &before), "what the server says of it now");
        printf("        before: %lld s, %s\n", (long long)(before.position_ns / S), before.played ? "watched" : "not watched");
        convertWhole(&c, film, account);
        tellServer(location, account);
        dlWorkerRecord(account, location, before.position_ns, before.played, time(NULL) + 10);  // As it was found.
        JfUserData after;
        expect(waitTold(account, location) && serverState(location, &after) &&
                   after.position_ns / S == before.position_ns / S && after.played == before.played,
               "left as it was found");
        jfCatalogFree(&c);
    }
    dlWorkerStop();
    jfSyncStop();
    printf("%d failures\n", g_failures);
    return g_failures ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (getenv("JF_TOKEN") && getenv("JF_URL") && getenv("JF_USER_ID")) {
        system("rm -rf /tmp/kvdl /tmp/kv_dl.txt /tmp/kv_sync.txt");
        mkdir("/tmp/kvdl", 0777);
        return jellyfinOnly(getenv("JF_URL"), getenv("JF_USER_ID"), getenv("JF_TOKEN"));
    }
    const char *user = getenv("EMBY_USER"), *password = getenv("EMBY_PASSWORD");
    if (argc < 2 || !user || !password) {
        fprintf(stderr, "EMBY_USER=... EMBY_PASSWORD=... %s http://server:8097\n", argv[0]);
        return 2;
    }
    system("rm -rf /tmp/kvdl /tmp/kv_dl.txt /tmp/kv_sync.txt");
    mkdir("/tmp/kvdl", 0777);
    JfSyncFiles files = {"/tmp/kv_jf.txt", "/tmp/kv_jf_lib.txt", "/tmp/kv_emby.txt", "/tmp/kv_emby_lib.txt",
                         "/tmp/kv_plex.txt", "/tmp/kv_plex_lib.txt"};
    remove(files.emby);
    remove(files.emby_library);
    remove(files.plex);
    remove(files.plex_library);
    const char *plex_token = getenv("PLEX_TOKEN"), *plex_url = getenv("PLEX_URL");
    bool with_plex = plex_token && plex_url;
    if (with_plex)
        keepPlexSession(files.plex, plex_token, plex_url);
    expect(jfSyncStart(&files, "host", true, SERVER_EMBY), "start");
    expect(jfSyncSetServer(argv[1]), "the server's address");
    jfSyncSignInWithPassword(user, password);
    expect(waitSignedIn(), "signed in with a password");
    jfSyncRefresh();
    expect(dlWorkerStart("/tmp/kv_dl.txt", "/tmp/kv_sync.txt", NULL), "the download thread starts");
    tryServer(SERVER_EMBY, EMBY_PREFIX);
    if (with_plex)
        tryServer(SERVER_PLEX, PLEX_PREFIX);
    dlWorkerStop();
    DownloadList kept;
    dlListInit(&kept);
    bool partial = getenv("SYNC_TEST") || getenv("NETWORK_TEST");  // They stop after their own test.
    expect(dlListLoad(&kept, "/tmp/kv_dl.txt") && (partial || kept.count == (with_plex ? 4u : 2u)),
           "the list is kept on the card");
    dlListFree(&kept);
    jfSyncStop();
    printf("%d failures\n", g_failures);
    return g_failures ? 1 : 0;
}
