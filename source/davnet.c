#include "davnet.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "browser.h"
#include "folderlist.h"
#include "ftpnet.h"
#include "lang.h"
#include "net.h"
#include "sftpnet.h"
#include "smbnet.h"

#define LISTING_MAX_BYTES (8u << 20)
#define LIST_STALL_S 6L   // Files waits for the list: a server that's gone shouldn't hold it long.
#define LIST_TIMEOUT_S 20L
#define HTTP_MULTI_STATUS 207
#define HTTP_UNAUTHORIZED 401
#define HTTP_FORBIDDEN 403
#define HTTP_NOT_FOUND 404
#define HTTP_NOT_ALLOWED 405
#define HTTP_NOT_IMPLEMENTED 501
#define HTTP_BAD_REQUEST 400
#define PATH_MAX_DAV 512
#define URL_MAX 2048
#define AUTH_MAX 512

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static DavAccount g_account;
static char g_path[PATH_MAX_DAV];               // The account's file, to keep an SFTP server's key in.
static char g_index_address[DAV_ADDRESS_MAX];  // An http(s) folder found to be a web index, not WebDAV.

void davUseAccount(const DavAccount *a)
{
    pthread_mutex_lock(&g_lock);
    g_account = *a;
    pthread_mutex_unlock(&g_lock);
}

void davGetAccount(DavAccount *out)
{
    pthread_mutex_lock(&g_lock);
    *out = g_account;
    pthread_mutex_unlock(&g_lock);
}

bool davConfigured(void)
{
    pthread_mutex_lock(&g_lock);
    bool set = g_account.address[0] != '\0';
    pthread_mutex_unlock(&g_lock);
    return set;
}

void davLoadAccount(const char *path)
{
    snprintf(g_path, sizeof(g_path), "%s", path);
    DavAccount a;
    char text[1024] = "";
    FILE *f = fopen(path, "r");
    if (f) {
        size_t n = fread(text, 1, sizeof(text) - 1, f);
        text[n] = '\0';
        fclose(f);
    }
    bool older = davAccountRead(&a, text);
    davUseAccount(&a);
    memset(text, 0, sizeof(text));
    memset(&a, 0, sizeof(a));
    if (older)
        davSaveAccount(path);  // A password kept as typed: now as its hash, or encoded.
}

bool davSaveAccount(const char *path)
{
    DavAccount a;
    char text[1024];
    davGetAccount(&a);
    bool ok = davAccountWrite(&a, text, sizeof(text));
    FILE *f = ok ? fopen(path, "w") : NULL;
    ok = f && fputs(text, f) >= 0;
    if (f)
        ok = fclose(f) == 0 && ok;
    memset(text, 0, sizeof(text));
    memset(&a, 0, sizeof(a));
    return ok;
}

bool davPasswordMissing(void)
{
    pthread_mutex_lock(&g_lock);
    bool missing = g_account.user[0] && g_account.forget_password && !g_account.password[0];
    pthread_mutex_unlock(&g_lock);
    return missing;
}

// Not asked for yet (not remembered): nothing is sent, and why says so.
static bool sayMissing(char *why, size_t why_size)
{
    if (!davPasswordMissing())
        return false;
    snprintf(why, why_size, "%s", T("The network folder's password isn't remembered: it's asked for when the folder is used."));
    return true;
}

static DavProtocol protocolInUse(void)
{
    pthread_mutex_lock(&g_lock);
    DavProtocol p = davProtocolOf(g_account.address);
    pthread_mutex_unlock(&g_lock);
    return p;
}

bool davReadsFiles(void)
{
    DavProtocol p = protocolInUse();
    return p == DAV_SMB || p == DAV_FTP || p == DAV_SFTP;
}

// An SFTP server's key met for the first time: kept in the account and its file.
static void keepKey(const char *address, const char *key)
{
    if (!key[0])
        return;
    pthread_mutex_lock(&g_lock);
    bool kept = !strcmp(g_account.address, address) && !g_account.host_key[0];
    if (kept)
        snprintf(g_account.host_key, sizeof(g_account.host_key), "%s", key);
    pthread_mutex_unlock(&g_lock);
    if (kept && g_path[0])
        davSaveAccount(g_path);
}

void davForget(void)
{
    smbForget();
    ftpForget();
    sftpForget();
    pthread_mutex_lock(&g_lock);
    g_index_address[0] = '\0';
    pthread_mutex_unlock(&g_lock);
}

struct DavFile {
    DavProtocol protocol;
    void *file;  // SmbFile, FtpFile or SftpFile.
};

DavFile *davFileOpen(const char *location, char *why, size_t why_size)
{
    if (sayMissing(why, why_size))
        return NULL;
    DavAccount a;
    davGetAccount(&a);
    DavFile *f = calloc(1, sizeof(*f));
    char key[DAV_HOST_KEY_MAX] = "";
    if (f) {
        f->protocol = davProtocolOf(a.address);
        switch (f->protocol) {
        case DAV_SMB: f->file = smbFileOpen(&a, location, why, why_size); break;
        case DAV_FTP: f->file = ftpFileOpen(&a, location, why, why_size); break;
        case DAV_SFTP: f->file = sftpFileOpen(&a, location, key, why, why_size); break;
        default: snprintf(why, why_size, "%s", T("Set up the network folder in Settings first.")); break;
        }
    } else {
        snprintf(why, why_size, "%s", T("Out of memory"));
    }
    keepKey(a.address, key);
    memset(&a, 0, sizeof(a));
    if (f && !f->file) {
        free(f);
        f = NULL;
    }
    return f;
}

int64_t davFileSize(const DavFile *f)
{
    switch (f->protocol) {
    case DAV_SMB: return smbFileSize(f->file);
    case DAV_FTP: return ftpFileSize(f->file);
    case DAV_SFTP: return sftpFileSize(f->file);
    default: return -1;
    }
}

int64_t davFileRead(DavFile *f, int64_t offset, uint8_t *to, size_t size, const volatile bool *cancel)
{
    switch (f->protocol) {
    case DAV_SMB: return smbFileRead(f->file, offset, to, size, cancel);
    case DAV_FTP: return ftpFileRead(f->file, offset, to, size, cancel);
    case DAV_SFTP: return sftpFileRead(f->file, offset, to, size, cancel);
    default: return -1;
    }
}

void davFileClose(DavFile *f)
{
    if (!f)
        return;
    switch (f->protocol) {
    case DAV_SMB: smbFileClose(f->file); break;
    case DAV_FTP: ftpFileClose(f->file); break;
    case DAV_SFTP: sftpFileClose(f->file); break;
    default: break;
    }
    free(f);
}

bool davStreamRequest(const char *location, char *url, size_t url_size, char *auth, size_t auth_size)
{
    if (davPasswordMissing())
        return false;
    DavAccount a;
    davGetAccount(&a);
    bool ok = davProtocolOf(a.address) == DAV_WEBDAV && davUrlOf(a.address, location, url, url_size) &&
              davAuthorization(&a, auth, auth_size);
    memset(&a, 0, sizeof(a));
    return ok;
}

bool davReadWhole(const char *location, size_t max_bytes, char **data, size_t *size, char *why, size_t why_size)
{
    *data = NULL;
    *size = 0;
    if (!davReadsFiles()) {  // WebDAV or a web index: one GET.
        char url[URL_MAX], auth[AUTH_MAX];
        if (!davStreamRequest(location, url, sizeof(url), auth, sizeof(auth))) {
            snprintf(why, why_size, "%s", T("Set up the network folder in Settings first."));
            return false;
        }
        NetClient *c = netClientOpen();
        NetRequest request = {.url = url, .authorization = auth, .stall_s = LIST_STALL_S, .timeout_s = LIST_TIMEOUT_S};
        NetResponse r;
        bool ok = c && netFetchText(c, &request, max_bytes, &r);
        netClientClose(c);
        memset(auth, 0, sizeof(auth));
        if (!ok) {
            snprintf(why, why_size, "%s", r.error);
            return false;
        }
        *data = r.data;
        *size = r.size;
        return true;
    }
    DavFile *f = davFileOpen(location, why, why_size);
    if (!f)
        return false;
    int64_t length = davFileSize(f);
    char *buffer = length >= 0 && (uint64_t)length <= max_bytes ? malloc((size_t)length + 1) : NULL;
    int64_t got = buffer ? davFileRead(f, 0, (uint8_t *)buffer, (size_t)length, NULL) : -1;
    davFileClose(f);
    if (got != length) {
        free(buffer);
        snprintf(why, why_size, "%s", T("The file couldn't be read."));
        return false;
    }
    buffer[length] = '\0';
    *data = buffer;
    *size = (size_t)length;
    return true;
}

// What a failed answer means, for the person.
static void explain(const NetResponse *r, char *why, size_t size)
{
    const char *text;
    switch (r->problem) {
    case NET_NO_NAME: text = T("The name in the address couldn't be found. Check it, or use the server's IP address."); break;
    case NET_NO_CONNECT: text = T("Nothing answered at that address and port. Is the server on, and is the port right?"); break;
    case NET_TIMEOUT: text = T("The server didn't answer in time. Is the console on the same network as the server?"); break;
    case NET_SECURE: text = T("The secure connection failed. Does the server use https on that port?"); break;
    case NET_CERTIFICATE: text = T("The server's certificate isn't trusted."); break;
    case NET_STATUS:
        if (r->status == HTTP_UNAUTHORIZED) {
            text = T("Wrong user name or password.");
        } else if (r->status == HTTP_FORBIDDEN) {
            text = T("The server doesn't let this user see that folder.");
        } else if (r->status == HTTP_NOT_FOUND) {
            text = T("That folder isn't on the server.");
        } else if (r->status == HTTP_NOT_ALLOWED) {
            text = T("That address answers, but it isn't a WebDAV folder. Check the path after the port.");
        } else {
            snprintf(why, size, T("The server answered %ld."), r->status);
            return;
        }
        break;
    default: text = T("The connection failed. Check the address and the network."); break;
    }
    snprintf(why, size, "%s", text);
}

// Asks for a folder's listing with PROPFIND (WebDAV) or GET (a web index); url gets what was asked (the
// listing's hrefs are under it). False with why if no answer came; *response holds any answer that did.
static bool askFolder(const char *location, bool webdav, NetResponse *response, char url[URL_MAX], char *why,
                      size_t why_size)
{
    memset(response, 0, sizeof(*response));
    DavAccount a;
    davGetAccount(&a);
    char auth[AUTH_MAX];
    if (!a.address[0]) {
        snprintf(why, why_size, "%s", T("Set up the network folder in Settings first."));
        return false;
    }
    bool ok = davUrlOf(a.address, location, url, URL_MAX) && davAuthorization(&a, auth, sizeof(auth));
    memset(&a, 0, sizeof(a));
    if (!ok) {
        snprintf(why, why_size, "%s", T("That folder's name is too long."));
        return false;
    }
    size_t length = strlen(url);
    if (length + 1 < URL_MAX && url[length - 1] != '/')
        strcat(url, "/");  // A folder: some servers redirect without the '/', losing the method.
    NetClient *c = netClientOpen();
    NetRequest request = {.url = url, .authorization = auth, .custom_method = webdav ? "PROPFIND" : NULL,
                          .depth = webdav ? "1" : NULL, .stall_s = LIST_STALL_S, .timeout_s = LIST_TIMEOUT_S};
    ok = c && netFetchText(c, &request, LISTING_MAX_BYTES, response);
    netClientClose(c);
    memset(auth, 0, sizeof(auth));
    if (!c) {
        snprintf(why, why_size, "%s", T("The network can't be used."));
        return false;
    }
    if (!ok)
        explain(response, why, why_size);  // response->status says what the server answered, if it did.
    return ok;
}

// An answer to PROPFIND that says the server isn't WebDAV there, so a web index may be.
static bool notWebdav(const NetResponse *r, bool answered)
{
    long s = r->status;
    return (answered && s != HTTP_MULTI_STATUS) || s == HTTP_NOT_ALLOWED || s == HTTP_NOT_IMPLEMENTED ||
           s == HTTP_BAD_REQUEST || s == HTTP_FORBIDDEN;
}

static bool isIndexAddress(const char *address)
{
    pthread_mutex_lock(&g_lock);
    bool index = g_index_address[0] && !strcmp(g_index_address, address);
    pthread_mutex_unlock(&g_lock);
    return index;
}

// A WebDAV folder's listing, or a web server's index page when it isn't WebDAV (remembered for the address).
static bool webList(const char *address, const char *location, DavEntry **out, size_t *count, char *why,
                    size_t why_size)
{
    NetResponse response;
    char url[URL_MAX];
    if (!isIndexAddress(address)) {
        bool answered = askFolder(location, true, &response, url, why, why_size);
        if (answered && response.status == HTTP_MULTI_STATUS) {
            bool ok = davParseListing(response.data, url, out, count);
            netResponseFree(&response);
            if (!ok)
                snprintf(why, why_size, "%s",
                         T("That address answers, but it isn't a WebDAV folder. Check the path after the port."));
            return ok;
        }
        bool other = notWebdav(&response, answered);
        netResponseFree(&response);
        if (!other)
            return false;  // why says it: no answer, a wrong password, no such folder.
    }
    bool answered = askFolder(location, false, &response, url, why, why_size);
    bool ok = answered && folderParseHtmlIndex(response.data, url, out, count);
    netResponseFree(&response);
    if (answered && !ok)
        snprintf(why, why_size, "%s",
                 T("That address answers, but with neither a WebDAV folder nor a list of files. Check the path after the port."));
    if (ok) {
        pthread_mutex_lock(&g_lock);
        snprintf(g_index_address, sizeof(g_index_address), "%s", address);
        pthread_mutex_unlock(&g_lock);
    }
    return ok;
}

bool davList(const char *location, DavEntry **out, size_t *count, char *why, size_t why_size)
{
    if (sayMissing(why, why_size))
        return false;
    *out = NULL;
    *count = 0;
    DavAccount a;
    davGetAccount(&a);
    char key[DAV_HOST_KEY_MAX] = "";
    bool listed;
    switch (davProtocolOf(a.address)) {
    case DAV_SMB: listed = smbList(&a, location, out, count, why, why_size); break;
    case DAV_FTP: listed = ftpList(&a, location, out, count, why, why_size); break;
    case DAV_SFTP: listed = sftpList(&a, location, out, count, key, why, why_size); break;
    case DAV_WEBDAV: listed = webList(a.address, location, out, count, why, why_size); break;
    default:
        snprintf(why, why_size, "%s", T("Set up the network folder in Settings first."));
        listed = false;
        break;
    }
    keepKey(a.address, key);
    memset(&a, 0, sizeof(a));
    return listed;
}

bool davTest(char *out, size_t size)
{
    if (sayMissing(out, size))
        return false;
    DavEntry *entries;
    size_t count;
    DavAccount a;
    davGetAccount(&a);
    // SMB on a connection of its own: an answer even while the library waits on the server.
    bool listed = davIsSmbAddress(a.address) ? smbListFresh(&a, DAV_ROOT, &entries, &count, out, size)
                                             : davList(DAV_ROOT, &entries, &count, out, size);
    memset(&a, 0, sizeof(a));
    if (!listed)
        return false;
    int folders = 0, videos = 0;
    for (size_t i = 0; i < count; i++) {
        if (entries[i].is_dir)
            folders++;
        else if (browserIsVideo(entries[i].name))
            videos++;
    }
    free(entries);
    snprintf(out, size, T("The network folder answers. At its top: folders %d, videos %d."), folders, videos);
    return true;
}
