// Runs video.c's software path on a PC, as the player would: plays by a clock,
// seeks back and forth (also rapidly), reaches the end and closes mid-play.
//   host_video VIDEO...
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <switch.h>

#include "video.h"

#define NS_PER_MS 1000000ll
#define STEP_NS (20 * NS_PER_MS)
#define WAIT_LIMIT_MS 20000
#define SEEK_STORM 12

static int g_failures;

#define EXPECT(cond, what)                                 \
    do {                                                   \
        if (!(cond)) {                                     \
            printf("    FAILED: %s\n", what);              \
            g_failures++;                                  \
        }                                                  \
    } while (0)

static void sleepMs(int ms)
{
    struct timespec t = {0, ms * NS_PER_MS};
    nanosleep(&t, NULL);
}

// Waits for the first frame after opening or a seek; false if it never comes.
static bool waitFirst(Video *v, int64_t *pts)
{
    for (int waited = 0; waited < WAIT_LIMIT_MS; waited++) {
        if (videoNextPts(v, pts))
            return true;
        sleepMs(1);
    }
    return false;
}

// Plays media_ns from the first frame; the frames must come in order and with a picture.
static int play(Video *v, int64_t first, int64_t media_ns)
{
    int shown = 0;
    unsigned last_id = 0;
    for (int64_t clock = first; clock < first + media_ns; clock += STEP_NS) {
        const GfxVideoFrame *f = videoFrameAt(v, clock);
        if (f && f->id != last_id) {
            last_id = f->id;
            shown++;
            EXPECT(f->y && f->uv && f->width > 0 && f->height > 0, "a frame without pixels");
        }
        sleepMs(2);
    }
    return shown;
}

static void testFile(const char *path)
{
    char error[160] = "";
    Video *v = videoOpen(path, error, sizeof(error));
    printf("%s\n", path);
    if (!v) {
        printf("    FAILED to open: %s\n", error);
        g_failures++;
        return;
    }
    printf("    %s\n", videoDescription(v));
    EXPECT(strstr(videoDescription(v), "software") != NULL, "not on the software path");
    int64_t first = 0;
    EXPECT(waitFirst(v, &first), "no first frame");
    EXPECT(play(v, first, 1000 * NS_PER_MS) > 5, "too few frames in the first second");

    int64_t target = 4000 * NS_PER_MS, pts = 0;
    videoSeek(v, target);
    EXPECT(waitFirst(v, &pts), "no frame after a seek");
    EXPECT(pts >= target, "a frame from before the seek's target");
    EXPECT(play(v, pts, 500 * NS_PER_MS) > 2, "too few frames after a seek");

    for (int i = 0; i < SEEK_STORM; i++) {
        videoSeek(v, (int64_t)(i * 7 % 9) * 1000 * NS_PER_MS);
        sleepMs(i % 4);
    }
    target = 1000 * NS_PER_MS;
    videoSeek(v, target);
    EXPECT(waitFirst(v, &pts) && pts >= target, "no right frame after many seeks");

    // On to the end: everything is taken, and then it's finished.
    int64_t end = videoDuration(v) + 2000 * NS_PER_MS;
    char finished_error[96] = "";
    int waited = 0;
    while (!videoFinished(v, finished_error, sizeof(finished_error)) && waited++ < WAIT_LIMIT_MS) {
        videoFrameAt(v, end);
        sleepMs(1);
    }
    EXPECT(waited < WAIT_LIMIT_MS, "never reached the end");
    EXPECT(finished_error[0] == '\0', finished_error);

    // A seek back after the end still works, and closing while playing is fine.
    videoSeek(v, 0);
    EXPECT(waitFirst(v, &pts), "no frame after seeking back from the end");
    VideoStats stats = videoStats(v);
    printf("    %u decoded, %u shown, %u dropped, convert %.2f ms\n", stats.decoded, stats.shown, stats.dropped,
           stats.copy_ms);
    videoClose(v);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
        testFile(argv[i]);
    printf("%d failures\n", g_failures);
    return g_failures ? 1 : 0;
}
