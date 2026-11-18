// KaiVideo Jellyfin test: what the console's network gives with a real server.
// Signs in with Quick Connect, lists the library, and measures reading videos
// from it (straight on, and jumping) and fetching pictures. The server's
// address comes from sdmc:/kaivideo-jellytest-server.txt. Results, without
// titles or the sign-in, go to sdmc:/kaivideo-jellytest.txt; at the end it
// signs out.
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>
#include <switch.h>

#include "net.h"

#define LOG_PATH "sdmc:/kaivideo-jellytest.txt"
#define SERVER_PATH "sdmc:/kaivideo-jellytest-server.txt"
#define DEVICE_ID "kaivideo-network-test"
#define URL_MAX 768
#define AUTH_MAX 512
#define ID_MAX 64
#define SECRET_MAX 128
#define API_TIMEOUT_S 30L
#define LIST_MAX_BYTES (32u * 1024 * 1024)
#define LIST_LIMIT 300
#define POLL_NS 2000000000ull
#define TICK_NS 50000000ull
#define SIGN_IN_WAIT_NS (5ull * 60 * 1000000000ull)
#define READ_NS (15ull * 1000000000ull)
#define JUMP_BYTES (1024 * 1024)
#define PICTURES 8
#define MIN_HD_HEIGHT 1000
#define MAX_HD_WIDTH 2000
#define BITS_PER_BYTE 8.0
#define NS_PER_S 1e9
#define NS_PER_MS 1e6

static const double JUMP_POINTS[] = {0.12, 0.31, 0.47, 0.22, 0.68, 0.84, 0.53, 0.09};
#define JUMP_COUNT ((int)(sizeof(JUMP_POINTS) / sizeof(JUMP_POINTS[0])))

typedef struct {
    char id[ID_MAX], source[ID_MAX];
    int64_t size, bitrate;
    int width, height;
    char codec[16];
} Video;

static FILE *g_log;
static PadState g_pad;
static NetClient *g_net;
static char g_server[256], g_auth[AUTH_MAX], g_user[ID_MAX];
static bool g_signed_in;

static void say(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    if (g_log) {
        va_start(args, fmt);
        vfprintf(g_log, fmt, args);
        va_end(args);
        fflush(g_log);
    }
    consoleUpdate(NULL);
}

static uint64_t nowNs(void)
{
    return armTicksToNs(armGetSystemTick());
}

static bool plusPressed(void)
{
    padUpdate(&g_pad);
    return (padGetButtonsDown(&g_pad) & HidNpadButton_Plus) != 0;
}

static void setAuthorization(const char *token)
{
    snprintf(g_auth, sizeof(g_auth),
             "MediaBrowser Client=\"KaiVideo\", Device=\"Nintendo Switch 2\", DeviceId=\"" DEVICE_ID
             "\", Version=\"" KAIVIDEO_VERSION "\"%s%s%s",
             token ? ", Token=\"" : "", token ? token : "", token ? "\"" : "");
}

static bool readServer(void)
{
    FILE *f = fopen(SERVER_PATH, "r");
    if (!f)
        return false;
    bool ok = fgets(g_server, sizeof(g_server), f) != NULL;
    fclose(f);
    g_server[strcspn(g_server, "\r\n ")] = '\0';
    size_t n = strlen(g_server);
    if (n && g_server[n - 1] == '/')
        g_server[n - 1] = '\0';
    return ok && g_server[0];
}

// Calls the server and parses its JSON answer (NULL if it failed or has none).
static json_t *api(const char *path, const char *post, double *ms)
{
    char url[URL_MAX];
    snprintf(url, sizeof(url), "%s%s", g_server, path);
    NetRequest request = {.url = url, .authorization = g_auth, .post_json = post, .timeout_s = API_TIMEOUT_S};
    NetResponse r;
    uint64_t start = nowNs();
    bool ok = netFetchText(g_net, &request, LIST_MAX_BYTES, &r);
    if (ms)
        *ms = (double)(nowNs() - start) / NS_PER_MS;
    if (!ok) {
        say("   request failed: %s\n", r.error);
        return NULL;
    }
    json_t *root = json_loads(r.data, 0, NULL);
    netResponseFree(&r);
    return root;
}

static const char *textOf(const json_t *object, const char *key)
{
    const char *value = json_string_value(json_object_get(object, key));
    return value ? value : "";
}

static bool waitForApproval(const char *secret)
{
    char path[256];
    snprintf(path, sizeof(path), "/QuickConnect/Connect?secret=%s", secret);
    uint64_t start = nowNs(), last = 0;
    while (appletMainLoop() && nowNs() - start < SIGN_IN_WAIT_NS) {
        if (plusPressed())
            return false;
        if (nowNs() - last >= POLL_NS) {
            last = nowNs();
            json_t *state = api(path, NULL, NULL);
            bool done = state && json_is_true(json_object_get(state, "Authenticated"));
            json_decref(state);
            if (done)
                return true;
        }
        consoleUpdate(NULL);
        svcSleepThread(TICK_NS);
    }
    return false;
}

static bool signIn(void)
{
    setAuthorization(NULL);
    double ms = 0;
    json_t *started = api("/QuickConnect/Initiate", "{}", &ms);
    if (!started) {
        say("The server didn't start Quick Connect.\n");
        return false;
    }
    say("Server answered in %.0f ms.\n", ms);
    char secret[SECRET_MAX];
    snprintf(secret, sizeof(secret), "%s", textOf(started, "Secret"));
    printf("\n   Quick Connect code:  %s\n\n", textOf(started, "Code"));
    say("In Jellyfin, on your phone or PC: your user, Quick Connect, enter the code. + stops.\n");
    json_decref(started);
    if (!waitForApproval(secret)) {
        say("Not approved.\n");
        return false;
    }
    char body[SECRET_MAX + 32];
    snprintf(body, sizeof(body), "{\"Secret\":\"%s\"}", secret);
    json_t *session = api("/Users/AuthenticateWithQuickConnect", body, NULL);
    const char *token = textOf(session, "AccessToken");
    const char *user = textOf(json_object_get(session, "User"), "Id");
    bool ok = session && token[0] && user[0];
    if (ok) {
        setAuthorization(token);
        snprintf(g_user, sizeof(g_user), "%s", user);
        g_signed_in = true;
        say("Signed in.\n");
    } else {
        say("The sign-in failed.\n");
    }
    json_decref(session);
    return ok;
}

static bool readVideo(const json_t *item, Video *v)
{
    const json_t *source = json_array_get(json_object_get(item, "MediaSources"), 0);
    if (!source)
        return false;
    memset(v, 0, sizeof(*v));
    snprintf(v->id, sizeof(v->id), "%s", textOf(item, "Id"));
    snprintf(v->source, sizeof(v->source), "%s", textOf(source, "Id"));
    v->size = json_integer_value(json_object_get(source, "Size"));
    v->bitrate = json_integer_value(json_object_get(source, "Bitrate"));
    size_t i;
    const json_t *stream;
    json_array_foreach(json_object_get(source, "MediaStreams"), i, stream) {
        if (strcmp(textOf(stream, "Type"), "Video"))
            continue;
        snprintf(v->codec, sizeof(v->codec), "%s", textOf(stream, "Codec"));
        v->width = (int)json_integer_value(json_object_get(stream, "Width"));
        v->height = (int)json_integer_value(json_object_get(stream, "Height"));
        break;
    }
    return v->size > 0;
}

typedef struct {
    uint64_t start, first_ns, deadline;
    int64_t bytes;
} Reading;

static bool countBytes(const void *data, size_t size, void *user)
{
    (void)data;
    Reading *r = user;
    uint64_t now = nowNs();
    if (!r->first_ns)
        r->first_ns = now - r->start;
    r->bytes += (int64_t)size;
    return !r->deadline || now < r->deadline;
}

static bool readRange(const Video *v, int64_t from, int64_t count, uint64_t limit_ns, Reading *r)
{
    char url[URL_MAX], range[64], error[NET_ERROR_MAX] = "";
    snprintf(url, sizeof(url), "%s/Videos/%s/stream?static=true&mediaSourceId=%s", g_server, v->id, v->source);
    if (count > 0)
        snprintf(range, sizeof(range), "%" PRId64 "-%" PRId64, from, from + count - 1);
    else
        snprintf(range, sizeof(range), "%" PRId64 "-", from);
    NetRequest request = {.url = url, .authorization = g_auth, .range = range};
    *r = (Reading){.start = nowNs()};
    r->deadline = limit_ns ? r->start + limit_ns : 0;
    long status = 0;
    bool ok = netFetch(g_net, &request, countBytes, r, &status, error, sizeof(error));
    if (!ok)
        say("   read failed (%ld): %s\n", status, error);
    return ok;
}

static void testVideo(const char *label, const Video *v)
{
    double mbps = (double)v->bitrate / 1e6;
    say("\n== %s: %s %dx%d, %.1f Mb/s, %.1f GB\n", label, v->codec, v->width, v->height, mbps,
        (double)v->size / (1024.0 * 1024 * 1024));
    Reading r;
    if (readRange(v, v->size / 10, 0, READ_NS, &r)) {
        double seconds = (double)(nowNs() - r.start) / NS_PER_S;
        double got = (double)r.bytes * BITS_PER_BYTE / seconds / 1e6;
        say("   straight read: %.1f Mb/s (%.1fx the video), first byte %.0f ms\n", got, mbps > 0 ? got / mbps : 0,
            (double)r.first_ns / NS_PER_MS);
    }
    double first_sum = 0, whole_sum = 0, worst = 0;
    int done = 0;
    for (int i = 0; i < JUMP_COUNT; i++) {
        if (!readRange(v, (int64_t)((double)v->size * JUMP_POINTS[i]), JUMP_BYTES, 0, &r))
            continue;
        double whole = (double)(nowNs() - r.start) / NS_PER_MS;
        first_sum += (double)r.first_ns / NS_PER_MS;
        whole_sum += whole;
        if (whole > worst)
            worst = whole;
        done++;
    }
    if (done)
        say("   jumps (%d): first byte %.0f ms, 1 MB in %.0f ms, slowest %.0f ms\n", done, first_sum / done,
            whole_sum / done, worst);
}

static void testPictures(json_t *items)
{
    double sum = 0, worst = 0;
    int64_t bytes = 0;
    int done = 0;
    size_t i;
    json_t *item;
    json_array_foreach(items, i, item) {
        if (done == PICTURES)
            break;
        char url[URL_MAX], error[NET_ERROR_MAX] = "";
        snprintf(url, sizeof(url), "%s/Items/%s/Images/Primary?maxHeight=360&quality=85", g_server,
                 textOf(item, "Id"));
        NetRequest request = {.url = url, .authorization = g_auth, .timeout_s = API_TIMEOUT_S};
        Reading r = {.start = nowNs()};
        if (!netFetch(g_net, &request, countBytes, &r, NULL, error, sizeof(error)))
            continue;
        double ms = (double)(nowNs() - r.start) / NS_PER_MS;
        sum += ms;
        bytes += r.bytes;
        if (ms > worst)
            worst = ms;
        done++;
    }
    if (done)
        say("\nPictures (%d): %.0f ms each, slowest %.0f ms, %.0f KB each\n", done, sum / done, worst,
            (double)bytes / done / 1024.0);
}

static void runTests(void)
{
    char path[512];
    snprintf(path, sizeof(path),
             "/Items?userId=%s&Recursive=true&IncludeItemTypes=Movie,Episode&Fields=MediaSources"
             "&SortBy=DateCreated&SortOrder=Descending&Limit=%d&enableTotalRecordCount=true",
             g_user, LIST_LIMIT);
    double ms = 0;
    json_t *list = api(path, NULL, &ms);
    json_t *items = json_object_get(list, "Items");
    if (!items) {
        say("The library couldn't be listed.\n");
        json_decref(list);
        return;
    }
    say("Library: %" JSON_INTEGER_FORMAT " videos; the newest %zu with details in %.0f ms\n",
        json_integer_value(json_object_get(list, "TotalRecordCount")), json_array_size(items), ms);
    Video hevc = {0}, h264 = {0}, v;
    size_t i;
    json_t *item;
    json_array_foreach(items, i, item) {
        if (!readVideo(item, &v) || v.height < MIN_HD_HEIGHT || v.width > MAX_HD_WIDTH)
            continue;
        if (!hevc.id[0] && !strcmp(v.codec, "hevc"))
            hevc = v;
        if (!h264.id[0] && !strcmp(v.codec, "h264"))
            h264 = v;
    }
    if (hevc.id[0])
        testVideo("1080p HEVC", &hevc);
    if (h264.id[0])
        testVideo("1080p H.264", &h264);
    testPictures(items);
    json_decref(list);
}

static void signOut(void)
{
    if (!g_signed_in)
        return;
    char url[URL_MAX];
    snprintf(url, sizeof(url), "%s/Sessions/Logout", g_server);
    NetRequest request = {.url = url, .authorization = g_auth, .post_json = "{}", .timeout_s = API_TIMEOUT_S};
    NetResponse r;
    bool ok = netFetchText(g_net, &request, 4096, &r);
    if (ok)
        netResponseFree(&r);
    say("\n%s\n", ok ? "Signed out." : "The sign-out failed: remove this device in Jellyfin.");
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&g_pad);
    g_log = fopen(LOG_PATH, "w");
    say("KaiVideo Jellyfin test\n");
    if (!readServer())
        say("No server in " SERVER_PATH ".\n");
    else if (!netInit())
        say("The network can't be used.\n");
    else {
        g_net = netClientOpen();
        say("Server: %s\n", g_server);
        if (g_net && signIn())
            runTests();
        signOut();
        netClientClose(g_net);
        netExit();
    }
    say("\nDone. Press + to exit.\n");
    while (appletMainLoop() && !plusPressed()) {
        consoleUpdate(NULL);
        svcSleepThread(TICK_NS);
    }
    if (g_log)
        fclose(g_log);
    consoleExit(NULL);
    return 0;
}
