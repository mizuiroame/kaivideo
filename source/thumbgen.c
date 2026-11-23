#include "thumbgen.h"
#include "xtreamvod.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <switch.h>

#include "image_config.h"
#include "jfsync.h"
#include "net.h"
#include "stb_image.h"
#include "stb_image_write.h"
#include "video.h"

#define QUEUE_MAX 48
#define PATH_MAX_LEN 768
#define STACK_SIZE 0x80000             // Reading a video's format can decode its first frames.
#define DECODE_THREADS 2              // Leaves the CPU to the library while it works.
#define PRIORITY 0x2E                 // Below the UI's.
#define FRAME_WAIT_NS 8000000000ull   // A frame that hasn't come by then: give up on it.
#define POLL_NS 20000000ull
#define MAX_POINT_NS 600000000000ll   // Never further in than 10 minutes.
#define DARK_LUMA 28                  // Darker than this on average: try later in the video.
#define JPEG_QUALITY 85
#define IMAGE_MAX_BYTES (8u * 1024 * 1024)
#define IMAGE_TIMEOUT_S 20L
#define FETCH_SCALE 2
#define IMAGE_MAX_PIXELS (16 * 1000 * 1000)
#define STALE_FRAMES 20               // Not asked for this many frames: off the screen, dropped (asked again when shown).

typedef struct {
    char path[PATH_MAX_LEN], jpg[PATH_MAX_LEN];
    uint64_t key;
    bool large;
    uint64_t frame;  // The last frame that asked for it (drew it).
    uint32_t order;  // Its place among that frame's: the screen is drawn from its top left.
} Request;

static Thread g_thread;
static bool g_started;
static Mutex g_lock;
static CondVar g_wake;
// What's on screen now is made first, in the order it's drawn (top left
// first); what left the screen is dropped.
static Request g_queue[QUEUE_MAX];
static int g_count;
static uint64_t g_frame;             // thumbgenNextFrame's count.
static uint32_t g_order;             // Requests in this frame so far.
static uint64_t g_done[QUEUE_MAX];
static int g_done_count;
static volatile bool g_stop, g_paused;
static volatile bool g_busy;         // Using the decoder.
static NetClient *g_net;             // Only this thread uses it.
static volatile bool g_net_held;     // The network is held (netInit, on the main thread: its count isn't
                                     // shared safely) from the first picture asked of it until it stops:
                                     // an IPTV list's logos come with nothing else holding it.

// The connection, opened the first time it's needed. NULL if it can't be.
static NetClient *connection(void)
{
    if (!g_net && g_net_held)
        g_net = netClientOpen();
    return g_net;
}
static volatile bool g_net_abort;    // Paused or stopping: no waiting for the server.

// Waits for a frame at or after the point, or gives up (paused, stopped,
// the video ended or failed). The frame stays valid until v is used again.
static const GfxVideoFrame *frameAt(Video *v, int64_t point_ns)
{
    videoSeek(v, point_ns);
    for (u64 waited = 0; waited < FRAME_WAIT_NS && !g_stop && !g_paused; waited += POLL_NS) {
        const GfxVideoFrame *f = videoFrameAt(v, INT64_MAX);
        if (f)
            return f;
        char error[8];
        if (videoFinished(v, error, sizeof(error)))
            return NULL;
        svcSleepThread(POLL_NS);
    }
    return NULL;
}

static void markNone(const char *jpg)
{
    FILE *none = fopen(jpg, "wb");  // The mark: this video has no picture.
    if (none) {
        fwrite(THUMB_NONE_MARK, 1, THUMB_NONE_MARK_SIZE, none);
        fclose(none);
    }
}

// One of the item's pictures from the server, as RGB (free with stbi_image_free).
static uint8_t *fetchPicture(const char *location, const char *kind, int max_width, int *w, int *h, long *status)
{
    char url[JF_URL_MAX], auth[JF_AUTH_MAX];
    if (!jfSyncImageRequest(location, kind, max_width, url, sizeof(url), auth, sizeof(auth)))
        return NULL;
    if (!connection())
        return NULL;
    NetRequest request = {.url = url, .authorization = auth, .cancel = &g_net_abort, .timeout_s = IMAGE_TIMEOUT_S};
    NetResponse answer;
    bool ok = netFetchText(g_net, &request, IMAGE_MAX_BYTES, &answer);
    *status = answer.status;
    if (!ok)
        return NULL;
    int channels;
    const stbi_uc *data = (const stbi_uc *)answer.data;
    bool fits = stbi_info_from_memory(data, (int)answer.size, w, h, &channels) && *w > 0 && *h > 0 &&
                (long long)*w * *h <= IMAGE_MAX_PIXELS;
    uint8_t *rgb = fits ? stbi_load_from_memory(data, (int)answer.size, w, h, &channels, 3) : NULL;
    netResponseFree(&answer);
    return rgb;
}

// A picture from the server: its own or, for a poster, the wide one if there is one.
static void makeRemote(const Request *r)
{
    int w = 0, h = 0, wide_w = 0, wide_h = 0;
    int out_w = r->large ? THUMB_LARGE_W : THUMB_W, out_h = r->large ? THUMB_LARGE_H : THUMB_H;
    int fetch_w = out_w * FETCH_SCALE;  // A little more than needed: it's cropped and averaged down.
    long status = 0, wide_status = 0;
    uint8_t *rgb = fetchPicture(r->path, "Primary", fetch_w, &w, &h, &status);
    if (!rgb || h > w) {
        uint8_t *wide = fetchPicture(r->path, "Backdrop", fetch_w, &wide_w, &wide_h, &wide_status);
        if (wide) {
            if (rgb)
                stbi_image_free(rgb);
            rgb = wide;
            w = wide_w;
            h = wide_h;
        }
    }
    uint8_t *out = rgb ? malloc((size_t)out_w * out_h * 3) : NULL;
    if (out) {
        thumbgenFit(rgb, w, h, out, out_w, out_h);
        if (!stbi_write_jpg(r->jpg, out_w, out_h, 3, out, JPEG_QUALITY))
            remove(r->jpg);
        free(out);
    } else if (status == 404 && (wide_status == 404 || wide_status == 0)) {
        markNone(r->jpg);  // The server has none. Without an answer, it's tried again next time.
    }
    if (rgb)
        stbi_image_free(rgb);
}

// A picture at a plain address (an IPTV channel's logo): all of it, centered on the cards' dark color,
// its transparent parts over it too. None if it can't be had or read.
static const uint8_t CARD_BACK[3] = {0x1C, 0x1C, 0x21};  // The cards' dark color.

// A logo (RGBA, w x h) all of it, centered with a margin on the cards' color.
static void drawLogo(const uint8_t *rgba, int w, int h, uint8_t *out, int out_w, int out_h)
{
    double scale = (out_w * 0.8) / w < (out_h * 0.8) / h ? (out_w * 0.8) / w : (out_h * 0.8) / h;
    int dw = (int)(w * scale), dh = (int)(h * scale);
    dw = dw < 1 ? 1 : dw;
    dh = dh < 1 ? 1 : dh;
    int x0 = (out_w - dw) / 2, y0 = (out_h - dh) / 2;
    for (int y = 0; y < out_h; y++)
        for (int x = 0; x < out_w; x++) {
            uint8_t *o = out + ((size_t)y * out_w + x) * 3;
            memcpy(o, CARD_BACK, 3);
            if (x < x0 || x >= x0 + dw || y < y0 || y >= y0 + dh)
                continue;
            int sx = (int)((x - x0) / scale), sy = (int)((y - y0) / scale);
            sx = sx < w ? sx : w - 1;
            sy = sy < h ? sy : h - 1;
            const uint8_t *px = rgba + ((size_t)sy * w + sx) * 4;
            for (int c = 0; c < 3; c++)
                o[c] = (uint8_t)((px[c] * px[3] + CARD_BACK[c] * (255 - px[3])) / 255);
        }
}

// A poster (RGBA): over the cards' color, then cropped to fill the card as a video's picture is.
static bool drawPoster(const uint8_t *rgba, int w, int h, uint8_t *out, int out_w, int out_h)
{
    uint8_t *rgb = malloc((size_t)w * h * 3);
    if (!rgb)
        return false;
    for (size_t i = 0; i < (size_t)w * h; i++)
        for (int c = 0; c < 3; c++)
            rgb[i * 3 + c] = (uint8_t)((rgba[i * 4 + c] * rgba[i * 4 + 3] + CARD_BACK[c] * (255 - rgba[i * 4 + 3])) / 255);
    thumbgenFit(rgb, w, h, out, out_w, out_h);
    free(rgb);
    return true;
}

// A picture at a plain address: an IPTV channel's logo (shown whole), or a film's or series'
// poster ("poster:<address>", filling the card). None if it can't be had or read.
static void makeUrl(const Request *r)
{
    if (!connection())
        return;
    bool poster = !strncmp(r->path, XTREAM_POSTER_PREFIX, sizeof(XTREAM_POSTER_PREFIX) - 1);
    const char *url = poster ? r->path + sizeof(XTREAM_POSTER_PREFIX) - 1 : r->path;
    NetRequest request = {.url = url, .cancel = &g_net_abort, .timeout_s = IMAGE_TIMEOUT_S,
                          .any_certificate = poster && xtreamVodAnyCertificate()};
    NetResponse answer;
    bool ok = netFetchText(g_net, &request, IMAGE_MAX_BYTES, &answer);
    int w = 0, h = 0, channels;
    const stbi_uc *data = (const stbi_uc *)answer.data;
    bool fits = ok && stbi_info_from_memory(data, (int)answer.size, &w, &h, &channels) && w > 0 && h > 0 &&
                (long long)w * h <= IMAGE_MAX_PIXELS;
    uint8_t *rgba = fits ? stbi_load_from_memory(data, (int)answer.size, &w, &h, &channels, 4) : NULL;
    long status = answer.status;
    netResponseFree(&answer);
    int out_w = r->large ? THUMB_LARGE_W : THUMB_W, out_h = r->large ? THUMB_LARGE_H : THUMB_H;
    uint8_t *out = rgba ? malloc((size_t)out_w * out_h * 3) : NULL;
    bool drawn = out && (poster ? drawPoster(rgba, w, h, out, out_w, out_h)
                                : (drawLogo(rgba, w, h, out, out_w, out_h), true));
    if (drawn && !stbi_write_jpg(r->jpg, out_w, out_h, 3, out, JPEG_QUALITY))
        remove(r->jpg);
    else if (!out && (status == 404 || status == 403 || (ok && !fits)))
        markNone(r->jpg);  // Gone, or not a picture. Without an answer, it's tried again next time.
    free(out);
    if (rgba)
        stbi_image_free(rgba);
}

// Makes one picture. False only when paused or stopped meanwhile (asked again later).
static bool make(const Request *r)
{
    if (!strncmp(r->path, "http://", 7) || !strncmp(r->path, "https://", 8) ||
        !strncmp(r->path, XTREAM_POSTER_PREFIX, sizeof(XTREAM_POSTER_PREFIX) - 1)) {
        makeUrl(r);
        return !g_stop && !g_paused;
    }
    if (xtreamIsLocation(r->path) || !strncmp(r->path, "stremio:", 8)) {  // Its poster is its picture, not a frame.
        markNone(r->jpg);
        return true;
    }
    if (jfIsRemoteLocation(r->path)) {
        makeRemote(r);
        return !g_stop && !g_paused;
    }
    char error[160];
    Video *v = videoOpenLimited(r->path, DECODE_THREADS, error, sizeof(error));
    const GfxVideoFrame *f = NULL;
    if (v) {
        int64_t length = videoDuration(v), point = length > 0 ? length / 5 : 30000000000ll;
        if (point > MAX_POINT_NS)
            point = MAX_POINT_NS;
        f = frameAt(v, point);
        // A black frame (a fade, the credits' start): a little later.
        for (int tries = 0; f && tries < 2 && thumbgenLuma(f->y, f->width, f->height, f->y_pitch) < DARK_LUMA;
             tries++) {
            point += length > 0 ? length / 12 : 60000000000ll;
            const GfxVideoFrame *later = frameAt(v, point);
            if (!later)
                break;
            f = later;
        }
    }
    bool interrupted = g_stop || g_paused;
    int out_w = r->large ? THUMB_LARGE_W : THUMB_W, out_h = r->large ? THUMB_LARGE_H : THUMB_H;
    uint8_t *rgb = f && !interrupted ? malloc((size_t)out_w * out_h * 3) : NULL;
    if (rgb) {
        thumbgenConvert(f->y, f->uv, f->width, f->height, f->y_pitch, f->uv_pitch, f->bt709, f->full_range, rgb,
                        out_w, out_h);
        if (!stbi_write_jpg(r->jpg, out_w, out_h, 3, rgb, JPEG_QUALITY))
            remove(r->jpg);
        free(rgb);
    } else if (!interrupted) {
        markNone(r->jpg);  // It can't be decoded.
    }
    if (v)
        videoClose(v);
    return !interrupted;
}

// The request to make next, under g_lock: the latest frame's, first drawn;
// requests no frame has asked for lately go. -1 if none is left.
static int nextRequest(void)
{
    int kept = 0;
    for (int i = 0; i < g_count; i++)
        if (g_queue[i].frame + STALE_FRAMES >= g_frame)
            g_queue[kept++] = g_queue[i];
    g_count = kept;
    int best = -1;
    for (int i = 0; i < g_count; i++) {
        const Request *q = &g_queue[i];
        if (best < 0 || q->frame > g_queue[best].frame ||
            (q->frame == g_queue[best].frame && q->order < g_queue[best].order))
            best = i;
    }
    return best;
}

static void thumbThread(void *arg)
{
    (void)arg;
    Request r;
    mutexLock(&g_lock);
    while (!g_stop) {
        if (g_paused || g_count == 0) {
            condvarWait(&g_wake, &g_lock);
            continue;
        }
        int at = nextRequest();
        if (at < 0)
            continue;  // All were off the screen.
        r = g_queue[at];
        memmove(&g_queue[at], &g_queue[at + 1], (size_t)(g_count - at - 1) * sizeof(Request));
        g_count--;
        g_busy = true;
        mutexUnlock(&g_lock);
        bool finished = make(&r);
        mutexLock(&g_lock);
        g_busy = false;
        condvarWakeAll(&g_wake);  // Someone may wait for the decoder.
        if (finished && g_done_count < QUEUE_MAX)
            g_done[g_done_count++] = r.key;
        else if (!finished && g_count < QUEUE_MAX)
            g_queue[g_count++] = r;  // Again, once resumed.
    }
    mutexUnlock(&g_lock);
}

bool thumbgenStart(void)
{
    if (g_started)
        return true;
    mutexInit(&g_lock);
    condvarInit(&g_wake);
    g_stop = false;
    g_net_abort = false;
    if (R_FAILED(threadCreate(&g_thread, thumbThread, NULL, NULL, STACK_SIZE, PRIORITY, -2)))
        return false;
    if (R_FAILED(threadStart(&g_thread))) {
        threadClose(&g_thread);
        return false;
    }
    g_started = true;
    return true;
}

void thumbgenStop(void)
{
    if (!g_started)
        return;
    mutexLock(&g_lock);
    g_stop = true;
    g_net_abort = true;
    condvarWakeAll(&g_wake);
    mutexUnlock(&g_lock);
    threadWaitForExit(&g_thread);
    threadClose(&g_thread);
    netClientClose(g_net);  // Before the network is let go.
    g_net = NULL;
    if (g_net_held)
        netExit();
    g_net_held = false;
    g_started = false;
}

void thumbgenRequest(const char *path, const char *jpg_path, uint64_t key, bool large)
{
    if (!g_started || strlen(path) >= PATH_MAX_LEN || strlen(jpg_path) >= PATH_MAX_LEN)
        return;
    bool remote = !strncmp(path, "http://", 7) || !strncmp(path, "https://", 8) || jfIsRemoteLocation(path) ||
                  !strncmp(path, XTREAM_POSTER_PREFIX, sizeof(XTREAM_POSTER_PREFIX) - 1);
    if (remote && !g_net_held)
        g_net_held = netInit();
    mutexLock(&g_lock);
    for (int i = 0; i < g_count; i++) {
        if (g_queue[i].key == key) {  // Drawn again: still on screen, in this place.
            g_queue[i].frame = g_frame;
            g_queue[i].order = g_order++;
            mutexUnlock(&g_lock);
            return;
        }
    }
    if (g_count == QUEUE_MAX) {  // Full: the one asked longest ago goes; it's asked again when shown.
        int oldest = 0;
        for (int i = 1; i < g_count; i++)
            if (g_queue[i].frame < g_queue[oldest].frame)
                oldest = i;
        memmove(&g_queue[oldest], &g_queue[oldest + 1], (size_t)(g_count - oldest - 1) * sizeof(Request));
        g_count--;
    }
    Request *r = &g_queue[g_count++];
    snprintf(r->path, sizeof(r->path), "%s", path);
    snprintf(r->jpg, sizeof(r->jpg), "%s", jpg_path);
    r->key = key;
    r->large = large;
    r->frame = g_frame;
    r->order = g_order++;
    condvarWakeAll(&g_wake);
    mutexUnlock(&g_lock);
}

void thumbgenNextFrame(void)
{
    if (!g_started)
        return;
    mutexLock(&g_lock);
    g_frame++;
    g_order = 0;
    mutexUnlock(&g_lock);
}

bool thumbgenTakeDone(uint64_t *key)
{
    if (!g_started)
        return false;
    mutexLock(&g_lock);
    bool any = g_done_count > 0;
    if (any)
        *key = g_done[--g_done_count];
    mutexUnlock(&g_lock);
    return any;
}

void thumbgenPause(bool paused)
{
    if (!g_started)
        return;
    mutexLock(&g_lock);
    g_paused = paused;
    g_net_abort = paused;
    condvarWakeAll(&g_wake);
    while (paused && g_busy)  // Until it has let go of the decoder.
        condvarWait(&g_wake, &g_lock);
    mutexUnlock(&g_lock);
}
