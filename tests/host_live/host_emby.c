// Runs jfsync's thread on a PC against an Emby server, as the console does:
// signs in with a password, reads the library, asks how to play two videos
// (straight from the file, and converted with a picture subtitle drawn on),
// and reports a playback. With PLEX_TOKEN and PLEX_URL, a Plex server is
// signed in too: the library holds both, and each video goes to its own.
//   EMBY_USER=... EMBY_PASSWORD=... [PLEX_TOKEN=... PLEX_URL=...] host_emby http://192.168.0.10:8096
// The password and token are never printed; for personal testing only.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "jfsync.h"
#include "plex.h"

#define WAIT_SECONDS 60
#define STEP_US 200000

static int g_failures;

static void expect(bool ok, const char *what)
{
    printf("%s %s\n", ok ? "  ok  " : "  FAIL", what);
    g_failures += !ok;
}

static bool waitSignedIn(void)
{
    JfStatus st;
    for (int i = 0; i < WAIT_SECONDS * 5; i++) {
        jfSyncGetStatus(&st);
        if (st.state == JF_SIGNED_IN || (st.state == JF_SIGNED_OUT && st.error[0]))
            break;
        usleep(STEP_US);
    }
    if (st.error[0])
        printf("        error: %s\n", st.error);
    return st.state == JF_SIGNED_IN;
}

static bool waitCatalog(JfCatalog *c)
{
    for (int i = 0; i < WAIT_SECONDS * 5; i++) {
        if (jfSyncTakeCatalog(c))
            return true;
        usleep(STEP_US);
    }
    return false;
}

static int findItem(const JfCatalog *c, const char *title, const char *prefix)
{
    for (size_t i = 0; i < c->library.count; i++)
        if (!strcmp(c->library.items[i].name.title, title) && !strncmp(c->library.items[i].path, prefix, strlen(prefix)))
            return (int)i;
    return -1;
}

static void tryPlans(const JfCatalog *c)
{
    char error[160];
    JfPlan plan;
    int film = findItem(c, "Paper Lanterns", EMBY_PREFIX), test = findItem(c, "KaiVideo Subtitle Test", EMBY_PREFIX);
    expect(film >= 0 && test >= 0, "both films are in the library");
    if (film < 0 || test < 0)
        return;
    const char *location = c->library.items[film].path;
    printf("        %s\n", location);
    expect(jfSyncResolve(location, 25000000, -1, -1, &plan, error, sizeof(error), NULL) && plan.direct,
           "a film plays straight from its file");
    char url[2048], auth[JF_AUTH_MAX];
    expect(jfSyncStreamRequest(location, url, sizeof(url), auth, sizeof(auth)), "its file's address");
    NetClient *net = netClientOpen();
    NetResponse r;
    NetRequest request = {.url = url, .authorization = auth, .range = "0-1023", .timeout_s = 30};
    bool read = netFetchText(net, &request, 4096, &r);
    expect(read && r.size == 1024, "a piece of the file");
    if (read)
        netResponseFree(&r);

    const char *sub = c->library.items[test].path;
    expect(jfSyncResolve(sub, 25000000, -1, -1, &plan, error, sizeof(error), NULL), "the subtitle test");
    printf("        direct=%d audio=%d pictures=%d\n", plan.direct, plan.audio_count, plan.picture_count);
    if (plan.picture_count > 0) {
        int burn = plan.pictures[0].index;
        expect(jfSyncResolve(sub, 25000000, plan.audio[plan.audio_count - 1].index, burn, &plan, error, sizeof(error),
                             NULL) && !plan.direct && plan.picture_burned == 0,
               "converted, with a picture subtitle drawn on and the other sound track");
        jfSyncConvertedRequest(sub, plan.transcode_url, url, sizeof(url), auth, sizeof(auth));
        NetRequest list = {.url = url, .authorization = auth, .timeout_s = 60};
        read = netFetchText(net, &list, 65536, &r);
        expect(read && strstr(r.data, "#EXTM3U"), "the conversion's list");
        if (read)
            netResponseFree(&r);
        jfSyncSetPlayback(plan.play_session, true, NULL);
        jfSyncReport(JF_REPORT_START, sub, 0, false);
        jfSyncReport(JF_REPORT_STOP, sub, 5000000000ll, false);
        jfSyncEndConversion(sub);
    }
    netClientClose(net);
}

// The same film from Plex, beside Emby's: its own session plays it.
static void tryPlex(const JfCatalog *c)
{
    int film = findItem(c, "Paper Lanterns", PLEX_PREFIX);
    expect(film >= 0, "Plex's Paper Lanterns is in the library too");
    if (film < 0)
        return;
    char error[160], url[2048], auth[JF_AUTH_MAX];
    JfPlan plan;
    const char *location = c->library.items[film].path;
    printf("        %s\n", location);
    expect(jfSyncResolve(location, 25000000, -1, -1, &plan, error, sizeof(error), NULL) && plan.direct,
           "it plays straight from Plex's file");
    expect(jfSyncStreamRequest(location, url, sizeof(url), auth, sizeof(auth)) && strstr(url, ":32400/library/parts/"),
           "from Plex's address");
}

// A Plex session as Settings would have kept it, for the server at url.
static void keepPlexSession(const char *file, const char *token, const char *url)
{
    PlexSession p = {0};
    snprintf(p.client_id, sizeof(p.client_id), "kaivideo-host-test");
    snprintf(p.token, sizeof(p.token), "%s", token);
    snprintf(p.server_token, sizeof(p.server_token), "%s", token);
    snprintf(p.server_uri, sizeof(p.server_uri), "%s", url);
    snprintf(p.server_name, sizeof(p.server_name), "Test");
    plexSessionSave(&p, file);
}

int main(int argc, char **argv)
{
    const char *user = getenv("EMBY_USER"), *password = getenv("EMBY_PASSWORD");
    if (argc < 2 || !user || !password) {
        fprintf(stderr, "EMBY_USER=... EMBY_PASSWORD=... %s http://server:8096\n", argv[0]);
        return 2;
    }
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
    JfCatalog c;
    bool got = waitCatalog(&c);
    expect(got, "the library came");
    if (got) {
        printf("        %zu videos, %zu films, %zu series, %d libraries\n", c.library.count, c.library.film_count,
               c.library.show_count, c.library.collection_count);
        for (int i = 0; i < c.library.collection_count; i++)
            printf("        tab: %s\n", c.library.collections[i].name);
        if (with_plex)
            expect(c.library.collection_count == 4 && c.library.count == 20 &&
                       !strcmp(c.library.collections[0].name, "Movies (Emby)") &&
                       !strcmp(c.library.collections[2].name, "Movies (Plex)"),
                   "both servers' libraries, named after their server");
        if (with_plex)
            expect(c.library.show_count == 2 && c.library.film_count == 12, "the series on both servers stay two");
        else
            expect(c.library.collection_count == 2 && c.library.film_count == 6 && c.library.show_count == 1,
                   "two libraries: 6 films, 1 series");
        tryPlans(&c);
        if (with_plex)
            tryPlex(&c);
        jfCatalogFree(&c);
    }
    jfSyncStop();  // Sends what's waiting.
    printf("%d failures\n", g_failures);
    return g_failures ? 1 : 0;
}
