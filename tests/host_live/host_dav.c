// The network folder against a real WebDAV server (tests/host_live/README):
//   DAV_URL=http://host:8098/ DAV_USER=... DAV_PASS=... ./host_dav
// Lists the top and a folder, reads a video's first and last blocks with
// ranges, and checks a wrong password and a wrong address say so.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "browser.h"
#include "davnet.h"
#include "net.h"

static int g_failed;

#define EXPECT(cond, ...)                 \
    do {                                  \
        if (!(cond)) {                    \
            g_failed++;                   \
            printf("FAIL: " __VA_ARGS__); \
            printf("\n");                 \
        }                                 \
    } while (0)

static bool count(const void *data, size_t size, void *user)
{
    (void)data;
    *(size_t *)user += size;
    return true;
}

// A video somewhere under location: its location in out.
static bool findVideo(const char *location, char *out, size_t size, int depth)
{
    DavEntry *e;
    size_t n;
    char why[256];
    if (depth > 3 || !davList(location, &e, &n, why, sizeof(why)))
        return false;
    bool found = false;
    for (size_t i = 0; i < n && !found; i++) {
        char child[1024];
        snprintf(child, sizeof(child), "%s%s%s", location, location[strlen(location) - 1] == '/' ? "" : "/", e[i].name);
        if (!e[i].is_dir && browserIsVideo(e[i].name)) {
            snprintf(out, size, "%s", child);
            printf("  video: %s (%lld bytes)\n", child, (long long)e[i].size);
            found = true;
        } else if (e[i].is_dir) {
            found = findVideo(child, out, size, depth + 1);
        }
    }
    free(e);
    return found;
}

static void readRanges(const char *location)
{
    char url[2048], auth[512];
    EXPECT(davStreamRequest(location, url, sizeof(url), auth, sizeof(auth)), "no request for %s", location);
    NetClient *c = netClientOpen();
    long long total = -1;
    size_t got = 0;
    long status = 0;
    char error[NET_ERROR_MAX];
    NetRequest first = {.url = url, .authorization = auth, .range = "0-1048575", .total_size = &total};
    EXPECT(netFetch(c, &first, count, &got, &status, error, sizeof(error)) && status == 206 && got == 1048576,
           "first block: status %ld, %zu bytes (%s)", status, got, error);
    printf("  first block: %ld, %zu bytes of %lld\n", status, got, total);
    char range[64];
    snprintf(range, sizeof(range), "%lld-", total - 1000);
    got = 0;
    NetRequest last = {.url = url, .authorization = auth, .range = range};
    EXPECT(netFetch(c, &last, count, &got, &status, error, sizeof(error)) && status == 206 && got == 1000,
           "last block: status %ld, %zu bytes", status, got);
    printf("  last block: %ld, %zu bytes\n", status, got);
    netClientClose(c);
}

int main(void)
{
    const char *url = getenv("DAV_URL"), *user = getenv("DAV_USER"), *pass = getenv("DAV_PASS");
    if (!url || !user || !pass || !netInit())
        return 2;
    DavAccount a = {0};
    EXPECT(davNormalizeAddress(url, a.address, sizeof(a.address)), "address %s", url);
    snprintf(a.user, sizeof(a.user), "%s", user);
    snprintf(a.password, sizeof(a.password), "%s", pass);
    davUseAccount(&a);

    char text[256];
    EXPECT(davTest(text, sizeof(text)), "test: %s", text);
    printf("test: %s\n", text);
    DavEntry *e;
    size_t n;
    EXPECT(davList("dav:/Movies", &e, &n, text, sizeof(text)) && n > 0, "Movies: %s", text);
    for (size_t i = 0; i < n; i++)
        printf("  %s%s\n", e[i].name, e[i].is_dir ? "/" : "");
    free(e);
    char video[1024];
    EXPECT(findVideo("dav:/", video, sizeof(video), 0), "no video found");
    readRanges(video);
    EXPECT(!davList("dav:/Not a folder", &e, &n, text, sizeof(text)), "a missing folder listed");
    printf("missing folder: %s\n", text);

    snprintf(a.password, sizeof(a.password), "wrong");
    davUseAccount(&a);
    EXPECT(!davTest(text, sizeof(text)) && strstr(text, "password"), "wrong password: %s", text);
    printf("wrong password: %s\n", text);
    DavAccount nowhere = {"http://127.0.0.1:1/", "", ""};
    davUseAccount(&nowhere);
    EXPECT(!davTest(text, sizeof(text)), "nothing there answered");
    printf("wrong port: %s\n", text);
    netExit();
    printf(g_failed ? "%d FAILED\n" : "all good\n", g_failed);
    return g_failed ? 1 : 0;
}
