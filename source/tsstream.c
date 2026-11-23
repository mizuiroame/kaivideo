#include "tsstream.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lang.h"
#include "mediafile.h"
#include "net.h"

#define READERS 2             // The picture's and the sound's.
#define IO_BUFFER 65536
#define WAIT_MS 100
#define START_TIMEOUT_S 15    // For the first bytes.
#define STALL_S 15            // A stream with nothing coming this long has dropped,
#define RECONNECTS 3          // and is asked for again this often.
#define SNIFF_BYTES 188       // One MPEG-TS packet: what the start is judged by.

typedef struct {
    bool used;
    long long pos;            // Where it reads, from the stream's start.
} TsReader;

static struct {
    bool running;
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t changed;
    char url[1024];
    bool any_certificate;
    StreamHeaders stream;     // The headers its list or addon asks for.
    uint8_t *buf;             // Bytes [base, end) of the stream, at buf[pos % TS_WINDOW].
    long long base, end;
    TsReader readers[READERS];
    bool ended, failed, quit;
    volatile bool cancel;     // Stops the transfer (stopping).
    char error[160];
    long status;              // The server's answer to the first request.
    unsigned interrupts;      // mediaFileInterrupts() when it started: a change stops the reads.
} g;

static pthread_once_t g_once = PTHREAD_ONCE_INIT;

static void initLock(void)
{
    pthread_mutex_init(&g.lock, NULL);
    pthread_cond_init(&g.changed, NULL);
}

static void waitChanged(void)
{
    struct timespec until;
    clock_gettime(CLOCK_REALTIME, &until);
    until.tv_nsec += WAIT_MS * 1000000L;
    if (until.tv_nsec >= 1000000000L) {
        until.tv_sec++;
        until.tv_nsec -= 1000000000L;
    }
    pthread_cond_timedwait(&g.changed, &g.lock, &until);
}

// The earliest place a reader is at, or end when none reads yet (the start is kept for them).
static long long slowest(void)
{
    long long at = -1;
    for (int i = 0; i < READERS; i++)
        if (g.readers[i].used && (at < 0 || g.readers[i].pos < at))
            at = g.readers[i].pos;
    return at;
}

static bool onBytes(const void *data, size_t size, void *user)
{
    (void)user;
    const uint8_t *bytes = data;
    pthread_mutex_lock(&g.lock);
    while (size > 0 && !g.quit) {
        long long room = (long long)TS_WINDOW - (g.end - g.base);
        if (room == 0) {
            long long keep_from = slowest();
            if (keep_from > g.base) {  // What every reader has passed goes.
                g.base = keep_from;
                continue;
            }
            waitChanged();  // A reader behind: the download waits for it (the server's sending slows).
            continue;
        }
        size_t n = (long long)size < room ? size : (size_t)room;
        size_t at = (size_t)(g.end % TS_WINDOW), first = TS_WINDOW - at < n ? TS_WINDOW - at : n;
        memcpy(g.buf + at, bytes, first);
        memcpy(g.buf, bytes + first, n - first);
        g.end += (long long)n;
        bytes += n;
        size -= n;
        pthread_cond_broadcast(&g.changed);
    }
    bool go_on = !g.quit;
    pthread_mutex_unlock(&g.lock);
    return go_on;
}

static void *streamThread(void *arg)
{
    (void)arg;
    NetClient *c = netClientOpen();
    int tries = 0;
    for (;;) {
        NetRequest q;
        memset(&q, 0, sizeof(q));
        q.url = g.url;
        q.cancel = &g.cancel;
        q.stall_s = g.end > 0 ? STALL_S : START_TIMEOUT_S;
        q.any_certificate = g.any_certificate;
        q.stream = &g.stream;
        long status = 0;
        char error[NET_ERROR_MAX] = "";
        long long before = g.end;
        bool ok = c && netFetch(c, &q, onBytes, NULL, &status, error, sizeof(error));
        pthread_mutex_lock(&g.lock);
        if (!g.status)
            g.status = status;
        bool again = !g.quit && g.end > 0 && tries < RECONNECTS;  // It played, then dropped: picked up again.
        if (g.end > before)
            tries = 0;
        if (!again) {
            g.ended = ok || g.quit;
            g.failed = !g.ended;
            snprintf(g.error, sizeof(g.error), "%s", error[0] ? error : T("The channel's stream stopped."));
            pthread_cond_broadcast(&g.changed);
            pthread_mutex_unlock(&g.lock);
            break;
        }
        tries++;
        pthread_mutex_unlock(&g.lock);
    }
    netClientClose(c);
    return NULL;
}

void tsStreamStop(void)
{
    pthread_once(&g_once, initLock);
    if (!g.running)
        return;
    pthread_mutex_lock(&g.lock);
    g.quit = true;
    g.cancel = true;
    pthread_cond_broadcast(&g.changed);
    pthread_mutex_unlock(&g.lock);
    pthread_join(g.thread, NULL);
    free(g.buf);
    g.buf = NULL;
    g.running = false;
}

TsStart tsStreamStart(const char *url, bool any_certificate, const StreamHeaders *stream,
                      const volatile bool *cancel, char *error, size_t error_size)
{
    pthread_once(&g_once, initLock);
    tsStreamStop();
    pthread_mutex_lock(&g.lock);
    snprintf(g.url, sizeof(g.url), "%s", url);
    g.any_certificate = any_certificate;
    if (stream)
        g.stream = *stream;
    else
        memset(&g.stream, 0, sizeof(g.stream));
    g.base = g.end = 0;
    memset(g.readers, 0, sizeof(g.readers));
    g.ended = g.failed = g.quit = g.cancel = false;
    g.error[0] = '\0';
    g.status = 0;
    g.interrupts = mediaFileInterrupts();
    g.buf = malloc(TS_WINDOW);
    pthread_mutex_unlock(&g.lock);
    if (!g.buf || pthread_create(&g.thread, NULL, streamThread, NULL) != 0) {
        free(g.buf);
        g.buf = NULL;
        snprintf(error, error_size, "%s", T("Out of memory"));
        return TS_FAILED;
    }
    g.running = true;
    // Its first bytes say what it is: MPEG-TS packets start with 0x47; a list with "#EXTM3U".
    pthread_mutex_lock(&g.lock);
    while (g.end < SNIFF_BYTES && !g.ended && !g.failed && !(cancel && *cancel))
        waitChanged();
    bool ts = g.end > 0 && g.buf[0] == 0x47;
    bool playlist = g.end >= 7 && !memcmp(g.buf, "#EXTM3U", 7);
    bool got = g.end > 0;
    long status = g.status;
    char why[160];
    snprintf(why, sizeof(why), "%s", g.error);
    pthread_mutex_unlock(&g.lock);
    if (ts)
        return TS_STARTED;
    tsStreamStop();
    if (playlist)
        return TS_PLAYLIST;
    if (cancel && *cancel)
        snprintf(error, error_size, "%s", "");
    else if (status == 404 || status == 410)  // An error page came, not the stream.
        snprintf(error, error_size, "%s", T("The channel isn't at that address any more."));
    else if (status == 401 || status == 403 || status == 451)
        snprintf(error, error_size, "%s", T("The channel refuses to play here (it may play only in its country)."));
    else if (status >= 400)
        snprintf(error, error_size, T("The channel's server answered %ld."), status);
    else if (got)
        snprintf(error, error_size, "%s", T("This channel's stream isn't MPEG-TS or HLS: KaiVideo can't play it."));
    else
        snprintf(error, error_size, "%s", why[0] ? why : T("The channel doesn't answer: it may be off the air."));
    return TS_FAILED;
}

bool tsStreamIsLocation(const char *location)
{
    return !strncmp(location, TS_LOCATION, sizeof(TS_LOCATION) - 1);
}

static int readPacket(void *opaque, uint8_t *out, int size)
{
    TsReader *r = opaque;
    pthread_mutex_lock(&g.lock);
    int result;
    for (;;) {
        if (r->pos < g.base)
            r->pos = g.base;  // Left behind (it can't be, the download waits): on from what's kept.
        if (r->pos < g.end) {
            long long have = g.end - r->pos;
            size_t n = have < size ? (size_t)have : (size_t)size;
            size_t at = (size_t)(r->pos % TS_WINDOW), first = TS_WINDOW - at < n ? TS_WINDOW - at : n;
            memcpy(out, g.buf + at, first);
            memcpy(out + first, g.buf, n - first);
            r->pos += (long long)n;
            pthread_cond_broadcast(&g.changed);  // Room may be made.
            result = (int)n;
            break;
        }
        if (g.ended || g.failed) {
            result = AVERROR_EOF;
            break;
        }
        if (g.quit || mediaFileInterrupts() != g.interrupts) {
            result = AVERROR_EXIT;
            break;
        }
        waitChanged();
    }
    pthread_mutex_unlock(&g.lock);
    return result;
}

bool tsStreamOpenInput(AVFormatContext **fmt, AVIOContext **io)
{
    pthread_once(&g_once, initLock);
    pthread_mutex_lock(&g.lock);
    TsReader *r = NULL;
    for (int i = 0; i < READERS && g.running && !r; i++)
        if (!g.readers[i].used) {
            r = &g.readers[i];
            r->used = true;
            r->pos = g.base;  // From the start kept: picture and sound begin at the same place.
        }
    pthread_mutex_unlock(&g.lock);
    if (!r)
        return false;
    uint8_t *buffer = av_malloc(IO_BUFFER);
    *io = buffer ? avio_alloc_context(buffer, IO_BUFFER, 0, r, readPacket, NULL, NULL) : NULL;
    if (!*io) {
        av_free(buffer);
        pthread_mutex_lock(&g.lock);
        r->used = false;
        pthread_mutex_unlock(&g.lock);
        return false;
    }
    (*io)->seekable = 0;
    (*fmt)->pb = *io;
    (*fmt)->flags |= AVFMT_FLAG_CUSTOM_IO;
    const AVInputFormat *ts = av_find_input_format("mpegts");
    return avformat_open_input(fmt, NULL, ts, NULL) >= 0;
}

void tsStreamCloseInput(AVIOContext *io)
{
    if (!io)
        return;
    TsReader *r = io->opaque;
    pthread_mutex_lock(&g.lock);
    r->used = false;  // The download no longer waits for it.
    pthread_cond_broadcast(&g.changed);
    pthread_mutex_unlock(&g.lock);
    av_freep(&io->buffer);
    avio_context_free(&io);
}
