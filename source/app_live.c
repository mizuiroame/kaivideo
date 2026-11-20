// Live TV: the guide, read by a thread now and then, its tab, and playing a channel.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <switch.h>

#include "app.h"
#include "buttons.h"
#include "ui_library.h"
#include "ui_live.h"

#define SHOWN_REFRESH_NS (60ull * 1000000000ull)    // While its tab shows: what's on changes.
#define IDLE_REFRESH_NS (600ull * 1000000000ull)    // Otherwise: just whether there are channels.
#define LIVE_DRAG_H 84                               // A row of the list.

static void *readGuide(void *arg)
{
    App *app = arg;
    app->live_read_count = jfSyncFetchLive(&app->live_incoming, (int64_t)time(NULL));
    __atomic_store_n(&app->live_reading, false, __ATOMIC_RELEASE);
    return NULL;
}

static bool liveShown(const App *app)
{
    return app->view.tab == TAB_LIVE && app->view.show < 0 && !app->player;
}

// Takes what the thread read. True when it finished.
static bool takeGuide(App *app)
{
    if (!app->live_read_at || __atomic_load_n(&app->live_reading, __ATOMIC_ACQUIRE))
        return false;
    pthread_join(app->live_thread, NULL);
    if (app->live_read_count >= 0) {  // The server answered: its channels, none included.
        bool had = app->live.count > 0;
        liveGuideFree(&app->live);
        app->live = app->live_incoming;
        liveGuideInit(&app->live_incoming);
        if (had != (app->live.count > 0))
            appLibraryRefreshHome(app);  // The tab comes or goes.
        libviewLiveMove(&app->view, app->live.count, 0);
        if (app->view.live_at <= (int64_t)time(NULL))
            app->view.live_at = 0;  // The program the cursor was on has started: it's on now.
    }
    return true;
}

void appLiveUpdate(App *app, uint64_t now)
{
    static bool joined = true;
    if (!joined)
        joined = takeGuide(app);
    bool signed_in = app->config.use_jellyfin && jfSyncSignedIn(SERVER_JELLYFIN);  // Live TV is Jellyfin's.
    uint64_t every = liveShown(app) ? SHOWN_REFRESH_NS : IDLE_REFRESH_NS;
    bool due = !app->live_read_at || now - app->live_read_at >= every || app->live_read_soon;
    if (!signed_in || !joined || app->player || !due)
        return;
    app->live_read_soon = false;
    app->live_read_at = now;
    liveGuideInit(&app->live_incoming);
    app->live_reading = true;
    joined = pthread_create(&app->live_thread, NULL, readGuide, app) != 0;
    if (joined)
        app->live_reading = false;
}

void appLiveReadSoon(App *app)
{
    app->live_read_soon = true;
}

void appLiveExit(App *app)
{
    if (app->live_read_at && app->live_reading)
        pthread_join(app->live_thread, NULL);
    liveGuideFree(&app->live);
    liveGuideFree(&app->live_incoming);
}

// A channel's location, and its name in the player's bar: "One  ·  What's on".
static void describeChannel(const LiveChannel *c, char *location, size_t location_size, char *name, size_t name_size)
{
    liveLocation(location, location_size, c->id);
    snprintf(name, name_size, "%s%s%s", c->name, c->now.title[0] ? "  \xC2\xB7  " : "", c->now.title);
}

static void playChannel(App *app, int index, uint64_t now)
{
    if (index < 0 || index >= app->live.count)
        return;
    const LiveChannel *c = &app->live.channels[index];
    char location[LIBRARY_PATH_MAX], name[APP_NAME_MAX];
    describeChannel(c, location, sizeof(location), name, sizeof(name));
    app->open_from_start = true;  // A channel has no place to resume.
    if (!appStartVideo(app, location, name, now))
        app->open_from_start = false;
}

// A tap on a program in the guide: the cursor goes to it, and a tap on the
// one it's on watches its channel.
static void tapProgram(App *app, LibHit hit, uint64_t now)
{
    LibView *v = &app->view;
    if (hit.a < 0 || hit.a >= app->live.count)
        return;
    const LiveChannel *c = &app->live.channels[hit.a];
    if (hit.b < 0 || hit.b >= c->program_count)
        return;
    int64_t now_s = (int64_t)time(NULL), at = v->live_at > now_s ? v->live_at : now_s;
    if (hit.a == v->live.cursor && liveProgramAt(c, at) == hit.b) {
        playChannel(app, hit.a, now);
        return;
    }
    v->live.cursor = hit.a;
    libviewLiveMove(v, app->live.count, 0);
    v->live_at = c->programs[hit.b].start > now_s ? c->programs[hit.b].start : 0;
}

void appLiveInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now)
{
    LibView *v = &app->view;
    int dy = (pressed & BUTTONS_UP) ? -1 : (pressed & BUTTONS_DOWN) ? 1 : 0;
    int dx = (pressed & BUTTONS_LEFT) ? -1 : (pressed & BUTTONS_RIGHT) ? 1 : 0;
    if (dy)
        libviewLiveMove(v, app->live.count, dy);
    if (dx && v->live.cursor >= 0 && v->live.cursor < app->live.count)
        v->live_at = liveStep(&app->live.channels[v->live.cursor], v->live_at, (int64_t)time(NULL), dx);
    if (pressed & HidNpadButton_A)
        playChannel(app, v->live.cursor, now);
    if (touch.type == TOUCH_TAP) {
        LibHit hit = uiLibraryTap(touch.x, touch.y);
        if (hit.kind == LIBHIT_LIVE_ROW) {
            v->live.cursor = hit.a;
            playChannel(app, hit.a, now);
        } else if (hit.kind == LIBHIT_LIVE_PROGRAM) {
            tapProgram(app, hit, now);
        }
    } else if (touch.type == TOUCH_DRAG && touch.start_y >= UI_TAB_BAR_H) {
        app->drag_rows -= touch.dy;
        for (; app->drag_rows >= LIVE_DRAG_H; app->drag_rows -= LIVE_DRAG_H)
            libviewLiveMove(v, app->live.count, 1);
        for (; app->drag_rows <= -LIVE_DRAG_H; app->drag_rows += LIVE_DRAG_H)
            libviewLiveMove(v, app->live.count, -1);
    } else if (touch.type == TOUCH_DRAG_END) {
        app->drag_rows = 0;
    }
}

// How far the console's clock is from UTC, in seconds.
static int64_t utcOffset(void)
{
    time_t now = time(NULL);
    struct tm local;
    char text[32];
    if (!localtime_r(&now, &local))
        return 0;
    strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &local);
    int64_t as_utc = jfParseDate(text);
    return as_utc ? as_utc - (int64_t)now : 0;
}

void appLiveDraw(const App *app)
{
    uiDrawLiveTab(&app->view, &app->live, (int64_t)time(NULL), utcOffset(), app->config.clock_24h,
                  app->live_read_at && app->live.count == 0 && app->live_reading);
}

bool appLiveNeighbor(App *app, int delta, char *location, size_t location_size, char *name, size_t name_size)
{
    int at = -1;
    for (int i = 0; i < app->live.count && at < 0; i++) {
        char here[LIBRARY_PATH_MAX];
        liveLocation(here, sizeof(here), app->live.channels[i].id);
        if (!strcmp(here, app->playing_path))
            at = i;
    }
    if (at < 0 || app->live.count < 2)
        return false;
    int next = ((at + delta) % app->live.count + app->live.count) % app->live.count;
    describeChannel(&app->live.channels[next], location, location_size, name, name_size);
    app->view.live.cursor = next;  // Back in the list, it's on the channel watched last.
    libviewLiveMove(&app->view, app->live.count, 0);
    return true;
}

bool appPlayingLive(const App *app)
{
    return app->player && liveIsLocation(app->playing_path);
}
