#include "iptvload.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lang.h"
#include "net.h"

#define LIST_MAX_BYTES (32u << 20)
#define STALL_S 30

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_thread;
static bool g_running;
static volatile bool g_cancel;
static IptvLoadStatus g_status;
static IptvList g_list;     // Read, not taken yet.
static IptvGuide g_guide;
static XtreamCatalog g_catalog;
static bool g_ready;
static IptvAccount g_account;
static int64_t g_now;

void iptvGuideFree(IptvGuide *g)
{
    for (int i = 0; g->descs && i < g->count * XMLTV_PER_CHANNEL; i++)
        free(g->descs[i]);
    free(g->descs);
    free(g->programs);
    free(g->counts);
    memset(g, 0, sizeof(*g));
}

static void setState(IptvLoadState state)
{
    pthread_mutex_lock(&g_lock);
    g_status.state = state;
    pthread_mutex_unlock(&g_lock);
}

static bool fetchText(NetClient *c, const char *url, NetResponse *r)
{
    NetRequest q;
    memset(&q, 0, sizeof(q));
    q.url = url;
    q.cancel = &g_cancel;
    q.stall_s = STALL_S;
    q.any_certificate = g_account.any_certificate;
    return netFetchText(c, &q, LIST_MAX_BYTES, r);
}

// The channels, from the M3U or the Xtream account. False with error set.
static bool readList(NetClient *c, IptvList *list, char *error, size_t size)
{
    NetResponse r;
    memset(&r, 0, sizeof(r));
    if (g_account.kind == IPTV_M3U) {
        bool ok = fetchText(c, g_account.address, &r);
        if (!ok)
            snprintf(error, size, "%s", r.error[0] ? r.error : T("The list couldn't be read."));
        else if (!iptvParseM3u(r.data, r.size, list))
            snprintf(error, size, "%s", T("That address isn't an M3U list."));
        netResponseFree(&r);
        return ok && error[0] == '\0';
    }
    XtreamAccount x;
    iptvXtreamOf(&g_account, &x);
    char url[IPTV_URL_MAX + 256];
    XtreamAuth auth;
    bool ok = xtreamApiUrl(&x, "", NULL, url, sizeof(url)) && fetchText(c, url, &r);
    if (!ok) {
        snprintf(error, size, "%s", r.error[0] ? r.error : T("The server didn't answer."));
    } else if (!xtreamParseAuth(r.data, &auth)) {
        snprintf(error, size, "%s", T("That isn't an Xtream server, or the user or password is wrong."));
        ok = false;
    } else if (auth.refused) {
        snprintf(error, size, "%s", T("The server says the user or password is wrong."));
        ok = false;
    } else if (!auth.active) {
        snprintf(error, size, T("The account isn't active (%s)."), auth.status[0] ? auth.status : "?");
        ok = false;
    }
    netResponseFree(&r);
    if (ok && xtreamApiUrl(&x, "get_live_categories", NULL, url, sizeof(url)) && fetchText(c, url, &r))
        xtreamParseCategories(r.data, list);  // Without them, the channels come without groups.
    netResponseFree(&r);
    if (ok) {
        ok = xtreamApiUrl(&x, "get_live_streams", NULL, url, sizeof(url)) && fetchText(c, url, &r) &&
             xtreamParseLive(r.data, &x, list);
        if (!ok)
            snprintf(error, size, "%s", r.error[0] ? r.error : T("The server's channels couldn't be read."));
        netResponseFree(&r);
    }
    memset(&x, 0, sizeof(x));
    memset(url, 0, sizeof(url));
    return ok;
}

// An Xtream account's films and series. Without them (an old server, an error), the channels still come.
static void readCatalog(NetClient *c, XtreamCatalog *catalog)
{
    if (g_account.kind != IPTV_XTREAM)
        return;
    XtreamAccount x;
    iptvXtreamOf(&g_account, &x);
    char url[IPTV_URL_MAX + 256];
    NetResponse r;
    memset(&r, 0, sizeof(r));
    // The categories first, so the films and series are put in them as they're read. Without them,
    // Films and Series just show everything.
    if (!g_cancel && xtreamApiUrl(&x, "get_vod_categories", NULL, url, sizeof(url)) && fetchText(c, url, &r))
        xtreamParseVodCategories(r.data, catalog, false);
    netResponseFree(&r);
    if (!g_cancel && xtreamApiUrl(&x, "get_vod_streams", NULL, url, sizeof(url)) && fetchText(c, url, &r))
        xtreamParseFilms(r.data, catalog);
    netResponseFree(&r);
    if (!g_cancel && xtreamApiUrl(&x, "get_series_categories", NULL, url, sizeof(url)) && fetchText(c, url, &r))
        xtreamParseVodCategories(r.data, catalog, true);
    netResponseFree(&r);
    if (!g_cancel && xtreamApiUrl(&x, "get_series", NULL, url, sizeof(url)) && fetchText(c, url, &r))
        xtreamParseShows(r.data, catalog);
    netResponseFree(&r);
    memset(&x, 0, sizeof(x));
    memset(url, 0, sizeof(url));
}

static bool feedGuide(const void *data, size_t size, void *user)
{
    return !g_cancel && xmltvFeed(user, data, size);
}

// The guide's programs for the list's channels. guide_error set when it can't be read.
static void readGuide(NetClient *c, const IptvList *list, IptvGuide *guide, char *error, size_t size)
{
    char url[IPTV_URL_MAX + 256];
    iptvGuideUrl(&g_account, list->epg_url, url, sizeof(url));
    if (!url[0] || list->count == 0)
        return;
    pthread_mutex_lock(&g_lock);
    g_status.has_guide = true;
    pthread_mutex_unlock(&g_lock);
    const char **ids = malloc((size_t)list->count * sizeof(char *));
    XmltvReader *r = NULL;
    if (ids) {
        for (int i = 0; i < list->count; i++)
            ids[i] = list->channels[i].epg_id[0] ? list->channels[i].epg_id : list->channels[i].name;
        r = xmltvBegin(ids, list->count, g_now - IPTV_GUIDE_BEFORE_S, g_now + IPTV_GUIDE_AFTER_S);
        if (r)
            xmltvKeepDescriptions(r, g_now + IPTV_GUIDE_DESC_S);
    }
    guide->programs = calloc((size_t)list->count, sizeof(*guide->programs));
    guide->counts = calloc((size_t)list->count, sizeof(int));
    guide->descs = calloc((size_t)list->count * XMLTV_PER_CHANNEL, sizeof(char *));
    guide->count = list->count;  // For iptvGuideFree.
    if (!r || !guide->programs || !guide->counts || !guide->descs) {
        snprintf(error, size, "%s", T("Out of memory"));
        iptvGuideFree(guide);
        xmltvFree(r);
        free(ids);
        return;
    }
    NetRequest q;
    memset(&q, 0, sizeof(q));
    q.url = url;
    q.cancel = &g_cancel;
    q.stall_s = STALL_S;
    q.any_certificate = g_account.any_certificate;
    long status = 0;
    char net_error[NET_ERROR_MAX] = "";
    bool ok = netFetch(c, &q, feedGuide, r, &status, net_error, sizeof(net_error));
    memset(url, 0, sizeof(url));
    if (!ok)
        snprintf(error, size, "%s", net_error[0] ? net_error : T("The guide couldn't be read."));
    else if (xmltvMatched(r) == 0)
        snprintf(error, size, "%s", T("The guide has nothing for these channels."));
    for (int i = 0; i < list->count; i++) {
        int count;
        const LiveProgram *p = xmltvPrograms(r, i, &count);
        if (count > 0)
            memcpy(guide->programs[i], p, (size_t)count * sizeof(LiveProgram));
        guide->counts[i] = count;
        for (int j = 0; j < count; j++) {  // Out of memory: that one has none.
            const char *desc = xmltvDescription(r, i, j);
            guide->descs[(size_t)i * XMLTV_PER_CHANNEL + j] = desc ? strdup(desc) : NULL;
        }
    }
    pthread_mutex_lock(&g_lock);
    g_status.guide_programs = xmltvMatched(r);
    pthread_mutex_unlock(&g_lock);
    xmltvFree(r);
    free(ids);
}

static void *loadThread(void *arg)
{
    (void)arg;
    bool network = netInit();
    NetClient *c = network ? netClientOpen() : NULL;
    IptvList list;
    IptvGuide guide;
    XtreamCatalog catalog;
    memset(&list, 0, sizeof(list));
    memset(&guide, 0, sizeof(guide));
    memset(&catalog, 0, sizeof(catalog));
    char error[256] = "", guide_error[256] = "";
    bool ok = c && readList(c, &list, error, sizeof(error));
    if (!c)
        snprintf(error, sizeof(error), "%s", T("The network can't be used."));
    if (ok && !g_cancel)
        readCatalog(c, &catalog);
    if (ok && !g_cancel) {
        setState(IPTV_LOAD_GUIDE);
        readGuide(c, &list, &guide, guide_error, sizeof(guide_error));
    }
    if (c)
        netClientClose(c);
    if (network)
        netExit();
    pthread_mutex_lock(&g_lock);
    if (g_cancel) {
        iptvListFree(&list);
        iptvGuideFree(&guide);
        xtreamCatalogFree(&catalog);
    } else {
        iptvListFree(&g_list);
        iptvGuideFree(&g_guide);
        xtreamCatalogFree(&g_catalog);
        g_list = list;
        g_guide = guide;
        g_catalog = catalog;
        g_ready = ok;
        g_status.state = ok ? IPTV_LOAD_DONE : IPTV_LOAD_FAILED;
        snprintf(g_status.error, sizeof(g_status.error), "%s", error);
        snprintf(g_status.guide_error, sizeof(g_status.guide_error), "%s", guide_error);
    }
    pthread_mutex_unlock(&g_lock);
    return NULL;
}

void iptvLoadExit(void)
{
    if (!g_running)
        return;
    g_cancel = true;
    pthread_join(g_thread, NULL);
    g_running = false;
    g_cancel = false;
    memset(&g_account, 0, sizeof(g_account));
}

void iptvLoadStart(const IptvAccount *a, int64_t now_s)
{
    iptvLoadExit();
    pthread_mutex_lock(&g_lock);
    g_account = *a;
    g_now = now_s;
    unsigned serial = g_status.serial + 1;
    memset(&g_status, 0, sizeof(g_status));
    g_status.serial = serial;
    g_status.state = IPTV_LOAD_LIST;
    pthread_mutex_unlock(&g_lock);
    g_running = pthread_create(&g_thread, NULL, loadThread, NULL) == 0;  // 512 KB stacks: threadstack.c.
    if (!g_running) {
        pthread_mutex_lock(&g_lock);
        g_status.state = IPTV_LOAD_FAILED;
        snprintf(g_status.error, sizeof(g_status.error), "%s", T("Out of memory"));
        pthread_mutex_unlock(&g_lock);
    }
}

void iptvLoadStatus(IptvLoadStatus *out)
{
    pthread_mutex_lock(&g_lock);
    *out = g_status;
    pthread_mutex_unlock(&g_lock);
}

bool iptvLoadTake(IptvList *list, IptvGuide *guide, XtreamCatalog *catalog)
{
    pthread_mutex_lock(&g_lock);
    bool ready = g_ready;
    if (ready) {
        *list = g_list;
        *guide = g_guide;
        *catalog = g_catalog;
        memset(&g_catalog, 0, sizeof(g_catalog));
        memset(&g_list, 0, sizeof(g_list));
        memset(&g_guide, 0, sizeof(g_guide));
        g_ready = false;
    }
    pthread_mutex_unlock(&g_lock);
    return ready;
}
