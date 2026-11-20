// Talks to a Plex server on a PC through plexsync, as the console does: reads
// its library, asks how to play a few videos (straight from the file, and
// converted with a subtitle drawn on), and reads a piece of each.
//   PLEX_TOKEN=... host_plex http://192.168.0.10:32400
// The token is the server's (never printed); for personal testing only.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plexsync.h"

#define VERSION "host"
#define PIECE_BYTES 4096
#define STOP_TIMEOUT_S 10L

static int g_failures;

static void expect(bool ok, const char *what)
{
    printf("%s %s\n", ok ? "  ok  " : "  FAIL", what);
    g_failures += !ok;
}

static bool sink(const void *data, size_t size, void *user)
{
    (void)data;
    *(size_t *)user += size;
    return true;
}

// Bytes of the start of an address (a range when asked).
static size_t readPiece(NetClient *net, const char *url, bool range, long *status)
{
    size_t got = 0;
    char error[NET_ERROR_MAX];
    NetRequest request = {.url = url, .range = range ? "0-4095" : NULL, .timeout_s = 60};
    netFetch(net, &request, sink, &got, status, error, sizeof(error));
    return got;
}

static int findItem(const JfCatalog *c, const char *title)
{
    for (size_t i = 0; i < c->library.count; i++)
        if (!strcmp(c->library.items[i].name.title, title))
            return (int)i;
    return -1;
}

static void tryDirect(NetClient *net, const PlexSession *s, const JfCatalog *c)
{
    int i = findItem(c, "Paper Lanterns");
    expect(i >= 0, "Paper Lanterns is in the library");
    if (i < 0)
        return;
    const char *location = c->library.items[i].path;
    JfPlan plan;
    int64_t duration = 0;
    char error[160], url[PLEX_URL_MAX];
    expect(plexNetResolve(net, s, VERSION, location, 25000000, -1, -1, &plan, &duration, error, sizeof(error), NULL),
           "resolve a film");
    printf("        %s: direct=%d audio=%d duration=%llds\n", location, plan.direct, plan.audio_count,
           (long long)(duration / 1000000000));
    expect(plan.direct && duration > 0, "played straight from the file, with its length");
    long status = 0;
    expect(plexStreamUrl(s, VERSION, location, url, sizeof(url)) && readPiece(net, url, true, &status) == PIECE_BYTES &&
               status == 206, "read a piece of the file");
    expect(plexImageUrl(s, VERSION, location, "Primary", 300, url, sizeof(url)) && readPiece(net, url, false, &status) > 0 &&
               status == 200, "its poster");
    char path[320], item[PLEX_ID_MAX], part[PLEX_ID_MAX];
    plexLocationIds(location, item, part);
    plexTimelinePath(path, sizeof(path), item, "stopped", 0, duration);
    expect(plexNetCall(net, s, VERSION, path, false, STOP_TIMEOUT_S, &status), "tell the server where it stopped");
}

static void tryConverted(NetClient *net, const PlexSession *s, const JfCatalog *c)
{
    int i = findItem(c, "KaiVideo Subtitle Test");
    expect(i >= 0, "the subtitle test film is in the library");
    if (i < 0)
        return;
    const char *location = c->library.items[i].path;
    JfPlan plan;
    int64_t duration = 0;
    char error[160], url[PLEX_URL_MAX];
    expect(plexNetResolve(net, s, VERSION, location, 25000000, -1, -1, &plan, &duration, error, sizeof(error), NULL),
           "resolve the subtitle test");
    expect(plan.direct && plan.picture_count == 2 && plan.audio_count == 2, "direct, with 2 picture subtitles and 2 sound tracks");
    if (plan.picture_count < 1)
        return;
    int burn = plan.pictures[0].index, other_audio = plan.audio[1].index;
    expect(plexNetResolve(net, s, VERSION, location, 25000000, other_audio, burn, &plan, &duration, error, sizeof(error),
                          NULL), "resolve with the first picture subtitle drawn on, and the other sound track");
    printf("        converted=%d burned=%d audio_current=%d session=%s\n", !plan.direct, plan.picture_burned,
           plan.audio_current, plan.play_session);
    expect(!plan.direct && plan.picture_burned == 0 && plan.audio_current == 1, "converted, as asked");
    plexServerUrl(s, VERSION, plan.transcode_url, url, sizeof(url));
    NetResponse r;
    NetRequest request = {.url = url, .timeout_s = 60};
    bool listed = netFetchText(net, &request, 65536, &r);
    expect(listed && strstr(r.data, "#EXTM3U") && strstr(r.data, "index.m3u8"), "the conversion's master list");
    if (!listed)
        printf("        status %ld: %.200s\n", r.status, r.error);
    if (listed)
        netResponseFree(&r);
    char stop[128];
    snprintf(stop, sizeof(stop), "/video/:/transcode/universal/stop?session=%s", plan.play_session);
    long status = 0;
    plexNetCall(net, s, VERSION, stop, false, STOP_TIMEOUT_S, &status);
    printf("        stop: %ld\n", status);
    // Back to the first sound track, as it was.
    plexNetResolve(net, s, VERSION, location, 2000000, plan.audio[0].index, -1, &plan, &duration, error, sizeof(error),
                   NULL);
}

static void tryExternalSubtitle(NetClient *net, const PlexSession *s, const JfCatalog *c)
{
    int i = findItem(c, "The Quiet Workshop");
    JfPlan plan;
    int64_t duration;
    char error[160];
    bool ok = i >= 0 && plexNetResolve(net, s, VERSION, c->library.items[i].path, 25000000, -1, -1, &plan, &duration,
                                       error, sizeof(error), NULL);
    expect(ok && plan.sub_count == 1 && plan.subs[0].external, "an external subtitle is offered");
    NetResponse r;
    ok = ok && plexNetFetchSubtitle(net, s, VERSION, plan.subs[0].index, NULL, &r);
    expect(ok && strstr(r.data, "-->"), "and it can be read");
    if (ok)
        netResponseFree(&r);
}

int main(int argc, char **argv)
{
    const char *token = getenv("PLEX_TOKEN");
    if (argc < 2 || !token) {
        fprintf(stderr, "PLEX_TOKEN=... %s http://server:32400\n", argv[0]);
        return 2;
    }
    PlexSession s = {0};
    snprintf(s.client_id, sizeof(s.client_id), "kaivideo-host-test");
    snprintf(s.server_token, sizeof(s.server_token), "%s", token);
    snprintf(s.server_uri, sizeof(s.server_uri), "%s", argv[1]);
    netInit();
    NetClient *net = netClientOpen();
    JfCatalog c;
    jfCatalogInit(&c);
    expect(plexNetReadLibrary(net, &s, VERSION, &c, NULL, NULL), "read the library");
    libraryBuild(&c.library);
    printf("        %zu videos, %zu films, %zu series, %d libraries\n", c.library.count, c.library.film_count,
           c.library.show_count, c.library.collection_count);
    tryDirect(net, &s, &c);
    tryConverted(net, &s, &c);
    tryExternalSubtitle(net, &s, &c);
    jfCatalogFree(&c);
    netClientClose(net);
    netExit();
    printf("%d failures\n", g_failures);
    return g_failures ? 1 : 0;
}
