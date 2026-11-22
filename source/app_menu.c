// The + menu, as KaiPlayer's and KaiRadio's: + opens it (over the library or
// a playing video), + in it asks "Exit KaiVideo?", and + there exits. Also
// the help line at the bottom of the library's screens.
#include <stdio.h>

#include <switch.h>

#include "app.h"
#include "buttons.h"
#include "lang.h"
#include "ui_library.h"

#ifndef KAIVIDEO_VERSION
#define KAIVIDEO_VERSION "dev"
#endif

typedef enum {
    ITEM_NONE,  // menu_after_video's "nothing to run".
    ITEM_DOWNLOADS,
    ITEM_RESCAN,
    ITEM_SETTINGS,
    ITEM_BACK_TO_LIBRARY,
    ITEM_EXIT,
    ITEM_EXIT_CONFIRMED,
    ITEM_CANCEL,
} MenuItemId;

#define ITEMS_MAX 6

static const char *itemLabel(MenuItemId id)
{
    switch (id) {
    case ITEM_DOWNLOADS: return T("Downloads");
    case ITEM_RESCAN: return T("Update the library");
    case ITEM_SETTINGS: return T("Settings");
    case ITEM_BACK_TO_LIBRARY: return T("Back to the library");
    case ITEM_EXIT: return T("Exit KaiVideo");
    case ITEM_EXIT_CONFIRMED: return T("Exit");
    default: return T("Cancel");
    }
}

// This screen's actions (over a video: back to the library), how many in
// *screen; then the app's, the same on every screen. Or the exit question.
static int items(const App *app, MenuItemId out[ITEMS_MAX], int *screen)
{
    int n = 0;
    *screen = 0;
    if (app->app_menu == APP_MENU_EXIT) {
        out[n++] = ITEM_EXIT_CONFIRMED;
        out[n++] = ITEM_CANCEL;
        return n;
    }
    if (app->player)
        out[n++] = ITEM_BACK_TO_LIBRARY;
    *screen = n;
    out[n++] = ITEM_DOWNLOADS;
    if (app->library_root[0])
        out[n++] = ITEM_RESCAN;
    out[n++] = ITEM_SETTINGS;
    out[n++] = ITEM_EXIT;
    return n;
}

void appOpenMenu(App *app)
{
    app->app_menu = APP_MENU_MAIN;
    app->app_menu_cursor = 0;  // On the first item, as KaiPlayer's and KaiRadio's: this screen's, if it has some.
}

static void confirmExit(App *app)
{
    app->app_menu = APP_MENU_EXIT;
    app->app_menu_cursor = 0;
}

static void run(App *app, MenuItemId id, uint64_t now)
{
    app->app_menu = APP_MENU_NONE;
    bool library_item = id == ITEM_DOWNLOADS || id == ITEM_RESCAN || id == ITEM_SETTINGS;
    if (app->player && library_item) {  // These live on the library: the video stops first.
        app->menu_after_video = id;
        app->stop_requested = true;
        return;
    }
    switch (id) {
    case ITEM_DOWNLOADS: appDownloadsOpen(app, now); break;
    case ITEM_RESCAN: appLibraryRescan(app, now); break;
    case ITEM_SETTINGS: appOpenSettings(app); break;
    case ITEM_BACK_TO_LIBRARY: app->stop_requested = true; break;
    case ITEM_EXIT: confirmExit(app); break;
    case ITEM_EXIT_CONFIRMED: app->quit = true; break;
    default: break;
    }
}

void appMenuAfterVideo(App *app, uint64_t now)
{
    MenuItemId id = (MenuItemId)app->menu_after_video;
    app->menu_after_video = ITEM_NONE;
    if (id != ITEM_NONE && !app->player)
        run(app, id, now);
}

void appMenuInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now)
{
    MenuItemId ids[ITEMS_MAX];
    int screen, n = items(app, ids, &screen);
    if (pressed & HidNpadButton_Plus) {  // + again: the question, then out.
        if (app->app_menu == APP_MENU_MAIN)
            confirmExit(app);
        else
            app->quit = true;
        return;
    }
    if (pressed & BUTTONS_UP)
        app->app_menu_cursor = (app->app_menu_cursor + n - 1) % n;
    if (pressed & BUTTONS_DOWN)
        app->app_menu_cursor = (app->app_menu_cursor + 1) % n;
    if (pressed & HidNpadButton_B) {
        app->app_menu = APP_MENU_NONE;
        return;
    }
    if ((pressed & HidNpadButton_A) && app->app_menu_cursor < n) {
        run(app, ids[app->app_menu_cursor], now);
        return;
    }
    if (touch.type != TOUCH_TAP)
        return;
    LibHit hit = uiLibraryTap(touch.x, touch.y);
    if (hit.kind == LIBHIT_MENU_ITEM && hit.a < n)
        run(app, ids[hit.a], now);
    else if (hit.kind == LIBHIT_CLOSE)
        app->app_menu = APP_MENU_NONE;
}

void appMenuDraw(const App *app)
{
    if (app->app_menu == APP_MENU_NONE)
        return;
    MenuItemId ids[ITEMS_MAX];
    const char *labels[ITEMS_MAX];
    int screen, n = items(app, ids, &screen);
    for (int i = 0; i < n; i++)
        labels[i] = itemLabel(ids[i]);
    if (app->app_menu == APP_MENU_EXIT)
        uiDrawMenu(T("Exit KaiVideo?"), NULL, labels, n, 0, app->app_menu_cursor);
    else
        uiDrawMenu("KaiVideo", KAIVIDEO_VERSION, labels, n, screen, app->app_menu_cursor);
}

const char *appFooterHelp(const App *app)
{
    if (app->app_menu == APP_MENU_EXIT)
        return T("[A] choose  [B] cancel  [+] exit");
    if (app->app_menu == APP_MENU_MAIN)
        return T("[A] choose  [B] close  [+] exit");
    if (app->settings_open && app->jf_signing)
        return T("[B] cancel");
    if (app->settings_open && app->about_open)
        return T("[A][B] back");
    if (app->settings_open)
        return app->settings_page == 0 ? T("[^v] choose  [>][A] open  [B] close")
                                       : T("[^v] choose  [<>][A] change  [B] back");
    if (app->picking)
        return app->library_root[0] ? T("[A] open  [B] folder above  [X] use this folder  [-] cancel")
                                    : T("[A] open  [B] folder above  [X] use this folder  [+] menu");
    bool server = app->config.use_jellyfin;  // Favorites are kept on the server.
    if (app->view.details >= 0)
        return server ? T("[A] play  [Y] from the start  [X] watched  [R] favorite  [L] download  [B] back")
                      : T("[A] play  [Y] from the start  [X] watched  [B] back");
    if (app->view.show >= 0)
        return server
                   ? T("[A] play  [X] watched  [R] favorite  [L] download season  [<>] season  [B] back  [ZL][ZR] tabs")
                   : T("[A] play  [X] watched  [<>] season  [B] back  [Y] search  [ZL][ZR] tabs  [+] menu");
    switch (app->view.tab) {
    case TAB_DOWNLOADS: return appDownloadsHelp(app);
    case TAB_SEARCH: return T("[Y] type a search  [A] open  [ZL][ZR] tabs  [-] settings  [+] menu");
    case TAB_LIVE: return T("[A] watch  [^v] channel  [<>] guide  [ZL][ZR] tabs  [-] settings  [+] menu");
    case TAB_FILES: return T("[A] open  [B] folder above  [<>] page  [Y] search  [ZL][ZR] tabs  [+] menu");
    default: return T("[A] open  [Y] search  [ZL][ZR] tabs  [-] settings  [+] menu");
    }
}
