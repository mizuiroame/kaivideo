#include "ftpnet.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include <curl/curl.h>

#include "folderlist.h"
#include "lang.h"
#include "net.h"

#define URL_MAX 2048
#define LISTING_MAX_BYTES (8u << 20)
#define CONNECT_TIMEOUT_S 8L
#define LIST_TIMEOUT_S 30L
#define STALL_S 20L           // A slow server (a NAS that looks names up) may take this long to answer.
#define FTPES "ftpes://"
#define FTP_SCHEME "ftp://"
// A jump ahead this short is read through, not a new transfer: curl closes the connection a transfer
// was cut short on, and signing in again takes seconds on some servers (pure-ftpd checks a password slowly).
#define SKIP_MAX (8 << 20)

// A video is read as one transfer (RETR from where it's read), kept open while it's read in order: a new
// one only after a jump. Each block's own transfer would wait for a new data connection every 1 MB.
struct FtpFile {
    CURL *curl;
    CURLM *multi;
    // A second handle that only asks the file's size: run beside the transfer, it leaves a signed-in
    // connection in multi's cache, which the transfer after a jump takes instead of signing in again.
    CURL *spare;
    bool spare_running;
    char url[URL_MAX];
    int64_t size;
    bool running;      // A transfer is under way (in multi).
    bool ended;        // It ended (or failed) since it was last started.
    int64_t next;      // The file's offset of stash[0]: what the transfer gives next comes after the stash.
    uint8_t *stash;    // What came past the last read's end, kept for the next one.
    size_t stash_len, stash_cap;
    uint8_t *to;       // The read under way: its buffer, how much it wants and has.
    size_t want, got;
    int64_t skip;      // Bytes of the transfer to pass over first (a short jump ahead).
    uint64_t received; // Every byte the transfer gave: it's moving.
};

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static CURL *g_list;  // Kept between listings: one sign-in for a whole library scan.
static DavAccount g_list_account;

// The curl address of a location: ftpes:// is ftp:// with TLS required (see setup).
static bool urlOf(const DavAccount *a, const char *location, bool folder, char *out, size_t size)
{
    char url[URL_MAX];
    if (!davUrlOf(a->address, location, url, sizeof(url)))
        return false;
    size_t n = strlen(url);
    if (folder && url[n - 1] != '/') {
        if (n + 1 >= sizeof(url))
            return false;
        url[n++] = '/';
        url[n] = '\0';
    }
    bool explicit_tls = !strncasecmp(url, FTPES, sizeof(FTPES) - 1);
    const char *rest = explicit_tls ? url + sizeof(FTPES) - 1 : url;
    int written = snprintf(out, size, "%s%s", explicit_tls ? FTP_SCHEME : "", rest);
    return written > 0 && (size_t)written < size;
}

// The server's replies curl passes on as headers ("Content-Length: ..." for SIZE): not wanted.
static size_t dropHeader(char *data, size_t one, size_t count, void *user)
{
    (void)data, (void)user;
    return one * count;
}

static void setup(CURL *c, const DavAccount *a, const char *url)
{
    curl_easy_reset(c);  // Keeps the connection.
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, dropHeader);
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_PROTOCOLS, (long)(CURLPROTO_FTP | CURLPROTO_FTPS));
    curl_easy_setopt(c, CURLOPT_USERNAME, a->user[0] ? a->user : "anonymous");
    curl_easy_setopt(c, CURLOPT_PASSWORD, a->user[0] ? a->password : "");
    if (!strncasecmp(a->address, FTPES, sizeof(FTPES) - 1))
        curl_easy_setopt(c, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);  // Never falls back to plain FTP.
    bool check = netChecksCertificates();
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, check ? 1L : 0L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, check ? 2L : 0L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, CONNECT_TIMEOUT_S);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, STALL_S);
    curl_easy_setopt(c, CURLOPT_FTP_FILEMETHOD, (long)CURLFTPMETHOD_SINGLECWD);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
}

// What a failure means, for the person.
static void explain(CURLcode rc, long reply, char *why, size_t size)
{
    const char *text;
    switch (rc) {
    case CURLE_COULDNT_RESOLVE_HOST:
        text = T("The name in the address couldn't be found. Check it, or use the server's IP address.");
        break;
    case CURLE_COULDNT_CONNECT:
        text = T("Nothing answered at that address and port. Is the server on, and is the port right?");
        break;
    case CURLE_OPERATION_TIMEDOUT:
        text = T("The server didn't answer in time. Is the console on the same network as the server?");
        break;
    case CURLE_LOGIN_DENIED: text = T("Wrong user name or password."); break;
    case CURLE_REMOTE_ACCESS_DENIED:
    case CURLE_REMOTE_FILE_NOT_FOUND:
        text = T("That folder isn't on the server, or this user can't see it.");
        break;
    case CURLE_USE_SSL_FAILED: text = T("The server doesn't offer a secure connection: use ftp:// instead."); break;
    case CURLE_PEER_FAILED_VERIFICATION:
    case CURLE_SSL_CACERT_BADFILE: text = T("The server's certificate isn't trusted."); break;
    case CURLE_SSL_CONNECT_ERROR: text = T("The secure connection failed."); break;
    default:
        if (reply >= 400) {
            snprintf(why, size, T("The server answered %ld."), reply);
            return;
        }
        text = T("The connection failed. Check the address and the network.");
        break;
    }
    snprintf(why, size, "%s", text);
}

typedef struct {
    char *data;
    size_t size, max;
} Text;

static size_t addText(char *data, size_t one, size_t count, void *user)
{
    Text *t = user;
    size_t n = one * count;
    if (t->size + n > t->max)
        return 0;
    char *grown = realloc(t->data, t->size + n + 1);
    if (!grown)
        return 0;
    memcpy(grown + t->size, data, n);
    t->size += n;
    grown[t->size] = '\0';
    t->data = grown;
    return n;
}

// One listing with command ("MLSD", or NULL for LIST). The answer's text in t.
static CURLcode listWith(CURL *c, const DavAccount *a, const char *url, const char *command, Text *t, long *reply)
{
    setup(c, a, url);
    if (command)
        curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, command);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, LIST_TIMEOUT_S);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, addText);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, t);
    CURLcode rc = curl_easy_perform(c);
    *reply = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, reply);
    return rc;
}

static bool sameAccount(const DavAccount *a, const DavAccount *b)
{
    return !strcmp(a->address, b->address) && !strcmp(a->user, b->user) && !strcmp(a->password, b->password);
}

static void dropList(void)
{
    if (g_list)
        curl_easy_cleanup(g_list);
    g_list = NULL;
    memset(&g_list_account, 0, sizeof(g_list_account));
}

void ftpForget(void)
{
    pthread_mutex_lock(&g_lock);
    dropList();
    pthread_mutex_unlock(&g_lock);
}

// MLSD, else LIST. False with why.
static bool listOnce(CURL *c, const DavAccount *a, const char *url, DavEntry **out, size_t *count, char *why,
                     size_t why_size)
{
    Text t = {NULL, 0, LISTING_MAX_BYTES};
    long reply;
    CURLcode rc = listWith(c, a, url, "MLSD", &t, &reply);
    bool ok = rc == CURLE_OK && folderParseMlsd(t.data ? t.data : "", out, count);
    if (!ok && (rc == CURLE_OK || rc == CURLE_FTP_COULDNT_RETR_FILE || rc == CURLE_QUOTE_ERROR ||
                (reply >= 500 && reply < 503))) {  // MLSD unknown: the older LIST.
        free(t.data);
        t = (Text){NULL, 0, LISTING_MAX_BYTES};
        rc = listWith(c, a, url, NULL, &t, &reply);
        ok = rc == CURLE_OK && folderParseFtpList(t.data ? t.data : "", (int64_t)time(NULL), out, count);
    }
    free(t.data);
    if (!ok && rc == CURLE_OK)
        snprintf(why, why_size, "%s", T("The server's folder listing couldn't be read."));
    else if (!ok)
        explain(rc, reply, why, why_size);
    return ok;
}

bool ftpList(const DavAccount *a, const char *location, DavEntry **out, size_t *count, char *why, size_t why_size)
{
    *out = NULL;
    *count = 0;
    char url[URL_MAX];
    if (!urlOf(a, location, true, url, sizeof(url))) {
        snprintf(why, why_size, "%s", T("That folder's name is too long."));
        return false;
    }
    pthread_mutex_lock(&g_lock);
    bool ok = false;
    // A connection kept from before may have gone (the server restarted): one more try on a new one.
    for (int attempt = 0; attempt < 2 && !ok; attempt++) {
        bool fresh = false;
        if (!g_list || !sameAccount(&g_list_account, a)) {
            dropList();
            g_list = curl_easy_init();
            if (!g_list) {
                snprintf(why, why_size, "%s", T("The network can't be used."));
                break;
            }
            g_list_account = *a;
            fresh = true;
        }
        ok = listOnce(g_list, a, url, out, count, why, why_size);
        if (!ok && fresh)
            break;  // A new connection that fails: the folder, not the connection.
        if (!ok)
            dropList();
    }
    pthread_mutex_unlock(&g_lock);
    return ok;
}

// Runs a handle in f's multi until it's done (its connection stays in multi's cache). Its result.
static CURLcode runInMulti(FtpFile *f, CURL *c)
{
    if (curl_multi_add_handle(f->multi, c) != CURLM_OK)
        return CURLE_FAILED_INIT;
    CURLcode rc = CURLE_OPERATION_TIMEDOUT;
    for (bool done = false; !done;) {
        int still = 0;
        if (curl_multi_perform(f->multi, &still) != CURLM_OK)
            break;
        int left;
        for (CURLMsg *m; (m = curl_multi_info_read(f->multi, &left)) != NULL;) {
            if (m->msg != CURLMSG_DONE)
                continue;
            if (m->easy_handle == c) {
                rc = m->data.result;
                done = true;
            } else if (m->easy_handle == f->spare && c != f->spare) {
                curl_multi_remove_handle(f->multi, f->spare);
                f->spare_running = false;
            }
        }
        if (!done)
            curl_multi_poll(f->multi, NULL, 0, 100, NULL);
    }
    curl_multi_remove_handle(f->multi, c);
    if (c == f->spare)
        f->spare_running = false;
    return rc;
}

// The spare signs in beside the transfer, if it isn't doing so already.
static void warmSpare(FtpFile *f)
{
    if (f->spare && !f->spare_running)
        f->spare_running = curl_multi_add_handle(f->multi, f->spare) == CURLM_OK;
}

FtpFile *ftpFileOpen(const DavAccount *a, const char *location, char *why, size_t why_size)
{
    FtpFile *f = calloc(1, sizeof(*f));
    if (!f || !urlOf(a, location, false, f->url, sizeof(f->url)) || !(f->curl = curl_easy_init()) ||
        !(f->multi = curl_multi_init())) {
        snprintf(why, why_size, "%s", T("The network can't be used."));
        ftpFileClose(f);
        return NULL;
    }
    // Its size, with SIZE; the account is kept in the handle for the reads. The spare signs in at the
    // same time (opening reads the start, then the end: a jump at once).
    setup(f->curl, a, f->url);
    curl_easy_setopt(f->curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(f->curl, CURLOPT_WRITEFUNCTION, dropHeader);  // SIZE's answer comes this way too.
    curl_easy_setopt(f->curl, CURLOPT_TIMEOUT, LIST_TIMEOUT_S);
    f->spare = curl_easy_init();  // Without it, a jump only signs in again.
    if (f->spare) {
        setup(f->spare, a, f->url);
        curl_easy_setopt(f->spare, CURLOPT_NOBODY, 1L);
        curl_easy_setopt(f->spare, CURLOPT_WRITEFUNCTION, dropHeader);
        curl_easy_setopt(f->spare, CURLOPT_TIMEOUT, LIST_TIMEOUT_S);
        warmSpare(f);
    }
    CURLcode rc = runInMulti(f, f->curl);
    curl_off_t length = -1;
    long reply = 0;
    curl_easy_getinfo(f->curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &length);
    curl_easy_getinfo(f->curl, CURLINFO_RESPONSE_CODE, &reply);
    if (rc != CURLE_OK || length < 0) {
        explain(rc == CURLE_OK ? CURLE_REMOTE_FILE_NOT_FOUND : rc, reply, why, why_size);
        ftpFileClose(f);
        return NULL;
    }
    f->size = (int64_t)length;
    return f;
}

int64_t ftpFileSize(const FtpFile *f)
{
    return f->size;
}

// What the transfer gives: into the read under way, the rest kept for the next one.
static size_t takeBytes(char *data, size_t one, size_t count, void *user)
{
    FtpFile *f = user;
    size_t total = one * count, n = total, into = 0;
    f->received += total;
    if (f->skip > 0) {
        size_t pass = (int64_t)n < f->skip ? n : (size_t)f->skip;
        f->skip -= (int64_t)pass;
        data += pass;
        n -= pass;
    }
    if (f->to && f->got < f->want) {
        into = f->want - f->got < n ? f->want - f->got : n;
        memcpy(f->to + f->got, data, into);
        f->got += into;
    }
    size_t rest = n - into;
    if (rest) {
        if (f->stash_len + rest > f->stash_cap) {
            size_t cap = f->stash_cap ? f->stash_cap : 64 * 1024;
            while (cap < f->stash_len + rest)
                cap *= 2;
            uint8_t *grown = realloc(f->stash, cap);
            if (!grown)
                return 0;  // Fails the transfer: the next read starts it again.
            f->stash = grown;
            f->stash_cap = cap;
        }
        memcpy(f->stash + f->stash_len, data + into, rest);
        f->stash_len += rest;
    }
    return total;
}

static void stopTransfer(FtpFile *f)
{
    if (f->running)
        curl_multi_remove_handle(f->multi, f->curl);
    f->running = false;
    f->stash_len = 0;
    f->skip = 0;
}

// Lets a spare that's signing in finish (it isn't removed: that would drop its connection), up to STALL_S.
static void waitForSpare(FtpFile *f)
{
    time_t started = time(NULL);
    while (f->spare_running && time(NULL) - started <= STALL_S) {
        int still = 0;
        if (curl_multi_perform(f->multi, &still) != CURLM_OK)
            break;
        int left;
        for (CURLMsg *m; (m = curl_multi_info_read(f->multi, &left)) != NULL;)
            if (m->msg == CURLMSG_DONE && m->easy_handle == f->spare) {
                curl_multi_remove_handle(f->multi, f->spare);
                f->spare_running = false;
            }
        if (f->spare_running)
            curl_multi_poll(f->multi, NULL, 0, 50, NULL);
    }
}

// A transfer from offset to the file's end.
static bool startTransfer(FtpFile *f, int64_t offset)
{
    stopTransfer(f);
    waitForSpare(f);  // Signing in already: sooner than signing in anew.
    char range[32];
    snprintf(range, sizeof(range), "%lld-", (long long)offset);
    curl_easy_setopt(f->curl, CURLOPT_NOBODY, 0L);
    curl_easy_setopt(f->curl, CURLOPT_RANGE, range);
    curl_easy_setopt(f->curl, CURLOPT_TIMEOUT, 0L);
    // Not read while playback is paused: curl's own speed limit would count that as a stall.
    curl_easy_setopt(f->curl, CURLOPT_LOW_SPEED_LIMIT, 0L);
    curl_easy_setopt(f->curl, CURLOPT_WRITEFUNCTION, takeBytes);
    curl_easy_setopt(f->curl, CURLOPT_WRITEDATA, f);
    f->running = curl_multi_add_handle(f->multi, f->curl) == CURLM_OK;
    f->ended = false;
    f->next = offset;
    warmSpare(f);  // Signs in now, while this one reads: the next jump finds it ready.
    return f->running;
}

// Runs the transfer until the read has what it wants, it ends, it stalls or cancel. True if it has it.
static bool pump(FtpFile *f, const volatile bool *cancel)
{
    time_t last = time(NULL);
    uint64_t had = f->received;
    while (f->got < f->want && !f->ended) {
        if (cancel && *cancel)
            return false;
        int still = 0;
        if (curl_multi_perform(f->multi, &still) != CURLM_OK)
            f->ended = true;
        int left;
        for (CURLMsg *m; (m = curl_multi_info_read(f->multi, &left)) != NULL;) {
            if (m->msg != CURLMSG_DONE)
                continue;
            if (m->easy_handle == f->spare) {  // Signed in and idle: its connection waits in the cache.
                curl_multi_remove_handle(f->multi, f->spare);
                f->spare_running = false;
            } else {
                f->ended = true;
            }
        }
        if (f->received != had) {
            had = f->received;
            last = time(NULL);
        } else if (time(NULL) - last > STALL_S) {
            return false;  // Nothing came for a while: the server or the network is gone.
        }
        if (f->got < f->want && !f->ended)
            curl_multi_poll(f->multi, NULL, 0, 100, NULL);
    }
    return f->got == f->want;
}

int64_t ftpFileRead(FtpFile *f, int64_t offset, uint8_t *to, size_t size, const volatile bool *cancel)
{
    if (offset >= f->size || !size)
        return 0;
    if ((int64_t)size > f->size - offset)
        size = (size_t)(f->size - offset);
    f->to = to;
    f->want = size;
    f->got = 0;
    if (f->running && !f->ended && offset > f->next && offset - f->next <= SKIP_MAX) {  // A short jump ahead.
        int64_t gap = offset - f->next;
        if (gap <= (int64_t)f->stash_len) {
            memmove(f->stash, f->stash + gap, f->stash_len - (size_t)gap);
            f->stash_len -= (size_t)gap;
        } else {
            f->skip = gap - (int64_t)f->stash_len;
            f->stash_len = 0;
        }
        f->next = offset;
    }
    if (f->running && offset == f->next) {  // In order: what was kept first.
        size_t k = f->stash_len < size ? f->stash_len : size;
        memcpy(to, f->stash, k);
        memmove(f->stash, f->stash + k, f->stash_len - k);
        f->stash_len -= k;
        f->got = k;
    } else {
        stopTransfer(f);  // A jump: a new transfer from there.
    }
    bool ok = f->got == size;
    // A transfer that ended early (the server let an idle one go): started again where the read is.
    for (int attempt = 0; attempt < 3 && !ok && !(cancel && *cancel); attempt++) {
        if ((!f->running || f->ended) && !startTransfer(f, offset + (int64_t)f->got))
            break;
        ok = pump(f, cancel);
        if (!ok && !f->ended)
            break;  // Stalled or cancelled, not ended: no point starting again at once.
    }
    f->next = offset + (int64_t)f->got;
    f->to = NULL;
    if (!ok) {
        stopTransfer(f);
        return -1;
    }
    return (int64_t)f->got;
}

void ftpFileClose(FtpFile *f)
{
    if (!f)
        return;
    if (f->running)
        curl_multi_remove_handle(f->multi, f->curl);
    if (f->spare_running)
        curl_multi_remove_handle(f->multi, f->spare);
    if (f->curl)
        curl_easy_cleanup(f->curl);
    if (f->spare)
        curl_easy_cleanup(f->spare);
    if (f->multi)
        curl_multi_cleanup(f->multi);
    free(f->stash);
    free(f);
}
