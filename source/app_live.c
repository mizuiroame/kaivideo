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

// What's on an IPTV channel now and next, and its programs not over yet.
static void iptvPrograms(const App *app, int index, LiveChannel *c, int64_t now_s)
{
    const IptvGuide *g = &app->iptv_guide;
    if (index >= g->count)
        return;
    for (int j = 0; j < g->counts[index] && c->program_count < LIVE_PROGRAMS_PER_CHANNEL; j++) {
        const LiveProgram *p = &g->programs[index][j];
        if (p->end <= now_s)
            continue;
        c->programs[c->program_count++] = *p;
        if (p->start <= now_s)
            c->now = *p;
        else if (!c->next.title[0] && !c->next.start)
            c->next = *p;
    }
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
    g.channels = total > 0 ? calloc((size_t)total, sizeof(LiveChannel)) : NULL;
    if (total > 0 && !g.channels)
        total = 0;
    int n = 0;
    for (int i = 0; i < server && n < total; i++)
        g.channels[n++] = app->live_server.channels[i];
    int64_t now_s = (int64_t)time(NULL);
    for (int i = 0; app->config.use_iptv && i < app->iptv.count && n < total; i++) {
        const IptvChannel *ic = &app->iptv.channels[i];
        if (group >= 0 && ic->group != group)
            continue;
        LiveChannel *c = &g.channels[n++];
        iptvLocation(c->id, sizeof(c->id), i);
        snprintf(c->name, sizeof(c->name), "%.90s", ic->name);  // The guide's rows hold less.
        snprintf(c->number, sizeof(c->number), "%s", ic->number);
        c->logo = ic->logo[0] ? ic->logo : NULL;
        iptvPrograms(app, i, c, now_s);
    }
    g.count = n;
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
    LiveChannel *c = calloc(1, sizeof(*c));  // Its programs make it big for the stack.
    if (!c)
        return;
    iptvLocation(c->id, sizeof(c->id), index);
    snprintf(c->name, sizeof(c->name), "%.90s", app->iptv.channels[index].name);
    iptvPrograms(app, index, c, (int64_t)time(NULL));
    playLive(app, c, now);
    free(c);
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
        appLibraryDrag(app, touch, now);  // The guide follows the finger, as the other lists.
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
    IptvLoadStatus s;
    iptvLoadStatus(&s);
    bool loading = (app->live_read_at && app->live.count == 0 && app->live_reading) ||
                   (app->live.count == 0 && (s.state == IPTV_LOAD_LIST || s.state == IPTV_LOAD_GUIDE));
    const char *group = NULL;
    if (app->config.use_iptv && app->iptv.group_count > 0)
        group = app->iptv_group >= 0 ? app->iptv.groups[app->iptv_group].name : T("All channels");
    int selected = app->config.use_iptv ? appLiveIptvSelected(app) : -1;
    int favorite = selected >= 0 ? appIptvIsFavorite(app, selected) : -1;
    uiDrawLiveTab(&app->view, &app->live, (int64_t)time(NULL), utcOffset(), app->config.clock_24h, loading, group,
                  app->config.use_iptv, favorite);
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
