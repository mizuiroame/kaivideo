// An Xtream account's films and series in the IPTV view's library (xtreamvod.h):
// the catalog the load brought, and a series' episodes read on a thread of
// their own when its page opens.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "buttons.h"
#include "lang.h"
#include "net.h"
#include "ui_library.h"
#include "ui_live.h"

#define EPISODES_MAX_BYTES (16u << 20)
#define STALL_S 20

static struct {
    pthread_t thread;
    bool running;          // Started, not joined yet.
    volatile bool done;    // It finished: episodes and ok are set.
    volatile bool cancel;
    long long show_id;
    char url[IPTV_URL_MAX + 256];
    bool any_certificate;
    XtreamEpisode *episodes;
    int count;
    bool ok;
} g;

static void *readEpisodes(void *arg)
{
    (void)arg;
    NetClient *c = netClientOpen();  // The network is held while a list is set (app_iptv.c).
    NetRequest q;
    memset(&q, 0, sizeof(q));
    q.url = g.url;
    q.cancel = &g.cancel;
    q.stall_s = STALL_S;
    q.any_certificate = g.any_certificate;
    NetResponse r;
    memset(&r, 0, sizeof(r));
    bool fetched = c && netFetchText(c, &q, EPISODES_MAX_BYTES, &r);
    g.ok = fetched && xtreamParseEpisodes(r.data, &g.episodes, &g.count);
    if (!g.ok)
        appLog("IPTV: series %lld's episodes couldn't be read%s%s", g.show_id, r.error[0] ? ": " : "", r.error);
    netResponseFree(&r);
    netClientClose(c);
    memset(g.url, 0, sizeof(g.url));
    __atomic_store_n(&g.done, true, __ATOMIC_RELEASE);
    return NULL;
}

static void stopReading(void)
{
    if (!g.running)
        return;
    g.cancel = true;
    pthread_join(g.thread, NULL);
    g.running = false;
    g.cancel = false;
    xtreamEpisodesFree(g.episodes, g.count);
    g.episodes = NULL;
    g.count = 0;
}

static void askEpisodes(long long show_id)
{
    char extra[48];
    snprintf(extra, sizeof(extra), "series_id=%lld", show_id);
    if (g.running || !xtreamVodApiUrl("get_series_info", extra, g.url, sizeof(g.url)))
        return;
    g.show_id = show_id;
    g.done = false;
    g.ok = false;
    g.episodes = NULL;
    g.count = 0;
    g.any_certificate = xtreamVodAnyCertificate();
    g.running = pthread_create(&g.thread, NULL, readEpisodes, NULL) == 0;
    if (!g.running)
        memset(g.url, 0, sizeof(g.url));
}

void appXtreamInstall(App *app)
{
    if (!app->config.use_iptv)
        return;
    Library built;
    libraryInit(&built, IPTV_PREFIX);
    xtreamFillLibrary(&built, &app->xtream, T("Reading the episodes..."), T("The episodes couldn't be read."));
    libraryBuild(&built);
    appLibraryInstallBuilt(app, &built);
}

// --- Categories (Y on Films or Series) ---

// The categories of Films (grid GRID_FILMS) or Series.
static const IptvGroup *groupsOf(const App *app, int grid, int *count)
{
    *count = grid == GRID_FILMS ? app->xtream.film_group_count : app->xtream.show_group_count;
    return grid == GRID_FILMS ? app->xtream.film_groups : app->xtream.show_groups;
}

// Films and Series show every film and series again.
static void showAll(App *app)
{
    for (int grid = GRID_FILMS; grid <= GRID_SERIES; grid++) {
        libviewSetGridOnly(&app->view, (LibGrid)grid, NULL, 0);
        free(app->xtream_only[grid]);
        app->xtream_only[grid] = NULL;
    }
}

void appXtreamApplyGroups(App *app)
{
    showAll(app);
    for (int grid = GRID_FILMS; grid <= GRID_SERIES; grid++) {
        int count, chosen = app->xtream_group[grid] - 1;
        groupsOf(app, grid, &count);
        if (chosen >= count)
            chosen = -1;
        app->xtream_group[grid] = chosen + 1;
        bool shows = grid == GRID_SERIES;
        size_t max = shows ? app->library.show_count : app->library.film_count;
        int *refs = chosen >= 0 ? malloc((max + 1) * sizeof(int)) : NULL;
        if (!refs)
            continue;  // All of them (or out of memory: all of them too).
        size_t n = libraryGroupRefs(&app->library, shows, chosen, refs, max);
        app->xtream_only[grid] = refs;
        libviewSetGridOnly(&app->view, (LibGrid)grid, refs, (int)n);
    }
}

// The grid shown, if it's Films or Series and has categories: else -1.
static int groupedGrid(const App *app)
{
    const LibView *v = &app->view;
    int grid = v->iptv && v->show < 0 ? libviewGrid(v) : -1, count = 0;
    if (grid == GRID_FILMS || grid == GRID_SERIES)
        groupsOf(app, grid, &count);
    return count > 0 ? grid : -1;
}

bool appXtreamHasGroups(const App *app)
{
    return app->config.use_iptv && groupedGrid(app) >= 0;
}

static const char *allLabel(int grid)
{
    return grid == GRID_FILMS ? T("All films") : T("All series");
}

void appXtreamSetGridLabels(const App *app)
{
    for (int grid = GRID_FILMS; grid <= GRID_SERIES; grid++) {
        int count;
        const IptvGroup *groups = groupsOf(app, grid, &count);
        int chosen = app->xtream_group[grid] - 1;
        const char *label = chosen >= 0 && chosen < count ? groups[chosen].name : allLabel(grid);
        uiLibrarySetGridGroup((LibGrid)grid, app->config.use_iptv && count > 0 ? label : NULL);
    }
}

void appXtreamOpenGroups(App *app)
{
    int grid = groupedGrid(app);
    if (grid < 0)
        return;
    app->xtream_groups_open = true;
    app->xtream_group_cursor = app->xtream_group[grid];
}

static void chooseGroup(App *app, int grid, int row)
{
    int count;
    groupsOf(app, grid, &count);
    if (row < 0 || row > count)
        return;
    app->xtream_group[grid] = row;  // Row 0: all of them.
    app->xtream_groups_open = false;
    app->view.grid[grid].cursor = 0;
    app->view.grid[grid].first = 0;
    app->view.grid[grid].offset = 0;
    appXtreamApplyGroups(app);
}

void appXtreamGroupsInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now)
{
    (void)now;
    int grid = groupedGrid(app), count = 0;
    if (grid < 0) {
        app->xtream_groups_open = false;
        return;
    }
    groupsOf(app, grid, &count);
    pressed = buttonsRightStickAsDpad(pressed);
    int rows = count + 1, *cursor = &app->xtream_group_cursor;
    if (pressed & BUTTONS_UP)
        *cursor = *cursor > 0 ? *cursor - 1 : 0;
    if (pressed & BUTTONS_DOWN)
        *cursor = *cursor + 1 < rows ? *cursor + 1 : rows - 1;
    if (pressed & BUTTONS_PAGE_UP)
        *cursor = *cursor > UI_GROUP_ROWS ? *cursor - UI_GROUP_ROWS : 0;
    if (pressed & BUTTONS_PAGE_DOWN)
        *cursor = *cursor + UI_GROUP_ROWS < rows ? *cursor + UI_GROUP_ROWS : rows - 1;
    if (pressed & HidNpadButton_A)
        chooseGroup(app, grid, *cursor);
    if (pressed & (HidNpadButton_B | HidNpadButton_Y))
        app->xtream_groups_open = false;
    if (touch.type == TOUCH_TAP) {
        LibHit hit = uiLibraryTap(touch.x, touch.y);
        if (hit.kind == LIBHIT_LIVE_GROUP_ROW)
            chooseGroup(app, grid, hit.a);
        else if (hit.kind == LIBHIT_CLOSE || hit.kind == LIBHIT_LIVE_GROUPS)
            app->xtream_groups_open = false;
    }
}

void appXtreamGroupsDraw(const App *app)
{
    int grid = groupedGrid(app), count;
    if (!app->xtream_groups_open || grid < 0)
        return;
    const IptvGroup *groups = groupsOf(app, grid, &count);
    int all = grid == GRID_FILMS ? app->xtream.film_count : app->xtream.show_count;
    uiDrawGroupList(T("Categories"), allLabel(grid), all, groups, count, app->xtream_group_cursor,
                    app->xtream_group[grid]);
}

// --- The catalog ---

void appXtreamTake(App *app, XtreamCatalog *c)
{
    stopReading();
    showAll(app);
    xtreamCatalogFree(&app->xtream);
    app->xtream = *c;
    memset(c, 0, sizeof(*c));
    app->xtream_group[GRID_FILMS] = app->xtream_group[GRID_SERIES] = 0;  // Another list's categories.
    app->xtream_groups_open = false;
    if (app->xtream.film_count || app->xtream.show_count)
        appLog("IPTV: %d films, %d series", app->xtream.film_count, app->xtream.show_count);
    appXtreamInstall(app);
}

// The series whose page is open, if its episodes weren't read yet: -1 if none.
static long long shownStandIn(const App *app)
{
    const LibView *v = &app->view;
    int item = -1;
    if (v->show >= 0 && (size_t)v->show < app->library.show_count && app->library.shows[v->show].count > 0)
        item = app->library.shows[v->show].episodes[0];
    else if (v->details >= 0 && (size_t)v->details < app->library.count)
        item = v->details;
    return item >= 0 ? xtreamStandInShow(app->library.items[item].path) : -1;
}

void appXtreamUpdate(App *app)
{
    if (!app->config.use_iptv)
        return;
    if (g.running && __atomic_load_n(&g.done, __ATOMIC_ACQUIRE)) {
        pthread_join(g.thread, NULL);
        g.running = false;
        xtreamShowSetEpisodes(&app->xtream, g.show_id, g.ok ? g.episodes : NULL, g.ok ? g.count : 0);
        if (!g.ok)
            xtreamEpisodesFree(g.episodes, g.count);
        g.episodes = NULL;
        g.count = 0;
        appXtreamInstall(app);
        return;
    }
    long long show = shownStandIn(app);
    const XtreamShow *s = show > 0 ? xtreamFindShow(&app->xtream, show) : NULL;
    if (s && s->state == XTREAM_EPISODES_UNREAD)
        askEpisodes(show);
}

void appXtreamExit(App *app)
{
    stopReading();
    showAll(app);
    xtreamCatalogFree(&app->xtream);
    app->xtream_group[GRID_FILMS] = app->xtream_group[GRID_SERIES] = 0;
    app->xtream_groups_open = false;
}
