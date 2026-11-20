// The + menu, as KaiPlayer's and KaiRadio's: + opens it (over the library or
// a playing video), + in it asks "Exit KaiVideo?", and + there exits. Also
// the help line at the bottom of the library's screens.
#include <stdio.h>

#include <switch.h>

#include "app.h"
#include "buttons.h"
#include "ui_library.h"

#ifndef KAIVIDEO_VERSION
#define KAIVIDEO_VERSION "dev"
#endif

typedef enum {
    ITEM_DOWNLOADS,
    ITEM_RESCAN,
    ITEM_SETTINGS,
    ITEM_BACK_TO_LIBRARY,
    ITEM_EXIT,
    ITEM_EXIT_CONFIRMED,
    ITEM_CANCEL,
} MenuItemId;

#define ITEMS_MAX 5

static const char *itemLabel(MenuItemId id)
{
    switch (id) {
    case ITEM_DOWNLOADS: return "Downloads";
    case ITEM_RESCAN: return "Look for new videos";
    case ITEM_SETTINGS: return "Settings";
    case ITEM_BACK_TO_LIBRARY: return "Back to the library";
    case ITEM_EXIT: return "Exit KaiVideo";
    case ITEM_EXIT_CONFIRMED: return "Exit";
    default: return "Cancel";
    }
}

// The menu's items now: over a video, over the library, or the exit question.
static int items(const App *app, MenuItemId out[ITEMS_MAX])
{
    int n = 0;
    if (app->app_menu == APP_MENU_EXIT) {
        out[n++] = ITEM_EXIT_CONFIRMED;
        out[n++] = ITEM_CANCEL;
        return n;
    }
    if (app->player) {
        out[n++] = ITEM_BACK_TO_LIBRARY;
    } else {
        out[n++] = ITEM_DOWNLOADS;
        if (app->library_root[0])
            out[n++] = ITEM_RESCAN;
        out[n++] = ITEM_SETTINGS;
    }
    out[n++] = ITEM_EXIT;
    return n;
}

void appOpenMenu(App *app)
{
    app->app_menu = APP_MENU_MAIN;
    MenuItemId ids[ITEMS_MAX];
    int n = items(app, ids);
    app->app_menu_cursor = 0;
    for (int i = 0; i < n; i++)  // On Settings, where there is one.
        if (ids[i] == ITEM_SETTINGS)
            app->app_menu_cursor = i;
}

static void confirmExit(App *app)
{
    app->app_menu = APP_MENU_EXIT;
    app->app_menu_cursor = 0;
}

static void run(App *app, MenuItemId id, uint64_t now)
{
    app->app_menu = APP_MENU_NONE;
    switch (id) {
    case ITEM_DOWNLOADS: appDownloadsOpen(app); break;
    case ITEM_RESCAN: appLibraryRescan(app, now); break;
    case ITEM_SETTINGS: appOpenSettings(app); break;
    case ITEM_BACK_TO_LIBRARY: app->stop_requested = true; break;
    case ITEM_EXIT: confirmExit(app); break;
    case ITEM_EXIT_CONFIRMED: app->quit = true; break;
    default: break;
    }
}

void appMenuInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now)
{
    MenuItemId ids[ITEMS_MAX];
    int n = items(app, ids);
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
    int n = items(app, ids);
    for (int i = 0; i < n; i++)
        labels[i] = itemLabel(ids[i]);
    if (app->app_menu == APP_MENU_EXIT)
        uiDrawMenu("Exit KaiVideo?", NULL, labels, n, app->app_menu_cursor);
    else
        uiDrawMenu("KaiVideo", KAIVIDEO_VERSION, labels, n, app->app_menu_cursor);
}

const char *appFooterHelp(const App *app)
{
    if (app->app_menu == APP_MENU_EXIT)
        return "[A] choose  [B] cancel  [+] exit";
    if (app->app_menu == APP_MENU_MAIN)
        return "[A] choose  [B] close  [+] exit";
    if (app->downloads_open)
        return "[A] play / pause / go on  [X] delete  [B] close";
    if (app->settings_open && app->jf_signing)
        return "[B] cancel";
    if (app->settings_open && app->about_open)
        return "[A][B] back";
    if (app->settings_open)
        return app->settings_page == 0 ? "[^v] choose  [>][A] open  [B] close"
                                       : "[^v] choose  [<>][A] change  [B] back";
    if (app->picking)
        return app->library_root[0] ? "[A] open  [B] folder above  [X] use this folder  [-] cancel"
                                    : "[A] open  [B] folder above  [X] use this folder  [+] menu";
    bool server = app->config.use_jellyfin;  // Favorites are kept on the server.
    if (app->view.details >= 0)
        return server ? "[A] play  [Y] from the start  [X] watched  [R] favorite  [L] download  [B] back"
                      : "[A] play  [Y] from the start  [X] watched  [B] back";
    if (app->view.show >= 0)
        return server ? "[A] play  [X] watched  [R] favorite  [L] download  [<>] season  [B] back  [ZL][ZR] tabs"
                      : "[A] play  [X] watched  [<>] season  [B] back  [Y] search  [ZL][ZR] tabs  [+] menu";
    switch (app->view.tab) {
    case TAB_SEARCH: return "[Y] type a search  [A] open  [ZL][ZR] tabs  [-] settings  [+] menu";
    case TAB_LIVE: return "[A] watch  [^v] channel  [<>] guide  [ZL][ZR] tabs  [-] settings  [+] menu";
    case TAB_FILES: return "[A] open  [B] folder above  [<>] page  [Y] search  [ZL][ZR] tabs  [+] menu";
    default: return "[A] open  [Y] search  [ZL][ZR] tabs  [-] settings  [+] menu";
    }
}
