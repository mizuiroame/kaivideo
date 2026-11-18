// Runs video.c's software path on a PC, as the player would: plays by a clock,
// seeks back and forth (also rapidly), reaches the end and closes mid-play.
//   host_video VIDEO...
#include <stdio.h>
#include <stdlib.h>
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
    unsigned previews = videoStats(v).previews, id_before = videoFrameAt(v, first)->id;
    videoSeek(v, target);
    const GfxVideoFrame *preview = NULL;
    for (int waited = 0; waited < WAIT_LIMIT_MS && videoStats(v).previews == previews; waited++) {
        preview = videoFrameAt(v, target - 1);  // Before the target: only a preview can come.
        sleepMs(1);
    }
    EXPECT(videoStats(v).previews == previews + 1, "no picture while seeking");
    EXPECT(preview && preview->id != id_before && preview->y, "the seek's picture isn't shown");
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
    printf("    %u decoded, %u shown, %u dropped, %u previews, convert %.2f ms\n", stats.decoded, stats.shown,
           stats.dropped, stats.previews, stats.copy_ms);
    videoClose(v);
}

static char *readWhole(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *data = malloc((size_t)length);
    *size = data ? fread(data, 1, (size_t)length, f) : 0;
    fclose(f);
    return data;
}

// The picture over a gray background, as a PPM to look at.
static void dumpPicture(const AssPicture *p, int width, int height, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "P6 %d %d 255\n", width, height);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint32_t c = 0;
            if (x >= p->x && x < p->x + p->w && y >= p->y && y < p->y + p->h)
                c = p->pixels[(y - p->y) * p->w + x - p->x];
            unsigned a = c >> 24;
            for (int shift = 0; shift <= 16; shift += 8)
                fputc((int)((((c >> shift) & 0xFF) * a + 90 * (255 - a)) / 255), f);
        }
    }
    fclose(f);
}

// A file with ASS: its lines come out as pictures at their times.
static void testStyled(const char *path, const char *dump)
{
    char error[160] = "";
    Video *v = videoOpen(path, error, sizeof(error));
    printf("%s (styled)\n", path);
    if (!v) {
        printf("    FAILED to open: %s\n", error);
        g_failures++;
        return;
    }
    enum { WIDTH = 1280, HEIGHT = 720, STEP_MS = 40, LOOK_MS = 6000 };
    const AssArea area = {WIDTH, HEIGHT, 1.0, 0.0};
    AssPicture p = {0}, best = {0};
    int shown = 0, changes = 0;
    unsigned last_id = 0;
    bool styled = true;
    for (int ms = 0; ms < LOOK_MS && styled; ms += STEP_MS) {
        int64_t at = ms * NS_PER_MS;
        videoFrameAt(v, at);
        sleepMs(STEP_MS / 4);
        styled = videoSubtitlePicture(v, 0, at, &area, &p);
        if (p.w > 0)
            shown++;
        if (p.id != last_id)
            changes++;
        last_id = p.id;
        if (p.w * p.h > best.w * best.h) {
            best = p;
            if (dump)
                dumpPicture(&p, WIDTH, HEIGHT, dump);
        }
    }
    EXPECT(styled, "the ASS track isn't drawn with its styles");
    EXPECT(shown > 0, "no styled line was drawn");
    EXPECT(changes > 1, "the picture never changed");
    EXPECT(!videoSubtitlePicture(v, VIDEO_SUB_TRACKS, 0, &area, &p), "a track that doesn't exist");

    // Lifted for the bar: the line at the bottom goes up, the sign at the top stays.
    const int64_t two_lines_ns = 500 * NS_PER_MS;
    const AssArea lifted = {WIDTH, HEIGHT, 1.0, 20.0};
    videoSubtitlePicture(v, 0, two_lines_ns, &area, &p);
    int bottom = p.y + p.h, top = p.y;
    videoSubtitlePicture(v, 0, two_lines_ns, &lifted, &p);
    EXPECT(p.y + p.h < bottom - HEIGHT / 10, "the bottom line didn't go up");
    EXPECT(p.y == top, "the sign at the top moved");
    printf("    %d of the moments looked at had a line, %d changes, largest %dx%d\n", shown, changes, best.w, best.h);
    videoClose(v);
}

// host_video [--styled FILE [DUMP.ppm]] VIDEO...
int main(int argc, char **argv)
{
    size_t regular_size = 0;
    char *regular = readWhole("data/font_regular.ttf", &regular_size);
    AssFont fonts[1] = {{"font_regular.ttf", regular, regular_size}};
    videoSetSubtitleFonts(fonts, regular ? 1 : 0);
    int first = 1;
    if (argc > 2 && !strcmp(argv[1], "--styled")) {
        bool has_dump = argc > 3 && strstr(argv[3], ".ppm");
        testStyled(argv[2], has_dump ? argv[3] : NULL);
        first = has_dump ? 4 : 3;
    }
    for (int i = first; i < argc; i++)
        testFile(argv[i]);
    free(regular);
    printf("%d failures\n", g_failures);
    return g_failures ? 1 : 0;
}
