// Runs a conversion against a real Jellyfin server on a PC: asks for the first
// video the console can't play as it is, reads it through hlsio.c, checks its
// times start near zero, and jumps.   host_converted FOLDER
// FOLDER holds a signed-in jellyfin.txt. Prints no titles.
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <switch.h>

#include "hlsio.h"
#include "jfsync.h"
#include "video.h"

#define PATH_MAX_LEN 512
#define WAIT_MS 90000
#define NS_PER_MS 1000000ll
#define NS_PER_S 1000000000ll
#define MAX_BITRATE 8000000
#define START_TOLERANCE_NS (8 * NS_PER_S)
#define JUMP_TOLERANCE_NS (8 * NS_PER_S)

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
    nanosleep(&(struct timespec){0, ms * NS_PER_MS}, NULL);
}

static int64_t nowMs(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000 + t.tv_nsec / NS_PER_MS;
}

static bool waitFrame(Video *v, int64_t hold_ns, int64_t *pts)
{
    for (int i = 0; i < WAIT_MS; i++) {
        videoFrameAt(v, hold_ns);
        if (videoNextPts(v, pts))
            return true;
        char error[96] = "";
        if (videoFinished(v, error, sizeof(error))) {
            printf("ended early: %s\n", error);
            return false;
        }
        sleepMs(1);
    }
    return false;
}

static void playConverted(const char *location)
{
    JfPlan plan;
    char error[160] = "", url[JF_URL_MAX + JF_TRANSCODE_URL_MAX], auth[JF_AUTH_MAX];
    EXPECT(jfSyncResolve(location, MAX_BITRATE, -1, &plan, error, sizeof(error), NULL), error);
    EXPECT(!plan.direct, "the server chose to send the original");
    printf("%d sound tracks, %d text subtitle tracks\n", plan.audio_count, plan.sub_count);
    jfSyncConvertedRequest(plan.transcode_url, url, sizeof(url), auth, sizeof(auth));
    int64_t t = nowMs();
    EXPECT(hlsStart(url, auth, 0, NULL, error, sizeof(error)), error);
    Video *v = videoOpen(HLS_LOCATION, error, sizeof(error));
    EXPECT(v != NULL, error);
    if (v) {
        printf("%s, %.0f s long, opened in %d ms\n", videoDescription(v), videoDuration(v) / 1e9, (int)(nowMs() - t));
        int64_t pts = -1;
        EXPECT(waitFrame(v, -1, &pts), "no first frame");
        printf("first frame at %.3f s\n", pts / 1e9);
        EXPECT(pts >= 0 && pts < START_TOLERANCE_NS, "the times don't start near zero");
        int64_t target = videoDuration(v) / 2;
        t = nowMs();
        videoSeek(v, target);
        EXPECT(waitFrame(v, target - 1, &pts), "no frame after the jump");
        printf("jumped to %.1f, frame at %.1f, in %d ms\n", target / 1e9, pts / 1e9, (int)(nowMs() - t));
        EXPECT(pts >= target - NS_PER_S && pts < target + JUMP_TOLERANCE_NS, "the jump landed elsewhere");
        videoClose(v);
    }
    hlsStop();
    jfSyncSetPlayback(plan.play_session, true);
    jfSyncEndConversion(location);
}

int main(int argc, char **argv)
{
    if (argc < 2)
        return 2;
    char session[PATH_MAX_LEN], catalog[PATH_MAX_LEN];
    snprintf(session, sizeof(session), "%s/jellyfin.txt", argv[1]);
    snprintf(catalog, sizeof(catalog), "%s/catalog.txt", argv[1]);
    EXPECT(jfSyncStart(session, catalog, "test", true), "didn't start");
    JfCatalog c;
    bool have = false;
    for (int i = 0; i < WAIT_MS && !have; i++) {
        have = jfSyncTakeCatalog(&c);
        sleepMs(1);
    }
    EXPECT(have, "no library");
    int chosen = -1;
    for (size_t i = 0; have && i < c.library.count && chosen < 0; i++)
        if (c.library.items[i].blocked == JF_HDR || c.library.items[i].blocked == JF_TOO_LARGE)
            chosen = (int)i;
    EXPECT(chosen >= 0, "nothing needs converting");
    if (chosen >= 0)
        playConverted(c.library.items[chosen].path);
    if (have)
        jfCatalogFree(&c);
    jfSyncStop();
    printf("%d failures\n", g_failures);
    return g_failures ? 1 : 0;
}
