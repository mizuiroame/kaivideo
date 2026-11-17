// KaiVideo: a video player for Switch 2 homebrew (kaiHEN + libounce). Video
// is decoded by the console's hardware decoder and drawn with the GPU; sound
// is decoded with FFmpeg and is the clock.
//
// For now (stage 3) it plays every video in sdmc:/kaivideo-test, one after
// another: A pauses, left and right go 10 seconds back or forward, L and R a
// minute, B goes to the next video and + exits. What each played like goes
// to sdmc:/kaivideo.log.
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <switch.h>

#include "audio.h"
#include "font.h"
#include "gfx.h"
#include "player.h"
#include "util.h"

#define FOLDER "sdmc:/kaivideo-test"
#define LOG_PATH "sdmc:/kaivideo.log"
#define MAX_FILES 64
#define NAME_MAX_LEN 256
#define OVERLAY_NS 3000000000ull     // How long the bar stays after a press.
#define SEEK_SHORT_NS 10000000000ll
#define SEEK_LONG_NS 60000000000ll
#define MARGIN 40
#define BAR_H 120

#define COLOR_TEXT GFX_RGB(0xF2, 0xF2, 0xF5)
#define COLOR_DIM GFX_RGB(0xA4, 0xA4, 0xB0)
#define COLOR_TRACK GFX_RGB(0x40, 0x40, 0x48)
#define COLOR_ACCENT GFX_RGB(0xFF, 0x6B, 0x9A)  // KaiPlayer's pink.

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

// The largest box with the video's shape that fits the screen, centered (framebuffer pixels).
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

// The bar over the bottom of the picture: name, time, progress and formats.
static void drawOverlay(const Player *p, const char *name)
{
    int y = GFX_HEIGHT - BAR_H;
    // A fade from the picture into the bar, in bands (gradients are opaque).
    for (int i = 0; i < 12; i++)
        gfxBlendRect(0, y - 60 + i * 5, GFX_WIDTH, 5, GFX_RGB(0, 0, 0), (unsigned)(i * 200 / 12));
    gfxBlendRect(0, y, GFX_WIDTH, BAR_H, GFX_RGB(0, 0, 0), 200);
    if (playerPaused(p)) {  // Two bars: paused.
        gfxFillRoundRect(MARGIN, y + 18, 7, 26, 2, COLOR_TEXT);
        gfxFillRoundRect(MARGIN + 13, y + 18, 7, 26, 2, COLOR_TEXT);
    } else {
        gfxTriangle(MARGIN, y + 18, MARGIN, y + 44, MARGIN + 22, y + 31, COLOR_TEXT);
    }
    fontDrawClipped(FONT_BOLD, 22, MARGIN + 40, y + 16, GFX_WIDTH - 2 * MARGIN - 260, name, COLOR_TEXT);
    char pos[16], len[16], time[40];
    double position = playerPosition(p) / 1e9, duration = playerDuration(p) / 1e9;
    utilFormatTime(pos, sizeof(pos), position);
    utilFormatTime(len, sizeof(len), duration);
    snprintf(time, sizeof(time), "%s / %s", pos, len);
    fontDraw(FONT_REGULAR, 20, GFX_WIDTH - MARGIN - fontTextWidth(FONT_REGULAR, 20, time), y + 18, time, COLOR_TEXT);
    int track_w = GFX_WIDTH - 2 * MARGIN, done = duration > 0 ? (int)(track_w * (position / duration)) : 0;
    if (done > track_w)
        done = track_w;
    gfxFillRoundRect(MARGIN, y + 60, track_w, 6, 3, COLOR_TRACK);
    if (done > 0)
        gfxFillRoundRect(MARGIN, y + 60, done, 6, 3, COLOR_ACCENT);
    gfxFillCircle(MARGIN + done, y + 63, 8, COLOR_ACCENT);
    char info[256];
    snprintf(info, sizeof(info), "%s  \xC2\xB7  %s", playerVideoInfo(p), playerAudioInfo(p));
    fontDrawClipped(FONT_REGULAR, 16, MARGIN, y + 82, track_w, info, COLOR_DIM);
}

// Plays one file. Returns false when the user asked to exit.
static bool play(const char *path, const char *name, PadState *pad)
{
    char error[160];
    uint64_t start = nowNs();
    Player *p = playerOpen(path, start, error, sizeof(error));
    if (!p) {
        logLine("%s: %s", path, error);
        return true;
    }
    logLine("%s: %s; %s", path, playerVideoInfo(p), playerAudioInfo(p));
    int w, h;
    double aspect;
    playerSize(p, &w, &h, &aspect);
    float bx, by, bw, bh;
    fit(aspect, &bx, &by, &bw, &bh);
    bool keep_going = true;
    uint64_t overlay_until = start + OVERLAY_NS;
    unsigned seeks = 0;
    while (appletMainLoop()) {
        padUpdate(pad);
        uint64_t now = nowNs();
        u64 down = padGetButtonsDown(pad);
        if (down)
            overlay_until = now + OVERLAY_NS;
        if (down & HidNpadButton_Plus) {
            keep_going = false;
            break;
        }
        if (down & HidNpadButton_B)
            break;
        if (down & HidNpadButton_A)
            playerTogglePause(p, now);
        int64_t seek = (down & HidNpadButton_Left) ? -SEEK_SHORT_NS : (down & HidNpadButton_Right) ? SEEK_SHORT_NS
                       : (down & HidNpadButton_L) ? -SEEK_LONG_NS : (down & HidNpadButton_R) ? SEEK_LONG_NS : 0;
        if (seek) {
            playerSeekBy(p, seek, now);
            seeks++;
        }
        if (playerFinished(p, error, sizeof(error)))
            break;
        const GfxVideoFrame *frame = playerUpdate(p, now);
        gfxBegin();
        gfxClear(GFX_RGB(0, 0, 0));
        if (frame)
            gfxDrawVideo(frame, bx, by, bw, bh);
        if (playerPaused(p) || now < overlay_until)
            drawOverlay(p, name);
        gfxEnd();
    }
    VideoStats s = playerStats(p);
    double secs = (nowNs() - start) / 1e9;
    logLine("  %s%s%.1f s, %u seeks: %u frames decoded, %u shown, %u dropped as late; %.2f ms to take a frame",
            error[0] ? error : "", error[0] ? "; " : "", secs, seeks, s.decoded, s.shown, s.dropped, s.copy_ms);
    playerClose(p);
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
    if (!gfxInit() || !fontInit()) {
        logLine("The GPU or the fonts couldn't start.");
        if (g_log)
            fclose(g_log);
        return 1;
    }
    if (!audioSystemInit())
        logLine("The audio output couldn't start: videos play silent.");
    static char names[MAX_FILES][NAME_MAX_LEN];
    size_t count = listVideos(names, MAX_FILES);
    logLine("KaiVideo stage 3: %zu videos in " FOLDER, count);
    char path[NAME_MAX_LEN + 32];
    for (size_t i = 0; i < count; i++) {
        snprintf(path, sizeof(path), FOLDER "/%s", names[i]);
        if (!play(path, names[i], &pad))
            break;
    }
    logLine("Done.");
    audioSystemExit();
    fontExit();
    gfxExit();
    if (g_log)
        fclose(g_log);
    return 0;
}
