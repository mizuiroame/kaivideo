// Runs the Jellyfin path on a PC against fake_server.py: signs in, reads the
// library, plays its video through the network reader (also jumping), tells
// the server, and signs out.   host_jellyfin SERVER WORK_FOLDER
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <switch.h>

#include <libavutil/error.h>

#include "blockcache.h"
#include "hlsio.h"
#include "jfprobe.h"
#include "tpfetch.h"
#include "jfsync.h"
#include "mediafile.h"
#include "video.h"

#define WAIT_MS 20000
#define NS_PER_MS 1000000ll
#define PATH_MAX_LEN 512
#define BIG_LOCATION JF_PREFIX "big/big"
#define READ_SIZE (64 * 1024)
#define INTERRUPT_AFTER_MS 200
#define QUICK_MS 1500

static int g_failures;

#define EXPECT(cond, what)                     \
    do {                                       \
        if (!(cond)) {                         \
            printf("FAILED: %s\n", what);      \
            g_failures++;                      \
        }                                      \
    } while (0)

static void sleepMs(int ms)
{
    struct timespec t = {0, ms * NS_PER_MS};
    nanosleep(&t, NULL);
}

static bool waitState(JfSignState state)
{
    JfStatus s;
    for (int i = 0; i < WAIT_MS; i++) {
        jfSyncGetStatus(&s);
        if (s.state == state)
            return true;
        sleepMs(1);
    }
    return false;
}

static bool waitCatalog(JfCatalog *c)
{
    for (int i = 0; i < WAIT_MS; i++) {
        if (jfSyncTakeCatalog(c))
            return true;
        sleepMs(1);
    }
    return false;
}

static void playIt(const char *location)
{
    char error[160] = "";
    Video *v = videoOpen(location, error, sizeof(error));
    if (!v) {
        printf("FAILED to open: %s\n", error);
        g_failures++;
        return;
    }
    int64_t pts = 0;
    bool first = false;
    for (int i = 0; i < WAIT_MS && !first; i++) {
        first = videoNextPts(v, &pts);
        sleepMs(1);
    }
    EXPECT(first, "no first frame over the network");
    int64_t target = videoDuration(v) / 2;
    videoSeek(v, target);
    first = false;
    for (int i = 0; i < WAIT_MS && !first; i++) {
        videoFrameAt(v, target - 1);
        first = videoNextPts(v, &pts);
        sleepMs(1);
    }
    EXPECT(first && pts >= target, "no frame after a jump over the network");
    printf("%s; jumped to %.1f s\n", videoDescription(v), pts / 1e9);
    videoClose(v);
}

static int64_t nowMs(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000 + t.tv_nsec / NS_PER_MS;
}

static void setServerFailing(const char *folder, bool failing)
{
    char path[PATH_MAX_LEN];
    snprintf(path, sizeof(path), "%s/log.fail", folder);
    if (failing) {
        FILE *f = fopen(path, "w");
        if (f)
            fclose(f);
    } else {
        remove(path);
    }
}

// The server stops answering: what was fetched is still read, then an error, not an end.
static void testServerFails(const char *folder)
{
    static uint8_t buffer[READ_SIZE];
    MediaFile *f = mediaFileOpen(BIG_LOCATION);
    EXPECT(f != NULL, "the big file didn't open");
    if (!f)
        return;
    setServerFailing(folder, true);
    int64_t total = 0;
    int got;
    while ((got = mediaFileRead(f, buffer, READ_SIZE)) > 0)
        total += got;
    setServerFailing(folder, false);
    EXPECT(total >= BLOCK_SIZE, "what was fetched wasn't given");
    EXPECT(got != AVERROR_EOF && mediaFileFailed(f), "a failed server looked like the file's end");
    printf("read %.1f MB before the failure\n", total / 1048576.0);
    mediaFileClose(f);
}

static void *interruptSoon(void *arg)
{
    (void)arg;
    sleepMs(INTERRUPT_AFTER_MS);
    mediaFileInterruptAll();
    return NULL;
}

// A read waiting for a server that doesn't answer gives up when interrupted, and closing is quick.
static void testInterrupt(const char *folder)
{
    static uint8_t buffer[READ_SIZE];
    MediaFile *f = mediaFileOpen(BIG_LOCATION);
    EXPECT(f != NULL, "the big file didn't open again");
    if (!f)
        return;
    setServerFailing(folder, true);
    mediaFileSeek(f, 100 * BLOCK_SIZE, SEEK_SET);
    pthread_t thread;
    pthread_create(&thread, NULL, interruptSoon, NULL);
    int64_t start = nowMs();
    int got = mediaFileRead(f, buffer, READ_SIZE);
    int64_t waited = nowMs() - start;
    pthread_join(thread, NULL);
    EXPECT(got < 0 && !mediaFileFailed(f), "an interrupted read");
    EXPECT(waited < QUICK_MS, "an interrupted read took too long");
    start = nowMs();
    mediaFileClose(f);
    EXPECT(nowMs() - start < QUICK_MS, "closing took too long");
    setServerFailing(folder, false);
    printf("interrupted after %d ms\n", (int)waited);
}

// A converted stream started partway: its times are the video's, and its length the whole.
static void testConvertedStart(const char *server)
{
    enum { START_S = 9, WHOLE_MIN_S = 9 };
    char url[PATH_MAX_LEN], error[160] = "";
    snprintf(url, sizeof(url), "%s/hls/master.m3u8", server);
    EXPECT(hlsStart(url, "none", START_S * 1000000000ll, error, sizeof(error)), error);
    EXPECT(hlsBaseNs() > 0 && hlsBaseNs() <= START_S * 1000000000ll, "the stream didn't start partway");
    Video *v = videoOpen(HLS_LOCATION, error, sizeof(error));
    EXPECT(v != NULL, error);
    if (v) {
        int64_t pts = -1;
        bool got = false;
        for (int i = 0; i < WAIT_MS && !got; i++) {
            got = videoNextPts(v, &pts);
            sleepMs(1);
        }
        EXPECT(got && pts >= hlsBaseNs() - 100 * NS_PER_MS && pts < hlsBaseNs() + 3000 * NS_PER_MS,
               "the first frame isn't at the start asked for");
        EXPECT(videoDuration(v) >= WHOLE_MIN_S * 1000000000ll, "the length isn't the whole video's");
        printf("started at %.2f s, first frame %.2f s, %.1f s long\n", hlsBaseNs() / 1e9, pts / 1e9,
               videoDuration(v) / 1e9);
        videoClose(v);
    }
    hlsStop();
}

static void setSegmentSlow(const char *folder, bool slow)
{
    char path[PATH_MAX_LEN];
    snprintf(path, sizeof(path), "%s/log.slow", folder);
    if (!slow) {
        remove(path);
        return;
    }
    FILE *f = fopen(path, "w");
    if (f)
        fclose(f);
}

// A frame at or after at_ns, within limit_ms. How long it took, -1 if it didn't come.
static int waitForFrame(Video *v, int64_t at_ns, int limit_ms)
{
    int64_t start = nowMs(), pts;
    while (nowMs() - start < limit_ms) {
        if (videoNextPts(v, &pts) && pts >= at_ns)
            return (int)(nowMs() - start);
        sleepMs(1);
    }
    return -1;
}

// The server is late with a segment: a jump elsewhere doesn't wait for it, and
// playback doesn't count on it until it's here.
static void testJumpWhileWaiting(const char *server, const char *folder)
{
    enum { IN_SECOND_S = 9, BACK_S = 1, NEAR_END_OF_FIRST_S = 7 };
    char url[PATH_MAX_LEN], error[160] = "";
    snprintf(url, sizeof(url), "%s/hls/master.m3u8", server);
    setSegmentSlow(folder, true);
    EXPECT(hlsStart(url, "none", 0, error, sizeof(error)), error);
    Video *v = videoOpen(HLS_LOCATION, error, sizeof(error));
    EXPECT(v != NULL, error);
    if (v) {
        EXPECT(waitForFrame(v, 0, WAIT_MS) >= 0, "no first frame");
        EXPECT(hlsCushionReady(0), "the first segment alone covers its start");
        EXPECT(!hlsCushionReady(NEAR_END_OF_FIRST_S * 1000000000ll), "counted on a segment that isn't here");
        double ready = hlsSecondsReady(1000000000ll);
        EXPECT(ready > 7.2 && ready < 7.4, "the seconds ready aren't the first segment's rest");
        EXPECT(netBytesReceived() > 0, "nothing counted as received");
        videoSeek(v, IN_SECOND_S * 1000000000ll);
        sleepMs(INTERRUPT_AFTER_MS);  // It's waiting for the late segment now.
        videoSeek(v, BACK_S * 1000000000ll);
        int waited = waitForFrame(v, BACK_S * 1000000000ll, QUICK_MS);
        EXPECT(waited >= 0, "a jump waited for a segment it doesn't need");
        printf("jumped away from a late segment in %d ms\n", waited);
        setSegmentSlow(folder, false);
        for (int i = 0; i < WAIT_MS && !hlsCushionReady(NEAR_END_OF_FIRST_S * 1000000000ll); i++)
            sleepMs(1);
        EXPECT(hlsCushionReady(NEAR_END_OF_FIRST_S * 1000000000ll), "the late segment never counted");
        videoClose(v);
    }
    setSegmentSlow(folder, false);
    hlsStop();
}

// Live TV: the list grows while it plays, and playback goes on into what was added.
static void testLiveList(const char *server)
{
    enum { INTO_SECOND_S = 9, LIVE_WAIT_MS = 30000 };
    char url[PATH_MAX_LEN], error[160] = "";
    snprintf(url, sizeof(url), "%s/hlslive/master.m3u8", server);
    EXPECT(hlsStart(url, "none", 0, error, sizeof(error)), error);
    // Only the first segment is made yet: playing from it would catch up with the server.
    EXPECT(hlsIsLive() && !hlsCushionReady(0), "live TV would start at the edge of what's made");
    Video *v = videoOpen(HLS_LOCATION, error, sizeof(error));
    EXPECT(v != NULL, error);
    if (v) {
        int64_t pts = -1, last = -1;
        int64_t start = nowMs();
        // Takes frames as they come (as the player shows them), until one past the first segment.
        while (nowMs() - start < LIVE_WAIT_MS && last < INTO_SECOND_S * 1000000000ll) {
            if (videoNextPts(v, &pts)) {
                last = pts;
                videoFrameAt(v, pts);
            }
            sleepMs(1);
        }
        EXPECT(last >= INTO_SECOND_S * 1000000000ll, "playback didn't go on into the segment added later");
        printf("live: reached %.2f s after %d ms\n", last / 1e9, (int)(nowMs() - start));
        videoClose(v);
    }
    hlsStop();
}

static bool waitProbe(JfProbeResult *r)
{
    for (int i = 0; i < WAIT_MS; i++) {
        if (jfProbeTake(r))
            return true;
        sleepMs(1);
    }
    return false;
}

// Settings' questions: finding the server on the network, and testing addresses.
static void testProbes(const char *server)
{
    JfProbeResult r;
    EXPECT(jfProbeStartFind("127.0.0.1") && waitProbe(&r), "the search didn't finish");
    EXPECT(r.finding && r.ok && !strcmp(r.found, server), "the server wasn't found at the address that answered");
    printf("find: %s\n", r.message);
    EXPECT(jfProbeStartTest(server) && waitProbe(&r), "the test didn't finish");
    EXPECT(!r.finding && r.ok && strstr(r.message, "Fake Den"), "the server didn't pass the test");
    EXPECT(jfProbeStartTest("http://127.0.0.1:1") && waitProbe(&r), "the test didn't finish");
    EXPECT(!r.ok && strstr(r.message, "port"), "a closed port wasn't named");
    printf("test: %s\n", r.message);
}

int main(int argc, char **argv)
{
    if (argc < 3)
        return 2;
    char session[PATH_MAX_LEN], catalog[PATH_MAX_LEN];
    snprintf(session, sizeof(session), "%s/jellyfin.txt", argv[2]);
    snprintf(catalog, sizeof(catalog), "%s/catalog.txt", argv[2]);
    EXPECT(jfSyncStart(session, catalog, "test", true), "didn't start");
    testProbes(argv[1]);
    EXPECT(jfSyncSetServer(argv[1]), "the server's address");
    jfSyncSignIn();
    EXPECT(waitState(JF_SIGNED_IN), "didn't sign in");
    JfCatalog c;
    EXPECT(waitCatalog(&c), "no library");
    EXPECT(c.library.count == 1 && c.library.film_count == 1, "wrong library");
    EXPECT(c.library.collection_count == 1 && !strcmp(c.library.collections[0].name, "Fake Films") &&
               c.library.count == 1 && c.library.items[0].collection == 0 && c.library.items[0].favorite,
           "the server's library or the favorite didn't come");
    if (c.library.count == 1) {
        char location[LIBRARY_PATH_MAX];
        snprintf(location, sizeof(location), "%s", c.library.items[0].path);
        playIt(location);
        testConvertedStart(argv[1]);
        testJumpWhileWaiting(argv[1], argv[2]);
        testLiveList(argv[1]);
        testServerFails(argv[2]);
        testInterrupt(argv[2]);
        jfSyncReport(JF_REPORT_START, location, 0, false);
        jfSyncReport(JF_REPORT_PROGRESS, location, NS_PER_MS, false);
        jfSyncReport(JF_REPORT_PROGRESS, location, 2 * NS_PER_MS, true);
        jfSyncReport(JF_REPORT_STOP, location, 3 * NS_PER_MS, false);
        jfSyncSetPlayed(location, true);
        jfSyncSetPlayed(location, false);
        // The server's trickplay: the picture of 1.5 s is the sheet's second, green.
        tpfetchStart(location);
        const uint32_t *picture = NULL;
        int w = 0, h = 0;
        bool changed = false;
        for (int i = 0; i < WAIT_MS && !picture; i++) {
            picture = tpfetchPicture(1500 * NS_PER_MS, &w, &h, &changed);
            sleepMs(1);
        }
        uint32_t pixel = picture ? picture[10 * w + 20] : 0;
        EXPECT(picture && w == 40 && h == 20 && changed && (pixel & 0xFF) < 100 && ((pixel >> 8) & 0xFF) > 150,
               "the trickplay picture didn't come, or isn't the right one");
        bool again;
        EXPECT(tpfetchPicture(1600 * NS_PER_MS, &w, &h, &again) == picture && !again, "the same picture was cut again");
        tpfetchStop();
        JfSegment segments[JF_SEGMENTS_MAX];
        EXPECT(jfSyncFetchSegments(location, segments, JF_SEGMENTS_MAX) == 1 && segments[0].kind == JF_SEGMENT_INTRO &&
                   segments[0].end_ns == 5000000000ll,
               "the intro didn't come from the server");
        jfSyncSetFavorite(location, false);
        jfSyncSetFavorite(JF_PREFIX "eeee1111/" JF_SERIES_SOURCE, true);
    }
    jfCatalogFree(&c);
    jfSyncSignOut();
    EXPECT(waitState(JF_SIGNED_OUT), "didn't sign out");
    jfSyncStop();
    printf("%d failures\n", g_failures);
    return g_failures ? 1 : 0;
}
