// KaiVideo: a video player for Switch 2 homebrew (kaiHEN + libounce). Video
// is decoded by the console's hardware decoder and drawn with the GPU.
//
// For now (stage 2) it plays every video in sdmc:/kaivideo-test, full
// screen, one after another: B goes to the next, + exits. What each played
// like goes to sdmc:/kaivideo.log.
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <switch.h>

#include "gfx.h"
#include "video.h"

#define FOLDER "sdmc:/kaivideo-test"
#define LOG_PATH "sdmc:/kaivideo.log"
#define MAX_FILES 64
#define NAME_MAX_LEN 256

static FILE *g_log;

static void logLine(const char *fmt, ...)
{
    if (!g_log)
        return;
    va_list args;
    va_start(args, fmt);
    vfprintf(g_log, fmt, args);
    va_end(args);
    fputc('\n', g_log);
    fflush(g_log);
}

static bool isVideo(const char *name)
{
    const char *dot = strrchr(name, '.');
    return name[0] != '.' && dot && (!strcasecmp(dot, ".mp4") || !strcasecmp(dot, ".mkv") || !strcasecmp(dot, ".webm"));
}

static int compareNames(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

static size_t listVideos(char names[][NAME_MAX_LEN], size_t max)
{
    DIR *dir = opendir(FOLDER);
    if (!dir)
        return 0;
    size_t count = 0;
    struct dirent *e;
    while (count < max && (e = readdir(dir)) != NULL)
        if (isVideo(e->d_name) && strlen(e->d_name) < NAME_MAX_LEN)
            strcpy(names[count++], e->d_name);
    closedir(dir);
    qsort(names, count, NAME_MAX_LEN, compareNames);
    return count;
}

// The largest box with the video's shape that fits the screen, centered.
static void fit(double aspect, float *x, float *y, float *w, float *h)
{
    double screen = (double)GFX_FB_WIDTH / GFX_FB_HEIGHT;
    if (aspect > screen) {
        *w = GFX_FB_WIDTH;
        *h = (float)(GFX_FB_WIDTH / aspect);
    } else {
        *h = GFX_FB_HEIGHT;
        *w = (float)(GFX_FB_HEIGHT * aspect);
    }
    *x = (GFX_FB_WIDTH - *w) / 2.0f;
    *y = (GFX_FB_HEIGHT - *h) / 2.0f;
}

static uint64_t nowNs(void)
{
    return armTicksToNs(armGetSystemTick());
}

// Plays one file. Returns false when the user asked to exit.
static bool play(const char *path, PadState *pad)
{
    char error[160];
    Video *v = videoOpen(path, error, sizeof(error));
    if (!v) {
        logLine("%s: %s", path, error);
        return true;
    }
    logLine("%s: %s", path, videoDescription(v));
    int w, h;
    double aspect;
    videoSize(v, &w, &h, &aspect);
    float bx, by, bw, bh;
    fit(aspect, &bx, &by, &bw, &bh);
    bool keep_going = true;
    uint64_t start = nowNs();
    while (appletMainLoop()) {
        padUpdate(pad);
        u64 down = padGetButtonsDown(pad);
        if (down & HidNpadButton_Plus) {
            keep_going = false;
            break;
        }
        if (down & HidNpadButton_B)
            break;
        if (videoFinished(v, error, sizeof(error)))
            break;
        const GfxVideoFrame *frame = videoFrameAt(v, nowNs());
        gfxBegin();
        gfxClear(GFX_RGB(0, 0, 0));
        if (frame)
            gfxDrawVideo(frame, bx, by, bw, bh);
        gfxEnd();
    }
    VideoStats s = videoStats(v);
    double secs = (nowNs() - start) / 1e9;
    logLine("  %s%s%.1f s: %u frames decoded, %u shown, %u dropped as late; %.2f ms to take a frame",
            error[0] ? error : "", error[0] ? "; " : "", secs, s.decoded, s.shown, s.dropped, s.copy_ms);
    videoClose(v);
    return keep_going;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    g_log = fopen(LOG_PATH, "w");
    if (!gfxInit()) {
        logLine("The GPU couldn't start.");
        if (g_log)
            fclose(g_log);
        return 1;
    }
    static char names[MAX_FILES][NAME_MAX_LEN];
    size_t count = listVideos(names, MAX_FILES);
    logLine("KaiVideo stage 2: %zu videos in " FOLDER, count);
    char path[NAME_MAX_LEN + 32];
    for (size_t i = 0; i < count; i++) {
        snprintf(path, sizeof(path), FOLDER "/%s", names[i]);
        if (!play(path, &pad))
            break;
    }
    logLine("Done.");
    gfxExit();
    if (g_log)
        fclose(g_log);
    return 0;
}
