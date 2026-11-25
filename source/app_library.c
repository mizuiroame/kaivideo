// The library's screens: the tabs (Home, Films, Series, Search, Files), a
// series' page, and choosing the library folder. L and R (or ZL and ZR) change tab, Y searches, X marks an
// episode watched, - opens Settings, + the menu.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <switch.h>

#include "app.h"
#include "davnet.h"
#include "background.h"
#include "buttons.h"
#include "gfx.h"
#include "keyboard_system.h"
#include "lang.h"
#include "libscan.h"
#include "thumbs.h"
#include "ui_library.h"
#include "ui_style.h"
#include "xtreamvod.h"

#define BUTTONS_TAB_LEFT (HidNpadButton_L | HidNpadButton_ZL)
#define BUTTONS_TAB_RIGHT (HidNpadButton_R | HidNpadButton_ZR)

static void cachePath(const App *app, char *out, size_t size)
{
    appConfigPath(out, size, app->config.use_network ? "library-network.txt" : "library.txt");
}

// The folder the library is of: the card's, or the network folder's top.
static const char *libraryRoot(const App *app)
{
    return app->config.use_network ? DAV_ROOT : app->library_root;
}

#define NETWORK_COLOR UI_NETWORK_COLOR
#define IPTV_COLOR UI_IPTV_COLOR
#define ADDONS_COLOR UI_ADDONS_COLOR

// --- The library and its scan ---

// What Home was made of, once each time it changes: a crash just after leaves a trace in the log.
static void logHome(const App *app, bool showcase)
{
    static char root[LIBRARY_PATH_MAX];
    static size_t count = (size_t)-1, shows;
    static int rows = -1;
    const Library *lib = &app->library;
    if (!strcmp(root, lib->root) && count == lib->count && shows == lib->show_count && rows == app->home.count)
        return;
    snprintf(root, sizeof(root), "%s", lib->root);
    count = lib->count;
    shows = lib->show_count;
    rows = app->home.count;
    appLog("Home (%s) of %s: %zu videos, %zu series, %d rows", showcase ? "Showcase" : "classic", lib->root,
           lib->count, lib->show_count, app->home.count);
}

void appLibraryRefreshHome(App *app)
{
    // The Showcase (the default) or the classic rows; the IPTV list's Home is its channels' (app_iptvhome.c).
    bool showcase = !app->config.home_classic && !app->config.use_iptv;
    if (showcase) {
        homeComputeShowcase(&app->home, &app->library, &app->resume, &app->watched);
        homeSetTiles(&app->home, app->config.home_tiles);
    } else {
        homeCompute(&app->home, &app->library, &app->resume, &app->watched);
    }
    logHome(app, showcase);
    if (app->view.showcase != showcase)
        libviewSetHomeShowcase(&app->view, showcase);
    appLiveRebuild(app);  // The server's channels in its view, the IPTV list's in its own.
    if (app->config.use_iptv) {
        libviewSetIptvTabs(&app->view, &app->library);
        appXtreamApplyGroups(app);  // Films and Series in the category chosen.
    } else
        libviewSetTabs(&app->view, &app->library, app->config.use_jellyfin || app->config.use_addons,  // Libraries are tabs.
                       app->live.count > 0, appDownloadsTabShown(app));
    libviewClamp(&app->view, &app->library, &app->home);
}

static void startScan(App *app)
{
    const char *root = libraryRoot(app);
    if (!root[0])
        return;
    if (!libscanStart(root))
        appLog("The library scan couldn't start.");
}

// The card's library, or the network folder's: what was found last time at once, and a new scan.
static void loadCardLibrary(App *app)
{
    if (app->config.use_network && !davConfigured())
        app->config.use_network = false;  // Forgotten: the card's.
    const char *root = libraryRoot(app);
    if (!root[0]) {
        libraryInit(&app->library, "");
        // The first run: the welcome asks where the videos are (appWelcomeTick).
        return;
    }
    // What was found last time shows at once; a new scan replaces it.
    char path[LIBRARY_PATH_MAX];
    cachePath(app, path, sizeof(path));
    if (!libraryLoad(&app->library, path) || strcmp(app->library.root, root) != 0) {
        libraryFree(&app->library);
        libraryInit(&app->library, root);
    }
    appDownloadsNameLibrary(app, &app->library);
    appLibraryRefreshHome(app);
    startScan(app);
}

void appLibraryStart(App *app)
{
    libviewInit(&app->view);
    if (app->config.use_iptv && !app->config.use_jellyfin && appIptvConfigured()) {
        appLibraryShowIptv(app);
        return;
    }
    app->config.use_iptv = false;
    if (app->config.use_addons && !app->config.use_jellyfin && appStremioConfigured()) {
        appLibraryShowAddons(app);
        return;
    }
    app->config.use_addons = false;
    if (app->config.use_jellyfin && appJellyfinEnsure(app)) {
        libraryInit(&app->library, JF_ROOT);
        appLibraryRefreshHome(app);
        return;
    }
    app->config.use_jellyfin = false;
    loadCardLibrary(app);
}

// The server signed in after kind (or the first, from -1), or -1 for none.
static int nextSignedIn(int kind)
{
    for (int k = kind + 1; k < SERVER_KIND_COUNT; k++)
        if (jfSyncSignedIn((ServerKind)k))
            return k;
    return -1;
}

void appLibrarySetSource(App *app, bool jellyfin)
{
    int kind = jfSyncSignedIn((ServerKind)app->config.server_view) ? app->config.server_view : nextSignedIn(-1);
    if (jellyfin && kind >= 0) {
        appLibraryShowServer(app, (ServerKind)kind);
        return;
    }
    libscanStop();
    app->config.use_jellyfin = app->config.use_network = app->config.use_iptv = app->config.use_addons = false;
    appSaveSettings(app);
    libraryFree(&app->library);
    libviewInit(&app->view);
    loadCardLibrary(app);
}

void appLibraryShowNetwork(App *app)
{
    libscanStop();
    app->config.use_jellyfin = app->config.use_iptv = app->config.use_addons = false;
    app->config.use_network = true;
    appSaveSettings(app);
    libraryFree(&app->library);
    libviewInit(&app->view);
    loadCardLibrary(app);
}

void appLibraryShowIptv(App *app)
{
    libscanStop();
    app->config.use_jellyfin = app->config.use_network = app->config.use_addons = false;
    app->config.use_iptv = true;
    appSaveSettings(app);
    libraryFree(&app->library);
    libraryInit(&app->library, IPTV_PREFIX);  // Its channels, and an Xtream account's films and series.
    libviewInit(&app->view);
    app->view.tab = TAB_HOME;
    appIptvHomeReset(app);
    appXtreamInstall(app);
    appLibraryRefreshHome(app);
}

void appLibraryShowAddons(App *app)
{
    libscanStop();
    app->config.use_jellyfin = app->config.use_network = app->config.use_iptv = false;
    app->config.use_addons = true;
    appSaveSettings(app);
    libraryFree(&app->library);
    libraryInit(&app->library, STREMIO_PREFIX);
    libviewInit(&app->view);
    appStremioInstall(app);  // Their catalogs (what's come so far).
}

void appLibraryNetworkChanged(App *app)
{
    if (app->config.use_jellyfin || !app->config.use_network)
        return;
    if (davConfigured())
        appLibraryShowNetwork(app);  // Another address or user: its library again.
    else
        appLibrarySetSource(app, false);
}

static void installCopy(App *app, const JfCatalog *kept);

void appLibraryShowServer(App *app, ServerKind kind)
{
    libscanStop();
    bool changed = !app->config.use_jellyfin || app->config.server_view != (int)kind;
    app->config.use_jellyfin = true;
    app->config.use_network = app->config.use_iptv = app->config.use_addons = false;
    app->config.server_view = kind;
    appSaveSettings(app);
    if (changed)
        libviewInit(&app->view);
    if (app->server_has[kind]) {
        installCopy(app, &app->server_catalogs[kind]);
    } else {
        libraryFree(&app->library);
        libraryInit(&app->library, JF_ROOT);
        appLibraryRefreshHome(app);
        jfSyncRefresh();
    }
    if (kind == SERVER_JELLYFIN)
        appLiveReadSoon(app);  // The server may have channels now that it didn't have before.
}

// X goes through the card, the network folder (if there's one) and each server signed in, in turn.
void appLibraryToggleSource(App *app, uint64_t now)
{
    appLog("Source: next, from %s", app->config.use_addons    ? "addons"
                                    : app->config.use_iptv    ? "IPTV"
                                    : app->config.use_jellyfin ? serverKindName((ServerKind)app->config.server_view)
                                    : app->config.use_network ? "the network folder"
                                                              : "the card");
    if (app->config.use_addons) {  // The last: back to the card.
        appLibrarySetSource(app, false);
        return;
    }
    if (app->config.use_iptv) {
        if (appStremioConfigured())
            appLibraryShowAddons(app);
        else
            appLibrarySetSource(app, false);
        return;
    }
    if (appJellyfinEnsure(app))
        jfSyncGetStatus(&app->jf);
    bool on_card = !app->config.use_jellyfin && !app->config.use_network;
    if (on_card && davConfigured()) {
        appNetworkEnsurePassword(app, now);  // Without it, what was found before shows.
        appLibraryShowNetwork(app);
        return;
    }
    int next = nextSignedIn(app->config.use_jellyfin ? app->config.server_view : -1);
    if (next >= 0)
        appLibraryShowServer(app, (ServerKind)next);
    else if (appIptvConfigured())
        appLibraryShowIptv(app);
    else if (appStremioConfigured())
        appLibraryShowAddons(app);
    else if (!on_card)
        appLibrarySetSource(app, false);
    else {  // Nothing else to go to: Settings, to add one.
        appOpenSettings(app);
        appNotify(app, NOTICE_INFO, T("No other source yet: add one in Settings."), now);
    }
}

void appLibraryServerCatalog(App *app, ServerKind kind, JfCatalog *c)
{
    if (app->server_has[kind])
        jfCatalogFree(&app->server_catalogs[kind]);
    app->server_has[kind] = c != NULL;
    if (c)
        app->server_catalogs[kind] = *c;
    bool viewed = app->config.use_jellyfin && app->config.server_view == (int)kind;
    if (viewed && c)
        installCopy(app, c);
    else if (viewed)  // Signed out: the next server, or the card.
        appLibrarySetSource(app, nextSignedIn(-1) >= 0);
    if (c)
        appDownloadsCatalogCame(app, kind);
}

static void reapplyFavoriteChanges(App *app);

// What's on screen follows the library: indexes change with it.
static void afterNewLibrary(App *app, const char *details_path)
{
    if (app->player) {
        app->playing_item = libraryFind(&app->library, app->playing_path);
        app->next_item = libraryNextEpisode(&app->library, app->playing_item);
    }
    app->view.details = details_path[0] ? libraryFind(&app->library, details_path) : -1;
    appLibraryRefreshHome(app);
}

void appLibraryInstallBuilt(App *app, Library *built)
{
    char details[LIBRARY_PATH_MAX] = "";
    if (app->view.details >= 0 && (size_t)app->view.details < app->library.count)
        snprintf(details, sizeof(details), "%s", app->library.items[app->view.details].path);
    libraryFree(&app->library);
    app->library = *built;
    libraryInit(built, IPTV_PREFIX);  // The library is the app's now.
    afterNewLibrary(app, details);
}

void appLibraryInstallCatalog(App *app, JfCatalog *c)
{
    char details[LIBRARY_PATH_MAX] = "";
    if (app->view.details >= 0 && (size_t)app->view.details < app->library.count)
        snprintf(details, sizeof(details), "%s", app->library.items[app->view.details].path);
    jfMergeUserData(c, &app->resume, &app->watched);
    appDownloadsOverlayOffline(app);  // What was watched offline and isn't on the server yet.
    libraryFree(&app->library);
    app->library = c->library;
    libraryInit(&c->library, JF_ROOT);  // The library is the app's now.
    reapplyFavoriteChanges(app);
    thumbsRetryMissing();
    afterNewLibrary(app, details);
}

// Shows a copy of a server's kept library (the app's library is its own).
static void installCopy(App *app, const JfCatalog *kept)
{
    JfCatalog copy;
    jfCatalogInit(&copy);
    if (jfCatalogAppend(&copy, kept, NULL)) {
        libraryBuild(&copy.library);
        appLibraryInstallCatalog(app, &copy);
    }
    jfCatalogFree(&copy);
}

void appLibraryExit(App *app)
{
    libscanStop();
    libraryFree(&app->library);
    browserFree(&app->picker);
}

void appLibraryUpdate(App *app)
{
    Library found;
    bool ok;
    if (app->config.use_jellyfin || !libscanTake(&found, &ok))
        return;
    if (strcmp(found.root, libraryRoot(app)) != 0) {  // An old folder's scan.
        libraryFree(&found);
        return;
    }
    appLog("Library %s: %zu videos, %zu films, %zu series%s", found.root, found.count, found.film_count,
           found.show_count, ok ? "" : " (the folder can't be read)");
    if (!ok && found.count == 0 && app->library.count > 0) {  // The card or server didn't answer: keep what was there.
        libraryFree(&found);
        return;
    }
    appDownloadsNameLibrary(app, &found);
    libraryFree(&app->library);
    app->library = found;
    char path[LIBRARY_PATH_MAX];
    cachePath(app, path, sizeof(path));
    if (ok)
        librarySave(&app->library, path);
    afterNewLibrary(app, "");
}

static void useFolder(App *app, const char *folder)
{
    app->config.background[0] = '\0';  // Its images were in the old folder.
    backgroundLoad(folder, "", app->config.blur);
    libscanStop();
    app->config.use_jellyfin = app->config.use_network = false;  // A folder on the card was chosen: the library is there.
    snprintf(app->library_root, sizeof(app->library_root), "%s", folder);
    appSaveSettings(app);
    libraryFree(&app->library);
    libraryInit(&app->library, app->library_root);
    libviewInit(&app->view);
    appLibraryRefreshHome(app);
    app->picking = false;
    startScan(app);
}

void appOpenPicker(App *app)
{
    const char *start = app->library_root[0] ? app->library_root : BROWSER_ROOT;
    if (browserOpen(&app->picker, start) || browserOpen(&app->picker, BROWSER_ROOT))
        app->picking = true;
}

// --- Playing and opening ---

// "Paper Lanterns (1998)", "Rook · S01E03 · Lamentia".
static void itemName(const Library *lib, int item, char *out, size_t size)
{
    const LibraryItem *it = &lib->items[item];
    if (it->show < 0 && it->name.year)
        snprintf(out, size, "%s (%d)", it->name.title, it->name.year);
    else if (it->show < 0)
        snprintf(out, size, "%s", it->name.title);
    else if (it->name.title[0])
        snprintf(out, size, "%s \xC2\xB7 S%02dE%02d \xC2\xB7 %s", lib->shows[it->show].name, it->name.season,
                 it->name.episode, it->name.title);
    else
        snprintf(out, size, "%s \xC2\xB7 S%02dE%02d", lib->shows[it->show].name, it->name.season, it->name.episode);
}

static void playItem(App *app, int item, uint64_t now)
{
    const LibraryItem *it = &app->library.items[item];
    char name[APP_NAME_MAX];
    itemName(&app->library, item, name, sizeof(name));
    appStartVideo(app, it->path, name, now);
}

// A series opens its page; a video on a server or an Xtream film its details; one on the card plays.
static void openRef(App *app, LibraryRef ref, uint64_t now)
{
    if (ref.is_show) {
        libviewOpenShow(&app->view, &app->library, ref.index, -1);
        return;
    }
    const char *path = app->library.items[ref.index].path;
    if (jfIsRemoteLocation(path) || xtreamFilmOf(path) > 0)
        app->view.details = ref.index;
    else
        playItem(app, ref.index, now);
}

static void setWatched(App *app, int item)
{
    const char *path = app->library.items[item].path;
    bool watched = !watchedHas(&app->watched, path);
    watchedSet(&app->watched, path, watched);
    appDownloadsMarked(app, path, watched);
    appSaveWatched(app);
    if (jfIsRemoteLocation(path))
        jfSyncSetPlayed(path, watched);
    appLibraryRefreshHome(app);  // Up next changed.
}

// Marks the episode under the cursor on a series' page watched, or not.
static void toggleWatched(App *app)
{
    int item = libviewShowItem(&app->view, &app->library);
    if (item >= 0)
        setWatched(app, item);
}

// The location the server knows a film or series by. False for videos on the card.
static bool favoriteLocation(const Library *lib, LibraryRef ref, const char **out)
{
    if (ref.is_show) {
        const LibraryShow *show = &lib->shows[ref.index];
        *out = show->count ? lib->items[show->episodes[0]].series_art : NULL;
    } else {
        *out = lib->items[ref.index].path;
    }
    return *out && jfIsRemoteLocation(*out);
}

// Kept until the next catalog comes, which may have been read before it.
static void rememberFavoriteChange(App *app, const char *location, bool favorite)
{
    int at = 0;
    while (at < app->favorite_change_count && strcmp(app->favorite_changes[at], location))
        at++;
    if (at == APP_FAVORITE_CHANGES) {  // Full: the oldest goes.
        memmove(app->favorite_changes[0], app->favorite_changes[1], (APP_FAVORITE_CHANGES - 1) * LIBRARY_PATH_MAX);
        memmove(&app->favorite_values[0], &app->favorite_values[1], (APP_FAVORITE_CHANGES - 1) * sizeof(bool));
        at = APP_FAVORITE_CHANGES - 1;
    }
    snprintf(app->favorite_changes[at], LIBRARY_PATH_MAX, "%s", location);
    app->favorite_values[at] = favorite;
    if (at == app->favorite_change_count)
        app->favorite_change_count++;
}

// The film or series a favorite's location names, in the library.
static bool findFavorite(const Library *lib, const char *location, LibraryRef *out)
{
    int item = libraryFind(lib, location);
    if (item >= 0) {
        *out = (LibraryRef){false, item};
        return true;
    }
    for (size_t s = 0; s < lib->show_count; s++) {
        const LibraryShow *show = &lib->shows[s];
        const char *art = show->count ? lib->items[show->episodes[0]].series_art : NULL;
        if (art && !strcmp(art, location)) {
            *out = (LibraryRef){true, (int)s};
            return true;
        }
    }
    return false;
}

// Puts this console's latest favorites on a catalog that may be older than them.
static void reapplyFavoriteChanges(App *app)
{
    for (int i = 0; i < app->favorite_change_count; i++) {
        LibraryRef ref;
        if (findFavorite(&app->library, app->favorite_changes[i], &ref))
            librarySetFavorite(&app->library, ref, app->favorite_values[i]);
    }
    app->favorite_change_count = 0;
}

// Marks a film, or a series (an episode's), as a favorite or not, here and on the server.
static void toggleFavorite(App *app, LibraryRef ref, uint64_t now)
{
    Library *lib = &app->library;
    if (!ref.is_show && lib->items[ref.index].show >= 0)
        ref = (LibraryRef){true, lib->items[ref.index].show};
    const char *location;
    if (!favoriteLocation(lib, ref, &location))
        return;
    bool favorite = !(ref.is_show ? lib->shows[ref.index].favorite : lib->items[ref.index].favorite);
    librarySetFavorite(lib, ref, favorite);
    jfSyncSetFavorite(location, favorite);
    rememberFavoriteChange(app, location, favorite);
    appLibraryRefreshHome(app);
    appNotify(app, NOTICE_DONE, favorite ? T("Added to Favorites.") : T("Removed from Favorites."), now);
}

// True if a film or series can be a favorite: it comes from the server.
static bool canBeFavorite(const App *app, LibraryRef ref)
{
    const char *location;
    if (!ref.is_show && app->library.items[ref.index].show >= 0)
        ref = (LibraryRef){true, app->library.items[ref.index].show};
    return favoriteLocation(&app->library, ref, &location);
}

void appLibraryPlay(App *app, int item, uint64_t now)
{
    playItem(app, item, now);
}

static bool hasResume(const App *app, int item)
{
    int64_t at;
    return resumeFind(&app->resume, app->library.items[item].path, &at, NULL);
}

// The series page shown is a server's: its season can be downloaded.
static bool seasonDownloadable(const App *app)
{
    char label[128];
    bool done;
    return appSeasonDownloadLabel(app, app->view.show, app->view.season, &done, label, sizeof(label)) != NULL;
}

static void detailsInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now)
{
    LibView *v = &app->view;
    int item = v->details;
    LibHit hit = touch.type == TOUCH_TAP ? uiLibraryTap(touch.x, touch.y) : (LibHit){LIBHIT_NONE, 0, 0};
    if ((pressed & HidNpadButton_X) || hit.kind == LIBHIT_DETAILS_WATCHED) {
        setWatched(app, item);
    } else if ((pressed & HidNpadButton_R) || hit.kind == LIBHIT_FAVORITE) {
        toggleFavorite(app, (LibraryRef){false, item}, now);
    } else if ((pressed & HidNpadButton_L) || hit.kind == LIBHIT_DETAILS_DOWNLOAD) {
        appDownloadItem(app, item, now);
    } else if ((pressed & HidNpadButton_A) || hit.kind == LIBHIT_DETAILS_PLAY) {
        v->details = -1;
        playItem(app, item, now);
    } else if (((pressed & HidNpadButton_Y) && hasResume(app, item)) || hit.kind == LIBHIT_DETAILS_START) {
        v->details = -1;
        app->open_from_start = true;
        playItem(app, item, now);
        if (!app->opening)
            app->open_from_start = false;  // It didn't start.
    } else if ((pressed & HidNpadButton_B) || hit.kind == LIBHIT_CLOSE) {
        v->details = -1;
    }
}

static void openSelected(App *app, uint64_t now)
{
    LibraryRef ref;
    if (libviewSelected(&app->view, &app->library, &app->home, &ref))
        openRef(app, ref, now);
}

static void search(App *app, uint64_t now)
{
    app->view.tab = TAB_SEARCH;
    app->view.show = -1;
    char text[LIBVIEW_QUERY_MAX];
    switch (systemKeyboardInput(T("Search films and series"),app->view.query, text, sizeof(text),
                                LIBVIEW_QUERY_MAX - 1)) {
    case SYSTEM_KEYBOARD_OK:
        libviewSearch(&app->view, &app->library, text);
        appStremioSearch(app, text);  // In the Addons view, they're searched too.
        break;
    case SYSTEM_KEYBOARD_UNAVAILABLE: appNotify(app, NOTICE_ERROR, T("The keyboard couldn't open."), now); break;
    default: break;
    }
}

// --- Folder lists (Files, and choosing the library folder) ---

static void moveCursor(Browser *b, long delta)
{
    if (b->count == 0)
        return;
    long next = (long)b->cursor + delta;
    b->cursor = next < 0 ? 0 : next >= (long)b->count ? b->count - 1 : (size_t)next;
    b->free = false;  // A button: the list keeps the cursor in its middle again.
}

// Opens the entry under the cursor: a folder, or (not when picking) a video.
static void openEntry(App *app, Browser *b, bool picking, uint64_t now)
{
    if (b->cursor >= b->count)
        return;
    char target[BROWSER_PATH_MAX];
    if (browserSelectedPath(b, target, sizeof(target)) && davIsLocation(target) && !appNetworkEnsurePassword(app, now))
        return;
    if (b->items[b->cursor].is_dir) {
        if (!browserEnter(b))
            appNotify(app, NOTICE_ERROR, b->problem[0] ? b->problem : T("That folder can't be opened."), now);
    } else if (!picking) {
        char path[BROWSER_PATH_MAX];
        if (browserSelectedPath(b, path, sizeof(path)) && appStartVideo(app, path, b->items[b->cursor].name, now))
            appSaveSettings(app);  // The Files tab's folder.
    }
}

static BrowseMode pickMode(const App *app)
{
    return app->library_root[0] ? BROWSE_PICK : BROWSE_PICK_FIRST;
}

static void folderInput(App *app, Browser *b, bool picking, uint64_t pressed, TouchEvent touch, uint64_t now)
{
    if (pressed & BUTTONS_UP) moveCursor(b, -1);
    if (pressed & BUTTONS_DOWN) moveCursor(b, 1);
    if (pressed & BUTTONS_PAGE_UP) moveCursor(b, -UI_ROWS);
    if (pressed & BUTTONS_PAGE_DOWN) moveCursor(b, UI_ROWS);
    if (pressed & HidNpadButton_A) openEntry(app, b, picking, now);
    if ((pressed & HidNpadButton_B) && !browserUp(b) && picking)
        app->picking = false;  // At the card's top: the picker closes (the first time, back to the welcome).
    if (picking && (pressed & HidNpadButton_X)) {
        useFolder(app, b->path);
        return;
    }
    if (touch.type == TOUCH_TAP) {
        size_t row;
        switch (uiTapBrowser(b, touch.x, touch.y, picking ? pickMode(app) : BROWSE_FILES, &row)) {
        case TAP_USE_FOLDER: useFolder(app, b->path); break;
        case TAP_CANCEL: app->picking = false; break;
        case TAP_UP: browserUp(b); break;
        case TAP_ROW:
            b->cursor = row;
            openEntry(app, b, picking, now);
            break;
        default: break;
        }
    } else if (touch.type == TOUCH_DRAG) {
        appLibraryDrag(app, touch, now);
    }
}

// --- Buttons ---

// Up, down, left and right on the current screen (dx, dy: -1, 0 or 1).
static void move(App *app, int dx, int dy)
{
    LibView *v = &app->view;
    if (v->show >= 0) {
        libviewShowMove(v, &app->library, dx, dy);
    } else if (v->tab == TAB_HOME) {
        libviewHomeMove(v, &app->home, dx, dy);
    } else if (libviewGrid(v) >= 0) {
        int grid = libviewGrid(v);
        libviewGridMove(v, (LibGrid)grid, libviewGridCount(v, &app->library, grid), dx, dy);
    } else if (v->tab == TAB_SEARCH) {
        libviewResultsMove(v, dy);
    }
}

static void buttonInput(App *app, uint64_t pressed, uint64_t now)
{
    LibView *v = &app->view;
    int dx = (pressed & BUTTONS_LEFT) ? -1 : (pressed & BUTTONS_RIGHT) ? 1 : 0;
    int dy = (pressed & BUTTONS_UP) ? -1 : (pressed & BUTTONS_DOWN) ? 1 : 0;
    if (dx || dy)
        move(app, dx, dy);
    if (pressed & HidNpadButton_A) {
        if (v->tab == TAB_SEARCH && v->show < 0 && v->result_count == 0)
            search(app, now);
        else
            openSelected(app, now);
    }
    if ((pressed & HidNpadButton_B) && v->show >= 0)
        v->show = -1;
    if ((pressed & HidNpadButton_X) && v->show >= 0)
        toggleWatched(app);
}

// --- Touch ---

static void tapInput(App *app, LibHit hit, uint64_t now)
{
    LibView *v = &app->view;
    switch (hit.kind) {
    case LIBHIT_TAB: libviewGoToTab(v, hit.a); break;
    case LIBHIT_MENU: appOpenMenu(app); break;
    case LIBHIT_DOWNLOADS: appDownloadsOpen(app, now); break;
    case LIBHIT_SOURCE: appLibraryToggleSource(app, now); break;
    case LIBHIT_HOME_CARD: {
        // The Showcase shows what a tap chose in its large picture first; a second tap opens it.
        bool chosen = v->home_row == hit.a && v->home_cols[hit.a].cursor == hit.b;
        v->home_row = hit.a;
        v->home_cols[hit.a].cursor = hit.b;
        libviewHomeMove(v, &app->home, 0, 0);
        if (!v->showcase || chosen)
            openSelected(app, now);
        break;
    }
    case LIBHIT_HERO: openSelected(app, now); break;
    case LIBHIT_GRID_CARD:
        if (libviewGrid(v) < 0)
            break;
        v->grid[libviewGrid(v)].cursor = hit.a;
        openSelected(app, now);
        break;
    case LIBHIT_BACK: v->show = -1; break;
    case LIBHIT_SEASON: libviewShowMove(v, &app->library, hit.a - v->season, 0); break;
    case LIBHIT_EPISODE:
        v->episodes.cursor = hit.a;
        openSelected(app, now);
        break;
    case LIBHIT_WATCHED:
        v->episodes.cursor = hit.a;
        toggleWatched(app);
        break;
    case LIBHIT_SEARCH_BOX: search(app, now); break;
    case LIBHIT_RESULT:
        v->results_scroll.cursor = hit.a;
        openSelected(app, now);
        break;
    default: break;
    }
}

void appLibraryInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now)
{
    // Over the library the right stick moves like the d-pad too, a row or card at a time (the owner found
    // it doing nothing on Home and the tabs' grids, and jumping a page in Files); paging is left and right.
    pressed = buttonsRightStickAsDpad(pressed);
    if (app->app_menu != APP_MENU_NONE || app->settings_open)
        kineticStop(&app->kinetic);
    else
        appLibraryGlide(app, pressed, &touch, now);
    if (app->app_menu != APP_MENU_NONE) {
        appMenuInput(app, pressed, touch, now);
        return;
    }
    if (app->settings_open) {
        appSettingsInput(app, pressed, touch, now);
        return;
    }
    if (pressed & HidNpadButton_Plus) {
        appOpenMenu(app);
        return;
    }
    if (app->picking) {
        // Leaving without choosing goes back to the folder there was (the first time, to the welcome).
        if (pressed & HidNpadButton_Minus)
            app->picking = false;
        else
            folderInput(app, &app->picker, true, pressed, touch, now);
        return;
    }
    LibView *v = &app->view;
    if (app->streams_open) {  // The streams to choose from take the buttons.
        appStremioStreamsInput(app, pressed, touch, now);
        return;
    }
    if (v->details >= 0) {
        detailsInput(app, pressed, touch, now);
        return;
    }
    // On a series' page from the server, R marks the series a favorite (ZR still changes tab).
    LibraryRef shown = {true, v->show};
    bool favorite_here = v->show >= 0 && (size_t)v->show < app->library.show_count && canBeFavorite(app, shown);
    if (favorite_here && (pressed & HidNpadButton_R)) {
        toggleFavorite(app, shown, now);
        return;
    }
    // And L downloads the season shown (ZL still changes tab).
    bool season_download = v->show >= 0 && (size_t)v->show < app->library.show_count && seasonDownloadable(app);
    if (season_download && (pressed & HidNpadButton_L)) {
        appDownloadSeason(app, v->show, v->season, now);
        return;
    }
    bool live_tab = v->tab == TAB_LIVE && v->show < 0;
    if (live_tab && app->live_groups_open) {  // The IPTV list's groups take the buttons until one is chosen.
        appLiveGroupsInput(app, pressed, touch, now);
        return;
    }
    bool iptv_home = app->config.use_iptv && v->tab == TAB_HOME && v->show < 0;
    if ((iptv_home || (live_tab && app->config.use_iptv)) && (pressed & HidNpadButton_R)) {
        // R: the channel under the cursor in the favorites (ZR still changes tab).
        int channel = iptv_home ? iptvHomeChannel(&app->iptv_home, app->iptv_home_view.rows.cursor,
                                                  app->iptv_home_view.cols[app->iptv_home_view.rows.cursor].cursor)
                                : appLiveIptvSelected(app);
        if (channel >= 0)
            appIptvToggleFavorite(app, channel, now);
        return;
    }
    if (live_tab && (pressed & HidNpadButton_Y) && app->config.use_iptv && app->iptv.group_count > 0) {
        appLiveOpenGroups(app);
        return;
    }
    // An Xtream account's categories, over Films or Series: Y there, as for the Live tab's groups.
    bool grouped = appXtreamHasGroups(app);
    if (grouped && app->xtream_groups_open) {
        appXtreamGroupsInput(app, pressed, touch, now);
        return;
    }
    app->xtream_groups_open = false;
    if (grouped && (pressed & HidNpadButton_Y)) {
        appXtreamOpenGroups(app);
        return;
    }
    if (pressed & (BUTTONS_TAB_LEFT | BUTTONS_TAB_RIGHT)) {
        libviewNextTab(v, (pressed & BUTTONS_TAB_LEFT) ? -1 : 1);
        return;  // A tap this frame was aimed at the old tab.
    }
    if (pressed & HidNpadButton_Minus) {
        appOpenSettings(app);
        return;
    }
    bool downloads = v->tab == TAB_DOWNLOADS && v->show < 0;  // X and Y are its rows' there.
    // The IPTV view has Search only with an Xtream account's films and series.
    if ((pressed & HidNpadButton_Y) && !downloads && (!app->config.use_iptv || app->library.count > 0)) {
        search(app, now);
        return;
    }
    if ((pressed & HidNpadButton_X) && v->show < 0 && !downloads) {
        appLibraryToggleSource(app, now);
        return;
    }
    LibHit hit = touch.type == TOUCH_TAP ? uiLibraryTap(touch.x, touch.y) : (LibHit){LIBHIT_NONE, 0, 0};
    bool header = hit.kind == LIBHIT_TAB || hit.kind == LIBHIT_MENU || hit.kind == LIBHIT_SOURCE ||
                  hit.kind == LIBHIT_DOWNLOADS;
    if (v->tab == TAB_FILES && v->show < 0) {
        if (header)
            tapInput(app, hit, now);
        else if (!(pressed & (BUTTONS_TAB_LEFT | BUTTONS_TAB_RIGHT)))
            folderInput(app, &app->browser, false, pressed, touch, now);
        return;
    }
    if (v->tab == TAB_DOWNLOADS && v->show < 0) {
        if (header)
            tapInput(app, hit, now);
        else
            appDownloadsInput(app, pressed, touch, now);
        return;
    }
    if (v->tab == TAB_LIVE && v->show < 0) {
        if (header)
            tapInput(app, hit, now);
        else
            appLiveInput(app, pressed, touch, now);
        return;
    }
    if (iptv_home) {
        if (header)
            tapInput(app, hit, now);
        else
            appIptvHomeInput(app, pressed, touch, now);
        return;
    }
    buttonInput(app, pressed, now);
    if (hit.kind == LIBHIT_FAVORITE && favorite_here)
        toggleFavorite(app, shown, now);
    else if (hit.kind == LIBHIT_SHOW_DOWNLOAD && season_download)
        appDownloadSeason(app, v->show, v->season, now);
    else if (hit.kind == LIBHIT_LIVE_GROUPS && grouped)
        appXtreamOpenGroups(app);
    else if (hit.kind != LIBHIT_NONE)
        tapInput(app, hit, now);
    else if (touch.type == TOUCH_DRAG)
        appLibraryDrag(app, touch, now);
}

// --- Drawing ---

void appLibraryRescan(App *app, uint64_t now)
{
    if (app->config.use_jellyfin) {
        jfSyncRefresh();
    } else if (!libraryRoot(app)[0]) {
        appShowMessage(app, T("Choose the library folder first."), now);
    } else if (libscanBusy(NULL)) {
        appShowMessage(app, T("Already looking for videos."), now);
    } else if (app->config.use_network && !appNetworkEnsurePassword(app, now)) {
        return;
    } else {
        startScan(app);
    }
}

// A series page's buttons at the top right: favorite (R) and download the season (L).
static void drawShowButtons(const App *app)
{
    const LibView *v = &app->view;
    if (v->show < 0 || (size_t)v->show >= app->library.show_count)
        return;
    LibraryRef shown = {true, v->show};
    bool favorites = canBeFavorite(app, shown), favorite = app->library.shows[v->show].favorite, done;
    if (favorites)
        uiDrawShowFavorite(favorite);
    char label[128];
    if (appSeasonDownloadLabel(app, v->show, v->season, &done, label, sizeof(label)))
        uiDrawShowDownload(label, done, favorites, favorite);
}

void appLibraryDraw(App *app, uint64_t now)
{
    appApplyViewAccent(app);
    // Messages are said in the notice bubble (main.c draws it over everything).
    uiLibraryResetHits();
    uiLibrarySetWatched(&app->watched);
    ServerKind view = (ServerKind)app->config.server_view;
    if (app->config.use_jellyfin)
        uiLibrarySetSource(serverKindName(view), uiServerColor(view));
    else if (app->config.use_network)
        uiLibrarySetSource(T("Network folder"), NETWORK_COLOR);
    else if (app->config.use_iptv)
        uiLibrarySetSource(T("IPTV"), IPTV_COLOR);
    else if (app->config.use_addons)
        uiLibrarySetSource(T("Addons"), ADDONS_COLOR);
    else
        uiLibrarySetSource(T("SD card"), COLOR_ACCENT);
    if (app->picking) {
        uiDrawBackground();
        uiDrawBrowser(&app->picker, &app->resume, pickMode(app));
        uiDrawFooterStrip();
        appSettingsDraw(app);
        appMenuDraw(app);
        uiDrawFooterHelp(appFooterHelp(app));
        return;
    }
    char status[128] = "";
    size_t found;
    if (app->config.use_jellyfin && app->jf.refreshing)
        snprintf(status, sizeof(status), T("Updating the servers' libraries... %d"),app->jf.loaded);  // All signed in.
    else if (libscanBusy(&found))
        snprintf(status, sizeof(status), T("Looking for videos... %zu"),found);
    else if (appStremioReadingMore(app))
        snprintf(status, sizeof(status), "%s", T("Reading more of this catalog..."));
    if (app->view.tab == TAB_FILES && app->view.show < 0) {
        uiDrawBackground();
        uiDrawTabBar(&app->view, &app->library, status);
        uiDrawBrowser(&app->browser, &app->resume, BROWSE_FILES);
        uiDrawFooterStrip();
    } else {
        appXtreamSetGridLabels(app);
        uiDrawLibrary(&app->view, &app->library, &app->home, &app->resume,
                      app->library_root[0] != '\0' || app->config.use_jellyfin || app->config.use_iptv, status);
        if (app->view.tab == TAB_LIVE && app->view.show < 0)
            appLiveDraw(app);
        if (app->view.iptv && app->view.tab == TAB_HOME && app->view.show < 0)
            appIptvHomeDraw(app);
        if (app->view.tab == TAB_DOWNLOADS && app->view.show < 0)
            appDownloadsDraw(app);
        appXtreamGroupsDraw(app);
        drawShowButtons(app);
    }
    if (app->view.details >= 0 && (size_t)app->view.details < app->library.count) {
        int item = app->view.details;
        char download[128], note[320];
        uiDrawDetails(&app->library, item, &app->resume, appDetailsNote(app, item, note, sizeof(note)),
                      canBeFavorite(app, (LibraryRef){false, item}),
                      appDownloadLabel(app, item, download, sizeof(download)));
    }
    if (app->streams_open)
        appStremioStreamsDraw(app);
    appSettingsDraw(app);
    appMenuDraw(app);
    uiDrawFooterHelp(appFooterHelp(app));
}
