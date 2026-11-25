// Live TV: the guide, read by a thread now and then, its tab, and playing a channel.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <switch.h>

#include "app.h"
#include "buttons.h"
#include "lang.h"
#include "ui_library.h"
#include "ui_live.h"

#define SHOWN_REFRESH_NS (60ull * 1000000000ull)    // While its tab shows: what's on changes.
#define IDLE_REFRESH_NS (600ull * 1000000000ull)    // Otherwise: just whether there are channels.

static int64_t g_read_from, g_read_to;  // The hours the thread reads.

static void *readGuide(void *arg)
{
    App *app = arg;
    app->live_read_count = jfSyncFetchLive(&app->live_incoming, (int64_t)time(NULL), g_read_from, g_read_to);
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
        liveGuideFree(&app->live_server);
        app->live_server = app->live_incoming;
        liveGuideInit(&app->live_incoming);
        appLibraryRefreshHome(app);  // The tab may come or go; the guide shown is made again.
        if (app->view.live_at <= (int64_t)time(NULL))
            app->view.live_at = 0;  // The program the cursor was on has started: it's on now.
    }
    return true;
}

bool appIsLiveLocation(const char *location)
{
    return liveIsLocation(location) || iptvIsLocation(location);
}

// What's on an IPTV channel now and next, and its programs not over yet (into
// programs, XMLTV_PER_CHANNEL of them at most): how many.
static int iptvPrograms(const App *app, int index, LiveChannel *c, LiveProgram *programs, int64_t now_s)
{
    const IptvGuide *g = &app->iptv_guide;
    int count = 0;
    for (int j = 0; index < g->count && j < g->counts[index] && count < XMLTV_PER_CHANNEL; j++) {
        const LiveProgram *p = &g->programs[index][j];
        if (p->end <= now_s)
            continue;
        programs[count++] = *p;
        if (p->start <= now_s)
            c->now = *p;
        else if (!c->next.title[0] && !c->next.start)
            c->next = *p;
    }
    return count;
}

void appLiveRebuild(App *app)
{
    bool jellyfin = app->config.use_jellyfin && app->config.server_view == SERVER_JELLYFIN;  // Its Live TV.
    int server = jellyfin ? app->live_server.count : 0, group = app->iptv_group;
    if (group >= app->iptv.group_count)
        group = app->iptv_group = -1;
    int iptv = 0;
    for (int i = 0; app->config.use_iptv && i < app->iptv.count; i++)  // In the IPTV view only.
        iptv += group < 0 || app->iptv.channels[i].group == group;
    LiveGuide g;
    liveGuideInit(&g);
    int total = server + iptv < LIVE_CHANNELS_MAX ? server + iptv : LIVE_CHANNELS_MAX;
    int programs = server ? app->live_server.pool_count : 0;
    if (iptv > 0)
        programs += (total - (server < total ? server : total)) * XMLTV_PER_CHANNEL;
    if (!liveGuideReserve(&g, total, programs))
        total = 0;
    for (int i = 0; i < server && g.count < total; i++) {
        const LiveChannel *c = &app->live_server.channels[i];
        liveGuideAdd(&g, c, c->programs, c->program_count);
    }
    if (server) {  // The hours the server's programs were read for.
        g.from = app->live_server.from;
        g.to = app->live_server.to;
    }
    int64_t now_s = (int64_t)time(NULL);
    LiveProgram kept[XMLTV_PER_CHANNEL];
    for (int i = 0; app->config.use_iptv && i < app->iptv.count && g.count < total; i++) {
        const IptvChannel *ic = &app->iptv.channels[i];
        if (group >= 0 && ic->group != group)
            continue;
        LiveChannel c = {0};
        iptvLocation(c.id, sizeof(c.id), i);
        snprintf(c.name, sizeof(c.name), "%.90s", ic->name);  // The guide's rows hold less.
        snprintf(c.number, sizeof(c.number), "%s", ic->number);
        c.logo = ic->logo[0] ? ic->logo : NULL;
        liveGuideAdd(&g, &c, kept, iptvPrograms(app, i, &c, kept, now_s));
    }
    if (app->dvr)  // Marks made since the guide was read stay.
        dvrMarkGuide(&g, app->dvr->timers, app->dvr->timer_count);
    liveGuideFree(&app->live);
    app->live = g;
    libviewLiveMove(&app->view, app->live.count, 0);
}

void appLiveIptvChanged(App *app)
{
    appIptvHomeRebuild(app);
    if (app->live_group_cursor > app->iptv.group_count)
        app->live_group_cursor = 0;
    appLibraryRefreshHome(app);
}

// --- The IPTV list's groups (Y on the Live tab) ---

static void chooseGroup(App *app, int row)
{
    if (row < 0 || row > app->iptv.group_count)
        return;
    app->iptv_group = row - 1;  // Row 0: all its channels.
    app->live_groups_open = false;
    app->view.live.cursor = 0;
    app->view.live.first = 0;
    appLiveRebuild(app);
}

void appLiveGroupsInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now)
{
    (void)now;
    pressed = buttonsRightStickAsDpad(pressed);
    int rows = app->iptv.group_count + 1;
    int *cursor = &app->live_group_cursor;
    if (pressed & BUTTONS_UP)
        *cursor = *cursor > 0 ? *cursor - 1 : 0;
    if (pressed & BUTTONS_DOWN)
        *cursor = *cursor + 1 < rows ? *cursor + 1 : rows - 1;
    if (pressed & BUTTONS_PAGE_UP)
        *cursor = *cursor > LIBVIEW_LIVE_ROWS ? *cursor - LIBVIEW_LIVE_ROWS : 0;
    if (pressed & BUTTONS_PAGE_DOWN)
        *cursor = *cursor + LIBVIEW_LIVE_ROWS < rows ? *cursor + LIBVIEW_LIVE_ROWS : rows - 1;
    if (pressed & HidNpadButton_A)
        chooseGroup(app, *cursor);
    if (pressed & (HidNpadButton_B | HidNpadButton_Y))
        app->live_groups_open = false;
    if (touch.type == TOUCH_TAP) {
        LibHit hit = uiLibraryTap(touch.x, touch.y);
        if (hit.kind == LIBHIT_LIVE_GROUP_ROW)
            chooseGroup(app, hit.a);
        else if (hit.kind == LIBHIT_CLOSE || hit.kind == LIBHIT_LIVE_GROUPS)
            app->live_groups_open = false;
    }
}

void appLiveOpenGroups(App *app)
{
    if (app->iptv.group_count == 0)
        return;
    app->live_groups_open = true;
    app->live_group_cursor = app->iptv_group + 1;
}

// The guide's view moved to hours it hasn't read: they're read next (once for each).
static void followView(App *app)
{
    int64_t now_s = (int64_t)time(NULL), from, to;
    int64_t view_from = liveGridStart(now_s, app->view.live_at, appUtcOffset());
    LiveGuide asked = {.from = app->live_window_from, .to = app->live_window_to};
    if (!liveWindowFor(&app->live_server, now_s, view_from, &from, &to) ||
        !liveWindowFor(&asked, now_s, view_from, &from, &to))
        return;  // Read, or being read (or asked for in vain: the next refresh asks again).
    app->live_window_from = from;
    app->live_window_to = to;
    app->live_read_soon = true;
}

void appLiveUpdate(App *app, uint64_t now)
{
    static bool joined = true;
    if (!joined)
        joined = takeGuide(app);
    appDvrUpdate(app, now);
    bool signed_in = app->config.use_jellyfin && jfSyncSignedIn(SERVER_JELLYFIN);  // Live TV is Jellyfin's.
    if (signed_in && liveShown(app))
        followView(app);
    uint64_t every = liveShown(app) ? SHOWN_REFRESH_NS : IDLE_REFRESH_NS;
    bool due = !app->live_read_at || now - app->live_read_at >= every || app->live_read_soon;
    if (!signed_in || !joined || app->player || !due)
        return;
    app->live_read_soon = false;
    app->live_read_at = now;
    int64_t now_s = (int64_t)time(NULL);
    if (app->live_window_to <= now_s) {  // Nothing asked yet, or hours gone by: from now.
        app->live_window_from = now_s;
        app->live_window_to = now_s + LIVE_WINDOW_S;
    }
    g_read_from = app->live_window_from;
    g_read_to = app->live_window_to;
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
    appDvrExit(app);
    liveGuideFree(&app->live);
    liveGuideFree(&app->live_server);
    liveGuideFree(&app->live_incoming);
}

// A channel's location, and its name in the player's bar: "One  ·  What's on".
static void describeChannel(const LiveChannel *c, char *location, size_t location_size, char *name, size_t name_size)
{
    if (iptvIsLocation(c->id))
        snprintf(location, location_size, "%s", c->id);  // An IPTV channel's id is its location.
    else
        liveLocation(location, location_size, c->id);
    snprintf(name, name_size, "%s%s%s", c->name, c->now.title[0] ? "  \xC2\xB7  " : "", c->now.title);
}

static void playLive(App *app, const LiveChannel *c, uint64_t now)
{
    char location[LIBRARY_PATH_MAX], name[APP_NAME_MAX];
    describeChannel(c, location, sizeof(location), name, sizeof(name));
    app->open_from_start = true;  // A channel has no place to resume.
    if (!appStartVideo(app, location, name, now))
        app->open_from_start = false;
}

static void playChannel(App *app, int index, uint64_t now)
{
    if (index >= 0 && index < app->live.count)
        playLive(app, &app->live.channels[index], now);
}

void appLivePlayIptv(App *app, int index, uint64_t now)
{
    if (index < 0 || index >= app->iptv.count)
        return;
    LiveChannel c = {0};
    LiveProgram kept[XMLTV_PER_CHANNEL];
    iptvLocation(c.id, sizeof(c.id), index);
    snprintf(c.name, sizeof(c.name), "%.90s", app->iptv.channels[index].name);
    iptvPrograms(app, index, &c, kept, (int64_t)time(NULL));  // What's on now names it.
    playLive(app, &c, now);
}

int appLiveIptvSelected(const App *app)
{
    int cursor = app->view.live.cursor;
    return cursor >= 0 && cursor < app->live.count ? iptvLocationIndex(app->live.channels[cursor].id) : -1;
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

#define DRAG_AXIS_PX 12  // A finger's first pixels say whether it goes along the hours or down the channels.

// A finger on the guide: down the channels as the other lists, or along the hours (from the grid).
static void dragGuide(App *app, TouchEvent touch, uint64_t now)
{
    int ax = touch.x - touch.start_x, ay = touch.y - touch.start_y;
    if (app->live_drag_axis == 0) {
        if (abs(ax) < DRAG_AXIS_PX && abs(ay) < DRAG_AXIS_PX)
            return;
        app->live_drag_axis = abs(ax) > abs(ay) && touch.start_x >= uiLiveGridX() ? 1 : 2;
        app->live_drag_rest = ax;  // What it moved before it was known counts too.
    } else {
        app->live_drag_rest += touch.dx;
    }
    if (app->live_drag_axis == 2) {
        appLibraryDrag(app, touch, now);  // The guide follows the finger, as the other lists.
        return;
    }
    // Along the hours: to the left goes later, the width of the grid two hours.
    int64_t px_per_step = uiLiveGridW() / (LIVE_GRID_SPAN_S / LIVE_GRID_STEP_S) / 6;  // Five minutes.
    int64_t steps = -app->live_drag_rest / px_per_step;
    if (steps) {
        app->live_drag_rest += (int)(steps * px_per_step);
        app->view.live_at = liveShift(app->view.live_at, (int64_t)time(NULL), steps * (LIVE_GRID_STEP_S / 6));
    }
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
        } else if (hit.kind == LIBHIT_LIVE_GROUPS) {
            appLiveOpenGroups(app);
        } else if (hit.kind == LIBHIT_FAVORITE && appLiveIptvSelected(app) >= 0) {
            appIptvToggleFavorite(app, appLiveIptvSelected(app), now);
        }
    } else if (touch.type == TOUCH_DRAG) {
        dragGuide(app, touch, now);
    } else if (touch.type == TOUCH_DRAG_END || touch.type == TOUCH_PRESS) {
        app->live_drag_axis = 0;
        app->live_drag_rest = 0;
    }
}

// How far the console's clock is from UTC, in seconds.
int64_t appUtcOffset(void)
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
    IptvLoadStatus s;
    iptvLoadStatus(&s);
    bool loading = (app->live_read_at && app->live.count == 0 && app->live_reading) ||
                   (app->live.count == 0 && (s.state == IPTV_LOAD_LIST || s.state == IPTV_LOAD_GUIDE));
    const char *group = NULL;
    if (app->config.use_iptv && app->iptv.group_count > 0)
        group = app->iptv_group >= 0 ? app->iptv.groups[app->iptv_group].name : T("All channels");
    int selected = app->config.use_iptv ? appLiveIptvSelected(app) : -1;
    int favorite = selected >= 0 ? appIptvIsFavorite(app, selected) : -1;
    if (appDvrDrawPanel(app))
        return;
    uiDrawLiveTab(&app->view, &app->live, (int64_t)time(NULL), appUtcOffset(), app->config.clock_24h, loading, group,
                  app->config.use_iptv, favorite, appDvrAvailable(app));
    appDvrDrawChoices(app);
    if (app->live_groups_open)
        uiDrawLiveGroups(&app->iptv, app->live_group_cursor, app->iptv_group + 1);
}

bool appLiveNeighbor(App *app, int delta, char *location, size_t location_size, char *name, size_t name_size)
{
    int at = -1;
    for (int i = 0; i < app->live.count && at < 0; i++) {
        char here[LIBRARY_PATH_MAX], name_here[APP_NAME_MAX];
        describeChannel(&app->live.channels[i], here, sizeof(here), name_here, sizeof(name_here));
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
    return app->player && (appIsLiveLocation(app->playing_path) || app->playing_live_stream);
}
