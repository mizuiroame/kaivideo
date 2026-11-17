// KaiVideo: a video player for Switch 2 homebrew (kaiHEN + libounce). Video
// is decoded by the console's hardware decoder and drawn with the GPU; sound
// is decoded with FFmpeg and is the clock. Choose a video on the card, then
// A pauses, left and right go 10 seconds back or forward, L and R a minute,
// and B goes back to the list. Every control works with touch too.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <switch.h>

#include "approot.h"
#include "audio.h"
#include "browser.h"
#include "buttons.h"
#include "font.h"
#include "gfx.h"
#include "input.h"
#include "player.h"
#include "resume.h"
#include "touch.h"
#include "ui.h"
#include "util.h"

#define OLD_CONFIG_FOLDER "sdmc:/config/kaivideo"  // Before files went next to the NRO.
#define OVERLAY_NS 3000000000ull     // How long the player's bar stays after a press.
#define MESSAGE_NS 5000000000ull     // How long an error shows in the list.
#define SEEK_SHORT_NS 10000000000ll
#define SEEK_LONG_NS 60000000000ll
#define SCREEN_W 1280
#define SCREEN_H 720
#define PANEL_W 1920  // In case the touch panel reports its own resolution.

static FILE *g_log;
// KaiVideo's own files, next to its NRO (approot.h): settings.txt,
// resume.txt and kaivideo.log.
static char g_folder[APPROOT_PATH_MAX];

static void configPath(char *out, size_t size, const char *name)
{
    snprintf(out, size, "%s/%s", g_folder, name);
}

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

static uint64_t nowNs(void)
{
    return armTicksToNs(armGetSystemTick());
}

// --- The folder kept between runs ---

static void loadFolder(char *out, size_t size)
{
    snprintf(out, size, "%s", BROWSER_ROOT);
    char path[APPROOT_PATH_MAX + 32];
    configPath(path, sizeof(path), "settings.txt");
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char line[BROWSER_PATH_MAX + 16];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, "folder=sdmc:/", 13) == 0)
            snprintf(out, size, "%s", line + 7);
    }
    fclose(f);
}

static void saveFolder(const char *folder)
{
    char path[APPROOT_PATH_MAX + 32];
    configPath(path, sizeof(path), "settings.txt");
    FILE *f = fopen(path, "w");
    if (!f)
        return;
    if (!strpbrk(folder, "\r\n"))
        fprintf(f, "folder=%s\n", folder);
    fclose(f);
}

// --- Touch ---

static TouchEvent readTouch(TouchTracker *tracker, uint64_t now)
{
    static bool full_hd;  // Seen coordinates beyond 1280x720: scale them down.
    HidTouchScreenState state = {0};
    bool touching = hidGetTouchScreenStates(&state, 1) > 0 && state.count > 0;
    int x = 0, y = 0;
    if (touching) {
        x = (int)state.touches[0].x;
        y = (int)state.touches[0].y;
        if (x >= SCREEN_W || y >= SCREEN_H)
            full_hd = true;
        if (full_hd) {
            x = x * SCREEN_W / PANEL_W;
            y = y * SCREEN_W / PANEL_W;
        }
    }
    return touchUpdate(tracker, touching, x, y, now);
}

// --- The app ---

typedef struct {
    Browser browser;
    Player *player;
    char playing_name[BROWSER_NAME_MAX];
    char playing_path[BROWSER_PATH_MAX];
    ResumeList resume;
    char bar_note[96];          // Said in the player's bar for a while ("Resumed at 12:34").
    uint64_t bar_note_until;
    uint64_t overlay_until;
    unsigned seeks;
    uint64_t started;
    char message[200];
    uint64_t message_until;
    int drag_rows;
    bool quit;
} App;

static void showMessage(App *app, const char *message, uint64_t now)
{
    snprintf(app->message, sizeof(app->message), "%s", message);
    app->message_until = now + MESSAGE_NS;
}

static void startVideo(App *app, uint64_t now)
{
    char path[BROWSER_PATH_MAX], error[160];
    if (!browserSelectedPath(&app->browser, path, sizeof(path)))
        return;
    app->player = playerOpen(path, now, error, sizeof(error));
    if (!app->player) {
        logLine("%s: %s", path, error);
        showMessage(app, error, now);
        return;
    }
    snprintf(app->playing_name, sizeof(app->playing_name), "%s", app->browser.items[app->browser.cursor].name);
    snprintf(app->playing_path, sizeof(app->playing_path), "%s", path);
    app->bar_note[0] = '\0';
    int64_t resume_at;
    if (resumeFind(&app->resume, path, &resume_at, NULL)) {
        // Where it was left: go on from there, and say how to start over.
        playerSeekTo(app->player, resume_at, now);
        char at[16];
        utilFormatTime(at, sizeof(at), resume_at / 1e9);
        snprintf(app->bar_note, sizeof(app->bar_note), "Resumed at %s  \xC2\xB7  ZL from the start", at);
        app->bar_note_until = now + OVERLAY_NS + 2000000000ull;
    }
    logLine("%s: %s; %s", path, playerVideoInfo(app->player), playerAudioInfo(app->player));
    app->overlay_until = now + OVERLAY_NS;
    app->seeks = 0;
    app->started = now;
    saveFolder(app->browser.path);
}

static void stopVideo(App *app, const char *error, bool ended)
{
    // Remember where it was left (or, watched to the end, forget it).
    int64_t duration = playerDuration(app->player);
    resumeRemember(&app->resume, app->playing_path, ended ? duration : playerPosition(app->player), duration);
    char path[APPROOT_PATH_MAX + 32];
    configPath(path, sizeof(path), "resume.txt");
    resumeSave(&app->resume, path);
    VideoStats s = playerStats(app->player);
    logLine("  %s%s%.1f s, %u seeks: %u frames decoded, %u shown, %u dropped as late", error && error[0] ? error : "",
            error && error[0] ? "; " : "", (nowNs() - app->started) / 1e9, app->seeks, s.decoded, s.shown,
            s.dropped);
    playerClose(app->player);
    app->player = NULL;
}

// Opens the entry under the cursor: a folder, or a video.
static void openSelected(App *app, uint64_t now)
{
    if (app->browser.cursor >= app->browser.count)
        return;
    if (app->browser.items[app->browser.cursor].is_dir) {
        if (!browserEnter(&app->browser))
            showMessage(app, "That folder can't be opened.", now);
    } else {
        startVideo(app, now);
    }
}

static void moveCursor(Browser *b, long delta)
{
    if (b->count == 0)
        return;
    long next = (long)b->cursor + delta;
    b->cursor = next < 0 ? 0 : next >= (long)b->count ? b->count - 1 : (size_t)next;
}

static void browserInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now)
{
    if (pressed & HidNpadButton_Plus) {
        app->quit = true;
        return;
    }
    if (pressed & BUTTONS_UP) moveCursor(&app->browser, -1);
    if (pressed & BUTTONS_DOWN) moveCursor(&app->browser, 1);
    if (pressed & BUTTONS_PAGE_UP) moveCursor(&app->browser, -UI_ROWS);
    if (pressed & BUTTONS_PAGE_DOWN) moveCursor(&app->browser, UI_ROWS);
    if (pressed & HidNpadButton_A) openSelected(app, now);
    if (pressed & HidNpadButton_B) browserUp(&app->browser);
    if (touch.type == TOUCH_TAP) {
        size_t row;
        switch (uiTapBrowser(&app->browser, touch.x, touch.y, &row)) {
        case TAP_UP: browserUp(&app->browser); break;
        case TAP_ROW:
            app->browser.cursor = row;
            openSelected(app, now);
            break;
        default: break;
        }
    } else if (touch.type == TOUCH_DRAG && touch.start_y >= UI_LIST_Y) {
        // Dragging up shows the rows below, as on a phone.
        app->drag_rows -= touch.dy;
        for (; app->drag_rows >= UI_ROW_H; app->drag_rows -= UI_ROW_H)
            moveCursor(&app->browser, 1);
        for (; app->drag_rows <= -UI_ROW_H; app->drag_rows += UI_ROW_H)
            moveCursor(&app->browser, -1);
    } else if (touch.type == TOUCH_DRAG_END) {
        app->drag_rows = 0;
    }
}

// False when playback should end (back to the list).
static bool playerInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now)
{
    Player *p = app->player;
    bool overlay = playerPaused(p) || now < app->overlay_until;
    if (pressed || touch.type == TOUCH_TAP)
        app->overlay_until = now + OVERLAY_NS;
    if (pressed & (HidNpadButton_B | HidNpadButton_Plus))
        return false;
    if (pressed & HidNpadButton_A)
        playerTogglePause(p, now);
    if (pressed & HidNpadButton_ZL) {  // From the start.
        playerSeekTo(p, 0, now);
        app->seeks++;
        app->bar_note[0] = '\0';
    }
    int64_t seek = (pressed & BUTTONS_LEFT)    ? -SEEK_SHORT_NS
                   : (pressed & BUTTONS_RIGHT) ? SEEK_SHORT_NS
                   : (pressed & HidNpadButton_L) ? -SEEK_LONG_NS
                   : (pressed & HidNpadButton_R) ? SEEK_LONG_NS
                                                 : 0;
    if (seek) {
        playerSeekBy(p, seek, now);
        app->seeks++;
    }
    if (touch.type == TOUCH_TAP && overlay) {  // A first tap only shows the bar.
        double fraction = 0;
        switch (uiTapPlayer(touch.x, touch.y, &fraction)) {
        case TAP_CLOSE: return false;
        case TAP_SEEK:
            playerSeekTo(p, (int64_t)(fraction * playerDuration(p)), now);
            app->seeks++;
            break;
        case TAP_PAUSE:
        case TAP_PICTURE: playerTogglePause(p, now); break;
        default: break;
        }
    }
    return true;
}

static void drawPlayer(App *app, const GfxVideoFrame *frame, uint64_t now)
{
    int w, h;
    double aspect;
    playerSize(app->player, &w, &h, &aspect);
    // The largest box with the video's shape that fits the screen, centered.
    float bw = GFX_FB_WIDTH, bh = (float)(GFX_FB_WIDTH / aspect);
    if (bh > GFX_FB_HEIGHT) {
        bh = GFX_FB_HEIGHT;
        bw = (float)(GFX_FB_HEIGHT * aspect);
    }
    gfxClear(GFX_RGB(0, 0, 0));
    if (frame)
        gfxDrawVideo(frame, (GFX_FB_WIDTH - bw) / 2.0f, (GFX_FB_HEIGHT - bh) / 2.0f, bw, bh);
    if (playerPaused(app->player) || now < app->overlay_until)
        uiDrawPlayerBar(app->player, app->playing_name, now < app->bar_note_until ? app->bar_note : NULL);
}

int main(int argc, char **argv)
{
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    hidInitializeTouchScreen();
    // Files next to the NRO; the first time, what the old folder had is copied there.
    bool next_to_nro = appRootFolder(argc > 0 ? argv[0] : NULL, "kaivideo", g_folder, sizeof(g_folder));
    appRootMakeFolder(g_folder);
    int moved = next_to_nro ? appRootMigrate(OLD_CONFIG_FOLDER, g_folder) : 0;
    char path[APPROOT_PATH_MAX + 32];
    configPath(path, sizeof(path), "kaivideo.log");
    g_log = fopen(path, "w");
    logLine("Files in %s%s", g_folder, moved ? " (copied from " OLD_CONFIG_FOLDER ")" : "");
    if (!gfxInit() || !fontInit()) {
        logLine("The GPU or the fonts couldn't start.");
        if (g_log)
            fclose(g_log);
        return 1;
    }
    if (!audioSystemInit())
        logLine("The audio output couldn't start: videos play silent.");

    static App app;
    char folder[BROWSER_PATH_MAX];
    loadFolder(folder, sizeof(folder));
    configPath(path, sizeof(path), "resume.txt");
    resumeLoad(&app.resume, path);
    if (!browserOpen(&app.browser, folder))
        browserOpen(&app.browser, BROWSER_ROOT);  // The folder kept is gone.
    InputRepeat repeat;
    inputRepeatInit(&repeat, BUTTONS_REPEATING);
    TouchTracker touch;
    touchReset(&touch);

    while (!app.quit && appletMainLoop()) {
        padUpdate(&pad);
        uint64_t now = nowNs();
        uint64_t pressed = inputRepeatUpdate(&repeat, padGetButtonsDown(&pad), padGetButtons(&pad), now);
        TouchEvent t = readTouch(&touch, now);
        gfxBegin();
        if (app.player) {
            char error[160] = "";
            bool keep = playerInput(&app, pressed, t, now);
            bool ended = false;
            if (keep && playerFinished(app.player, error, sizeof(error))) {
                keep = false;
                ended = !error[0];
            }
            if (!keep) {
                stopVideo(&app, error, ended);
                if (error[0])
                    showMessage(&app, error, now);
                uiDrawBrowser(&app.browser, &app.resume, now < app.message_until ? app.message : NULL);
            } else {
                drawPlayer(&app, playerUpdate(app.player, now), now);
            }
        } else {
            browserInput(&app, pressed, t, now);
            if (app.player)
                drawPlayer(&app, playerUpdate(app.player, now), now);
            else
                uiDrawBrowser(&app.browser, &app.resume, now < app.message_until ? app.message : NULL);
        }
        gfxEnd();
    }
    if (app.player)
        stopVideo(&app, NULL, false);
    saveFolder(app.browser.path);
    browserFree(&app.browser);
    logLine("Done.");
    audioSystemExit();
    fontExit();
    gfxExit();
    if (g_log)
        fclose(g_log);
    return 0;
}
