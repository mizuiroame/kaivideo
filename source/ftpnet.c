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
#define STALL_S 8L
#define FTPES "ftpes://"
#define FTP_SCHEME "ftp://"

struct FtpFile {
    CURL *curl;
    char url[URL_MAX];
    int64_t size;
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

FtpFile *ftpFileOpen(const DavAccount *a, const char *location, char *why, size_t why_size)
{
    FtpFile *f = calloc(1, sizeof(*f));
    if (!f || !urlOf(a, location, false, f->url, sizeof(f->url)) || !(f->curl = curl_easy_init())) {
        snprintf(why, why_size, "%s", T("The network can't be used."));
        ftpFileClose(f);
        return NULL;
    }
    // Its size, with SIZE; the account is kept in the handle for the reads.
    setup(f->curl, a, f->url);
    curl_easy_setopt(f->curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(f->curl, CURLOPT_WRITEFUNCTION, dropHeader);  // SIZE's answer comes this way too.
    curl_easy_setopt(f->curl, CURLOPT_TIMEOUT, LIST_TIMEOUT_S);
    CURLcode rc = curl_easy_perform(f->curl);
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
    curl_easy_setopt(f->curl, CURLOPT_NOBODY, 0L);
    return f;
}

int64_t ftpFileSize(const FtpFile *f)
{
    return f->size;
}

typedef struct {
    uint8_t *to;
    size_t size, got;
} Into;

static size_t addBytes(char *data, size_t one, size_t count, void *user)
{
    Into *in = user;
    size_t n = one * count;
    if (in->got + n > in->size)
        return 0;  // More than asked: the server ignored the range.
    memcpy(in->to + in->got, data, n);
    in->got += n;
    return n;
}

static int onProgress(void *user, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d)
{
    (void)a, (void)b, (void)c, (void)d;
    const volatile bool *cancel = user;
    return cancel && *cancel ? 1 : 0;
}

int64_t ftpFileRead(FtpFile *f, int64_t offset, uint8_t *to, size_t size, const volatile bool *cancel)
{
    if (offset >= f->size || !size)
        return 0;
    if ((int64_t)size > f->size - offset)
        size = (size_t)(f->size - offset);
    char range[64];
    snprintf(range, sizeof(range), "%lld-%lld", (long long)offset, (long long)(offset + (int64_t)size - 1));
    Into in = {to, size, 0};
    curl_easy_setopt(f->curl, CURLOPT_RANGE, range);
    curl_easy_setopt(f->curl, CURLOPT_TIMEOUT, 0L);
    curl_easy_setopt(f->curl, CURLOPT_WRITEFUNCTION, addBytes);
    curl_easy_setopt(f->curl, CURLOPT_WRITEDATA, &in);
    curl_easy_setopt(f->curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(f->curl, CURLOPT_XFERINFOFUNCTION, onProgress);
    curl_easy_setopt(f->curl, CURLOPT_XFERINFODATA, (void *)cancel);
    CURLcode rc = curl_easy_perform(f->curl);
    if (cancel && *cancel)
        return -1;
    // The whole range came; curl may report the transfer it cut short as an error.
    return in.got == size || (rc == CURLE_OK && in.got > 0) ? (int64_t)in.got : -1;
}

void ftpFileClose(FtpFile *f)
{
    if (!f)
        return;
    if (f->curl)
        curl_easy_cleanup(f->curl);
    free(f);
}
