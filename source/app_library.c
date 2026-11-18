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
#include "ui_library.h"
#include "ui_style.h"

#define BUTTONS_TAB_LEFT (HidNpadButton_L | HidNpadButton_ZL)
#define BUTTONS_TAB_RIGHT (HidNpadButton_R | HidNpadButton_ZR)
#define HOME_DRAG_W 232  // A card and its gap: dragging this far moves a Home row by one.
#define GRID_DRAG_H 186  // A row of the grid.
#define LIST_DRAG_H 58   // A row of a list.

static void cachePath(char *out, size_t size)
{
    appConfigPath(out, size, "library.txt");
}

// --- The library and its scan ---

void appLibraryRefreshHome(App *app)
{
    homeCompute(&app->home, &app->library, &app->resume, &app->watched);
    libviewClamp(&app->view, &app->library, &app->home);
}

static void startScan(App *app)
{
    if (!app->library_root[0])
        return;
    if (!libscanStart(app->library_root))
        appLog("The library scan couldn't start.");
}

void appLibraryStart(App *app)
{
    libviewInit(&app->view);
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
    if (!libscanTake(&found, &ok))
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
    if (app->player) {  // The indexes were the old library's.
        app->playing_item = libraryFind(&app->library, app->playing_path);
        app->next_item = libraryNextEpisode(&app->library, app->playing_item);
    }
    char path[LIBRARY_PATH_MAX];
    cachePath(path, sizeof(path));
    if (ok)
        librarySave(&app->library, path);
    appLibraryRefreshHome(app);
}

static void useFolder(App *app, const char *folder)
{
    app->config.background[0] = '\0';  // Its images were in the old folder.
    backgroundLoad(folder, "", app->config.blur);
    libscanStop();
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

static void openRef(App *app, LibraryRef ref, uint64_t now)
{
    if (ref.is_show) {
        libviewOpenShow(&app->view, &app->library, ref.index, -1);
        return;
    }
    char name[APP_NAME_MAX];
    itemName(&app->library, ref.index, name, sizeof(name));
    appStartVideo(app, app->library.items[ref.index].path, name, now);
}

// Marks the episode under the cursor on a series' page watched, or not.
static void toggleWatched(App *app)
{
    int item = libviewShowItem(&app->view, &app->library);
    if (item < 0)
        return;
    const char *path = app->library.items[item].path;
    watchedSet(&app->watched, path, !watchedHas(&app->watched, path));
    appSaveWatched(app);
    appLibraryRefreshHome(app);  // Up next changed.
}

void appLibraryPlay(App *app, int item, uint64_t now)
{
    openRef(app, (LibraryRef){false, item}, now);
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

static void dragRows(App *app, int dy, int row_h, void (*move)(App *, int))
{
    app->drag_rows -= dy;  // Dragging up shows the rows below, as on a phone.
    for (; app->drag_rows >= row_h; app->drag_rows -= row_h)
        move(app, 1);
    for (; app->drag_rows <= -row_h; app->drag_rows += row_h)
        move(app, -1);
}

static Browser *g_drag_browser;

static void moveDragBrowser(App *app, int delta)
{
    (void)app;
    moveCursor(g_drag_browser, delta);
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
    } else if (touch.type == TOUCH_DRAG && touch.start_y >= UI_LIST_Y) {
        g_drag_browser = b;
        dragRows(app, touch.dy, UI_ROW_H, moveDragBrowser);
    }
}

// --- Buttons ---

static void homeMoveRows(App *app, int delta)
{
    libviewHomeMove(&app->view, &app->home, 0, delta);
}

static void gridMoveRows(App *app, int delta)
{
    LibGrid grid = app->view.tab == TAB_FILMS ? GRID_FILMS : GRID_SERIES;
    int count = grid == GRID_FILMS ? (int)app->library.film_count : (int)app->library.show_count;
    libviewGridMove(&app->view, grid, count, 0, delta);
}

static void showMoveRows(App *app, int delta)
{
    libviewShowMove(&app->view, &app->library, 0, delta);
}

static void resultsMoveRows(App *app, int delta)
{
    libviewResultsMove(&app->view, delta);
}

// Up, down, left and right on the current screen (dx, dy: -1, 0 or 1).
static void move(App *app, int dx, int dy)
{
    LibView *v = &app->view;
    if (v->show >= 0) {
        libviewShowMove(v, &app->library, dx, dy);
    } else if (v->tab == TAB_HOME) {
        libviewHomeMove(v, &app->home, dx, dy);
    } else if (v->tab == TAB_FILMS || v->tab == TAB_SERIES) {
        LibGrid grid = v->tab == TAB_FILMS ? GRID_FILMS : GRID_SERIES;
        int count = grid == GRID_FILMS ? (int)app->library.film_count : (int)app->library.show_count;
        libviewGridMove(v, grid, count, dx, dy);
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
    case LIBHIT_TAB:
        v->tab = (LibTab)hit.a;
        v->show = -1;
        break;
    case LIBHIT_MENU: appOpenMenu(app); break;
    case LIBHIT_HOME_CARD:
        v->home_row = hit.a;
        v->home_cols[hit.a].cursor = hit.b;
        libviewHomeMove(v, &app->home, 0, 0);
        openSelected(app, now);
        break;
    case LIBHIT_GRID_CARD:
        v->grid[v->tab == TAB_FILMS ? GRID_FILMS : GRID_SERIES].cursor = hit.a;
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

static void dragInput(App *app, TouchEvent touch)
{
    LibView *v = &app->view;
    if (touch.start_y < UI_TAB_BAR_H)
        return;
    if (v->show >= 0) {
        dragRows(app, touch.dy, LIST_DRAG_H, showMoveRows);
    } else if (v->tab == TAB_HOME) {
        // Across a row moves along it; up and down, between rows.
        LibHit hit = uiLibraryTap(touch.start_x, touch.start_y);
        if (hit.kind == LIBHIT_HOME_CARD) {
            app->drag_cols -= touch.dx;
            if (abs(app->drag_cols) >= HOME_DRAG_W / 4)  // Across, not a wobble of a drag down.
                v->home_row = hit.a;
            for (; app->drag_cols >= HOME_DRAG_W; app->drag_cols -= HOME_DRAG_W)
                libviewHomeMove(v, &app->home, 1, 0);
            for (; app->drag_cols <= -HOME_DRAG_W; app->drag_cols += HOME_DRAG_W)
                libviewHomeMove(v, &app->home, -1, 0);
        }
        dragRows(app, touch.dy, HOME_DRAG_W - 30, homeMoveRows);
    } else if (v->tab == TAB_FILMS || v->tab == TAB_SERIES) {
        dragRows(app, touch.dy, GRID_DRAG_H, gridMoveRows);
    } else if (v->tab == TAB_SEARCH) {
        dragRows(app, touch.dy, LIST_DRAG_H, resultsMoveRows);
    }
}

void appLibraryInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now)
{
    if (touch.type == TOUCH_DRAG_END || touch.type == TOUCH_NONE)
        app->drag_rows = app->drag_cols = 0;
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
    LibHit hit = touch.type == TOUCH_TAP ? uiLibraryTap(touch.x, touch.y) : (LibHit){LIBHIT_NONE, 0, 0};
    if (v->tab == TAB_FILES && v->show < 0) {
        if (hit.kind == LIBHIT_TAB || hit.kind == LIBHIT_MENU)
            tapInput(app, hit, now);
        else if (!(pressed & (BUTTONS_TAB_LEFT | BUTTONS_TAB_RIGHT)))
            folderInput(app, &app->browser, false, pressed, touch, now);
        return;
    }
    buttonInput(app, pressed, now);
    if (hit.kind != LIBHIT_NONE)
        tapInput(app, hit, now);
    else if (touch.type == TOUCH_DRAG)
        dragInput(app, touch);
}

// --- Drawing ---

void appLibraryRescan(App *app, uint64_t now)
{
    if (!app->library_root[0]) {
        appShowMessage(app, "Choose the library folder first.", now);
    } else if (libscanBusy(NULL)) {
        appShowMessage(app, "Already looking for videos.", now);
    } else {
        startScan(app);
    }
}

void appLibraryDraw(App *app, uint64_t now)
{
    const char *message = now < app->message_until ? app->message : NULL;
    bool overlay = app->settings_open || app->app_menu != APP_MENU_NONE;
    const char *under = overlay ? NULL : message;  // With an overlay open, said over it.
    uiLibraryResetHits();
    uiLibrarySetWatched(&app->watched);
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
    if (libscanBusy(&found))
        snprintf(status, sizeof(status), "Looking for videos... %zu", found);
    if (app->view.tab == TAB_FILES && app->view.show < 0) {
        uiDrawBackground();
        uiDrawTabBar(TAB_FILES, status);
        uiDrawBrowser(&app->browser, &app->resume, BROWSE_FILES);
        uiDrawFooterStrip();
        uiDrawToast(under);
    } else {
        uiDrawLibrary(&app->view, &app->library, &app->home, &app->resume, app->library_root[0] != '\0', status,
                      under);
    }
    appSettingsDraw(app);
    appMenuDraw(app);
    if (overlay)
        uiDrawToast(message);
    uiDrawFooterHelp(appFooterHelp(app));
}
