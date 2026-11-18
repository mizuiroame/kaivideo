// KaiVideo: a video player for Switch 2 homebrew (kaiHEN + libounce). Video
// is decoded by the console's hardware decoder and drawn with the GPU; sound
// is decoded with FFmpeg and is the clock. Choose a film or an episode in
// the library (app_library.c), or a file in the Files tab, then A pauses,
// left and right go 10 seconds back or forward, L and R a minute, X and Y
// open the lists of sound tracks and subtitles, and B goes back. Every
// control works with touch too.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <switch.h>

#include "app.h"
#include "approot.h"
#include "audio.h"
#include "browser.h"
#include "buttons.h"
#include "font.h"
#include "gfx.h"
#include "input.h"
#include "menu.h"
#include "player.h"
#include "resume.h"
#include "sysinfo.h"
#include "thumbs.h"
#include "ui_library.h"
#include "touch.h"
#include "ui.h"
#include "util.h"

#define OLD_CONFIG_FOLDER "sdmc:/config/kaivideo"  // Before files went next to the NRO.
#define OVERLAY_NS 3000000000ull     // How long the player's bar stays after a press.
#define MESSAGE_NS 5000000000ull     // How long an error shows in the list.
#define NOTE_NS 3000000000ull        // How long "Subtitles: English" shows.
#define SEEK_SHORT_NS 10000000000ll
#define SEEK_LONG_NS 60000000000ll
#define SCREEN_W 1280
#define SCREEN_H 720
#define PANEL_W 1920  // In case the touch panel reports its own resolution.

static FILE *g_log;
// KaiVideo's own files, next to its NRO (approot.h): settings.txt,
// resume.txt and kaivideo.log.
static char g_folder[APPROOT_PATH_MAX];

void appConfigPath(char *out, size_t size, const char *name)
{
    snprintf(out, size, "%s/%s", g_folder, name);
}

void appLog(const char *fmt, ...)
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

// --- Settings kept between runs: the library folder and the Files tab's ---

static void loadSettings(App *app, char *folder, size_t size)
{
    snprintf(folder, size, "%s", BROWSER_ROOT);
    configDefaults(&app->config);
    char path[APPROOT_PATH_MAX + 32];
    appConfigPath(path, sizeof(path), "settings.txt");
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char line[BROWSER_PATH_MAX];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, "folder=sdmc:/", 13) == 0)
            snprintf(folder, size, "%s", line + 7);
        else if (strncmp(line, "library=sdmc:/", 14) == 0)
            snprintf(app->library_root, sizeof(app->library_root), "%s", line + 8);
        else
            configReadLine(&app->config, line);
    }
    fclose(f);
}

void appSaveSettings(const App *app)
{
    char path[APPROOT_PATH_MAX + 32];
    appConfigPath(path, sizeof(path), "settings.txt");
    FILE *f = fopen(path, "w");
    if (!f)
        return;
    if (!strpbrk(app->browser.path, "\r\n"))
        fprintf(f, "folder=%s\n", app->browser.path);
    if (app->library_root[0] && !strpbrk(app->library_root, "\r\n"))
        fprintf(f, "library=%s\n", app->library_root);
    configWrite(&app->config, f);
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

// --- Playing ---

void appShowMessage(App *app, const char *message, uint64_t now)
{
    snprintf(app->message, sizeof(app->message), "%s", message);
    app->message_until = now + MESSAGE_NS;
}

bool appStartVideo(App *app, const char *path, const char *name, uint64_t now)
{
    char error[160];
    thumbsPause(true);  // The decoder is the player's now.
    app->player = playerOpen(path, now, error, sizeof(error));
    if (!app->player) {
        thumbsPause(false);
        appLog("%s: %s", path, error);
        appShowMessage(app, error, now);
        return false;
    }
    snprintf(app->playing_name, sizeof(app->playing_name), "%s", name);
    snprintf(app->playing_path, sizeof(app->playing_path), "%s", path);
    app->bar_note[0] = '\0';
    menuClose(&app->menu);
    int64_t resume_at;
    if (resumeFind(&app->resume, path, &resume_at, NULL)) {
        // Where it was left: go on from there, and say how to start over.
        playerSeekTo(app->player, resume_at, now);
        char at[16];
        utilFormatTime(at, sizeof(at), resume_at / 1e9);
        snprintf(app->bar_note, sizeof(app->bar_note), "Resumed at %s  \xC2\xB7  ZL from the start", at);
        app->bar_note_until = now + OVERLAY_NS + 2000000000ull;
    }
    appLog("%s: %s; %s", path, playerVideoInfo(app->player), playerAudioInfo(app->player));
    for (int i = 0; i < playerAudioTrackCount(app->player); i++)
        appLog("  sound %d: %s%s", i + 1, playerAudioTrackName(app->player, i),
               i == playerAudioTrack(app->player) ? " (on)" : "");
    for (int i = 0; i < playerSubtitleCount(app->player); i++)
        appLog("  subtitles %d: %s%s", i + 1, playerSubtitleLabel(app->player, i),
               i == playerSubtitle(app->player) ? " (on)" : "");
    app->overlay_until = now + OVERLAY_NS;
    app->seeks = 0;
    app->started = now;
    return true;
}

static void stopVideo(App *app, const char *error, bool ended)
{
    // Remember where it was left (or, watched to the end, forget it).
    int64_t duration = playerDuration(app->player);
    resumeRemember(&app->resume, app->playing_path, ended ? duration : playerPosition(app->player), duration);
    char path[APPROOT_PATH_MAX + 32];
    appConfigPath(path, sizeof(path), "resume.txt");
    resumeSave(&app->resume, path);
    VideoStats s = playerStats(app->player);
    appLog("  %s%s%.1f s, %u seeks: %u frames decoded, %u shown, %u dropped as late", error && error[0] ? error : "",
            error && error[0] ? "; " : "", (nowNs() - app->started) / 1e9, app->seeks, s.decoded, s.shown,
            s.dropped);
    playerClose(app->player);
    app->player = NULL;
    thumbsPause(false);
    appLibraryRefreshHome(app);  // Continue watching changed.
}

static void sayInBar(App *app, uint64_t now, const char *fmt, const char *arg)
{
    snprintf(app->bar_note, sizeof(app->bar_note), fmt, arg);
    app->bar_note_until = now + NOTE_NS;
    app->overlay_until = now + NOTE_NS;
}

// Opens the list of sound tracks or subtitles (or says there's nothing to choose).
static void openMenu(App *app, UiMenuKind kind, uint64_t now)
{
    Player *p = app->player;
    menuClose(&app->menu);
    if (kind == UI_MENU_AUDIO && playerAudioTrackCount(p) < 2)
        sayInBar(app, now, "%s", "This video has one sound track.");
    else if (kind == UI_MENU_SUBTITLES && playerSubtitleCount(p) == 0)
        sayInBar(app, now, "%s", "This video has no subtitles.");
    else if (kind == UI_MENU_AUDIO)
        menuOpen(&app->menu, playerAudioTrackCount(p), 0, playerAudioTrack(p));
    else
        // Row 0 is "Off"; the size, color and style follow the tracks.
        menuOpen(&app->menu, playerSubtitleCount(p) + 1 + SUBOPT_COUNT, SUBOPT_COUNT, playerSubtitle(p) + 1);
    app->menu_kind = kind;
    app->drag_rows = 0;
}

// Changes one of the subtitles' options (the list's fixed rows) and keeps it.
static void stepOption(App *app, int row, int delta)
{
    SubtitleOption option = (SubtitleOption)(row - (app->menu.count - app->menu.fixed));
    subtitleOptionStep(&app->config, option, delta);
    appApplySubtitleStyle(app);
    appSaveSettings(app);
}

// Plays or shows the list's row, said in the bar, and closes the list; an
// option row changes instead, and the list stays.
static void chooseRow(App *app, int row, uint64_t now)
{
    Player *p = app->player;
    if (row >= app->menu.count - app->menu.fixed) {
        stepOption(app, row, 1);
        return;
    }
    if (app->menu_kind == UI_MENU_AUDIO) {
        if (row != playerAudioTrack(p))
            playerSetAudioTrack(p, row, now);
        sayInBar(app, now, "Sound: %s", playerAudioTrackName(p, row));
    } else {
        playerSetSubtitle(p, row - 1);
        if (row == 0)
            sayInBar(app, now, "%s", "Subtitles off");
        else
            sayInBar(app, now, "Subtitles: %s", playerSubtitleLabel(p, row - 1));
    }
    menuClose(&app->menu);
}

// A tap outside the open list closes it; on the other list's button, that
// list opens instead.
static void tapOutsideMenu(App *app, int x, int y, uint64_t now)
{
    Player *p = app->player;
    double fraction;
    TapTarget target = uiTapPlayer(x, y, playerSubtitleCount(p) > 0, playerAudioTrackCount(p) > 1, &fraction);
    if (target == TAP_AUDIO && app->menu_kind != UI_MENU_AUDIO)
        openMenu(app, UI_MENU_AUDIO, now);
    else if (target == TAP_SUBTITLES && app->menu_kind != UI_MENU_SUBTITLES)
        openMenu(app, UI_MENU_SUBTITLES, now);
    else
        menuClose(&app->menu);
}

// The open list takes the buttons and touches: up and down move, A
// chooses, B closes; X and Y go to their list (or close it if it's open).
static void menuInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now)
{
    Menu *m = &app->menu;
    app->overlay_until = now + OVERLAY_NS;  // The bar stays while the list is open.
    if (pressed & BUTTONS_UP) menuMove(m, -1);
    if (pressed & BUTTONS_DOWN) menuMove(m, 1);
    if (menuOnFixed(m) && (pressed & (BUTTONS_LEFT | BUTTONS_RIGHT)))
        stepOption(app, m->cursor, (pressed & BUTTONS_LEFT) ? -1 : 1);
    if (pressed & HidNpadButton_A) {
        chooseRow(app, m->cursor, now);
        return;
    }
    if (pressed & HidNpadButton_B) {
        menuClose(m);
        return;
    }
    if (pressed & HidNpadButton_Plus) {  // The + menu, as everywhere.
        menuClose(m);
        appOpenMenu(app);
        return;
    }
    if (pressed & (HidNpadButton_X | HidNpadButton_Y)) {
        UiMenuKind kind = (pressed & HidNpadButton_X) ? UI_MENU_AUDIO : UI_MENU_SUBTITLES;
        if (kind == app->menu_kind)
            menuClose(m);
        else
            openMenu(app, kind, now);
        return;
    }
    int row;
    if (touch.type == TOUCH_TAP) {
        TapTarget target = uiTapTrackMenu(app->menu_kind, m, touch.x, touch.y, &row);
        if (target == TAP_MENU_ROW)
            chooseRow(app, row, now);
        else if (target == TAP_OUTSIDE)
            tapOutsideMenu(app, touch.x, touch.y, now);
    } else if (touch.type == TOUCH_DRAG &&
               uiTapTrackMenu(app->menu_kind, m, touch.start_x, touch.start_y, &row) != TAP_OUTSIDE) {
        // Dragging scrolls a long list, as on a phone.
        app->drag_rows -= touch.dy;
        for (; app->drag_rows >= UI_MENU_ROW_H; app->drag_rows -= UI_MENU_ROW_H)
            menuMove(m, 1);
        for (; app->drag_rows <= -UI_MENU_ROW_H; app->drag_rows += UI_MENU_ROW_H)
            menuMove(m, -1);
    } else if (touch.type == TOUCH_DRAG_END) {
        app->drag_rows = 0;
    }
}

// False when playback should end (back to the list).
static bool playerInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now)
{
    Player *p = app->player;
    if (app->app_menu != APP_MENU_NONE) {
        appMenuInput(app, pressed, touch, now);
        bool back = app->stop_requested;
        app->stop_requested = false;
        return !back;
    }
    if (app->menu.open) {
        menuInput(app, pressed, touch, now);
        return true;
    }
    bool overlay = playerPaused(p) || now < app->overlay_until;
    if (pressed || touch.type == TOUCH_TAP)
        app->overlay_until = now + OVERLAY_NS;
    if (pressed & HidNpadButton_B)
        return false;
    if (pressed & HidNpadButton_Plus) {
        appOpenMenu(app);
        return true;
    }
    if (pressed & HidNpadButton_A)
        playerTogglePause(p, now);
    if (pressed & HidNpadButton_Y)
        openMenu(app, UI_MENU_SUBTITLES, now);
    if (pressed & HidNpadButton_X)
        openMenu(app, UI_MENU_AUDIO, now);
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
        switch (uiTapPlayer(touch.x, touch.y, playerSubtitleCount(p) > 0, playerAudioTrackCount(p) > 1, &fraction)) {
        case TAP_CLOSE: return false;
        case TAP_SUBTITLES: openMenu(app, UI_MENU_SUBTITLES, now); break;
        case TAP_AUDIO: openMenu(app, UI_MENU_AUDIO, now); break;
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
    char error[160];
    if (playerTakeAudioError(app->player, error, sizeof(error))) {  // A track that can't be played.
        snprintf(app->bar_note, sizeof(app->bar_note), "Sound: %s. %s",
                 playerAudioTrackName(app->player, playerAudioTrack(app->player)), error);
        app->bar_note_until = now + NOTE_NS;
        app->overlay_until = now + NOTE_NS;
        appLog("  sound track %d: %s", playerAudioTrack(app->player) + 1, error);
    }
    bool bar = playerPaused(app->player) || now < app->overlay_until;
    char subtitle[1024];
    if (playerSubtitleText(app->player, subtitle, sizeof(subtitle)))
        uiDrawSubtitles(subtitle, bar);
    if (playerPaused(app->player) && !app->menu.open)
        uiDrawPausedSymbol();
    if (bar)
        uiDrawPlayerBar(app->player, app->playing_name,
                        now < app->bar_note_until && !app->menu.open ? app->bar_note : NULL);
    uiDrawTrackMenu(app->player, app->menu_kind, &app->menu, &app->config);
    if (app->app_menu != APP_MENU_NONE) {  // The + menu over the video.
        uiLibraryResetHits();
        appMenuDraw(app);
        uiDrawFooterHelp(appFooterHelp(app));
    }
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
    appConfigPath(path, sizeof(path), "kaivideo.log");
    g_log = fopen(path, "w");
    appLog("Files in %s%s", g_folder, moved ? " (copied from " OLD_CONFIG_FOLDER ")" : "");
    if (!gfxInit() || !fontInit()) {
        appLog("The GPU or the fonts couldn't start.");
        if (g_log)
            fclose(g_log);
        return 1;
    }
    if (!audioSystemInit())
        appLog("The audio output couldn't start: videos play silent.");

    static App app;
    char folder[BROWSER_PATH_MAX];
    loadSettings(&app, folder, sizeof(folder));
    appConfigPath(path, sizeof(path), "resume.txt");
    resumeLoad(&app.resume, path);
    if (!browserOpen(&app.browser, folder))
        browserOpen(&app.browser, BROWSER_ROOT);  // The folder kept is gone.
    appConfigPath(path, sizeof(path), "thumbs");
    thumbsInit(path);
    sysinfoInit();
    appSettingsStart(&app);
    appLibraryStart(&app);
    InputRepeat repeat;
    inputRepeatInit(&repeat, BUTTONS_REPEATING);
    TouchTracker touch;
    touchReset(&touch);

    while (!app.quit && appletMainLoop()) {
        padUpdate(&pad);
        uint64_t now = nowNs();
        uint64_t pressed = inputRepeatUpdate(&repeat, padGetButtonsDown(&pad), padGetButtons(&pad), now);
        TouchEvent t = readTouch(&touch, now);
        appLibraryUpdate(&app);
        thumbsUpdate();
        SysInfo sys;
        sysinfoGet(&sys, now);
        uiLibrarySetStatus(&sys, app.config.clock_24h);
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
                    appShowMessage(&app, error, now);
                appLibraryDraw(&app, now);
            } else {
                drawPlayer(&app, playerUpdate(app.player, now), now);
            }
        } else {
            appLibraryInput(&app, pressed, t, now);
            if (app.player)
                drawPlayer(&app, playerUpdate(app.player, now), now);
            else
                appLibraryDraw(&app, now);
        }
        gfxEnd();
    }
    if (app.player)
        stopVideo(&app, NULL, false);
    appSaveSettings(&app);
    appLibraryExit(&app);
    thumbsExit();
    sysinfoExit();
    browserFree(&app.browser);
    appLog("Done.");
    audioSystemExit();
    fontExit();
    gfxExit();
    if (g_log)
        fclose(g_log);
    return 0;
}
