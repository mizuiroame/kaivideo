#include "tpfetch.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "image_config.h"
#include "jellyfin.h"
#include "jfsync.h"
#include "net.h"
#include "stb_image.h"
#include "trickplay.h"

#define INFO_MAX_BYTES (512u * 1024)
#define SHEET_MAX_BYTES (8u * 1024 * 1024)
#define FETCH_TIMEOUT_S 20L
#define RGB 3

static struct {
    pthread_mutex_t lock;
    pthread_cond_t wake;
    pthread_t thread;
    bool running, stop;
    volatile bool cancel;
    char location[LIBRARY_PATH_MAX];
    bool known;          // The pictures there are have been asked about.
    TrickplayInfo info;  // Whether there are any: info.count > 0.
    int wanted;          // The sheet asked for, -1 for none.
    int have;            // The sheet decoded in sheet, -1 for none.
    uint8_t *sheet;      // RGB.
    int sheet_w, sheet_h;
    uint32_t *picture;   // Cut from the sheet.
    TrickplaySpot cut;   // Where picture came from.
    bool cut_valid;
} g = {.lock = PTHREAD_MUTEX_INITIALIZER, .wake = PTHREAD_COND_INITIALIZER};

// The answer to url, or false. Not under the lock.
static bool fetch(NetClient *net, int width, int sheet, size_t max, NetResponse *out)
{
    char url[JF_URL_MAX], auth[JF_AUTH_MAX];
    if (!jfSyncTrickplayRequest(g.location, width, sheet, url, sizeof(url), auth, sizeof(auth)))
        return false;
    NetRequest request = {.url = url, .authorization = auth, .cancel = &g.cancel, .timeout_s = FETCH_TIMEOUT_S};
    return netFetchText(net, &request, max, out);
}

static void readInfo(NetClient *net)
{
    char item[JF_ID_MAX], source[JF_ID_MAX];
    NetResponse r;
    TrickplayInfo info = {0};
    if (jfLocationIds(g.location, item, source) && fetch(net, 0, 0, INFO_MAX_BYTES, &r)) {
        trickplayParse(r.data, source, &info);
        netResponseFree(&r);
    }
    pthread_mutex_lock(&g.lock);
    g.info = info;
    g.known = true;
    pthread_mutex_unlock(&g.lock);
}

static void readSheet(NetClient *net, int sheet)
{
    NetResponse r;
    int w = 0, h = 0, channels;
    uint8_t *rgb = NULL;
    if (fetch(net, g.info.width, sheet, SHEET_MAX_BYTES, &r)) {
        rgb = stbi_load_from_memory((const uint8_t *)r.data, (int)r.size, &w, &h, &channels, RGB);
        netResponseFree(&r);
    }
    pthread_mutex_lock(&g.lock);
    free(g.sheet);
    g.sheet = rgb;
    g.sheet_w = w;
    g.sheet_h = h;
    g.have = sheet;   // Even when it failed: it isn't asked for again and again.
    g.cut_valid = false;
    pthread_mutex_unlock(&g.lock);
}

static void *fetchThread(void *arg)
{
    (void)arg;
    NetClient *net = netClientOpen();
    if (net)
        readInfo(net);
    pthread_mutex_lock(&g.lock);
    while (!g.stop && net) {
        if (g.info.count > 0 && g.wanted >= 0 && g.wanted != g.have) {
            int sheet = g.wanted;
            pthread_mutex_unlock(&g.lock);
            readSheet(net, sheet);
            pthread_mutex_lock(&g.lock);
            continue;
        }
        pthread_cond_wait(&g.wake, &g.lock);
    }
    pthread_mutex_unlock(&g.lock);
    netClientClose(net);
    return NULL;
}

void tpfetchStop(void)
{
    if (!g.running)
        return;
    pthread_mutex_lock(&g.lock);
    g.stop = true;
    g.cancel = true;
    pthread_cond_broadcast(&g.wake);
    pthread_mutex_unlock(&g.lock);
    pthread_join(g.thread, NULL);
    free(g.sheet);
    free(g.picture);
    g.sheet = NULL;
    g.picture = NULL;
    g.running = false;
}

void tpfetchStart(const char *location)
{
    tpfetchStop();
    snprintf(g.location, sizeof(g.location), "%s", location);
    g.stop = false;
    g.cancel = false;
    g.known = false;
    g.info = (TrickplayInfo){0};
    g.wanted = g.have = -1;
    g.cut_valid = false;
    g.running = pthread_create(&g.thread, NULL, fetchThread, NULL) == 0;
}

// Copies the picture at spot out of the sheet (RGB) into g.picture. Under the lock.
static bool cutPicture(const TrickplaySpot *spot)
{
    int w = g.info.width, h = g.info.height;
    if (!g.sheet || spot->x + w > g.sheet_w || spot->y + h > g.sheet_h)
        return false;
    if (!g.picture && !(g.picture = malloc((size_t)w * h * sizeof(uint32_t))))
        return false;
    for (int y = 0; y < h; y++) {
        const uint8_t *from = g.sheet + ((size_t)(spot->y + y) * g.sheet_w + spot->x) * RGB;
        uint32_t *to = g.picture + (size_t)y * w;
        for (int x = 0; x < w; x++, from += RGB)
            to[x] = 0xFF000000u | (uint32_t)from[2] << 16 | (uint32_t)from[1] << 8 | from[0];
    }
    g.cut = *spot;
    g.cut_valid = true;
    return true;
}

const uint32_t *tpfetchPicture(int64_t pos_ns, int *w, int *h, bool *changed)
{
    *changed = false;
    if (!g.running)
        return NULL;
    pthread_mutex_lock(&g.lock);
    TrickplaySpot spot;
    const uint32_t *picture = NULL;
    if (g.known && trickplayLocate(&g.info, pos_ns, &spot)) {
        if (spot.sheet != g.wanted) {
            g.wanted = spot.sheet;
            pthread_cond_broadcast(&g.wake);
        }
        bool same = g.cut_valid && g.cut.sheet == spot.sheet && g.cut.x == spot.x && g.cut.y == spot.y;
        if (g.have == spot.sheet && (same || cutPicture(&spot))) {
            *changed = !same;
            picture = g.picture;
            *w = g.info.width;
            *h = g.info.height;
        }
    }
    pthread_mutex_unlock(&g.lock);
    return picture;
}
