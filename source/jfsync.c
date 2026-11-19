#include "jfsync.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <jansson.h>

#include "net.h"

#define FILE_PATH_MAX 600
#define API_TIMEOUT_S 30L
#define REPORT_TIMEOUT_S 10L
#define CLOSING_TIMEOUT_S 3L     // What's left to tell when the app is closing.
#define CLOSING_SECONDS 5        // It doesn't wait longer than this for all of it.
#define REPORT_TRIES 3
#define CREDENTIAL_MAX 256
#define HTTP_UNAUTHORIZED 401
#define MAX_ITEMS 100000         // Against a server that never says it's done.
#define PLAN_MAX_BYTES (4u * 1024 * 1024)
#define PLAN_BODY_MAX 4096
#define SUBTITLE_MAX_BYTES (16u * 1024 * 1024)
#define SUBTITLE_TIMEOUT_S 120L  // The server may have to read them out of the file first.
#define PAGE_MAX_BYTES (32u * 1024 * 1024)
#define SMALL_MAX_BYTES (256u * 1024)
#define POLL_SECONDS 2
#define SIGN_IN_POLLS 300       // About ten minutes.
#define REPORTS_MAX 16
#define NS_PER_TICK 100
#define IMAGE_QUALITY 85
#define IMAGE_FORMAT "Jpg"  // What stb_image reads, whatever the server would prefer.

typedef struct {
    enum { JOB_REPORT, JOB_PLAYED, JOB_END_CONVERSION } type;
    JfReportKind kind;
    char location[LIBRARY_PATH_MAX];
    int64_t position_ns;
    bool flag;  // Paused; or played.
    int tries;
    char play_session[JF_ID_MAX + 24];
    bool converted;
} Job;

static struct {
    pthread_t thread;
    bool running;
    pthread_mutex_t lock;
    pthread_cond_t wake;
    NetClient *net;
    char session_file[FILE_PATH_MAX], catalog_file[FILE_PATH_MAX], version[32];
    // Under the lock:
    JfSession session;
    JfStatus status;
    bool stop, want_sign_in, want_sign_out, want_refresh, load_library;
    time_t stop_time;
    volatile unsigned sign_in_generation;  // Changes when a sign-in is asked for or called off.
    volatile bool abort_call;              // Stops the request under way.
    Job jobs[REPORTS_MAX];
    int job_count;
    JfCatalog ready;
    bool has_ready;
    char play_session[JF_ID_MAX + 24];
    bool converted;
    bool use_password;
    char sign_in_user[CREDENTIAL_MAX], sign_in_password[CREDENTIAL_MAX];
} g;

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

static JfSession sessionCopy(void)
{
    pthread_mutex_lock(&g.lock);
    JfSession s = g.session;
    pthread_mutex_unlock(&g.lock);
    return s;
}

// --- Requests ---

// Calls the API. The answer's text, to free with netResponseFree; false if it failed.
static bool callText(const char *path, const char *post, size_t max, long timeout_s, NetResponse *r)
{
    JfSession s = sessionCopy();
    char url[JF_URL_MAX], auth[JF_AUTH_MAX];
    snprintf(url, sizeof(url), "%s%s", s.server, path);
    jfAuthHeader(&s, g.version, auth, sizeof(auth));
    NetRequest request = {.url = url, .authorization = auth, .post_json = post, .cancel = &g.abort_call,
                          .timeout_s = timeout_s};
    return netFetchText(g.net, &request, max, r);
}

static json_t *callJson(const char *path, const char *post)
{
    NetResponse r;
    if (!callText(path, post, SMALL_MAX_BYTES, API_TIMEOUT_S, &r)) {
        setError(r.status ? "The server refused that." : "The server can't be reached.");
        return NULL;
    }
    json_t *root = json_loads(r.data, 0, NULL);
    netResponseFree(&r);
    if (!root)
        setError("The server's answer can't be read.");
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
        snprintf(g.session.token, sizeof(g.session.token), "%s", token);
        snprintf(g.session.user_id, sizeof(g.session.user_id), "%s", id);
        snprintf(g.session.user_name, sizeof(g.session.user_name), "%s", textOf(user, "Name"));
        snprintf(g.status.user, sizeof(g.status.user), "%s", g.session.user_name);
        g.status.error[0] = '\0';
        JfSession saved = g.session;
        pthread_mutex_unlock(&g.lock);
        jfSessionSave(&saved, g.session_file);
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
        setError(r.status == HTTP_UNAUTHORIZED ? "Wrong user name or password."
                 : r.status ? "The server refused that." : "The server can't be reached.");
        return false;
    }
    json_t *answer = json_loads(r.data, 0, NULL);
    netResponseFree(&r);
    return acceptSession(answer);
}

static void signIn(void)
{
    pthread_mutex_lock(&g.lock);
    unsigned generation = g.sign_in_generation;
    bool with_password = g.use_password;
    g.use_password = false;
    g.abort_call = false;
    makeDeviceId(&g.session);
    g.session.token[0] = '\0';
    pthread_mutex_unlock(&g.lock);
    bool ok;
    if (with_password) {
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
    NetResponse r;
    if (callText("/Sessions/Logout", "{}", SMALL_MAX_BYTES, REPORT_TIMEOUT_S, &r))
        netResponseFree(&r);
    pthread_mutex_lock(&g.lock);
    g.session.token[0] = g.session.user_id[0] = g.session.user_name[0] = '\0';
    g.status.user[0] = '\0';
    g.status.state = JF_SIGNED_OUT;
    JfSession saved = g.session;
    g.job_count = 0;
    pthread_mutex_unlock(&g.lock);
    jfSessionSave(&saved, g.session_file);
    remove(g.catalog_file);
}

// --- The library ---

static void publish(JfCatalog *c)
{
    pthread_mutex_lock(&g.lock);
    if (g.has_ready)
        jfCatalogFree(&g.ready);
    g.ready = *c;
    g.has_ready = true;
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

// Reads every page into c. False if one can't be had.
static bool readPages(JfCatalog *c, const char *user)
{
    int start = 0, total = 0;
    for (;;) {
        char path[512];
        snprintf(path, sizeof(path),
                 "/Items?userId=%s&Recursive=true&IncludeItemTypes=Movie,Episode&Fields=MediaSources,Overview,DateCreated"
                 "&SortBy=SortName&EnableImages=false&StartIndex=%d&Limit=%d",
                 user, start, JF_PAGE_ITEMS);
        NetResponse r;
        if (!callText(path, NULL, PAGE_MAX_BYTES, API_TIMEOUT_S, &r)) {
            setError(r.status == 401 ? "Jellyfin signed this console out." : "The library couldn't be read.");
            return false;
        }
        int count = jfCatalogAddPage(c, r.data, &total);
        netResponseFree(&r);
        if (count < 0)
            return false;
        start += count;
        setProgress(true, start, total);
        if (count < JF_PAGE_ITEMS || (total > 0 && start >= total) || start >= MAX_ITEMS)
            return true;
    }
}

static void refresh(void)
{
    JfSession s = sessionCopy();
    if (!s.token[0])
        return;
    setProgress(true, 0, 0);
    JfCatalog c;
    jfCatalogInit(&c);
    if (readPages(&c, s.user_id)) {
        setError("");
        libraryBuild(&c.library);
        jfCatalogSave(&c, g.catalog_file);
        publish(&c);
    } else {
        jfCatalogFree(&c);
    }
    setProgress(false, 0, 0);
}

// --- Telling the server ---

// False if the server couldn't be told.
static bool post(const char *path, const char *body, bool remove_it)
{
    JfSession s = sessionCopy();
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

// False if it should be tried again.
static bool runJob(const Job *job)
{
    char item[JF_ID_MAX], source[JF_ID_MAX], path[256], body[320];
    if (!jfLocationIds(job->location, item, source))
        return true;
    if (job->type == JOB_PLAYED) {
        JfSession s = sessionCopy();
        snprintf(path, sizeof(path), "/UserPlayedItems/%s?userId=%s", item, s.user_id);
        return post(path, "{}", !job->flag);
    }
    if (job->type == JOB_END_CONVERSION) {
        JfSession s = sessionCopy();
        snprintf(path, sizeof(path), "/Videos/ActiveEncodings?deviceId=%s&playSessionId=%s", s.device_id,
                 job->play_session);
        return post(path, NULL, true);
    }
    static const char *const ENDPOINTS[] = {"/Sessions/Playing", "/Sessions/Playing/Progress",
                                            "/Sessions/Playing/Stopped"};
    snprintf(body, sizeof(body),
             "{\"ItemId\":\"%s\",\"MediaSourceId\":\"%s\",\"PositionTicks\":%" PRId64
             ",\"IsPaused\":%s,\"CanSeek\":true,\"PlayMethod\":\"%s\",\"PlaySessionId\":\"%s\"}",
             item, source, job->position_ns / NS_PER_TICK, job->flag ? "true" : "false",
             job->converted ? "Transcode" : "DirectPlay", job->play_session);
    return post(ENDPOINTS[job->kind], body, false);
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

static void loadKept(void)
{
    if (!g.load_library || !sessionCopy().token[0])
        return;
    JfCatalog c;
    jfCatalogInit(&c);
    if (jfCatalogLoad(&c, g.catalog_file))
        publish(&c);
    else
        jfCatalogFree(&c);
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

bool jfSyncStart(const char *session_file, const char *catalog_file, const char *version, bool load_library)
{
    if (g.running)
        return true;
    if (!netInit())
        return false;
    pthread_mutex_init(&g.lock, NULL);
    pthread_cond_init(&g.wake, NULL);
    snprintf(g.session_file, sizeof(g.session_file), "%s", session_file);
    snprintf(g.catalog_file, sizeof(g.catalog_file), "%s", catalog_file);
    snprintf(g.version, sizeof(g.version), "%s", version);
    jfSessionLoad(&g.session, session_file);
    snprintf(g.status.server, sizeof(g.status.server), "%s", g.session.server);
    snprintf(g.status.user, sizeof(g.status.user), "%s", g.session.user_name);
    g.status.state = g.session.token[0] ? JF_SIGNED_IN : JF_SIGNED_OUT;
    g.load_library = load_library;
    g.want_refresh = load_library && g.session.token[0] != '\0';
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
    if (g.has_ready)
        jfCatalogFree(&g.ready);
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

bool jfSyncSetServer(const char *text)
{
    char server[JF_SERVER_MAX];
    if (!g.running || !jfServerNormalize(text, server, sizeof(server)))
        return false;
    pthread_mutex_lock(&g.lock);
    snprintf(g.session.server, sizeof(g.session.server), "%s", server);
    snprintf(g.status.server, sizeof(g.status.server), "%s", server);
    JfSession saved = g.session;
    pthread_mutex_unlock(&g.lock);
    jfSessionSave(&saved, g.session_file);
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

bool jfSyncTakeCatalog(JfCatalog *c)
{
    if (!g.running)
        return false;
    pthread_mutex_lock(&g.lock);
    bool any = g.has_ready;
    if (any) {
        *c = g.ready;
        g.has_ready = false;
    }
    pthread_mutex_unlock(&g.lock);
    return any;
}

static void addJob(Job *job)
{
    if (!g.running)
        return;
    pthread_mutex_lock(&g.lock);
    snprintf(job->play_session, sizeof(job->play_session), "%s", g.play_session);
    job->converted = g.converted;
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

void jfSyncSetPlayback(const char *play_session, bool converted)
{
    if (!g.running)
        return;
    pthread_mutex_lock(&g.lock);
    snprintf(g.play_session, sizeof(g.play_session), "%s", play_session);
    g.converted = converted;
    pthread_mutex_unlock(&g.lock);
}

void jfSyncEndConversion(const char *location)
{
    Job job = {.type = JOB_END_CONVERSION};
    snprintf(job.location, sizeof(job.location), "%s", location);
    addJob(&job);
}

void jfSyncSetPlayed(const char *location, bool played)
{
    Job job = {.type = JOB_PLAYED, .flag = played};
    snprintf(job.location, sizeof(job.location), "%s", location);
    addJob(&job);
}

bool jfSyncResolve(const char *location, long long max_bitrate, int audio_index, JfPlan *out, char *error,
                   size_t error_size)
{
    char item[JF_ID_MAX], source[JF_ID_MAX], url[JF_URL_MAX], auth[JF_AUTH_MAX];
    char *body = malloc(PLAN_BODY_MAX);
    NetClient *net = netClientOpen();
    bool ok = false;
    snprintf(error, error_size, "The server can't be reached.");
    if (g.running && body && net && jfLocationIds(location, item, source)) {
        JfSession s = sessionCopy();
        snprintf(url, sizeof(url), "%s/Items/%s/PlaybackInfo?userId=%s&mediaSourceId=%s", s.server, item, s.user_id, source);
        jfAuthHeader(&s, g.version, auth, sizeof(auth));
        jfPlaybackBody(body, PLAN_BODY_MAX, s.user_id, max_bitrate, audio_index);
        NetRequest request = {.url = url, .authorization = auth, .post_json = body, .timeout_s = API_TIMEOUT_S};
        NetResponse r;
        if (netFetchText(net, &request, PLAN_MAX_BYTES, &r)) {
            ok = jfPlanParse(r.data, out);
            netResponseFree(&r);
            if (!ok)
                snprintf(error, error_size, "The server can't play this video here.");
        }
    }
    netClientClose(net);
    free(body);
    return ok;
}

void jfSyncConvertedRequest(const char *url, char *full, size_t full_size, char *authorization, size_t auth_size)
{
    JfSession s = sessionCopy();
    snprintf(full, full_size, "%s%s", s.server, url);
    jfAuthHeader(&s, g.version, authorization, auth_size);
}

bool jfSyncFetchSubtitle(const char *location, int stream_index, const volatile bool *cancel, NetResponse *out)
{
    char item[JF_ID_MAX], source[JF_ID_MAX], url[JF_URL_MAX], auth[JF_AUTH_MAX];
    memset(out, 0, sizeof(*out));
    NetClient *net = g.running && jfLocationIds(location, item, source) ? netClientOpen() : NULL;
    if (!net)
        return false;
    JfSession s = sessionCopy();
    snprintf(url, sizeof(url), "%s/Videos/%s/%s/Subtitles/%d/0/Stream.srt", s.server, item, source, stream_index);
    jfAuthHeader(&s, g.version, auth, sizeof(auth));
    NetRequest request = {.url = url, .authorization = auth, .cancel = cancel, .timeout_s = SUBTITLE_TIMEOUT_S};
    bool ok = netFetchText(net, &request, SUBTITLE_MAX_BYTES, out);
    netClientClose(net);
    return ok;
}

bool jfSyncStreamRequest(const char *location, char *url, size_t url_size, char *authorization, size_t auth_size)
{
    char item[JF_ID_MAX], source[JF_ID_MAX];
    if (!g.running || !jfLocationIds(location, item, source))
        return false;
    JfSession s = sessionCopy();
    snprintf(url, url_size, "%s/Videos/%s/stream?static=true&mediaSourceId=%s", s.server, item, source);
    jfAuthHeader(&s, g.version, authorization, auth_size);
    return s.token[0] != '\0';
}

bool jfSyncImageRequest(const char *location, const char *kind, int max_width, char *url, size_t url_size,
                        char *authorization, size_t auth_size)
{
    char item[JF_ID_MAX], source[JF_ID_MAX];
    if (!g.running || !jfLocationIds(location, item, source))
        return false;
    JfSession s = sessionCopy();
    snprintf(url, url_size, "%s/Items/%s/Images/%s?maxWidth=%d&quality=%d&format=" IMAGE_FORMAT, s.server, item, kind,
             max_width, IMAGE_QUALITY);
    jfAuthHeader(&s, g.version, authorization, auth_size);
    return s.token[0] != '\0';
}
