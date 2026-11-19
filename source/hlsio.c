#include "hlsio.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <libavformat/avio.h>
#include <libavutil/mem.h>

#include "hlslist.h"
#include "mediafile.h"
#include "net.h"

#define SLOTS 10
#define READERS 2             // The picture's and the sound's.
#define AHEAD 4               // Segments kept ready from where each reads.
#define CUSHION_NS 4000000000ll  // Video in memory before playback starts or goes on from a jump.
#define LIVE_MARGIN_NS 2000000000ll  // Live TV: this much more than a segment ahead before starting.
#define IO_BUFFER (64 * 1024)
#define PLAYLIST_MAX (8u * 1024 * 1024)
#define SEGMENT_MAX (96u * 1024 * 1024)
#define PLAYLIST_TIMEOUT_S 30L
#define SEGMENT_STALL_S 40L   // The server may need a while to start converting.
#define FETCH_TRIES 3
#define RETRY_WAIT_NS 500000000L
#define WAIT_STEP_NS 50000000L
#define NS_PER_S 1000000000L
#define REREAD_NS 3000000000ull  // A growing list is read again this often (its segments are ~8 s).
#define LIVE_STALL_NS 30000000000ull  // A live list that stops growing this long: the channel stopped.

typedef enum { SLOT_EMPTY, SLOT_LOADING, SLOT_READY } SlotState;

typedef struct {
    int index;
    SlotState state;
    char *data;
    size_t size;
} Slot;

typedef struct {
    const void *owner;   // Its AVFormatContext.
    int segment;
} Reader;

static struct {
    bool running;
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t wake;
    NetClient *net;
    char authorization[512];
    HlsPlaylist list;
    Slot slots[SLOTS];
    Reader readers[READERS];
    int fetching;
    unsigned interrupts;
    volatile bool abort_fetch;
    bool stop, failed;
    char media_url[HLS_URL_MAX];  // Read again while the list grows (live TV).
    int64_t base_ns;              // The list's start: set at start, read without the lock.
    uint64_t grew_ns;             // When a live list last grew.
    uint64_t next_reread_ns;
} g;

bool hlsIsLocation(const char *path)
{
    return strncmp(path, HLS_LOCATION, sizeof(HLS_LOCATION) - 1) == 0;
}

// --- What to fetch ---

static int findSlot(int index)
{
    for (int i = 0; i < SLOTS; i++)
        if (g.slots[i].state != SLOT_EMPTY && g.slots[i].index == index)
            return i;
    return -1;
}

static bool wanted(int index)
{
    for (int r = 0; r < READERS; r++)
        if (g.readers[r].owner && index >= g.readers[r].segment && index < g.readers[r].segment + AHEAD)
            return true;
    return false;
}

// The nearest missing segment to any reader, or -1.
static int nextSegment(void)
{
    for (int distance = 0; distance < AHEAD; distance++)
        for (int r = 0; r < READERS; r++) {
            int index = g.readers[r].segment + distance;
            if (g.readers[r].owner && index < g.list.count && findSlot(index) < 0)
                return index;
        }
    return -1;
}

static int freeSlot(void)
{
    int victim = -1;
    for (int i = 0; i < SLOTS; i++) {
        if (g.slots[i].state == SLOT_EMPTY)
            return i;
        if (g.slots[i].state == SLOT_READY && !wanted(g.slots[i].index) &&
            (victim < 0 || g.slots[i].index < g.slots[victim].index))
            victim = i;
    }
    return victim;
}

// --- The thread ---

static bool fetchSegment(int index, NetResponse *r)
{
    NetRequest request = {.url = g.list.urls[index], .authorization = g.authorization, .cancel = &g.abort_fetch,
                          .stall_s = SEGMENT_STALL_S};
    for (int attempt = 0; attempt < FETCH_TRIES && !g.abort_fetch; attempt++) {
        if (netFetchText(g.net, &request, SEGMENT_MAX, r))
            return true;
        if (!g.abort_fetch)
            nanosleep(&(struct timespec){0, RETRY_WAIT_NS}, NULL);
    }
    return false;
}

static uint64_t monotonicNs(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * NS_PER_S + (uint64_t)t.tv_nsec;
}

static bool getText(const char *url, NetResponse *r);

// A live list: read it again, and take what was added (segments keep their numbers). Under the lock.
static void reread(void)
{
    g.next_reread_ns = monotonicNs() + REREAD_NS;
    if (!g.stop)
        g.abort_fetch = false;  // Left set by a fetch called off; stopping sets it again.
    pthread_mutex_unlock(&g.lock);
    NetResponse media;
    HlsPlaylist list;
    bool ok = getText(g.media_url, &media);
    if (ok) {
        ok = hlsPlaylistParse(media.data, g.media_url, 0, &list);
        netResponseFree(&media);
    }
    pthread_mutex_lock(&g.lock);
    uint64_t now = monotonicNs();
    if (ok && list.count > g.list.count)
        g.grew_ns = now;
    if (ok && list.count >= g.list.count) {
        bool ended = list.ended;
        hlsPlaylistFree(&g.list);
        g.list = list;
        g.list.ended = ended;
        pthread_cond_broadcast(&g.wake);
    } else if (ok) {
        hlsPlaylistFree(&list);  // Shorter: not the same list any more; kept as it was.
    }
    if (!g.list.ended && now - g.grew_ns > LIVE_STALL_NS) {
        g.failed = true;  // Readers waiting for more give up: playback ends, and says so.
        pthread_cond_broadcast(&g.wake);
    }
}

// Waits for something to do; a growing list is read again on time. Under the lock.
static void waitForWork(void)
{
    if (g.list.ended) {
        pthread_cond_wait(&g.wake, &g.lock);
        return;
    }
    struct timespec until;
    clock_gettime(CLOCK_REALTIME, &until);
    uint64_t now = monotonicNs(), left = g.next_reread_ns > now ? g.next_reread_ns - now : 0;
    until.tv_sec += (time_t)(left / NS_PER_S);
    until.tv_nsec += (long)(left % NS_PER_S);
    if (until.tv_nsec >= NS_PER_S) {
        until.tv_sec++;
        until.tv_nsec -= NS_PER_S;
    }
    pthread_cond_timedwait(&g.wake, &g.lock, &until);
}

static void *fetchThread(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&g.lock);
    while (!g.stop) {
        if (!g.list.ended && monotonicNs() >= g.next_reread_ns) {
            reread();
            continue;
        }
        int index = g.failed ? -1 : nextSegment();
        int slot = index >= 0 ? freeSlot() : -1;
        if (slot < 0) {
            waitForWork();
            continue;
        }
        free(g.slots[slot].data);
        g.slots[slot] = (Slot){.index = index, .state = SLOT_LOADING};
        g.fetching = index;
        g.abort_fetch = false;
        pthread_mutex_unlock(&g.lock);
        NetResponse r;
        bool ok = fetchSegment(index, &r);
        pthread_mutex_lock(&g.lock);
        bool aborted = g.abort_fetch;
        g.fetching = -1;
        g.slots[slot].state = ok ? SLOT_READY : SLOT_EMPTY;
        g.slots[slot].data = ok ? r.data : NULL;
        g.slots[slot].size = ok ? r.size : 0;
        if (!ok && !aborted)
            g.failed = true;
        pthread_cond_broadcast(&g.wake);
    }
    pthread_mutex_unlock(&g.lock);
    return NULL;
}

// --- Starting ---

static bool getText(const char *url, NetResponse *r)
{
    NetRequest request = {.url = url, .authorization = g.authorization, .cancel = &g.abort_fetch,  // Stopping cuts it short.
                          .timeout_s = PLAYLIST_TIMEOUT_S};
    return netFetchText(g.net, &request, PLAYLIST_MAX, r);
}

// The media playlist behind the master one, into g.list.
static bool readPlaylists(const char *master_url, int64_t start_ns)
{
    NetResponse master, media;
    char ref[HLS_URL_MAX], url[HLS_URL_MAX];
    if (!getText(master_url, &master))
        return false;
    bool ok = hlsFirstEntry(master.data, ref, sizeof(ref)) && hlsResolveUrl(master_url, ref, url, sizeof(url));
    netResponseFree(&master);
    if (!ok || !getText(url, &media))
        return false;
    ok = hlsPlaylistParse(media.data, url, start_ns, &g.list);
    netResponseFree(&media);
    snprintf(g.media_url, sizeof(g.media_url), "%s", url);
    g.base_ns = g.list.base_ns;
    g.grew_ns = monotonicNs();
    g.next_reread_ns = monotonicNs() + REREAD_NS;
    return ok;
}

int64_t hlsBaseNs(void)
{
    return g.running ? g.base_ns : 0;
}

bool hlsStart(const char *master_url, const char *authorization, int64_t start_ns, char *error, size_t error_size)
{
    hlsStop();
    memset(&g, 0, sizeof(g));
    pthread_mutex_init(&g.lock, NULL);
    pthread_cond_init(&g.wake, NULL);
    snprintf(g.authorization, sizeof(g.authorization), "%s", authorization);
    g.fetching = -1;
    g.interrupts = mediaFileInterrupts();
    g.net = netClientOpen();
    bool ok = g.net && readPlaylists(master_url, start_ns);
    g.running = ok && pthread_create(&g.thread, NULL, fetchThread, NULL) == 0;
    if (!g.running) {
        snprintf(error, error_size, "The server didn't start converting this video.");
        hlsPlaylistFree(&g.list);
        netClientClose(g.net);
        g.net = NULL;
        pthread_cond_destroy(&g.wake);
        pthread_mutex_destroy(&g.lock);
    }
    return g.running;
}

void hlsStop(void)
{
    if (!g.running)
        return;
    pthread_mutex_lock(&g.lock);
    g.stop = true;
    g.abort_fetch = true;
    pthread_cond_broadcast(&g.wake);
    pthread_mutex_unlock(&g.lock);
    pthread_join(g.thread, NULL);
    g.running = false;
    for (int i = 0; i < SLOTS; i++)
        free(g.slots[i].data);
    hlsPlaylistFree(&g.list);
    netClientClose(g.net);
    pthread_cond_destroy(&g.wake);
    pthread_mutex_destroy(&g.lock);
}

bool hlsFailed(void)
{
    if (!g.running)
        return false;
    pthread_mutex_lock(&g.lock);
    bool failed = g.failed;
    pthread_mutex_unlock(&g.lock);
    return failed;
}

bool hlsIsLive(void)
{
    if (!g.running)
        return false;
    pthread_mutex_lock(&g.lock);
    bool live = !g.list.ended;
    pthread_mutex_unlock(&g.lock);
    return live;
}

bool hlsCushionReady(int64_t from_ns)
{
    if (!g.running)
        return true;
    pthread_mutex_lock(&g.lock);
    // Live TV is made as it plays: a segment as long as the longest, and a
    // margin, must be there already, or playback catches up with the server.
    bool live = !g.list.ended;
    int64_t span = live ? g.list.longest_ns + LIVE_MARGIN_NS : CUSHION_NS;
    int first, last;
    hlsPlaylistSpan(&g.list, from_ns, span, &first, &last);
    if (last > first + AHEAD - 1)
        last = first + AHEAD - 1;  // Only this many are fetched ahead.
    bool ready = !live || from_ns + span <= g.list.end_ns;
    for (int i = first; i <= last && ready; i++) {
        int slot = findSlot(i);
        ready = slot >= 0 && g.slots[slot].state == SLOT_READY;
    }
    ready = ready || g.failed;
    pthread_mutex_unlock(&g.lock);
    return ready;
}

double hlsSecondsReady(int64_t from_ns)
{
    if (!g.running)
        return -1;
    pthread_mutex_lock(&g.lock);
    int first, last;
    hlsPlaylistSpan(&g.list, from_ns, 1, &first, &last);
    int64_t until = from_ns;
    for (int i = first; i < g.list.count; i++) {
        int slot = findSlot(i);
        if (slot < 0 || g.slots[slot].state != SLOT_READY)
            break;
        until = i + 1 < g.list.count ? g.list.starts_ns[i + 1] : until;  // The last one's end isn't known.
    }
    pthread_mutex_unlock(&g.lock);
    return until > from_ns ? (until - from_ns) / 1e9 : 0.0;
}

// --- What FFmpeg reads ---

typedef struct {
    char *data;
    size_t size, position;
} Memory;

static int memoryRead(void *opaque, uint8_t *buf, int size)
{
    Memory *m = opaque;
    size_t left = m->size - m->position;
    if (left == 0)
        return AVERROR_EOF;
    size_t n = (size_t)size < left ? (size_t)size : left;
    memcpy(buf, m->data + m->position, n);
    m->position += n;
    return (int)n;
}

static int64_t memorySeek(void *opaque, int64_t offset, int whence)
{
    Memory *m = opaque;
    if (whence & AVSEEK_SIZE)
        return (int64_t)m->size;
    int kind = whence & ~AVSEEK_FORCE;
    int64_t base = kind == SEEK_SET ? 0 : kind == SEEK_CUR ? (int64_t)m->position : (int64_t)m->size;
    if (base + offset < 0 || base + offset > (int64_t)m->size)
        return AVERROR(EINVAL);
    m->position = (size_t)(base + offset);
    return base + offset;
}

// Takes the copy of data.
static int openMemory(AVIOContext **pb, char *data, size_t size)
{
    Memory *m = calloc(1, sizeof(*m));
    uint8_t *buffer = av_malloc(IO_BUFFER);
    *pb = m && buffer ? avio_alloc_context(buffer, IO_BUFFER, 0, m, memoryRead, NULL, memorySeek) : NULL;
    if (!*pb) {
        av_free(buffer);
        free(m);
        free(data);
        return AVERROR(ENOMEM);
    }
    m->data = data;
    m->size = size;
    return 0;
}

static void noteReader(const void *owner, int segment)
{
    int r = 0;
    while (r < READERS - 1 && g.readers[r].owner && g.readers[r].owner != owner)
        r++;
    g.readers[r] = (Reader){owner, segment};
    if (g.fetching >= 0 && !wanted(g.fetching))
        g.abort_fetch = true;
    pthread_cond_broadcast(&g.wake);
}

// True if the reader has something better to do than wait. Its callback takes
// the reader's own lock, so ours isn't held meanwhile.
static bool readerInterrupted(const AVFormatContext *s)
{
    if (!s->interrupt_callback.callback)
        return false;
    pthread_mutex_unlock(&g.lock);
    bool yes = s->interrupt_callback.callback(s->interrupt_callback.opaque) != 0;
    pthread_mutex_lock(&g.lock);
    return yes;
}

// A copy of the segment, waiting for it. NULL if it won't be had, or the
// reader was interrupted. Under the lock.
static char *waitForSegment(const AVFormatContext *s, int index, size_t *size)
{
    for (;;) {
        int slot = findSlot(index);
        if (slot >= 0 && g.slots[slot].state == SLOT_READY) {
            char *copy = malloc(g.slots[slot].size ? g.slots[slot].size : 1);
            if (copy)
                memcpy(copy, g.slots[slot].data, g.slots[slot].size);
            *size = g.slots[slot].size;
            return copy;
        }
        if (g.stop || g.failed || mediaFileInterrupts() != g.interrupts || readerInterrupted(s))
            return NULL;
        struct timespec until;
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_nsec += WAIT_STEP_NS;
        if (until.tv_nsec >= NS_PER_S) {
            until.tv_sec++;
            until.tv_nsec -= NS_PER_S;
        }
        pthread_cond_timedwait(&g.wake, &g.lock, &until);
    }
}

static int ioOpen(AVFormatContext *s, AVIOContext **pb, const char *url, int flags, AVDictionary **options)
{
    (void)flags;
    (void)options;
    char *data = NULL;
    size_t size = 0;
    pthread_mutex_lock(&g.lock);
    if (!strcmp(url, HLS_LOCAL_INDEX)) {
        size = g.list.size;
        data = malloc(size + 1);
        if (data)
            memcpy(data, g.list.text, size);
    } else {
        int segment = hlsLocalSegment(url);
        if (segment >= 0 && segment < g.list.count) {
            noteReader(s, segment);
            data = waitForSegment(s, segment, &size);
        }
    }
    pthread_mutex_unlock(&g.lock);
    if (data)
        return openMemory(pb, data, size);
    bool interrupted = s->interrupt_callback.callback && s->interrupt_callback.callback(s->interrupt_callback.opaque);
    return interrupted ? AVERROR_EXIT : AVERROR(EIO);
}

static int ioClose(AVFormatContext *s, AVIOContext *pb)
{
    (void)s;
    if (!pb)
        return 0;
    Memory *m = pb->opaque;
    av_freep(&pb->buffer);
    avio_context_free(&pb);
    free(m->data);
    free(m);
    return 0;
}

bool hlsOpenInput(AVFormatContext **fmt, AVIOContext **playlist)
{
    if (!g.running || ioOpen(*fmt, playlist, HLS_LOCAL_INDEX, 0, NULL) < 0)
        return false;
    (*fmt)->pb = *playlist;
    (*fmt)->flags |= AVFMT_FLAG_CUSTOM_IO;
    (*fmt)->io_open = ioOpen;
    (*fmt)->io_close2 = ioClose;
    return avformat_open_input(fmt, HLS_LOCAL_INDEX, av_find_input_format("hls"), NULL) >= 0;
}

void hlsCloseInput(AVIOContext *playlist)
{
    ioClose(NULL, playlist);
}
