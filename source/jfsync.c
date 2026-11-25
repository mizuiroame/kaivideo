#include "jfsync.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <jansson.h>

#include "certtrust.h"
#include "jfsync_internal.h"
#include "lang.h"
#include "livetv.h"
#include "net.h"
#include "plexsync.h"

#define FILE_PATH_MAX 600
#define API_TIMEOUT_S 30L
#define REPORT_TIMEOUT_S 10L
#define CLOSING_TIMEOUT_S 3L     // What's left to tell when the app is closing.
#define CLOSING_SECONDS 5        // It doesn't wait longer than this for all of it.
#define REPORT_TRIES 3
#define CREDENTIAL_MAX 256
#define HTTP_UNAUTHORIZED 401
#define MAX_ITEMS 100000         // Against a server that never says it's done.
#define PAGE_MAX_BYTES (32u * 1024 * 1024)
#define SMALL_MAX_BYTES (256u * 1024)
#define POLL_SECONDS 2
#define SIGN_IN_POLLS 300       // About ten minutes.
#define REPORTS_MAX 16

typedef struct {
    enum { JOB_REPORT, JOB_PLAYED, JOB_END_CONVERSION, JOB_FAVORITE, JOB_CLOSE_LIVE } type;
    JfReportKind kind;
    char location[LIBRARY_PATH_MAX];
    int64_t position_ns;
    bool flag;  // Paused; or played; or a favorite.
    int tries;
    char play_session[JF_ID_MAX + 24];
    char live_stream[JF_LIVE_STREAM_MAX];  // JOB_CLOSE_LIVE's.
    bool converted;
    bool own_session;  // play_session is the job's own (a sound track converted alone), not the video's.
} Job;

static struct {
    pthread_t thread;
    bool running;
    pthread_mutex_t lock;
    pthread_cond_t wake;
    NetClient *net;
    char session_file[FILE_PATH_MAX], catalog_file[FILE_PATH_MAX], version[32];
    char plex_file[FILE_PATH_MAX], plex_catalog_file[FILE_PATH_MAX], plex_favorites_file[FILE_PATH_MAX];
    char emby_file[FILE_PATH_MAX], emby_catalog_file[FILE_PATH_MAX];
    // Under the lock:
    JfSession sessions[2];     // Jellyfin's, then Emby's (FAMILY).
    JfStatus status;
    bool stop, want_sign_in, want_sign_out, want_refresh, load_library;
    time_t stop_time;
    volatile unsigned sign_in_generation;  // Changes when a sign-in is asked for or called off.
    volatile bool abort_call;              // Stops the request under way.
    Job jobs[REPORTS_MAX];
    int job_count;
    JfCatalog ready[SERVER_KIND_COUNT];  // Each server's library, read and not yet taken.
    bool has_ready[SERVER_KIND_COUNT];
    char play_session[JF_ID_MAX + 24];
    char live_stream[JF_LIVE_STREAM_MAX];  // The playing channel's stream the server opened, or "".
    bool converted;
    bool use_password;
    char sign_in_user[CREDENTIAL_MAX], sign_in_password[CREDENTIAL_MAX];
    ServerKind kind;           // The one Settings shows: signing in and out are for it.
    PlexSession plex_session;
    char plex_item[PLEX_ID_MAX];  // The video last asked about, and its length: Plex's reports want it.
    int64_t plex_duration_ns;
} g;

static void wakeWith(bool *flag);
static void publishKept(void);

static void setError(const char *message)
{
    pthread_mutex_lock(&g.lock);
    snprintf(g.status.error, sizeof(g.status.error), "%s", message);
    pthread_mutex_unlock(&g.lock);
}

static void setState(JfSignState state, const char *code)
{
    pthread_mutex_lock(&g.lock);
    g.status.state = state;
    snprintf(g.status.code, sizeof(g.status.code), "%s", code);
    pthread_mutex_unlock(&g.lock);
}

#define FAMILY(kind) ((kind) == SERVER_EMBY ? 1 : 0)  // Its index in sessions.
#define CURRENT (g.sessions[FAMILY(g.kind)])            // The Jellyfin or Emby session Settings shows.

static JfSession sessionCopy(void)
{
    pthread_mutex_lock(&g.lock);
    JfSession s = CURRENT;
    pthread_mutex_unlock(&g.lock);
    return s;
}

// The library kept for a kind of server.
static const char *catalogFileFor(ServerKind kind)
{
    return kind == SERVER_PLEX ? g.plex_catalog_file : kind == SERVER_EMBY ? g.emby_catalog_file : g.catalog_file;
}

static const char *catalogFile(void)
{
    return catalogFileFor(g.kind);
}

// Where the Jellyfin or Emby session Settings shows is kept.
static const char *sessionFile(void)
{
    return g.kind == SERVER_EMBY ? g.emby_file : g.session_file;
}

static bool signedInLocked(ServerKind kind)
{
    if (kind == SERVER_PLEX)
        return g.plex_session.token[0] && g.plex_session.server_uri[0];
    return g.sessions[FAMILY(kind)].token[0] != '\0';
}

static bool signedIn(ServerKind kind)
{
    pthread_mutex_lock(&g.lock);
    bool in = signedInLocked(kind);
    pthread_mutex_unlock(&g.lock);
    return in;
}

static PlexSession plexCopy(void)
{
    pthread_mutex_lock(&g.lock);
    PlexSession s = g.plex_session;
    pthread_mutex_unlock(&g.lock);
    return s;
}

static bool usingPlex(void)
{
    pthread_mutex_lock(&g.lock);
    bool plex = g.kind == SERVER_PLEX;
    pthread_mutex_unlock(&g.lock);
    return plex;
}

// What the status says of who's signed in where.
static void showSession(void)
{
    if (g.kind == SERVER_PLEX) {
        snprintf(g.status.server, sizeof(g.status.server), "%s", g.plex_session.server_name);
        snprintf(g.status.user, sizeof(g.status.user), "%s", g.plex_session.user_name);
        g.status.state = g.plex_session.token[0] && g.plex_session.server_uri[0] ? JF_SIGNED_IN : JF_SIGNED_OUT;
    } else {
        snprintf(g.status.server, sizeof(g.status.server), "%s", CURRENT.server);
        snprintf(g.status.user, sizeof(g.status.user), "%s", CURRENT.user_name);
        g.status.state = CURRENT.token[0] ? JF_SIGNED_IN : JF_SIGNED_OUT;
    }
}

// --- Requests ---

// Calls a session's server. The answer's text, to free with netResponseFree; false if it failed.
static bool callAs(const JfSession *s, const char *path, const char *post, size_t max, long timeout_s, NetResponse *r)
{
    char url[JF_URL_MAX], auth[JF_AUTH_MAX];
    snprintf(url, sizeof(url), "%s%s", s->server, path);
    jfAuthHeader(s, g.version, auth, sizeof(auth));
    NetRequest request = {.url = url, .authorization = auth, .post_json = post, .cancel = &g.abort_call,
                          .timeout_s = timeout_s};
    return netFetchText(g.net, &request, max, r);
}

// The same with the session Settings shows.
static bool callText(const char *path, const char *post, size_t max, long timeout_s, NetResponse *r)
{
    JfSession s = sessionCopy();
    return callAs(&s, path, post, max, timeout_s, r);
}

static json_t *callJson(const char *path, const char *post)
{
    NetResponse r;
    if (!callText(path, post, SMALL_MAX_BYTES, API_TIMEOUT_S, &r)) {
        setError(r.status ? T("The server refused that.") : T("The server can't be reached."));
        return NULL;
    }
    json_t *root = json_loads(r.data, 0, NULL);
    netResponseFree(&r);
    if (!root)
        setError(T("The server's answer can't be read."));
    return root;
}

static const char *textOf(const json_t *object, const char *key)
{
    const char *value = json_string_value(json_object_get(object, key));
    return value ? value : "";
}

// --- Signing in ---

static void makeDeviceId(JfSession *s)
{
    if (s->device_id[0])
        return;
    unsigned seed = (unsigned)time(NULL) ^ (unsigned)(uintptr_t)s;
    snprintf(s->device_id, sizeof(s->device_id), "kaivideo-%08x%08x", rand_r(&seed), rand_r(&seed));
}

static bool waitApproved(const char *secret, unsigned generation)
{
    char path[256];
    snprintf(path, sizeof(path), "/QuickConnect/Connect?secret=%s", secret);
    for (int polls = 0; polls < SIGN_IN_POLLS && g.sign_in_generation == generation && !g.stop; polls++) {
        struct timespec until;
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_sec += POLL_SECONDS;
        pthread_mutex_lock(&g.lock);
        pthread_cond_timedwait(&g.wake, &g.lock, &until);
        pthread_mutex_unlock(&g.lock);
        if (g.sign_in_generation != generation || g.stop)
            return false;
        json_t *state = callJson(path, NULL);
        bool approved = state && json_is_true(json_object_get(state, "Authenticated"));
        json_decref(state);
        if (approved)
            return true;
    }
    return false;
}

// Keeps the session the server gave. False if the answer doesn't hold one.
static bool acceptSession(json_t *answer)
{
    const json_t *user = json_object_get(answer, "User");
    const char *token = textOf(answer, "AccessToken"), *id = textOf(user, "Id");
    bool ok = token[0] && id[0] && strlen(token) < JF_TOKEN_MAX && strlen(id) < JF_ID_MAX;
    if (ok) {
        pthread_mutex_lock(&g.lock);
        snprintf(CURRENT.token, sizeof(CURRENT.token), "%s", token);
        snprintf(CURRENT.user_id, sizeof(CURRENT.user_id), "%s", id);
        snprintf(CURRENT.user_name, sizeof(CURRENT.user_name), "%s", textOf(user, "Name"));
        snprintf(g.status.user, sizeof(g.status.user), "%s", CURRENT.user_name);
        g.status.error[0] = '\0';
        JfSession saved = CURRENT;
        pthread_mutex_unlock(&g.lock);
        jfSessionSave(&saved, sessionFile());
    }
    json_decref(answer);
    return ok;
}

static bool finishSignIn(const char *secret)
{
    char body[256];
    snprintf(body, sizeof(body), "{\"Secret\":\"%s\"}", secret);
    return acceptSession(callJson("/Users/AuthenticateWithQuickConnect", body));
}

static bool signInWithPassword(void)
{
    char user[CREDENTIAL_MAX], password[CREDENTIAL_MAX];
    pthread_mutex_lock(&g.lock);
    memcpy(user, g.sign_in_user, sizeof(user));
    memcpy(password, g.sign_in_password, sizeof(password));
    memset(g.sign_in_password, 0, sizeof(g.sign_in_password));
    pthread_mutex_unlock(&g.lock);
    json_t *request = json_pack("{s:s, s:s}", "Username", user, "Pw", password);
    char *body = request ? json_dumps(request, JSON_COMPACT) : NULL;
    json_decref(request);
    memset(password, 0, sizeof(password));
    NetResponse r = {0};
    bool ok = body && callText("/Users/AuthenticateByName", body, SMALL_MAX_BYTES, API_TIMEOUT_S, &r);
    if (body) {
        memset(body, 0, strlen(body));
        free(body);
    }
    if (!ok) {
        setError(r.status == HTTP_UNAUTHORIZED ? T("Wrong user name or password.")
                 : r.status                    ? T("The server refused that.")
                                               : T("The server can't be reached."));
        return false;
    }
    json_t *answer = json_loads(r.data, 0, NULL);
    netResponseFree(&r);
    return acceptSession(answer);
}

static void makeClientId(PlexSession *s)
{
    if (s->client_id[0])
        return;
    unsigned seed = (unsigned)time(NULL) ^ (unsigned)(uintptr_t)s;
    snprintf(s->client_id, sizeof(s->client_id), "kaivideo-%08x%08x", rand_r(&seed), rand_r(&seed));
}

// Waits for the code to be approved at plex.tv/link, then finds the account's server.
static bool plexSignIn(unsigned generation)
{
    pthread_mutex_lock(&g.lock);
    makeClientId(&g.plex_session);
    g.plex_session.token[0] = g.plex_session.server_uri[0] = '\0';
    PlexSession s = g.plex_session;
    pthread_mutex_unlock(&g.lock);
    long long id = 0;
    char code[16], error[128] = "";
    if (!plexNetStartPin(g.net, &s, g.version, &id, code, sizeof(code), &g.abort_call)) {
        setError(T("plex.tv can't be reached."));
        return false;
    }
    if (g.sign_in_generation != generation)
        return false;
    setState(JF_SIGN_IN_WAITING, code);
    bool approved = false;
    for (int polls = 0; polls < SIGN_IN_POLLS * POLL_SECONDS && !approved && g.sign_in_generation == generation && !g.stop;
         polls++) {
        struct timespec until;
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_sec += 1;  // Each second: plex.tv answers quickly, and the code is then done with.
        pthread_mutex_lock(&g.lock);
        pthread_cond_timedwait(&g.wake, &g.lock, &until);
        pthread_mutex_unlock(&g.lock);
        approved = g.sign_in_generation == generation && plexNetPollPin(g.net, &s, g.version, id, &g.abort_call);
    }
    if (approved) {
        pthread_mutex_lock(&g.lock);
        g.status.linked = true;
        pthread_mutex_unlock(&g.lock);
    }
    if (!approved || !plexNetPickServer(g.net, &s, g.version, error, sizeof(error), &g.abort_call)) {
        setError(error);
        return false;
    }
    pthread_mutex_lock(&g.lock);
    g.plex_session = s;
    g.status.error[0] = '\0';
    showSession();
    pthread_mutex_unlock(&g.lock);
    plexSessionSave(&s, g.plex_file);
    return true;
}

static void signIn(void)
{
    pthread_mutex_lock(&g.lock);
    unsigned generation = g.sign_in_generation;
    bool with_password = g.use_password;
    g.use_password = false;
    g.abort_call = false;
    if (g.kind != SERVER_PLEX) {
        makeDeviceId(&CURRENT);
        CURRENT.token[0] = '\0';
    }
    pthread_mutex_unlock(&g.lock);
    bool ok;
    if (usingPlex()) {
        ok = plexSignIn(generation);
    } else if (with_password) {
        ok = signInWithPassword();
    } else {
        json_t *started = callJson("/QuickConnect/Initiate", "{}");
        char secret[128] = "";
        if (started && g.sign_in_generation == generation) {
            snprintf(secret, sizeof(secret), "%s", textOf(started, "Secret"));
            setState(JF_SIGN_IN_WAITING, textOf(started, "Code"));
        }
        json_decref(started);
        ok = secret[0] && waitApproved(secret, generation) && finishSignIn(secret);
    }
    pthread_mutex_lock(&g.lock);
    g.abort_call = false;
    if (g.sign_in_generation != generation && !ok) {  // Called off: nothing to report, and maybe a new one waits.
        g.status.error[0] = '\0';
        if (!g.want_sign_in)
            g.status.state = JF_SIGNED_OUT;
    } else {
        g.status.state = ok ? JF_SIGNED_IN : JF_SIGNED_OUT;
        g.status.code[0] = '\0';
        g.want_refresh = g.want_refresh || ok;
    }
    pthread_mutex_unlock(&g.lock);
}

static void signOut(void)
{
    if (usingPlex()) {  // Plex keeps this console's sign-in on the account's page; it's forgotten here.
        pthread_mutex_lock(&g.lock);
        char client[PLEX_ID_MAX];
        snprintf(client, sizeof(client), "%s", g.plex_session.client_id);
        memset(&g.plex_session, 0, sizeof(g.plex_session));
        snprintf(g.plex_session.client_id, sizeof(g.plex_session.client_id), "%s", client);
        showSession();
        PlexSession saved = g.plex_session;
        g.job_count = 0;
        pthread_mutex_unlock(&g.lock);
        plexSessionSave(&saved, g.plex_file);
        remove(catalogFile());
        publishKept();  // The library without it, at once.
        return;
    }
    NetResponse r;
    if (callText("/Sessions/Logout", "{}", SMALL_MAX_BYTES, REPORT_TIMEOUT_S, &r))
        netResponseFree(&r);
    pthread_mutex_lock(&g.lock);
    CURRENT.token[0] = CURRENT.user_id[0] = CURRENT.user_name[0] = '\0';
    g.status.user[0] = '\0';
    g.status.state = JF_SIGNED_OUT;
    JfSession saved = CURRENT;
    g.job_count = 0;
    pthread_mutex_unlock(&g.lock);
    jfSessionSave(&saved, sessionFile());
    remove(catalogFile());
    publishKept();  // The library without it, at once.
}

// --- The library ---

static void publish(ServerKind kind, JfCatalog *c)
{
    pthread_mutex_lock(&g.lock);
    if (g.has_ready[kind])
        jfCatalogFree(&g.ready[kind]);
    g.ready[kind] = *c;
    g.has_ready[kind] = true;
    pthread_mutex_unlock(&g.lock);
}

static void setProgress(bool refreshing, int loaded, int total)
{
    pthread_mutex_lock(&g.lock);
    g.status.refreshing = refreshing;
    g.status.loaded = loaded;
    g.status.total = total;
    pthread_mutex_unlock(&g.lock);
}

// Reads the pages of one of the user's libraries (parent NULL: all of them at
// once) into c; loaded counts the videos read so far. False if one can't be had.
static bool readCollection(JfCatalog *c, const JfSession *s, const char *parent, int *loaded)
{
    const char *user = s->user_id;
    int start = 0, total = 0;
    for (;;) {
        char path[640];
        snprintf(path, sizeof(path),
                 "/Items?userId=%s%s%s&Recursive=true&IncludeItemTypes=Movie,Episode"
                 "&Fields=MediaSources,Overview,DateCreated&SortBy=SortName&EnableImages=false&StartIndex=%d&Limit=%d",
                 user, parent ? "&ParentId=" : "", parent ? parent : "", start, JF_PAGE_ITEMS);
        NetResponse r;
        if (!callAs(s, path, NULL, PAGE_MAX_BYTES, API_TIMEOUT_S, &r)) {
            setError(r.status == 401 ? T("The server signed this console out.") : T("The library couldn't be read."));
            return false;
        }
        int count = jfCatalogAddPage(c, r.data, &total);
        netResponseFree(&r);
        if (count < 0)
            return false;
        start += count;
        *loaded += count;
        setProgress(true, *loaded, parent ? 0 : total);
        if (count < JF_PAGE_ITEMS || (total > 0 && start >= total) || *loaded >= MAX_ITEMS)
            return true;
    }
}

// The user's libraries, as tabs. How many; 0 or less: read everything at once.
static int readViews(JfCatalog *c, const JfSession *s, bool emby)
{
    char path[256];
    if (emby)  // Emby keeps the older address.
        snprintf(path, sizeof(path), "/Users/%s/Views", s->user_id);
    else
        snprintf(path, sizeof(path), "/UserViews?userId=%s", s->user_id);
    NetResponse r;
    if (!callAs(s, path, NULL, SMALL_MAX_BYTES, API_TIMEOUT_S, &r))
        return -1;
    int views = jfCatalogAddViews(c, r.data);
    netResponseFree(&r);
    return views;
}

// The series marked as favorites: their episodes are read as favorites. Without them, none are.
static void readFavoriteSeries(JfCatalog *c, const JfSession *s)
{
    char path[320];
    snprintf(path, sizeof(path),
             "/Items?userId=%s&Recursive=true&IncludeItemTypes=Series&Filters=IsFavorite&EnableImages=false&Limit=%d",
             s->user_id, JF_FAVORITE_SERIES_MAX);
    NetResponse r;
    if (!callAs(s, path, NULL, PAGE_MAX_BYTES, API_TIMEOUT_S, &r))
        return;
    jfCatalogSetFavoriteSeries(c, r.data);
    netResponseFree(&r);
}

// Reads every video into c, library by library. False if a page can't be had.
static bool readPages(JfCatalog *c, const JfSession *s, bool emby)
{
    readFavoriteSeries(c, s);
    int loaded = 0;
    if (readViews(c, s, emby) <= 0)
        return readCollection(c, s, NULL, &loaded);
    for (int i = 0; i < c->library.collection_count; i++) {
        c->adding_collection = i;
        if (!readCollection(c, s, c->view_ids[i], &loaded))
            return false;
    }
    c->adding_collection = -1;
    return true;
}

static void plexProgress(int loaded, int total)
{
    setProgress(true, loaded, total);
}

// One server's library, read again, or (if it can't be read) as kept from
// before. False if there's neither.
static bool readServer(ServerKind kind, JfCatalog *c)
{
    jfCatalogInit(c);
    bool ok;
    if (kind == SERVER_PLEX) {
        PlexSession ps = plexCopy();
        ok = plexNetReadLibrary(g.net, &ps, g.version, c, plexProgress, &g.abort_call);
        if (ok)
            plexFavoritesApply(c, g.plex_favorites_file);
    } else {
        JfSession s = jfSyncSessionFor(kind);
        snprintf(c->prefix, sizeof(c->prefix), "%s", kind == SERVER_EMBY ? EMBY_PREFIX : JF_PREFIX);
        ok = readPages(c, &s, kind == SERVER_EMBY);
    }
    if (ok) {
        libraryBuild(&c->library);
        jfCatalogSave(c, catalogFileFor(kind));
        return true;
    }
    char message[96];
    snprintf(message, sizeof(message), T("%s's library couldn't be read: it shows as before."), serverKindName(kind));
    setError(message);
    jfCatalogFree(c);
    jfCatalogInit(c);
    return jfCatalogLoad(c, catalogFileFor(kind));
}

// The libraries of the servers signed in, each for the app to take.
static void publishAll(JfCatalog parts[SERVER_KIND_COUNT], const bool have[SERVER_KIND_COUNT])
{
    for (int k = 0; k < SERVER_KIND_COUNT; k++)
        if (have[k])
            publish((ServerKind)k, &parts[k]);
}

static void refresh(void)
{
    JfCatalog parts[SERVER_KIND_COUNT];
    bool have[SERVER_KIND_COUNT] = {false}, any = false;
    setError("");
    setProgress(true, 0, 0);
    for (int k = 0; k < SERVER_KIND_COUNT; k++)
        if (signedIn((ServerKind)k)) {
            any = true;
            have[k] = readServer((ServerKind)k, &parts[k]);
        }
    if (any)
        publishAll(parts, have);
    setProgress(false, 0, 0);
}

// --- Telling the server ---

// False if the server (of kind) couldn't be told.
static bool post(ServerKind kind, const char *path, const char *body, bool remove_it)
{
    JfSession s = jfSyncSessionFor(kind);
    char url[JF_URL_MAX], auth[JF_AUTH_MAX];
    snprintf(url, sizeof(url), "%s%s", s.server, path);
    jfAuthHeader(&s, g.version, auth, sizeof(auth));
    NetRequest request = {.url = url, .authorization = auth, .post_json = body, .custom_method = remove_it ? "DELETE" : NULL,
                          .timeout_s = g.stop ? CLOSING_TIMEOUT_S : REPORT_TIMEOUT_S};
    NetResponse r;
    bool ok = netFetchText(g.net, &request, SMALL_MAX_BYTES, &r);
    if (ok)
        netResponseFree(&r);
    return ok || r.status != 0;  // An answer, even a refusal, won't change by asking again.
}

// Plex's side of a job. False if it should be tried again.
static bool runPlexJob(const Job *job)
{
    char item[PLEX_ID_MAX], part[PLEX_ID_MAX], path[320];
    if (job->type == JOB_FAVORITE) {  // Kept on the console: Plex has none.
        plexFavoritesSet(g.plex_favorites_file, job->location, job->flag);
        return true;
    }
    if (!plexLocationIds(job->location, item, part) || job->type == JOB_CLOSE_LIVE)
        return true;
    if (job->type == JOB_PLAYED)
        plexPlayedPath(path, sizeof(path), item, job->flag);
    else if (job->type == JOB_END_CONVERSION)
        snprintf(path, sizeof(path), "/video/:/transcode/universal/stop?session=%s", job->play_session);
    else {
        static const char *const STATES[] = {"playing", "playing", "stopped"};
        pthread_mutex_lock(&g.lock);
        int64_t duration = !strcmp(g.plex_item, item) ? g.plex_duration_ns : 0;
        pthread_mutex_unlock(&g.lock);
        plexTimelinePath(path, sizeof(path), item, job->flag && job->kind != JF_REPORT_STOP ? "paused" : STATES[job->kind],
                         job->position_ns, duration);
    }
    PlexSession s = plexCopy();
    long status = 0;
    bool ok = plexNetCall(g.net, &s, g.version, path, false, g.stop ? CLOSING_TIMEOUT_S : REPORT_TIMEOUT_S, &status);
    return ok || status != 0;
}

// False if it should be tried again.
static bool runJob(const Job *job)
{
    if (plexIsLocation(job->location))
        return runPlexJob(job);
    char item[JF_ID_MAX], source[JF_ID_MAX], path[256], body[320];
    ServerKind kind = serverKindOf(job->location);
    if (!jfLocationIds(job->location, item, source))
        return true;
    if (job->type == JOB_PLAYED || job->type == JOB_FAVORITE) {
        JfSession s = jfSyncSessionFor(kind);
        const char *what = job->type == JOB_PLAYED ? "PlayedItems" : "FavoriteItems";
        if (kind == SERVER_EMBY)  // Emby keeps the older addresses.
            snprintf(path, sizeof(path), "/Users/%s/%s/%s", s.user_id, what, item);
        else
            snprintf(path, sizeof(path), "/User%s/%s?userId=%s", what, item, s.user_id);
        return post(kind, path, "{}", !job->flag);
    }
    if (job->type == JOB_CLOSE_LIVE) {
        snprintf(path, sizeof(path), "/LiveStreams/Close?liveStreamId=%s", job->live_stream);
        return post(kind, path, "{}", false);
    }
    if (job->type == JOB_END_CONVERSION) {
        JfSession s = jfSyncSessionFor(kind);
        snprintf(path, sizeof(path), "/Videos/ActiveEncodings?deviceId=%s&playSessionId=%s", s.device_id,
                 job->play_session);
        return post(kind, path, NULL, true);
    }
    static const char *const ENDPOINTS[] = {"/Sessions/Playing", "/Sessions/Playing/Progress",
                                            "/Sessions/Playing/Stopped"};
    JfReport report = {.item = item, .source = source, .position_ns = job->position_ns, .paused = job->flag,
                       .converted = job->converted, .play_session = job->play_session,
                       .live_stream = job->live_stream};
    jfReportBody(body, sizeof(body), &report);
    return post(kind, ENDPOINTS[job->kind], body, false);
}

// A report that didn't get through goes to the back of the line, a few times.
// Progress doesn't: a newer one comes by itself.
static void retryJob(Job job)
{
    pthread_mutex_lock(&g.lock);
    bool progress = job.type == JOB_REPORT && job.kind == JF_REPORT_PROGRESS;
    if (!progress && !g.stop && ++job.tries < REPORT_TRIES && g.job_count < REPORTS_MAX)
        g.jobs[g.job_count++] = job;
    pthread_mutex_unlock(&g.lock);
}

static bool takeJob(Job *out)
{
    if (g.job_count == 0)
        return false;
    *out = g.jobs[0];
    memmove(&g.jobs[0], &g.jobs[1], (size_t)(g.job_count - 1) * sizeof(Job));
    g.job_count--;
    return true;
}

// --- The thread ---

// The libraries of the servers signed in, as kept: at start while they're
// read again, and at once when one signs out.
static void publishKept(void)
{
    JfCatalog parts[SERVER_KIND_COUNT];
    bool have[SERVER_KIND_COUNT] = {false}, any = false;
    for (int k = 0; k < SERVER_KIND_COUNT; k++) {
        if (!signedIn((ServerKind)k))
            continue;
        jfCatalogInit(&parts[k]);
        have[k] = jfCatalogLoad(&parts[k], catalogFileFor((ServerKind)k));
        if (!have[k])
            jfCatalogFree(&parts[k]);
        any = any || have[k];
    }
    if (any)
        publishAll(parts, have);
}

static void loadKept(void)
{
    if (g.load_library)
        publishKept();
}

// One thing to do, in the order that keeps the server right. False to stop.
static bool doNext(void)
{
    pthread_mutex_lock(&g.lock);
    for (;;) {
        Job job;
        bool out_of_time = g.stop && time(NULL) - g.stop_time > CLOSING_SECONDS;
        if (!out_of_time && takeJob(&job)) {
            pthread_mutex_unlock(&g.lock);
            if (!runJob(&job))
                retryJob(job);
            return true;
        }
        if (g.stop) {
            pthread_mutex_unlock(&g.lock);
            return false;
        }
        bool out = g.want_sign_out, in = g.want_sign_in, again = g.want_refresh;
        if (out || in || again) {
            g.want_sign_out = false;
            g.want_sign_in = false;
            if (!out && !in)
                g.want_refresh = false;
            pthread_mutex_unlock(&g.lock);
            if (out)
                signOut();
            else if (in)
                signIn();
            else
                refresh();
            return true;
        }
        pthread_cond_wait(&g.wake, &g.lock);
    }
}

static void *syncThread(void *arg)
{
    (void)arg;
    loadKept();
    while (doNext()) {
    }
    return NULL;
}

// --- From the window's thread ---

bool jfSyncRunning(void)
{
    return g.running;
}

bool jfSyncStart(const JfSyncFiles *files, const char *version, bool load_library, ServerKind kind)
{
    if (g.running)
        return true;
    if (!netInit())
        return false;
    pthread_mutex_init(&g.lock, NULL);
    pthread_cond_init(&g.wake, NULL);
    snprintf(g.session_file, sizeof(g.session_file), "%s", files->jellyfin);
    snprintf(g.catalog_file, sizeof(g.catalog_file), "%s", files->jellyfin_library);
    snprintf(g.emby_file, sizeof(g.emby_file), "%s", files->emby);
    snprintf(g.emby_catalog_file, sizeof(g.emby_catalog_file), "%s", files->emby_library);
    snprintf(g.version, sizeof(g.version), "%s", version);
    snprintf(g.plex_file, sizeof(g.plex_file), "%s", files->plex);
    snprintf(g.plex_catalog_file, sizeof(g.plex_catalog_file), "%s", files->plex_library);
    // Plex has no favorites: they're kept beside its library, as "<library>_favorites.txt".
    size_t stem = strlen(files->plex_library) > 4 ? strlen(files->plex_library) - 4 : 0;
    snprintf(g.plex_favorites_file, sizeof(g.plex_favorites_file), "%.*s_favorites.txt", (int)stem, files->plex_library);
    g.kind = kind >= 0 && kind < SERVER_KIND_COUNT ? kind : SERVER_JELLYFIN;
    jfSessionLoad(&g.sessions[0], g.session_file);
    jfSessionLoad(&g.sessions[1], g.emby_file);
    trustSetAddress(TRUST_JELLYFIN, g.sessions[0].server);  // Each one's Certificate check is for its server.
    trustSetAddress(TRUST_EMBY, g.sessions[1].server);
    plexSessionLoad(&g.plex_session, g.plex_file);
    showSession();
    g.load_library = load_library;
    g.want_refresh = load_library && (signedInLocked(SERVER_JELLYFIN) || signedInLocked(SERVER_EMBY) ||
                                      signedInLocked(SERVER_PLEX));
    g.net = netClientOpen();
    g.running = g.net && pthread_create(&g.thread, NULL, syncThread, NULL) == 0;
    if (!g.running) {
        netClientClose(g.net);
        netExit();
    }
    return g.running;
}

void jfSyncStop(void)
{
    if (!g.running)
        return;
    pthread_mutex_lock(&g.lock);
    g.stop = true;
    g.stop_time = time(NULL);
    g.sign_in_generation++;
    pthread_cond_broadcast(&g.wake);
    pthread_mutex_unlock(&g.lock);
    pthread_join(g.thread, NULL);
    g.running = false;
    netClientClose(g.net);
    netExit();
    for (int k = 0; k < SERVER_KIND_COUNT; k++)
        if (g.has_ready[k])
            jfCatalogFree(&g.ready[k]);
    pthread_cond_destroy(&g.wake);
    pthread_mutex_destroy(&g.lock);
    memset(&g, 0, sizeof(g));
}

void jfSyncGetStatus(JfStatus *out)
{
    if (!g.running) {
        memset(out, 0, sizeof(*out));
        return;
    }
    pthread_mutex_lock(&g.lock);
    *out = g.status;
    pthread_mutex_unlock(&g.lock);
}

static void wakeWith(bool *flag)
{
    pthread_mutex_lock(&g.lock);
    *flag = true;
    pthread_cond_broadcast(&g.wake);
    pthread_mutex_unlock(&g.lock);
}

void jfSyncUseKind(ServerKind kind)
{
    if (!g.running || kind < 0 || kind >= SERVER_KIND_COUNT)
        return;
    pthread_mutex_lock(&g.lock);
    if (g.kind != kind) {  // Only what Settings shows: every server signed in stays in the library.
        g.kind = kind;
        g.sign_in_generation++;  // A sign-in under way was for the other one.
        g.abort_call = g.status.state == JF_SIGN_IN_STARTING || g.status.state == JF_SIGN_IN_WAITING;
        g.want_sign_in = false;
        g.status.error[0] = g.status.code[0] = '\0';
        showSession();
        pthread_cond_broadcast(&g.wake);
    }
    pthread_mutex_unlock(&g.lock);
}

bool jfSyncSignedIn(ServerKind kind)
{
    return g.running && kind >= 0 && kind < SERVER_KIND_COUNT && signedIn(kind);
}

void jfSyncUserName(ServerKind kind, char *out, size_t size)
{
    out[0] = '\0';
    if (!g.running || kind < 0 || kind >= SERVER_KIND_COUNT)
        return;
    pthread_mutex_lock(&g.lock);
    if (signedInLocked(kind))
        snprintf(out, size, "%s",
                 kind == SERVER_PLEX ? g.plex_session.user_name : g.sessions[FAMILY(kind)].user_name);
    pthread_mutex_unlock(&g.lock);
}

bool jfSyncAnySignedIn(void)
{
    return jfSyncSignedIn(SERVER_JELLYFIN) || jfSyncSignedIn(SERVER_EMBY) || jfSyncSignedIn(SERVER_PLEX);
}

ServerKind jfSyncKind(void)
{
    if (!g.running)
        return SERVER_JELLYFIN;
    pthread_mutex_lock(&g.lock);
    ServerKind kind = g.kind;
    pthread_mutex_unlock(&g.lock);
    return kind;
}

bool jfSyncSetServer(const char *text)
{
    char server[JF_SERVER_MAX];
    if (!g.running || !jfServerNormalize(text, server, sizeof(server)))
        return false;
    pthread_mutex_lock(&g.lock);
    snprintf(CURRENT.server, sizeof(CURRENT.server), "%s", server);
    snprintf(g.status.server, sizeof(g.status.server), "%s", server);
    trustSetAddress(FAMILY(g.kind) ? TRUST_EMBY : TRUST_JELLYFIN, server);
    JfSession saved = CURRENT;
    pthread_mutex_unlock(&g.lock);
    jfSessionSave(&saved, sessionFile());
    return true;
}

void jfSyncSignIn(void)
{
    if (!g.running)
        return;
    pthread_mutex_lock(&g.lock);
    g.want_sign_in = true;
    g.sign_in_generation++;
    g.abort_call = true;   // One being given up frees the thread for this one.
    g.status.state = JF_SIGN_IN_STARTING;  // From now: the caller is told at once.
    g.status.code[0] = '\0';
    g.status.error[0] = '\0';
    g.status.linked = false;
    pthread_cond_broadcast(&g.wake);
    pthread_mutex_unlock(&g.lock);
}

void jfSyncSignInWithPassword(const char *user, const char *password)
{
    if (!g.running)
        return;
    pthread_mutex_lock(&g.lock);
    snprintf(g.sign_in_user, sizeof(g.sign_in_user), "%s", user);
    snprintf(g.sign_in_password, sizeof(g.sign_in_password), "%s", password);
    g.use_password = true;
    pthread_mutex_unlock(&g.lock);
    jfSyncSignIn();
}

void jfSyncCancelSignIn(void)
{
    if (!g.running)
        return;
    pthread_mutex_lock(&g.lock);
    g.sign_in_generation++;
    g.abort_call = g.status.state == JF_SIGN_IN_STARTING || g.status.state == JF_SIGN_IN_WAITING;
    if (g.want_sign_in) {  // It hadn't started.
        g.want_sign_in = false;
        g.use_password = false;
        memset(g.sign_in_password, 0, sizeof(g.sign_in_password));
        g.status.state = JF_SIGNED_OUT;
    }
    pthread_cond_broadcast(&g.wake);
    pthread_mutex_unlock(&g.lock);
}

void jfSyncSignOut(void)
{
    if (g.running)
        wakeWith(&g.want_sign_out);
}

void jfSyncRefresh(void)
{
    if (g.running)
        wakeWith(&g.want_refresh);
}

bool jfSyncTakeCatalog(ServerKind kind, JfCatalog *c)
{
    if (!g.running || kind < 0 || kind >= SERVER_KIND_COUNT)
        return false;
    pthread_mutex_lock(&g.lock);
    bool any = g.has_ready[kind];
    if (any) {
        *c = g.ready[kind];
        g.has_ready[kind] = false;
    }
    pthread_mutex_unlock(&g.lock);
    return any;
}

static void addJob(Job *job)
{
    if (!g.running)
        return;
    pthread_mutex_lock(&g.lock);
    if (!job->own_session)
        snprintf(job->play_session, sizeof(job->play_session), "%s", g.play_session);
    job->converted = g.converted;
    if (job->type == JOB_REPORT)  // A channel's stream the server opened (closing one names its own).
        snprintf(job->live_stream, sizeof(job->live_stream), "%s", g.live_stream);
    bool progress = job->type == JOB_REPORT && job->kind == JF_REPORT_PROGRESS;
    bool merged = false;
    for (int i = 0; progress && i < g.job_count && !merged; i++)  // Only the latest progress counts.
        if (g.jobs[i].type == JOB_REPORT && g.jobs[i].kind == JF_REPORT_PROGRESS) {
            g.jobs[i] = *job;
            merged = true;
        }
    if (!merged && g.job_count < REPORTS_MAX)
        g.jobs[g.job_count++] = *job;
    pthread_cond_broadcast(&g.wake);
    pthread_mutex_unlock(&g.lock);
}

void jfSyncReport(JfReportKind kind, const char *location, int64_t position_ns, bool paused)
{
    Job job = {.type = JOB_REPORT, .kind = kind, .position_ns = position_ns, .flag = paused};
    snprintf(job.location, sizeof(job.location), "%s", location);
    addJob(&job);
}

void jfSyncSetPlayback(const char *play_session, bool converted, const char *live_stream)
{
    if (!g.running)
        return;
    pthread_mutex_lock(&g.lock);
    snprintf(g.play_session, sizeof(g.play_session), "%s", play_session);
    snprintf(g.live_stream, sizeof(g.live_stream), "%s", live_stream ? live_stream : "");
    g.converted = converted;
    pthread_mutex_unlock(&g.lock);
}

void jfSyncEndConversion(const char *location)
{
    Job job = {.type = JOB_END_CONVERSION};
    snprintf(job.location, sizeof(job.location), "%s", location);
    addJob(&job);
}

void jfSyncCloseLiveStream(const char *location, const char *live_stream_id)
{
    if (!live_stream_id[0])
        return;
    Job job = {.type = JOB_CLOSE_LIVE};
    snprintf(job.location, sizeof(job.location), "%s", location);
    snprintf(job.live_stream, sizeof(job.live_stream), "%s", live_stream_id);
    addJob(&job);
}

void jfSyncSetPlayed(const char *location, bool played)
{
    Job job = {.type = JOB_PLAYED, .flag = played};
    snprintf(job.location, sizeof(job.location), "%s", location);
    addJob(&job);
}

void jfSyncSetFavorite(const char *location, bool favorite)
{
    Job job = {.type = JOB_FAVORITE, .flag = favorite};
    snprintf(job.location, sizeof(job.location), "%s", location);
    addJob(&job);
}

void jfSyncEndConversionOf(const char *location, const char *play_session)
{
    Job job = {.type = JOB_END_CONVERSION, .own_session = true};
    snprintf(job.location, sizeof(job.location), "%s", location);
    snprintf(job.play_session, sizeof(job.play_session), "%s", play_session);
    addJob(&job);
}

// --- For jfrequest.c ---

bool jfSyncIsUp(void)
{
    return g.running;
}

const char *jfSyncVersionText(void)
{
    return g.version;
}

JfSession jfSyncSessionFor(ServerKind kind)
{
    JfSession s;
    memset(&s, 0, sizeof(s));
    if (!g.running || kind == SERVER_PLEX)
        return s;
    pthread_mutex_lock(&g.lock);
    s = g.sessions[FAMILY(kind)];
    pthread_mutex_unlock(&g.lock);
    return s;
}

PlexSession jfSyncPlexSession(void)
{
    return plexCopy();
}

void jfSyncNotePlexDuration(const char *item, int64_t duration_ns)
{
    pthread_mutex_lock(&g.lock);
    snprintf(g.plex_item, sizeof(g.plex_item), "%s", item);
    g.plex_duration_ns = duration_ns;
    pthread_mutex_unlock(&g.lock);
}
