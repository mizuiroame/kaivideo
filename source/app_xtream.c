// An Xtream account's films and series in the IPTV view's library (xtreamvod.h):
// the catalog the load brought, and a series' episodes or a film's details
// (plot, length, genres) read on a thread of their own when its page opens.
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
#include "ui_showcase.h"

#define EPISODES_MAX_BYTES (16u << 20)
#define FILM_INFO_MAX_BYTES (1u << 20)
#define STALL_S 20
#define HERO_SETTLE_FRAMES 20  // The Showcase's cursor rests this long on one before it's asked for.

// One question at a time: a series' episodes, or a film's details.
static struct {
    pthread_t thread;
    bool running;          // Started, not joined yet.
    volatile bool done;    // It finished: what it asked for and ok are set.
    volatile bool cancel;
    bool film;             // A film's details (film_id), not a series' episodes (show_id).
    long long show_id, film_id;
    char url[IPTV_URL_MAX + 256];
    bool any_certificate;
    XtreamEpisode *episodes;
    int count;
    XtreamFilmInfo info;
    bool ok;
} g;

static void *readAnswer(void *arg)
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
    bool fetched = c && netFetchText(c, &q, g.film ? FILM_INFO_MAX_BYTES : EPISODES_MAX_BYTES, &r);
    if (g.film) {
        g.ok = fetched && xtreamParseFilmInfo(r.data, &g.info);
        if (!g.ok)
            appLog("IPTV: film %lld's details couldn't be read%s%s", g.film_id, r.error[0] ? ": " : "", r.error);
    } else {
        g.ok = fetched && xtreamParseEpisodes(r.data, &g.episodes, &g.count);
        if (!g.ok)
            appLog("IPTV: series %lld's episodes couldn't be read%s%s", g.show_id, r.error[0] ? ": " : "", r.error);
    }
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
    xtreamFilmInfoFree(&g.info);
}

// Asks for a series' episodes (film false) or a film's details, unless a question is under way.
static void ask(bool film, long long id)
{
    char extra[48];
    snprintf(extra, sizeof(extra), film ? "vod_id=%lld" : "series_id=%lld", id);
    if (g.running || !xtreamVodApiUrl(film ? "get_vod_info" : "get_series_info", extra, g.url, sizeof(g.url)))
        return;
    g.film = film;
    g.show_id = film ? -1 : id;
    g.film_id = film ? id : -1;
    g.done = false;
    g.ok = false;
    g.episodes = NULL;
    g.count = 0;
    memset(&g.info, 0, sizeof(g.info));
    g.any_certificate = xtreamVodAnyCertificate();
    g.running = pthread_create(&g.thread, NULL, readAnswer, NULL) == 0;
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
    xtreamCatalogKeepInfo(c, &app->xtream);  // The films' details read before aren't asked for again.
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

// The film whose details are open, if they weren't read yet: -1 if none.
static long long shownFilm(App *app)
{
    const LibView *v = &app->view;
    if (v->details < 0 || (size_t)v->details >= app->library.count)
        return -1;
    long long id = xtreamFilmOf(app->library.items[v->details].path);
    const XtreamFilm *f = id > 0 ? xtreamFindFilm(&app->xtream, id) : NULL;
    return f && f->info_state == XTREAM_INFO_UNREAD ? id : -1;
}

// What the question brought, into the catalog.
static void takeAnswer(App *app)
{
    pthread_join(g.thread, NULL);
    g.running = false;
    if (g.film) {
        xtreamFilmSetInfo(&app->xtream, g.film_id, g.ok ? &g.info : NULL);
        xtreamFilmInfoFree(&g.info);  // Not taken: failed, or not one of them any more.
    } else {
        xtreamShowSetEpisodes(&app->xtream, g.show_id, g.ok ? g.episodes : NULL, g.ok ? g.count : 0);
        if (!g.ok)
            xtreamEpisodesFree(g.episodes, g.count);
    }
    g.episodes = NULL;
    g.count = 0;
}

// The film or series the Showcase's large picture rests on, when its details or episodes weren't read:
// only that one is asked for, never every one in its rows.
static void askForShowcase(App *app)
{
    static char resting[LIBRARY_PATH_MAX];  // What the cursor is on, and for how many frames.
    static int frames;
    LibraryRef ref;
    if (!uiShowcaseShown(&app->view, &app->library, &app->home, true) ||
        !libviewSelected(&app->view, &app->library, &app->home, &ref)) {
        resting[0] = '\0';
        return;
    }
    if (ref.is_show && ((size_t)ref.index >= app->library.show_count || app->library.shows[ref.index].count == 0))
        return;
    if (!ref.is_show && (size_t)ref.index >= app->library.count)
        return;
    const char *path = app->library.items[ref.is_show ? app->library.shows[ref.index].episodes[0] : ref.index].path;
    if (strcmp(path, resting)) {
        snprintf(resting, sizeof(resting), "%s", path);
        frames = 0;
    }
    if (++frames < HERO_SETTLE_FRAMES)
        return;
    long long show = xtreamStandInShow(path), film = xtreamFilmOf(path);
    const XtreamShow *s = show > 0 ? xtreamFindShow(&app->xtream, show) : NULL;
    const XtreamFilm *f = film > 0 ? xtreamFindFilm(&app->xtream, film) : NULL;
    if (s && s->state == XTREAM_EPISODES_UNREAD)
        ask(false, show);
    else if (f && f->info_state == XTREAM_INFO_UNREAD)
        ask(true, film);
}

void appXtreamUpdate(App *app)
{
    if (!app->config.use_iptv)
        return;
    if (g.running && __atomic_load_n(&g.done, __ATOMIC_ACQUIRE)) {
        takeAnswer(app);
        appXtreamInstall(app);
        return;
    }
    long long show = shownStandIn(app);
    const XtreamShow *s = show > 0 ? xtreamFindShow(&app->xtream, show) : NULL;
    if (s && s->state == XTREAM_EPISODES_UNREAD)
        ask(false, show);
    long long film = shownFilm(app);
    if (film > 0)
        ask(true, film);  // Only the film opened: not every film of the list.
    askForShowcase(app);
}

void appXtreamExit(App *app)
{
    stopReading();
    showAll(app);
    xtreamCatalogFree(&app->xtream);
    app->xtream_group[GRID_FILMS] = app->xtream_group[GRID_SERIES] = 0;
    app->xtream_groups_open = false;
}
