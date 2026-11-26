// The + menu, as KaiPlayer's and KaiRadio's: + opens it (over the library or
// a playing video), + in it asks "Exit KaiVideo?", and + there exits. Also
// the help line at the bottom of the library's screens, and the first run's
// welcome, which asks where the videos are.
#include <stdio.h>

#include <switch.h>

#include "app.h"
#include "approot.h"
#include "buttons.h"
#include "davnet.h"
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
    ITEM_WELCOME_CARD,     // The welcome's: a folder on the SD card,
    ITEM_WELCOME_NETWORK,  // the network folder,
    ITEM_WELCOME_SERVER,   // a media server,
    ITEM_WELCOME_IPTV,     // an IPTV list,
    ITEM_WELCOME_ADDONS,   // Stremio addons,
    ITEM_WELCOME_LATER,    // or later.
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
    case ITEM_WELCOME_CARD: return T("A folder on the SD card");
    case ITEM_WELCOME_NETWORK: return T("A network folder (SMB, WebDAV, FTP...)");
    case ITEM_WELCOME_SERVER: return T("A media server (Jellyfin, Emby, Plex)");
    case ITEM_WELCOME_IPTV: return T("An IPTV list");
    case ITEM_WELCOME_ADDONS: return T("Stremio addons");
    case ITEM_WELCOME_LATER: return T("Later");
    default: return T("Cancel");
    }
}

// This screen's actions (over a video: back to the library), how many in
// *screen; then the app's, the same on every screen. Or the exit question.
static int items(const App *app, MenuItemId out[ITEMS_MAX], int *screen)
{
    int n = 0;
    *screen = 0;
    if (app->app_menu == APP_MENU_WELCOME) {
        for (MenuItemId id = ITEM_WELCOME_CARD; id <= ITEM_WELCOME_LATER; id++)
            out[n++] = id;
        *screen = n - 1;  // "Later" under a line.
        return n;
    }
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

// Somewhere the videos come from: a folder on the card, the network
// folder, a server, an IPTV list or addons shown as the library.
static bool sourceSet(const App *app)
{
    const Config *c = &app->config;
    return app->library_root[0] || c->use_network || c->use_jellyfin || c->use_iptv || c->use_addons;
}

static void welcomeAnswered(App *app)
{
    app->config.welcomed = true;
    appSaveSettings(app);
}

void appWelcomeTick(App *app)
{
    if (app->config.welcomed)
        return;
    if (sourceSet(app)) {  // A folder chosen, a server signed in: nothing more to ask.
        if (app->app_menu == APP_MENU_WELCOME)
            app->app_menu = APP_MENU_NONE;
        welcomeAnswered(app);
        return;
    }
    if (app->app_menu != APP_MENU_NONE || app->settings_open || app->picking || app->player)
        return;
    // Set up in Settings but not shown yet: shown now (the next frame sees it and is answered).
    if (davConfigured())
        appLibraryShowNetwork(app);
    else if (appIptvConfigured())
        appLibraryShowIptv(app);
    else if (appStremioConfigured())
        appLibraryShowAddons(app);
    else {  // Back to it when what it opened was left without one (a sign-in failed, the picker cancelled).
        app->app_menu = APP_MENU_WELCOME;
        app->app_menu_cursor = 0;
    }
}

// Said when the welcome is answered "later": where downloads go meanwhile.
static void tellDownloadsFolder(App *app, uint64_t now)
{
    char folder[APPROOT_PATH_MAX + 32], message[APPROOT_PATH_MAX + 128];
    appDownloadsFolderFirstRun(folder, sizeof(folder));
    snprintf(message, sizeof(message), T("Downloads go to %s until you choose a folder."), folder);
    appNotify(app, NOTICE_INFO, message, now);
}

// The welcome's answer: the page that sets that source up, or later.
static void runWelcome(App *app, MenuItemId id, uint64_t now)
{
    app->settings_from_welcome = false;
    switch (id) {
    case ITEM_WELCOME_CARD: appOpenPicker(app); break;
    case ITEM_WELCOME_NETWORK: appOpenSettingsAt(app, SETTINGS_PAGE_NETWORK); break;
    case ITEM_WELCOME_SERVER: appOpenJellyfinSettings(app, now); break;
    case ITEM_WELCOME_IPTV: appOpenSettingsAt(app, SETTINGS_PAGE_IPTV); break;
    case ITEM_WELCOME_ADDONS: appOpenSettingsAt(app, SETTINGS_PAGE_ADDONS); break;
    default:
        welcomeAnswered(app);
        tellDownloadsFolder(app, now);
        break;
    }
    if (app->settings_open)
        app->settings_from_welcome = true;  // B on that page comes back here.
}

static void run(App *app, MenuItemId id, uint64_t now)
{
    if (id >= ITEM_WELCOME_CARD) {
        app->app_menu = APP_MENU_NONE;
        runWelcome(app, id, now);
        return;
    }
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
    pressed = buttonsRightStickAsDpad(pressed);
    MenuItemId ids[ITEMS_MAX];
    int screen, n = items(app, ids, &screen);
    if (pressed & HidNpadButton_Plus) {  // + again: the question, then out.
        if (app->app_menu != APP_MENU_EXIT)
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
        if (app->app_menu == APP_MENU_WELCOME)
            run(app, ITEM_WELCOME_LATER, now);  // B answers "later", as the row does.
        else
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
    else if (hit.kind == LIBHIT_CLOSE && app->app_menu != APP_MENU_WELCOME)
        app->app_menu = APP_MENU_NONE;  // A stray touch beside the welcome doesn't answer it.
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
    if (app->app_menu == APP_MENU_WELCOME)
        uiDrawMenu(T("Welcome! Where are your videos?"), NULL, labels, n, screen, app->app_menu_cursor);
    else if (app->app_menu == APP_MENU_EXIT)
        uiDrawMenu(T("Exit KaiVideo?"), NULL, labels, n, 0, app->app_menu_cursor);
    else
        uiDrawMenu("KaiVideo", KAIVIDEO_VERSION, labels, n, screen, app->app_menu_cursor);
}

const char *appFooterHelp(const App *app)
{
    if (app->app_menu == APP_MENU_WELCOME)
        return T("[A] choose  [B] later  [+] exit");
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
    if (app->streams_open)
        return T("[A] play  [^v] stream  [B] back");
    if (app->picking)
        return T("[A] open  [B] folder above  [X] use this folder  [-] cancel");
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
    case TAB_FILMS:
    case TAB_SERIES:
        if (app->xtream_groups_open && appXtreamHasGroups(app))
            return T("[A] choose  [^v] category  [B] back");
        if (appXtreamHasGroups(app))
            return T("[A] open  [Y] categories  [ZL][ZR] tabs  [-] settings  [+] menu");
        return T("[A] open  [Y] search  [ZL][ZR] tabs  [-] settings  [+] menu");
    case TAB_SEARCH: return T("[Y] type a search  [A] open  [ZL][ZR] tabs  [-] settings  [+] menu");
    case TAB_LIVE:
        if (appDvrHelp(app))
            return appDvrHelp(app);
        if (app->live_groups_open)
            return T("[A] choose  [^v] group  [B] back");
        if (app->config.use_iptv && app->iptv.group_count > 0)
            return T("[A] watch  [^v] channel  [<>] guide  [Y] groups  [R] favorite  [ZL][ZR] tabs");
        if (app->config.use_iptv)
            return T("[A] watch  [^v] channel  [<>] guide  [R] favorite  [ZL][ZR] tabs  [+] menu");
        if (appDvrAvailable(app))
            return T("[A] watch  [^v] channel  [<>] guide  [R] record  [L] recordings  [ZL][ZR] tabs  [+] menu");
        return T("[A] watch  [^v] channel  [<>] guide  [ZL][ZR] tabs  [-] settings  [+] menu");
    case TAB_HOME:
        // The IPTV list's: a channel (its own Home's, or the Showcase's rows of channels), or a film or series.
        if (app->config.use_iptv && (!app->view.showcase || app->home.count == 0 ||
                                     libviewSelectedChannel(&app->view, &app->home) >= 0))
            return T("[A] watch  [R] favorite  [X] source  [ZL][ZR] tabs  [-] settings  [+] menu");
        if (app->config.use_iptv)
            return T("[A] open  [Y] search  [X] source  [ZL][ZR] tabs  [-] settings  [+] menu");
        return T("[A] open  [Y] search  [ZL][ZR] tabs  [-] settings  [+] menu");
    case TAB_FILES:
        if (app->config.use_iptv && app->library.count == 0)  // Its view has no Search then.
            return T("[A] open  [B] folder above  [<>] page  [ZL][ZR] tabs  [+] menu");
        return T("[A] open  [B] folder above  [<>] page  [Y] search  [ZL][ZR] tabs  [+] menu");
    default: return T("[A] open  [Y] search  [ZL][ZR] tabs  [-] settings  [+] menu");
    }
}
