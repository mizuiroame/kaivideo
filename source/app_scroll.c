// Lists following a finger on the library's screens (and the Downloads
// panel): which list a drag moves, and gliding on after the finger lets go.
#include <stdlib.h>

#include "app.h"
#include "kinetic.h"
#include "scroll.h"
#include "ui.h"
#include "ui_library.h"
#include "ui_showcase.h"

typedef enum {
    SCROLL_NONE,
    SCROLL_HOME,
    SCROLL_HOME_ROW,
    SCROLL_GRID,
    SCROLL_EPISODES,
    SCROLL_RESULTS,
    SCROLL_FILES,
    SCROLL_LIVE,
    SCROLL_DOWNLOADS,
    SCROLL_IPTV_HOME,
    SCROLL_IPTV_ROW,
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
    case SCROLL_DOWNLOADS: return v->tab == TAB_DOWNLOADS && appDownloadsScrollPx(app, px);
    case SCROLL_IPTV_HOME: return appIptvHomeScrollPx(app, px);
    case SCROLL_IPTV_ROW: return appIptvHomeRowScrollPx(app, app->scroll_row, px);
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
    if (v->tab == TAB_DOWNLOADS && v->show < 0) {
        app->scroll_target = touch.start_y >= UI_TAB_BAR_H ? SCROLL_DOWNLOADS : SCROLL_NONE;
        return;
    }
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
        if (v->iptv && !uiShowcaseShown(v, &app->library, &app->home, true))  // The IPTV view's own Home.
            app->scroll_target = app->scroll_target == SCROLL_HOME_ROW ? SCROLL_IPTV_ROW : SCROLL_IPTV_HOME;
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
    bool across = app->scroll_target == SCROLL_HOME_ROW || app->scroll_target == SCROLL_IPTV_ROW;
    int px = across ? -touch.dx : -touch.dy;
    scrollTarget(app, px);
    if (!starting)
        kineticDrag(&app->kinetic, px, now);
}

// A finger came down, let go, or nothing: a list glides on, or stops.
void appLibraryGlide(App *app, uint64_t pressed, TouchEvent *touch, uint64_t now)
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

