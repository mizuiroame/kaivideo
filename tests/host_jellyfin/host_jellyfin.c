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

int main(int argc, char **argv)
{
    if (argc < 3)
        return 2;
    char session[PATH_MAX_LEN], catalog[PATH_MAX_LEN];
    snprintf(session, sizeof(session), "%s/jellyfin.txt", argv[2]);
    snprintf(catalog, sizeof(catalog), "%s/catalog.txt", argv[2]);
    EXPECT(jfSyncStart(session, catalog, "test", true), "didn't start");
    EXPECT(jfSyncSetServer(argv[1]), "the server's address");
    jfSyncSignIn();
    EXPECT(waitState(JF_SIGNED_IN), "didn't sign in");
    JfCatalog c;
    EXPECT(waitCatalog(&c), "no library");
    EXPECT(c.library.count == 1 && c.library.film_count == 1, "wrong library");
    if (c.library.count == 1) {
        char location[LIBRARY_PATH_MAX];
        snprintf(location, sizeof(location), "%s", c.library.items[0].path);
        playIt(location);
        testServerFails(argv[2]);
        testInterrupt(argv[2]);
        jfSyncReport(JF_REPORT_START, location, 0, false);
        jfSyncReport(JF_REPORT_PROGRESS, location, NS_PER_MS, false);
        jfSyncReport(JF_REPORT_PROGRESS, location, 2 * NS_PER_MS, true);
        jfSyncReport(JF_REPORT_STOP, location, 3 * NS_PER_MS, false);
        jfSyncSetPlayed(location, true);
        jfSyncSetPlayed(location, false);
    }
    jfCatalogFree(&c);
    jfSyncSignOut();
    EXPECT(waitState(JF_SIGNED_OUT), "didn't sign out");
    jfSyncStop();
    printf("%d failures\n", g_failures);
    return g_failures ? 1 : 0;
}
