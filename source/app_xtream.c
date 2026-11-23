// An Xtream account's films and series in the IPTV view's library (xtreamvod.h):
// the catalog the load brought, and a series' episodes read on a thread of
// their own when its page opens.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "lang.h"
#include "net.h"

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
    appLibraryInstallIptv(app, &built);
}

void appXtreamTake(App *app, XtreamCatalog *c)
{
    stopReading();
    xtreamCatalogFree(&app->xtream);
    app->xtream = *c;
    memset(c, 0, sizeof(*c));
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
    xtreamCatalogFree(&app->xtream);
}
