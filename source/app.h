// KaiVideo's state, shared by main.c (the loop and the player) and
// app_library.c (the library's screens, the Files tab and the choice of
// the library folder).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "browser.h"
#include "home.h"
#include "library.h"
#include "libview.h"
#include "menu.h"
#include "player.h"
#include "resume.h"
#include "settings.h"
#include "touch.h"
#include "ui.h"

#define APP_NAME_MAX 300

typedef struct App App;

struct App {
    Browser browser;            // The Files tab.
    Player *player;
    char playing_name[APP_NAME_MAX];
    char playing_path[BROWSER_PATH_MAX];
    ResumeList resume;
    char bar_note[256];         // Said in the player's bar for a while ("Resumed at 12:34").
    uint64_t bar_note_until;
    uint64_t overlay_until;
    Menu menu;                  // The open drop-down list, if any, over the player.
    UiMenuKind menu_kind;
    unsigned seeks;
    uint64_t started;
    char message[200];
    uint64_t message_until;
    int drag_rows, drag_cols;
    bool quit;

    // The library (app_library.c).
    char library_root[LIBRARY_PATH_MAX];  // "" until one is chosen.
    Library library;
    Home home;
    LibView view;
    bool picking;               // Choosing the library folder.
    Browser picker;

    // Settings (app_settings.c).
    Config config;
    bool settings_open, about_open;
    int settings_cursor;
};

// main.c
void appShowMessage(App *app, const char *message, uint64_t now);
// Plays path, named name in the player's bar. False (with a message) if it can't.
bool appStartVideo(App *app, const char *path, const char *name, uint64_t now);
// Builds a file's path in KaiVideo's own folder.
void appConfigPath(char *out, size_t size, const char *name);
void appSaveSettings(const App *app);
void appLog(const char *fmt, ...);

// app_library.c
void appLibraryStart(App *app);  // After the settings are read.
void appLibraryExit(App *app);
// Takes a finished scan, and works out Home again after a video.
void appLibraryUpdate(App *app);
void appLibraryRefreshHome(App *app);
void appLibraryInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now);
void appLibraryDraw(App *app, uint64_t now);
// Looks for videos again (Settings).
void appLibraryRescan(App *app, uint64_t now);
void appOpenPicker(App *app);

// app_settings.c
// Applies the settings read at start: theme, background, thumbnails, subtitles.
void appSettingsStart(App *app);
void appOpenSettings(App *app);
void appSettingsInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now);
void appSettingsDraw(const App *app);
