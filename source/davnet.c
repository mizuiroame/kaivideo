#include "davnet.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "browser.h"
#include "lang.h"
#include "net.h"

#define LISTING_MAX_BYTES (8u << 20)
#define LIST_STALL_S 6L   // Files waits for the list: a server that's gone shouldn't hold it long.
#define LIST_TIMEOUT_S 20L
#define HTTP_MULTI_STATUS 207
#define HTTP_UNAUTHORIZED 401
#define HTTP_FORBIDDEN 403
#define HTTP_NOT_FOUND 404
#define HTTP_NOT_ALLOWED 405
#define URL_MAX 2048
#define AUTH_MAX 512

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static DavAccount g_account;

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
    DavAccount a;
    char text[1024] = "";
    FILE *f = fopen(path, "r");
    if (f) {
        size_t n = fread(text, 1, sizeof(text) - 1, f);
        text[n] = '\0';
        fclose(f);
    }
    davAccountRead(&a, text);
    davUseAccount(&a);
    memset(text, 0, sizeof(text));
    memset(&a, 0, sizeof(a));
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

bool davStreamRequest(const char *location, char *url, size_t url_size, char *auth, size_t auth_size)
{
    DavAccount a;
    davGetAccount(&a);
    bool ok = a.address[0] && davUrlOf(a.address, location, url, url_size) && davAuthorization(&a, auth, auth_size);
    memset(&a, 0, sizeof(a));
    return ok;
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

// Asks for a folder's listing; url gets what was asked (the listing's hrefs are under it).
static bool propfind(const char *location, NetResponse *response, char url[URL_MAX], char *why, size_t why_size)
{
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
    NetRequest request = {.url = url, .authorization = auth, .custom_method = "PROPFIND", .depth = "1",
                          .stall_s = LIST_STALL_S, .timeout_s = LIST_TIMEOUT_S};
    memset(response, 0, sizeof(*response));
    ok = c && netFetchText(c, &request, LISTING_MAX_BYTES, response);
    netClientClose(c);
    memset(auth, 0, sizeof(auth));
    if (!c) {
        snprintf(why, why_size, "%s", T("The network can't be used."));
        return false;
    }
    if (!ok || response->status != HTTP_MULTI_STATUS) {
        if (ok && response->status / 100 == 2)  // A web page, not a listing.
            response->status = HTTP_NOT_ALLOWED;
        if (ok)
            response->problem = NET_STATUS;
        explain(response, why, why_size);
        netResponseFree(response);
        return false;
    }
    return true;
}

bool davList(const char *location, DavEntry **out, size_t *count, char *why, size_t why_size)
{
    *out = NULL;
    *count = 0;
    NetResponse response;
    char url[URL_MAX];
    if (!propfind(location, &response, url, why, why_size))
        return false;
    bool ok = davParseListing(response.data, url, out, count);
    netResponseFree(&response);
    if (!ok)
        snprintf(why, why_size, "%s", T("That address answers, but it isn't a WebDAV folder. Check the path after the port."));
    return ok;
}

bool davTest(char *out, size_t size)
{
    DavEntry *entries;
    size_t count;
    if (!davList(DAV_ROOT, &entries, &count, out, size))
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
