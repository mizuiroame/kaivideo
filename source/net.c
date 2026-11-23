#include "net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <curl/curl.h>
#ifdef __SWITCH__
#include <switch.h>
#endif

#define CONNECT_TIMEOUT_S 10L
#define STALL_TIMEOUT_S 20L  // No data for this long: the transfer has failed.
#define STALL_BYTES_PER_S 1L
#define MAX_REDIRECTS 5L
#define HEADER_MAX 1024
#define CONTENT_RANGE "content-range:"

struct NetClient {
    CURL *curl;
};

static int g_users;  // netInit calls not yet ended; on the main thread only.

bool netInit(void)
{
    if (g_users > 0) {
        g_users++;
        return true;
    }
#ifdef __SWITCH__
    if (R_FAILED(socketInitializeDefault()))
        return false;
#endif
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
#ifdef __SWITCH__
        socketExit();
#endif
        return false;
    }
    g_users = 1;
    return true;
}

void netExit(void)
{
    if (g_users <= 0 || --g_users > 0)
        return;
    curl_global_cleanup();
#ifdef __SWITCH__
    socketExit();
#endif
}

NetClient *netClientOpen(void)
{
    NetClient *c = calloc(1, sizeof(*c));
    if (c)
        c->curl = curl_easy_init();
    if (c && !c->curl) {
        free(c);
        c = NULL;
    }
    return c;
}

void netClientClose(NetClient *c)
{
    if (!c)
        return;
    curl_easy_cleanup(c->curl);
    free(c);
}

typedef struct {
    NetSink sink;
    void *user;
    bool stopped;
} Transfer;

static uint64_t g_bytes_received;

uint64_t netBytesReceived(void)
{
    return __atomic_load_n(&g_bytes_received, __ATOMIC_RELAXED);
}

static size_t onData(char *data, size_t one, size_t count, void *user)
{
    Transfer *t = user;
    size_t n = one * count;
    __atomic_add_fetch(&g_bytes_received, n, __ATOMIC_RELAXED);
    if (t->sink(data, n, t->user))
        return n;
    t->stopped = true;
    return 0;
}

// "Content-Range: bytes 0-99/1234": the 1234.
static size_t onHeader(char *data, size_t one, size_t count, void *user)
{
    long long *total = user;
    size_t n = one * count, name = sizeof(CONTENT_RANGE) - 1;
    if (n > name && n < HEADER_MAX && !strncasecmp(data, CONTENT_RANGE, name)) {
        char line[HEADER_MAX];
        memcpy(line, data, n);
        line[n] = '\0';
        const char *slash = strchr(line, '/');
        if (slash && slash[1] != '*')
            *total = strtoll(slash + 1, NULL, 10);
    }
    return n;
}

static int onProgress(void *user, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d)
{
    (void)a, (void)b, (void)c, (void)d;
    const volatile bool *cancel = user;
    return cancel && *cancel ? 1 : 0;
}

// The request's headers; the caller frees them. NULL is fine if there are none.
static struct curl_slist *buildHeaders(const NetRequest *r)
{
    struct curl_slist *headers = NULL;
    if (r->authorization && r->authorization[0]) {
        char line[HEADER_MAX];
        snprintf(line, sizeof(line), "Authorization: %s", r->authorization);
        headers = curl_slist_append(headers, line);
    }
    if (r->post_json)
        headers = curl_slist_append(headers, "Content-Type: application/json");
    if (r->accept_json)
        headers = curl_slist_append(headers, "Accept: application/json");
    if (r->depth) {
        char line[HEADER_MAX];
        snprintf(line, sizeof(line), "Depth: %s", r->depth);
        headers = curl_slist_append(headers, line);
    }
    // Checked when they were read (streamheaders.c): printable ASCII, no line breaks.
    if (r->stream && r->stream->referer[0]) {
        char line[HEADER_MAX];
        snprintf(line, sizeof(line), "Referer: %s", r->stream->referer);
        headers = curl_slist_append(headers, line);
    }
    if (r->stream && r->stream->origin[0]) {
        char line[HEADER_MAX];
        snprintf(line, sizeof(line), "Origin: %s", r->stream->origin);
        headers = curl_slist_append(headers, line);
    }
    return headers;
}

static bool g_check_certificates = true;

void netSetCheckCertificates(bool check)
{
    __atomic_store_n(&g_check_certificates, check, __ATOMIC_RELAXED);
}

bool netChecksCertificates(void)
{
    return __atomic_load_n(&g_check_certificates, __ATOMIC_RELAXED);
}

static NetProblem problemOf(CURLcode rc, long status)
{
    switch (rc) {
    case CURLE_OK: return status >= 200 && status < 300 ? NET_OK : NET_STATUS;
    case CURLE_COULDNT_RESOLVE_HOST: return NET_NO_NAME;
    case CURLE_COULDNT_CONNECT: return NET_NO_CONNECT;
    case CURLE_OPERATION_TIMEDOUT: return NET_TIMEOUT;
    case CURLE_PEER_FAILED_VERIFICATION:
    case CURLE_SSL_CACERT_BADFILE:
    case CURLE_SSL_ISSUER_ERROR: return NET_CERTIFICATE;
    case CURLE_SSL_CONNECT_ERROR: return NET_SECURE;
    default: return NET_OTHER;
    }
}

bool netAlwaysChecked(const char *url)
{
    const char *scheme = strstr(url, "://");
    const char *host = scheme ? scheme + 3 : url;
    size_t n = strcspn(host, ":/?#");
    // Plex's sign-in, and GitHub's (updates: their answer carries the checksum the download is checked by).
    static const char *const NAMES[] = {"plex.tv", "plex.direct", "github.com", "githubusercontent.com"};
    for (size_t i = 0; i < sizeof(NAMES) / sizeof(NAMES[0]); i++) {
        size_t m = strlen(NAMES[i]);
        if (n == m && !strncasecmp(host, NAMES[i], m))
            return true;
        if (n > m && host[n - m - 1] == '.' && !strncasecmp(host + n - m, NAMES[i], m))
            return true;
    }
    return false;
}

// Only http and https may be fetched. An https address may not be redirected to plain http.
static void limitProtocols(CURL *curl, const char *url)
{
    long redirects = strncasecmp(url, "https://", 8) == 0 ? CURLPROTO_HTTPS : CURLPROTO_HTTP | CURLPROTO_HTTPS;
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, redirects);
}

static void setOptions(CURL *curl, const NetRequest *r, struct curl_slist *headers, Transfer *t, char *error)
{
    curl_easy_reset(curl);  // Keeps the connection.
    bool unchecked = r->any_certificate || !__atomic_load_n(&g_check_certificates, __ATOMIC_RELAXED);
    if (unchecked && !netAlwaysChecked(r->url)) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    } else {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    }
    curl_easy_setopt(curl, CURLOPT_URL, r->url);
    limitProtocols(curl, r->url);
    bool own_agent = r->stream && r->stream->user_agent[0];
    curl_easy_setopt(curl, CURLOPT_USERAGENT, own_agent ? r->stream->user_agent : NET_USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    // A sign-in body (password) is never re-sent to wherever a redirect points.
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, r->post_json ? 0L : 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, MAX_REDIRECTS);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, r->stall_s ? r->stall_s : CONNECT_TIMEOUT_S);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, r->timeout_s);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, STALL_BYTES_PER_S);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, r->quiet_s ? r->quiet_s : r->stall_s ? r->stall_s : STALL_TIMEOUT_S);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onData);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, t);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, onProgress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, (void *)r->cancel);
    if (r->range)
        curl_easy_setopt(curl, CURLOPT_RANGE, r->range);
    if (r->post_json)
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, r->post_json);
    if (r->custom_method)
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, r->custom_method);
    if (r->total_size) {
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, onHeader);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, r->total_size);
    }
}

static bool fetch(NetClient *c, const NetRequest *request, NetSink sink, void *user, long *status, NetProblem *problem,
                  char *error, size_t error_size)
{
    char curl_error[CURL_ERROR_SIZE] = "";
    Transfer t = {sink, user, false};
    struct curl_slist *headers = buildHeaders(request);
    setOptions(c->curl, request, headers, &t, curl_error);
    CURLcode rc = curl_easy_perform(c->curl);
    long code = 0;
    curl_easy_getinfo(c->curl, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(headers);
    if (status)
        *status = code;
    if (problem)
        *problem = t.stopped ? problemOf(CURLE_OK, code) : problemOf(rc, code);
    bool answered = code >= 200 && code < 300;
    if ((rc == CURLE_OK || t.stopped) && answered)
        return true;
    if (rc != CURLE_OK && !t.stopped)
        snprintf(error, error_size, "%s", curl_error[0] ? curl_error : curl_easy_strerror(rc));
    else
        snprintf(error, error_size, "The server answered %ld", code);
    return false;
}

bool netFetch(NetClient *c, const NetRequest *request, NetSink sink, void *user, long *status, char *error,
              size_t error_size)
{
    return fetch(c, request, sink, user, status, NULL, error, error_size);
}

typedef struct {
    NetResponse *out;
    size_t max;
    bool too_big;
} TextSink;

static bool addText(const void *data, size_t size, void *user)
{
    TextSink *s = user;
    if (s->out->size + size > s->max) {
        s->too_big = true;
        return false;
    }
    char *grown = realloc(s->out->data, s->out->size + size + 1);
    if (!grown)
        return false;
    memcpy(grown + s->out->size, data, size);
    s->out->size += size;
    grown[s->out->size] = '\0';
    s->out->data = grown;
    return true;
}

bool netFetchText(NetClient *c, const NetRequest *request, size_t max_bytes, NetResponse *out)
{
    memset(out, 0, sizeof(*out));
    TextSink sink = {out, max_bytes, false};
    bool ok = fetch(c, request, addText, &sink, &out->status, &out->problem, out->error, sizeof(out->error));
    if (ok && sink.too_big) {
        snprintf(out->error, sizeof(out->error), "The answer is too large");
        ok = false;
    }
    if (ok && !out->data) {  // An empty answer is still text.
        out->data = calloc(1, 1);
        ok = out->data != NULL;
    }
    if (!ok)
        netResponseFree(out);
    return ok;
}

void netResponseFree(NetResponse *r)
{
    free(r->data);
    r->data = NULL;
    r->size = 0;
}
