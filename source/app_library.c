// The library's screens: the tabs (Home, Films, Series, Search, Files), a
// series' page, and choosing the library folder. L and R (or ZL and ZR) change tab, Y searches, X marks an
// episode watched, - opens Settings, + the menu.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <switch.h>

#include "app.h"
#include "background.h"
#include "buttons.h"
#include "gfx.h"
#include "keyboard_system.h"
#include "libscan.h"
#include "thumbs.h"
#include "ui_library.h"
#include "ui_style.h"

#define BUTTONS_TAB_LEFT (HidNpadButton_L | HidNpadButton_ZL)
#define BUTTONS_TAB_RIGHT (HidNpadButton_R | HidNpadButton_ZR)

static void cachePath(char *out, size_t size)
{
    appConfigPath(out, size, "library.txt");
}

// --- The library and its scan ---

void appLibraryRefreshHome(App *app)
{
    homeCompute(&app->home, &app->library, &app->resume, &app->watched);
    bool jellyfin = app->config.use_jellyfin && app->config.server_view == SERVER_JELLYFIN;  // Live TV is its.
    libviewSetTabs(&app->view, &app->library, app->config.use_jellyfin,  // The server's libraries are tabs.
                   jellyfin && app->live.count > 0);
    libviewClamp(&app->view, &app->library, &app->home);
}

static void startScan(App *app)
{
    if (!app->library_root[0])
        return;
    if (!libscanStart(app->library_root))
        appLog("The library scan couldn't start.");
}

// The card's library: what was found last time at once, and a new scan.
static void loadCardLibrary(App *app)
{
    if (!app->library_root[0]) {
        libraryInit(&app->library, "");
        app->picking = browserOpen(&app->picker, BROWSER_ROOT);  // The first run: choose a folder.
        return;
    }
    // What was found last time shows at once; a new scan replaces it.
    char path[LIBRARY_PATH_MAX];
    cachePath(path, sizeof(path));
    if (!libraryLoad(&app->library, path) || strcmp(app->library.root, app->library_root) != 0) {
        libraryFree(&app->library);
        libraryInit(&app->library, app->library_root);
    }
    appLibraryRefreshHome(app);
    startScan(app);
}

void appLibraryStart(App *app)
{
    libviewInit(&app->view);
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
    app->config.use_jellyfin = false;
    appSaveSettings(app);
    libraryFree(&app->library);
    libviewInit(&app->view);
    loadCardLibrary(app);
}

static void installCopy(App *app, const JfCatalog *kept);

void appLibraryShowServer(App *app, ServerKind kind)
{
    libscanStop();
    bool changed = !app->config.use_jellyfin || app->config.server_view != (int)kind;
    app->config.use_jellyfin = true;
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

// X goes through the card and each server signed in, in turn.
void appLibraryToggleSource(App *app, uint64_t now)
{
    if (appJellyfinEnsure(app))
        jfSyncGetStatus(&app->jf);
    int next = nextSignedIn(app->config.use_jellyfin ? app->config.server_view : -1);
    if (next >= 0)
        appLibraryShowServer(app, (ServerKind)next);
    else if (app->config.use_jellyfin)
        appLibrarySetSource(app, false);
    else
        appOpenJellyfinSettings(app, now);
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

void appLibraryInstallCatalog(App *app, JfCatalog *c)
{
    char details[LIBRARY_PATH_MAX] = "";
    if (app->view.details >= 0 && (size_t)app->view.details < app->library.count)
        snprintf(details, sizeof(details), "%s", app->library.items[app->view.details].path);
    jfMergeUserData(c, &app->resume, &app->watched);
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
    if (strcmp(found.root, app->library_root) != 0) {  // An old folder's scan.
        libraryFree(&found);
        return;
    }
    appLog("Library %s: %zu videos, %zu films, %zu series%s", found.root, found.count, found.film_count,
           found.show_count, ok ? "" : " (the folder can't be read)");
    if (!ok && found.count == 0 && app->library.count > 0) {  // The card didn't answer: keep what was there.
        libraryFree(&found);
        return;
    }
    libraryFree(&app->library);
    app->library = found;
    char path[LIBRARY_PATH_MAX];
    cachePath(path, sizeof(path));
    if (ok)
        librarySave(&app->library, path);
    afterNewLibrary(app, "");
}

static void useFolder(App *app, const char *folder)
{
    app->config.background[0] = '\0';  // Its images were in the old folder.
    backgroundLoad(folder, "", app->config.blur);
    libscanStop();
    app->config.use_jellyfin = false;  // A folder on the card was chosen: the library is there.
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

// A series opens its page; a video on the server its details; one on the card plays.
static void openRef(App *app, LibraryRef ref, uint64_t now)
{
    if (ref.is_show)
        libviewOpenShow(&app->view, &app->library, ref.index, -1);
    else if (jfIsRemoteLocation(app->library.items[ref.index].path))
        app->view.details = ref.index;
    else
        playItem(app, ref.index, now);
}

static void setWatched(App *app, int item)
{
    const char *path = app->library.items[item].path;
    bool watched = !watchedHas(&app->watched, path);
    watchedSet(&app->watched, path, watched);
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
    appShowMessage(app, favorite ? "Added to Favorites." : "Removed from Favorites.", now);
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

static void detailsInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now)
{
    LibView *v = &app->view;
    int item = v->details;
    LibHit hit = touch.type == TOUCH_TAP ? uiLibraryTap(touch.x, touch.y) : (LibHit){LIBHIT_NONE, 0, 0};
    if ((pressed & HidNpadButton_X) || hit.kind == LIBHIT_DETAILS_WATCHED) {
        setWatched(app, item);
    } else if ((pressed & HidNpadButton_R) || hit.kind == LIBHIT_FAVORITE) {
        toggleFavorite(app, (LibraryRef){false, item}, now);
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
    switch (systemKeyboardInput("Search films and series", app->view.query, text, sizeof(text),
                                LIBVIEW_QUERY_MAX - 1)) {
    case SYSTEM_KEYBOARD_OK: libviewSearch(&app->view, &app->library, text); break;
    case SYSTEM_KEYBOARD_UNAVAILABLE: appShowMessage(app, "The keyboard couldn't open.", now); break;
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
    if (b->items[b->cursor].is_dir) {
        if (!browserEnter(b))
            appShowMessage(app, "That folder can't be opened.", now);
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
    if (pressed & HidNpadButton_B) browserUp(b);
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
    case LIBHIT_SOURCE: appLibraryToggleSource(app, now); break;
    case LIBHIT_HOME_CARD:
        v->home_row = hit.a;
        v->home_cols[hit.a].cursor = hit.b;
        libviewHomeMove(v, &app->home, 0, 0);
        openSelected(app, now);
        break;
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

// --- Following a finger ---

typedef enum {
    SCROLL_NONE,
    SCROLL_HOME,
    SCROLL_HOME_ROW,
    SCROLL_GRID,
    SCROLL_EPISODES,
    SCROLL_RESULTS,
    SCROLL_FILES,
    SCROLL_LIVE,
} ScrollTarget;

// What's on screen: a list being scrolled stops if this changes.
static int viewKey(const App *app)
{
    const LibView *v = &app->view;
    return (((v->tab * 64 + v->collection + 1) * 65536 + v->show + 1) * 64 + v->season) * 2 + app->picking;
}

// Moves the list being scrolled by px (positive: up, or left across a Home row).
// False if it reached an end.
static bool scrollTarget(App *app, int px)
{
    LibView *v = &app->view;
    if (viewKey(app) != app->scroll_view)
        return false;
    int grid = libviewGrid(v), first, count;
    switch ((ScrollTarget)app->scroll_target) {
    case SCROLL_HOME: return libviewHomeScrollPx(v, &app->home, px);
    case SCROLL_HOME_ROW:
        v->home_row = app->scroll_row;  // The row a finger moves along is the one chosen.
        return libviewHomeRowScrollPx(v, &app->home, app->scroll_row, px);
    case SCROLL_GRID:
        return grid >= 0 && scrollPx(&v->grid[grid], px, LIBVIEW_GRID_ROW_PX, LIBVIEW_GRID_ROWS, LIBVIEW_COLS,
                                            libviewGridCount(v, &app->library, grid));
    case SCROLL_EPISODES:
        libviewSeasonRange(&app->library, v->show, v->season, &first, &count);
        return scrollPx(&v->episodes, px, LIBVIEW_LIST_ROW_PX, LIBVIEW_LIST_ROWS, 1, count);
    case SCROLL_RESULTS:
        return scrollPx(&v->results_scroll, px, LIBVIEW_LIST_ROW_PX, LIBVIEW_LIST_ROWS, 1, v->result_count);
    case SCROLL_FILES: return app->scroll_browser && browserScrollPx(app->scroll_browser, px, UI_ROW_H, UI_ROWS);
    case SCROLL_LIVE: return scrollPx(&v->live, px, LIBVIEW_LIVE_ROW_PX, LIBVIEW_LIVE_ROWS, 1, app->live.count);
    default: return false;
    }
}

// A drag begins: which list it moves (across a Home row if it went more
// across than down, as a phone decides).
static void startScroll(App *app, TouchEvent touch)
{
    LibView *v = &app->view;
    app->scroll_target = SCROLL_NONE;
    app->scroll_view = viewKey(app);
    if (app->picking || (v->tab == TAB_FILES && v->show < 0)) {  // The file list, under its folder's line.
        app->scroll_browser = app->picking ? &app->picker : &app->browser;
        app->scroll_target = touch.start_y >= UI_LIST_Y ? SCROLL_FILES : SCROLL_NONE;
        return;
    }
    if (touch.start_y < UI_TAB_BAR_H)
        return;
    if (v->tab == TAB_LIVE && v->show < 0) {
        app->scroll_target = SCROLL_LIVE;
    } else if (v->show >= 0) {
        app->scroll_target = SCROLL_EPISODES;
    } else if (v->tab == TAB_HOME) {
        LibHit hit = uiLibraryTap(touch.start_x, touch.start_y);
        bool across = abs(touch.x - touch.start_x) > abs(touch.y - touch.start_y);
        app->scroll_target = hit.kind == LIBHIT_HOME_CARD && across ? SCROLL_HOME_ROW : SCROLL_HOME;
        app->scroll_row = hit.a;
    } else if (libviewGrid(v) >= 0) {
        app->scroll_target = SCROLL_GRID;
    } else if (v->tab == TAB_SEARCH) {
        app->scroll_target = SCROLL_RESULTS;
    }
}

void appLibraryDrag(App *app, TouchEvent touch, uint64_t now)
{
    bool starting = !app->kinetic.dragging;
    if (starting) {
        startScroll(app, touch);
        kineticDrag(&app->kinetic, 0, now);
    }
    // The content goes with the finger: dragging up shows what's below.
    int px = app->scroll_target == SCROLL_HOME_ROW ? -touch.dx : -touch.dy;
    scrollTarget(app, px);
    if (!starting)
        kineticDrag(&app->kinetic, px, now);
}

// A finger came down, let go, or nothing: a list glides on, or stops.
static void glideInput(App *app, uint64_t pressed, TouchEvent *touch, uint64_t now)
{
    if (touch->type == TOUCH_PRESS) {  // A press stops a glide, and isn't a tap on what it stopped over.
        app->swallow_tap = kineticGliding(&app->kinetic);
        kineticStop(&app->kinetic);
    } else if (touch->type == TOUCH_TAP && app->swallow_tap) {
        touch->type = TOUCH_NONE;
    }
    if (touch->type != TOUCH_PRESS && touch->type != TOUCH_DRAG)
        app->swallow_tap = false;
    if (touch->type == TOUCH_DRAG_END && app->kinetic.dragging)
        kineticRelease(&app->kinetic, now);
    if (pressed)
        kineticStop(&app->kinetic);  // A button takes over from the finger.
    int px = kineticStep(&app->kinetic, now);
    if (px && !scrollTarget(app, px))
        kineticStop(&app->kinetic);  // An end, or another view.
}

void appLibraryInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now)
{
    if (app->app_menu != APP_MENU_NONE || app->settings_open)
        kineticStop(&app->kinetic);
    else
        glideInput(app, pressed, &touch, now);
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
        // Leaving without choosing goes back to the folder there was.
        if ((pressed & HidNpadButton_Minus) && app->library_root[0])
            app->picking = false;
        else
            folderInput(app, &app->picker, true, pressed, touch, now);
        return;
    }
    LibView *v = &app->view;
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
    if (pressed & (BUTTONS_TAB_LEFT | BUTTONS_TAB_RIGHT)) {
        libviewNextTab(v, (pressed & BUTTONS_TAB_LEFT) ? -1 : 1);
        return;  // A tap this frame was aimed at the old tab.
    }
    if (pressed & HidNpadButton_Minus) {
        appOpenSettings(app);
        return;
    }
    if (pressed & HidNpadButton_Y) {
        search(app, now);
        return;
    }
    if ((pressed & HidNpadButton_X) && v->show < 0) {
        appLibraryToggleSource(app, now);
        return;
    }
    LibHit hit = touch.type == TOUCH_TAP ? uiLibraryTap(touch.x, touch.y) : (LibHit){LIBHIT_NONE, 0, 0};
    if (v->tab == TAB_FILES && v->show < 0) {
        if (hit.kind == LIBHIT_TAB || hit.kind == LIBHIT_MENU || hit.kind == LIBHIT_SOURCE)
            tapInput(app, hit, now);
        else if (!(pressed & (BUTTONS_TAB_LEFT | BUTTONS_TAB_RIGHT)))
            folderInput(app, &app->browser, false, pressed, touch, now);
        return;
    }
    if (v->tab == TAB_LIVE && v->show < 0) {
        if (hit.kind == LIBHIT_TAB || hit.kind == LIBHIT_MENU || hit.kind == LIBHIT_SOURCE)
            tapInput(app, hit, now);
        else
            appLiveInput(app, pressed, touch, now);
        return;
    }
    buttonInput(app, pressed, now);
    if (hit.kind == LIBHIT_FAVORITE && favorite_here)
        toggleFavorite(app, shown, now);
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
    } else if (!app->library_root[0]) {
        appShowMessage(app, "Choose the library folder first.", now);
    } else if (libscanBusy(NULL)) {
        appShowMessage(app, "Already looking for videos.", now);
    } else {
        startScan(app);
    }
}

void appLibraryDraw(App *app, uint64_t now)
{
    appApplyViewAccent(app);
    const char *message = now < app->message_until ? app->message : NULL;
    bool overlay = app->settings_open || app->app_menu != APP_MENU_NONE;
    const char *under = overlay ? NULL : message;  // With an overlay open, said over it.
    uiLibraryResetHits();
    uiLibrarySetWatched(&app->watched);
    ServerKind view = (ServerKind)app->config.server_view;
    uiLibrarySetSource(app->config.use_jellyfin ? serverKindName(view) : "SD card",
                       app->config.use_jellyfin ? uiServerColor(view) : COLOR_ACCENT);
    if (app->picking) {
        uiDrawBackground();
        uiDrawBrowser(&app->picker, &app->resume, pickMode(app));
        uiDrawFooterStrip();
        uiDrawToast(under);
        appSettingsDraw(app);
        appMenuDraw(app);
        if (overlay)
            uiDrawToast(message);
        uiDrawFooterHelp(appFooterHelp(app));
        return;
    }
    char status[64] = "";
    size_t found;
    if (app->config.use_jellyfin && app->jf.refreshing)
        snprintf(status, sizeof(status), "Updating the servers' libraries... %d", app->jf.loaded);  // All signed in.
    else if (libscanBusy(&found))
        snprintf(status, sizeof(status), "Looking for videos... %zu", found);
    if (app->view.tab == TAB_FILES && app->view.show < 0) {
        uiDrawBackground();
        uiDrawTabBar(&app->view, &app->library, status);
        uiDrawBrowser(&app->browser, &app->resume, BROWSE_FILES);
        uiDrawFooterStrip();
        uiDrawToast(under);
    } else {
        uiDrawLibrary(&app->view, &app->library, &app->home, &app->resume,
                      app->library_root[0] != '\0' || app->config.use_jellyfin, status, under);
        if (app->view.tab == TAB_LIVE && app->view.show < 0)
            appLiveDraw(app);
        LibraryRef shown = {true, app->view.show};
        if (app->view.show >= 0 && (size_t)app->view.show < app->library.show_count && canBeFavorite(app, shown))
            uiDrawShowFavorite(app->library.shows[app->view.show].favorite);
    }
    if (app->view.details >= 0 && (size_t)app->view.details < app->library.count) {
        int item = app->view.details;
        uiDrawDetails(&app->library, item, &app->resume,
                      jfPlayabilityMessage((JfPlayability)app->library.items[item].blocked),
                      canBeFavorite(app, (LibraryRef){false, item}));
    }
    appSettingsDraw(app);
    appMenuDraw(app);
    if (overlay)
        uiDrawToast(message);
    uiDrawFooterHelp(appFooterHelp(app));
}
