#include "thumbgen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <switch.h>

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

typedef struct {
    char path[PATH_MAX_LEN], jpg[PATH_MAX_LEN];
    uint64_t key;
} Request;

static Thread g_thread;
static bool g_started;
static Mutex g_lock;
static CondVar g_wake;
static Request g_queue[QUEUE_MAX];   // A stack: the latest asked is made first.
static int g_count;
static uint64_t g_done[QUEUE_MAX];
static int g_done_count;
static volatile bool g_stop, g_paused;
static volatile bool g_busy;         // Using the decoder.

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

// Makes one picture. False only when paused or stopped meanwhile (asked again later).
static bool make(const Request *r)
{
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
    uint8_t *rgb = f && !interrupted ? malloc(THUMB_W * THUMB_H * 3) : NULL;
    if (rgb) {
        thumbgenConvert(f->y, f->uv, f->width, f->height, f->y_pitch, f->uv_pitch, f->bt709, f->full_range, rgb,
                        THUMB_W, THUMB_H);
        if (!stbi_write_jpg(r->jpg, THUMB_W, THUMB_H, 3, rgb, JPEG_QUALITY))
            remove(r->jpg);
        free(rgb);
    } else if (!interrupted) {
        FILE *none = fopen(r->jpg, "wb");  // The mark: this video has no picture (not decodable).
        if (none) {
            fwrite(THUMB_NONE_MARK, 1, THUMB_NONE_MARK_SIZE, none);
            fclose(none);
        }
    }
    if (v)
        videoClose(v);
    return !interrupted;
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
        r = g_queue[--g_count];
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
    condvarWakeAll(&g_wake);
    mutexUnlock(&g_lock);
    threadWaitForExit(&g_thread);
    threadClose(&g_thread);
    g_started = false;
}

void thumbgenRequest(const char *path, const char *jpg_path, uint64_t key)
{
    if (!g_started || strlen(path) >= PATH_MAX_LEN || strlen(jpg_path) >= PATH_MAX_LEN)
        return;
    mutexLock(&g_lock);
    for (int i = 0; i < g_count; i++) {
        if (g_queue[i].key == key) {  // Asked again: to the top.
            Request r = g_queue[i];
            memmove(&g_queue[i], &g_queue[i + 1], (size_t)(g_count - i - 1) * sizeof(Request));
            g_queue[g_count - 1] = r;
            mutexUnlock(&g_lock);
            return;
        }
    }
    if (g_count == QUEUE_MAX) {  // The oldest request drops off; it's asked again when shown.
        memmove(&g_queue[0], &g_queue[1], (QUEUE_MAX - 1) * sizeof(Request));
        g_count--;
    }
    Request *r = &g_queue[g_count++];
    snprintf(r->path, sizeof(r->path), "%s", path);
    snprintf(r->jpg, sizeof(r->jpg), "%s", jpg_path);
    r->key = key;
    condvarWakeAll(&g_wake);
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
    condvarWakeAll(&g_wake);
    while (paused && g_busy)  // Until it has let go of the decoder.
        condvarWait(&g_wake, &g_lock);
    mutexUnlock(&g_lock);
}
