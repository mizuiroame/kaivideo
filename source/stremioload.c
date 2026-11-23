#include "stremioload.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "lang.h"
#include "net.h"

#define STALL_S 20

typedef struct {
    pthread_t thread;
    bool running;              // Started and not joined.
    volatile bool done;
    volatile bool cancel;
    // What it's asked.
    StremioAddonList urls;
    StremioAddon addons[STREMIO_ADDONS_MAX];
    int addon_count;
    char type[16];
    char id[STREMIO_ID_MAX * 2];
    // What it found.
    StremioStore store;
    StremioVideo *videos;
    StremioStream *streams;
    int count;
    StremioList list;
    bool ok;
    char note[STREMIO_NOTE_MAX];
} Job;

static Job g_jobs[STREMIO_JOBS];

static void addNote(Job *j, const char *name, const char *why)
{
    size_t n = strlen(j->note);
    snprintf(j->note + n, sizeof(j->note) - n, "%s%s: %s", n ? "; " : "", name, why);
}

// An addon's address as notes name it: its host only (the rest may hold a key).
static void hostOf(const char *url, char *out, size_t size)
{
    const char *host = strstr(url, "://");
    host = host ? host + 3 : url;
    snprintf(out, size, "%.*s", (int)strcspn(host, "/?#"), host);
}

static bool fetch(NetClient *c, Job *j, const char *url, NetResponse *r)
{
    NetRequest q;
    memset(&q, 0, sizeof(q));
    q.url = url;
    q.cancel = &j->cancel;
    q.stall_s = STALL_S;
    memset(r, 0, sizeof(*r));
    return netFetchText(c, &q, STREMIO_TEXT_MAX, r);
}

// --- Loading the addons ---

static void readCatalog(NetClient *c, Job *j, const StremioAddon *a, const StremioCatalogDef *d, int addons)
{
    StremioStore *s = &j->store;
    char url[STREMIO_URL_MAX * 2];
    NetResponse r;
    if (s->list_count >= STREMIO_LISTS_MAX || !stremioCatalogUrl(a, d, NULL, url, sizeof(url)))
        return;
    StremioList *l = &s->lists[s->list_count];
    memset(l, 0, sizeof(*l));
    if (!fetch(c, j, url, &r) || !stremioParseMetas(r.data, &l->metas, &l->count)) {
        addNote(j, a->name, r.error[0] ? r.error : T("a catalog couldn't be read"));
    } else if (l->count > 0) {
        snprintf(l->type, sizeof(l->type), "%s", d->type);
        const char *kind = !strcmp(d->type, "series") ? T("Series") : T("Films");
        if (addons > 1)
            snprintf(l->title, sizeof(l->title), "%s \xC2\xB7 %s \xC2\xB7 %s", d->name, kind, a->name);
        else
            snprintf(l->title, sizeof(l->title), "%s \xC2\xB7 %s", d->name, kind);
        s->list_count++;
    } else {
        stremioListFree(l);
    }
    netResponseFree(&r);
}

static void *loadThread(void *arg)
{
    Job *j = arg;
    NetClient *c = netClientOpen();
    StremioStore *s = &j->store;
    for (int i = 0; c && i < j->urls.count && !j->cancel; i++) {
        NetResponse r;
        StremioAddon *a = &s->addons[s->addon_count];
        char host[STREMIO_NAME_MAX];
        hostOf(j->urls.urls[i], host, sizeof(host));
        if (!fetch(c, j, j->urls.urls[i], &r))
            addNote(j, host, r.error[0] ? r.error : T("it didn't answer"));
        else if (!stremioParseManifest(r.data, j->urls.urls[i], a))
            addNote(j, host, T("that isn't an addon's manifest"));
        else
            s->addon_count++;
        netResponseFree(&r);
    }
    for (int i = 0; i < s->addon_count && !j->cancel; i++) {
        const StremioAddon *a = &s->addons[i];
        if (a->needs_setup)
            addNote(j, a->name, T("it must be set up on its own page first"));
        for (int k = 0; k < a->catalog_count && !a->needs_setup && !j->cancel; k++)
            if (!a->catalogs[k].search_only)
                readCatalog(c, j, a, &a->catalogs[k], s->addon_count);
    }
    if (!c)
        addNote(j, "KaiVideo", T("The network can't be used."));
    netClientClose(c);
    j->ok = s->addon_count > 0;
    __atomic_store_n(&j->done, true, __ATOMIC_RELEASE);
    return NULL;
}

// --- Starting, stopping and taking ---

// Stops it (if it runs), and frees what it found and wasn't taken.
static void stopJob(Job *j)
{
    if (j->running) {
        j->cancel = true;
        pthread_join(j->thread, NULL);
        j->running = false;
    }
    stremioStoreFree(&j->store);
    stremioVideosFree(j->videos, j->videos ? j->count : 0);
    stremioStreamsFree(j->streams, j->streams ? j->count : 0);
    stremioListFree(&j->list);
    j->videos = NULL;
    j->streams = NULL;
}

// Clears a job for a new request, with the addons it may use.
static Job *prepare(StremioJob kind, const StremioStore *s)
{
    Job *j = &g_jobs[kind];
    stopJob(j);
    memset(j, 0, sizeof(*j));
    if (s) {
        j->addon_count = s->addon_count;
        memcpy(j->addons, s->addons, sizeof(StremioAddon) * (size_t)s->addon_count);
    }
    return j;
}

static void start(Job *j, void *(*run)(void *))
{
    j->running = pthread_create(&j->thread, NULL, run, j) == 0;  // 512 KB stacks: threadstack.c.
    if (!j->running) {
        snprintf(j->note, sizeof(j->note), "%s", T("Out of memory"));
        j->done = true;
    }
}

// Whether a job finished (and is joined). Its results stay in it for the caller.
static bool finished(Job *j)
{
    if (!__atomic_load_n(&j->done, __ATOMIC_ACQUIRE))
        return false;
    if (j->running) {
        pthread_join(j->thread, NULL);
        j->running = false;
    }
    j->done = false;
    return true;
}

void stremioLoadStart(const StremioAddonList *l)
{
    Job *j = prepare(STREMIO_JOB_LOAD, NULL);
    j->urls = *l;
    start(j, loadThread);
}

bool stremioLoadTake(StremioStore *out, bool *ok, char *note, size_t note_size)
{
    Job *j = &g_jobs[STREMIO_JOB_LOAD];
    if (!finished(j))
        return false;
    stremioStoreFree(out);
    *out = j->store;
    memset(&j->store, 0, sizeof(j->store));
    *ok = j->ok;
    snprintf(note, note_size, "%s", j->note);
    return true;
}

// --- A series' episodes ---

static void *episodesThread(void *arg)
{
    Job *j = arg;
    NetClient *c = netClientOpen();
    for (int i = 0; c && i < j->addon_count && !j->videos && !j->cancel; i++) {
        const StremioAddon *a = &j->addons[i];
        char url[STREMIO_URL_MAX * 2];
        if (!stremioHandles(a, STREMIO_META, "series", j->id) ||
            !stremioResourceUrl(a, STREMIO_META, "series", j->id, url, sizeof(url)))
            continue;
        NetResponse r;
        if (!fetch(c, j, url, &r) || !stremioParseVideos(r.data, &j->videos, &j->count))
            addNote(j, a->name, r.error[0] ? r.error : T("the series couldn't be read"));
        else if (j->count == 0) {
            stremioVideosFree(j->videos, 0);
            j->videos = NULL;
        }
        netResponseFree(&r);
    }
    netClientClose(c);
    __atomic_store_n(&j->done, true, __ATOMIC_RELEASE);
    return NULL;
}

void stremioAskEpisodes(const StremioStore *s, const char *series_id)
{
    Job *j = prepare(STREMIO_JOB_EPISODES, s);
    snprintf(j->id, sizeof(j->id), "%s", series_id);
    start(j, episodesThread);
}

bool stremioTakeEpisodes(char *series_id, size_t id_size, StremioVideo **videos, int *count)
{
    Job *j = &g_jobs[STREMIO_JOB_EPISODES];
    if (!finished(j))
        return false;
    snprintf(series_id, id_size, "%s", j->id);
    *videos = j->videos;
    *count = j->videos ? j->count : 0;
    j->videos = NULL;
    return true;
}

// --- A video's streams ---

static void *streamsThread(void *arg)
{
    Job *j = arg;
    NetClient *c = netClientOpen();
    for (int i = 0; c && i < j->addon_count && !j->cancel; i++) {
        const StremioAddon *a = &j->addons[i];
        char url[STREMIO_URL_MAX * 2];
        if (!stremioHandles(a, STREMIO_STREAM, j->type, j->id) ||
            !stremioResourceUrl(a, STREMIO_STREAM, j->type, j->id, url, sizeof(url)))
            continue;
        NetResponse r;
        if (!fetch(c, j, url, &r) || !stremioParseStreams(r.data, a->name, &j->streams, &j->count))
            addNote(j, a->name, r.error[0] ? r.error : T("its streams couldn't be read"));
        netResponseFree(&r);
    }
    netClientClose(c);
    __atomic_store_n(&j->done, true, __ATOMIC_RELEASE);
    return NULL;
}

void stremioAskStreams(const StremioStore *s, const char *type, const char *video_id)
{
    Job *j = prepare(STREMIO_JOB_STREAMS, s);
    snprintf(j->type, sizeof(j->type), "%s", type);
    snprintf(j->id, sizeof(j->id), "%s", video_id);
    start(j, streamsThread);
}

bool stremioTakeStreams(char *video_id, size_t id_size, StremioStream **streams, int *count, char *note,
                        size_t note_size)
{
    Job *j = &g_jobs[STREMIO_JOB_STREAMS];
    if (!finished(j))
        return false;
    snprintf(video_id, id_size, "%s", j->id);
    *streams = j->streams;
    *count = j->streams ? j->count : 0;
    j->streams = NULL;
    snprintf(note, note_size, "%s", j->note);
    return true;
}

// --- A search ---

static bool alreadyFound(const StremioList *l, const char *id)
{
    for (int i = 0; i < l->count; i++)
        if (!strcmp(l->metas[i].id, id))
            return true;
    return false;
}

// Moves the metas new to found into it (and frees the rest).
static void gather(StremioList *found, StremioMeta *metas, int count)
{
    StremioMeta *grown = realloc(found->metas, (size_t)(found->count + count) * sizeof(StremioMeta));
    if (grown)
        found->metas = grown;
    for (int i = 0; i < count; i++) {
        if (grown && found->count < STREMIO_METAS_MAX && !alreadyFound(found, metas[i].id)) {
            found->metas[found->count++] = metas[i];
            memset(&metas[i], 0, sizeof(metas[i]));
        }
    }
    stremioMetasFree(metas, count);
}

static void *searchThread(void *arg)
{
    Job *j = arg;
    NetClient *c = netClientOpen();
    for (int i = 0; c && i < j->addon_count && !j->cancel; i++) {
        const StremioAddon *a = &j->addons[i];
        for (int k = 0; k < a->catalog_count && !a->needs_setup && !j->cancel; k++) {
            char url[STREMIO_URL_MAX * 2];
            if (!a->catalogs[k].search || !stremioCatalogUrl(a, &a->catalogs[k], j->id, url, sizeof(url)))
                continue;
            NetResponse r;
            StremioMeta *metas = NULL;
            int count = 0;
            if (fetch(c, j, url, &r) && stremioParseMetas(r.data, &metas, &count))
                gather(&j->list, metas, count);
            netResponseFree(&r);
        }
    }
    netClientClose(c);
    __atomic_store_n(&j->done, true, __ATOMIC_RELEASE);
    return NULL;
}

void stremioAskSearch(const StremioStore *s, const char *query)
{
    Job *j = prepare(STREMIO_JOB_SEARCH, s);
    snprintf(j->id, sizeof(j->id), "%s", query);
    start(j, searchThread);
}

bool stremioTakeSearch(StremioList *found)
{
    Job *j = &g_jobs[STREMIO_JOB_SEARCH];
    if (!finished(j))
        return false;
    stremioListFree(found);
    *found = j->list;
    memset(&j->list, 0, sizeof(j->list));
    return true;
}

bool stremioBusy(StremioJob job)
{
    return job >= 0 && job < STREMIO_JOBS && g_jobs[job].running;
}

void stremioLoadExit(void)
{
    for (int k = 0; k < STREMIO_JOBS; k++)
        stopJob(&g_jobs[k]);
}

// --- The streams chosen ---

#define CHOSEN_MAX 4

static pthread_mutex_t g_chosen_lock = PTHREAD_MUTEX_INITIALIZER;
static struct {
    char location[LIBRARY_PATH_MAX];
    char url[STREMIO_URL_MAX * 2];
} g_chosen[CHOSEN_MAX];
static int g_chosen_next;

void stremioChoose(const char *location, const char *url)
{
    pthread_mutex_lock(&g_chosen_lock);
    int at = -1;
    for (int i = 0; i < CHOSEN_MAX && at < 0; i++)
        if (!strcmp(g_chosen[i].location, location))
            at = i;
    if (at < 0) {
        at = g_chosen_next;
        g_chosen_next = (g_chosen_next + 1) % CHOSEN_MAX;
    }
    snprintf(g_chosen[at].location, sizeof(g_chosen[at].location), "%s", location);
    snprintf(g_chosen[at].url, sizeof(g_chosen[at].url), "%s", url);
    pthread_mutex_unlock(&g_chosen_lock);
}

bool stremioChosenUrl(const char *location, char *url, size_t size)
{
    pthread_mutex_lock(&g_chosen_lock);
    bool found = false;
    for (int i = 0; i < CHOSEN_MAX && !found; i++)
        if (g_chosen[i].location[0] && !strcmp(g_chosen[i].location, location)) {
            found = (size_t)snprintf(url, size, "%s", g_chosen[i].url) < size;
        }
    pthread_mutex_unlock(&g_chosen_lock);
    return found;
}

bool stremioStreamRequest(const char *location, char *url, size_t url_size, char *authorization, size_t auth_size)
{
    if (auth_size > 0)
        authorization[0] = '\0';
    return stremioChosenUrl(location, url, url_size);
}

bool stremioChosenIsHls(const char *location)
{
    char url[STREMIO_URL_MAX * 2];
    if (!stremioChosenUrl(location, url, sizeof(url)))
        return false;
    char *query = strchr(url, '?');
    if (query)
        *query = '\0';
    size_t n = strlen(url);
    return n > 5 && !strcasecmp(url + n - 5, ".m3u8");
}
