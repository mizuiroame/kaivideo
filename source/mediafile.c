#include "mediafile.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <libavformat/avio.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>

#include "blockcache.h"
#include "hlsio.h"
#include "jfsync.h"
#include "net.h"

#define FETCH_TRIES 3         // With the stall below: about 16 s without the server, then it's lost.
#define FETCH_STALL_S 5L
#define RETRY_WAIT_MS 300     // Doubles with each try.
#define WAIT_STEP_MS 50
#define NS_PER_MS 1000000L
#define HTTP_PARTIAL 206

#define REMOTE_READERS 3      // Picture, sound, and a sound being changed.

static unsigned g_interrupts;    // Files opened before the last interrupt give up.

typedef struct {
    pthread_t thread;
    bool thread_started;
    pthread_mutex_t lock;
    pthread_cond_t wake;  // The reader moved, a block came, or stop.
    NetClient *net;
    char url[JF_URL_MAX], authorization[JF_AUTH_MAX];
    BlockSlot slots[BLOCK_SLOTS];
    uint8_t *memory;       // BLOCK_SLOTS blocks.
    char location[LIBRARY_PATH_MAX];
    int64_t readers[REMOTE_READERS];     // The block each is at.
    bool reader_used[REMOTE_READERS];
    int users;
    int64_t total;         // The file's length; -1 until known.
    int64_t fetching;      // The block on its way, or -1.
    volatile bool abort_fetch;  // The reader went elsewhere, or stop.
    bool stop, failed;
    unsigned interrupts;   // g_interrupts when it was opened.
} Remote;

static pthread_mutex_t g_shared_lock = PTHREAD_MUTEX_INITIALIZER;
static Remote *g_shared;         // The file being fetched, for a second reader to join.

struct MediaFile {
    FILE *file;
    Remote *remote;
    int reader;                  // Which of its readers this is.
    int64_t position;
    bool read_failed;
};

// --- The thread that fetches ahead ---

typedef struct {
    uint8_t *to;
    size_t size;
} Filling;

static bool fillBlock(const void *data, size_t size, void *user)
{
    Filling *f = user;
    size_t room = BLOCK_SIZE - f->size;
    size_t n = size < room ? size : room;
    memcpy(f->to + f->size, data, n);
    f->size += n;
    return n == size;
}

static int64_t totalBlocks(const Remote *r)
{
    return r->total < 0 ? -1 : (r->total + BLOCK_SIZE - 1) / BLOCK_SIZE;
}

// Waits, stopping early if the fetch is called off.
static void waitBeforeRetry(const Remote *r, int ms)
{
    for (int waited = 0; waited < ms && !r->abort_fetch; waited += WAIT_STEP_MS)
        nanosleep(&(struct timespec){0, WAIT_STEP_MS * NS_PER_MS}, NULL);
}

// True if the answer is the piece asked for, whole.
static bool blockIsRight(long status, long long total, int64_t from, size_t size)
{
    if (status != HTTP_PARTIAL || total <= from)
        return false;
    long long left = total - from;
    return (long long)size == (left < BLOCK_SIZE ? left : BLOCK_SIZE);
}

// Fetches one block into its slot's memory. How many bytes, 0 on failure.
static size_t fetchBlock(Remote *r, int64_t index, uint8_t *to, long long *total)
{
    char range[64], error[NET_ERROR_MAX];
    int64_t from = index * BLOCK_SIZE;
    snprintf(range, sizeof(range), "%" PRId64 "-%" PRId64, from, from + BLOCK_SIZE - 1);
    NetRequest request = {.url = r->url, .authorization = r->authorization, .range = range,
                          .cancel = &r->abort_fetch, .total_size = total, .stall_s = FETCH_STALL_S};
    for (int attempt = 0; attempt < FETCH_TRIES && !r->abort_fetch; attempt++) {
        Filling filling = {to, 0};
        long status = 0;
        *total = -1;
        if (netFetch(r->net, &request, fillBlock, &filling, &status, error, sizeof(error)) &&
            blockIsRight(status, *total, from, filling.size))
            return filling.size;
        waitBeforeRetry(r, RETRY_WAIT_MS << attempt);
    }
    return 0;
}

// Where the readers are. Returns how many. Under the lock.
static int readerBlocks(const Remote *r, int64_t out[REMOTE_READERS])
{
    int count = 0;
    for (int i = 0; i < REMOTE_READERS; i++)
        if (r->reader_used[i])
            out[count++] = r->readers[i];
    return count;
}

// Waits for a block to fetch and a slot for it. False to stop. Under the lock.
static bool nextJob(Remote *r, int64_t *index, int *slot)
{
    while (!r->stop) {
        int64_t blocks[REMOTE_READERS];
        int count = readerBlocks(r, blocks);
        *index = r->failed ? -1 : blockNextWanted(r->slots, BLOCK_SLOTS, blocks, count, totalBlocks(r));
        *slot = *index >= 0 ? blockVictim(r->slots, BLOCK_SLOTS, blocks, count) : -1;
        if (*slot >= 0)
            return true;
        pthread_cond_wait(&r->wake, &r->lock);
    }
    return false;
}

static void *fetchThread(void *arg)
{
    Remote *r = arg;
    pthread_mutex_lock(&r->lock);
    int64_t index;
    int slot;
    while (nextJob(r, &index, &slot)) {
        r->slots[slot] = (BlockSlot){.index = index, .state = BLOCK_LOADING};
        r->fetching = index;
        r->abort_fetch = false;
        pthread_mutex_unlock(&r->lock);
        long long total = -1;
        size_t size = fetchBlock(r, index, r->memory + (size_t)slot * BLOCK_SIZE, &total);
        pthread_mutex_lock(&r->lock);
        r->fetching = -1;
        bool aborted = r->abort_fetch;
        if (total >= 0)
            r->total = total;
        r->slots[slot].state = size ? BLOCK_READY : BLOCK_EMPTY;
        r->slots[slot].size = size;
        if (!size && !aborted)
            r->failed = true;
        pthread_cond_broadcast(&r->wake);
    }
    pthread_mutex_unlock(&r->lock);
    return NULL;
}

// --- Reading ---

static void remoteFree(Remote *r)
{
    if (!r)
        return;
    if (r->thread_started) {
        pthread_mutex_lock(&r->lock);
        r->stop = true;
        r->abort_fetch = true;
        pthread_cond_broadcast(&r->wake);
        pthread_mutex_unlock(&r->lock);
        pthread_join(r->thread, NULL);
    }
    netClientClose(r->net);
    pthread_cond_destroy(&r->wake);
    pthread_mutex_destroy(&r->lock);
    free(r->memory);
    free(r);
}

// The reader is at this block now: what's being fetched elsewhere can stop.
static void moveReader(Remote *r, int reader, int64_t block)
{
    if (block == r->readers[reader])
        return;
    r->readers[reader] = block;
    r->failed = false;
    int64_t blocks[REMOTE_READERS];
    int count = readerBlocks(r, blocks);
    if (r->fetching >= 0 && !blockIsWanted(r->fetching, blocks, count))
        r->abort_fetch = true;
    pthread_cond_broadcast(&r->wake);
}

static bool interrupted(const Remote *r)
{
    return __atomic_load_n(&g_interrupts, __ATOMIC_RELAXED) != r->interrupts;
}

// The ready slot of a block, waiting for it. -1 if it can't come: what's
// already here is still given when a later block failed. Under the lock.
static int waitForBlock(Remote *r, int64_t block)
{
    for (;;) {
        int slot = blockFind(r->slots, BLOCK_SLOTS, block);
        if (slot >= 0 && r->slots[slot].state == BLOCK_READY)
            return slot;
        if (r->stop || r->failed)
            return -1;
        if (interrupted(r)) {
            r->abort_fetch = true;
            return -1;
        }
        struct timespec until;
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_nsec += WAIT_STEP_MS * NS_PER_MS;
        if (until.tv_nsec >= 1000000000L) {
            until.tv_sec++;
            until.tv_nsec -= 1000000000L;
        }
        pthread_cond_timedwait(&r->wake, &r->lock, &until);
    }
}

static int remoteRead(MediaFile *f, uint8_t *buf, int size)
{
    Remote *r = f->remote;
    int64_t block = f->position / BLOCK_SIZE;
    size_t offset = (size_t)(f->position % BLOCK_SIZE);
    pthread_mutex_lock(&r->lock);
    moveReader(r, f->reader, block);
    int result = AVERROR_EOF;
    if (r->total < 0 || f->position < r->total) {
        int slot = waitForBlock(r, block);
        if (slot < 0) {
            result = AVERROR(EIO);
            f->read_failed = r->failed;
        } else if (offset < r->slots[slot].size) {
            size_t left = r->slots[slot].size - offset;
            result = (size_t)size < left ? size : (int)left;
            memcpy(buf, r->memory + (size_t)slot * BLOCK_SIZE + offset, (size_t)result);
            f->position += result;
        }
    }
    pthread_mutex_unlock(&r->lock);
    return result;
}

// Starts fetching, and waits for the first block, which tells the length.
static Remote *remoteOpen(const char *location)
{
    Remote *r = calloc(1, sizeof(*r));
    if (!r)
        return NULL;
    pthread_mutex_init(&r->lock, NULL);
    pthread_cond_init(&r->wake, NULL);
    snprintf(r->location, sizeof(r->location), "%s", location);
    r->total = -1;
    r->fetching = -1;
    r->users = 1;
    r->reader_used[0] = true;
    r->interrupts = __atomic_load_n(&g_interrupts, __ATOMIC_RELAXED);
    r->memory = malloc((size_t)BLOCK_SLOTS * BLOCK_SIZE);
    r->net = netClientOpen();
    bool ok = r->memory && r->net && jfSyncStreamRequest(location, r->url, sizeof(r->url), r->authorization,
                                                         sizeof(r->authorization));
    r->thread_started = ok && pthread_create(&r->thread, NULL, fetchThread, r) == 0;
    if (!r->thread_started) {
        remoteFree(r);
        return NULL;
    }
    pthread_mutex_lock(&r->lock);
    int slot = waitForBlock(r, 0);
    ok = slot >= 0 && r->total >= 0;
    pthread_mutex_unlock(&r->lock);
    if (!ok) {
        remoteFree(r);
        return NULL;
    }
    return r;
}

// Another reader of the file already being fetched, if it is. Under g_shared_lock.
static bool joinShared(MediaFile *f, const char *location)
{
    Remote *r = g_shared;
    if (!r || strcmp(r->location, location) || interrupted(r))
        return false;
    pthread_mutex_lock(&r->lock);
    bool joined = false;
    for (int i = 0; i < REMOTE_READERS && !joined; i++)
        if (!r->reader_used[i]) {
            r->reader_used[i] = true;
            r->readers[i] = 0;
            r->users++;
            f->reader = i;
            joined = true;
        }
    pthread_cond_broadcast(&r->wake);
    pthread_mutex_unlock(&r->lock);
    if (joined)
        f->remote = r;
    return joined;
}

// Picture and sound read the same file at about the same place: one download serves both.
static void remoteAttach(MediaFile *f, const char *location)
{
    pthread_mutex_lock(&g_shared_lock);
    bool joined = joinShared(f, location);
    pthread_mutex_unlock(&g_shared_lock);
    if (joined)
        return;
    f->remote = remoteOpen(location);
    f->reader = 0;
    pthread_mutex_lock(&g_shared_lock);
    if (f->remote && !g_shared)
        g_shared = f->remote;
    pthread_mutex_unlock(&g_shared_lock);
}

static void remoteDetach(MediaFile *f)
{
    Remote *r = f->remote;
    if (!r)
        return;
    pthread_mutex_lock(&g_shared_lock);
    pthread_mutex_lock(&r->lock);
    r->reader_used[f->reader] = false;
    bool last = --r->users == 0;
    pthread_cond_broadcast(&r->wake);
    pthread_mutex_unlock(&r->lock);
    if (last && g_shared == r)
        g_shared = NULL;
    pthread_mutex_unlock(&g_shared_lock);
    if (last)
        remoteFree(r);
}

MediaFile *mediaFileOpen(const char *location)
{
    MediaFile *f = calloc(1, sizeof(*f));
    if (!f)
        return NULL;
    if (jfIsRemoteLocation(location))
        remoteAttach(f, location);
    else
        f->file = fopen(location, "rb");
    if (!f->file && !f->remote) {
        free(f);
        return NULL;
    }
    return f;
}

double mediaFileMegabytesReady(void)
{
    pthread_mutex_lock(&g_shared_lock);
    Remote *r = g_shared;
    double megabytes = -1;
    if (r) {
        pthread_mutex_lock(&r->lock);
        int64_t blocks[REMOTE_READERS], from = INT64_MAX;
        int count = readerBlocks(r, blocks);
        for (int i = 0; i < count; i++)
            from = blocks[i] < from ? blocks[i] : from;
        size_t bytes = 0;
        for (int64_t b = from; count; b++) {
            int slot = blockFind(r->slots, BLOCK_SLOTS, b);
            if (slot < 0 || r->slots[slot].state != BLOCK_READY)
                break;
            bytes += r->slots[slot].size;
        }
        pthread_mutex_unlock(&r->lock);
        megabytes = bytes / 1e6;
    }
    pthread_mutex_unlock(&g_shared_lock);
    return megabytes;
}

void mediaFileInterruptAll(void)
{
    __atomic_add_fetch(&g_interrupts, 1, __ATOMIC_RELAXED);
}

unsigned mediaFileInterrupts(void)
{
    return __atomic_load_n(&g_interrupts, __ATOMIC_RELAXED);
}

bool mediaFileFailed(const MediaFile *f)
{
    return f ? f->read_failed : hlsFailed();
}

static bool openFileInput(MediaInput *in, AVFormatContext **fmt, const char *location, int buffer_size)
{
    in->file = mediaFileOpen(location);
    uint8_t *buffer = in->file ? av_malloc((size_t)buffer_size) : NULL;
    in->io = buffer ? avio_alloc_context(buffer, buffer_size, 0, in->file, mediaFileRead, NULL, mediaFileSeek) : NULL;
    if (!in->io) {
        av_free(buffer);
        return false;
    }
    (*fmt)->pb = in->io;
    (*fmt)->flags |= AVFMT_FLAG_CUSTOM_IO;
    return avformat_open_input(fmt, NULL, NULL, NULL) >= 0;
}

// Where a converted stream's time 0 is, in AV_TIME_BASE: its picture's start.
// A conversion begun mid-video starts its sound seconds before its picture
// (sound from before the place asked for), so the earliest track won't do:
// the picture is where the place asked for begins. Without one, the earliest.
static int64_t streamsStart(const AVFormatContext *fmt)
{
    for (unsigned i = 0; i < fmt->nb_streams; i++) {
        const AVStream *st = fmt->streams[i];
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && st->start_time != AV_NOPTS_VALUE &&
            !(st->disposition & AV_DISPOSITION_ATTACHED_PIC))
            return av_rescale_q(st->start_time, st->time_base, AV_TIME_BASE_Q);
    }
    return fmt->start_time;
}

bool mediaInputOpen(MediaInput *in, AVFormatContext **fmt, const char *location, int buffer_size)
{
    memset(in, 0, sizeof(*in));
    bool converted = hlsIsLocation(location);
    bool ok = converted ? hlsOpenInput(fmt, &in->playlist, location) : openFileInput(in, fmt, location, buffer_size);
    if (!ok || avformat_find_stream_info(*fmt, NULL) < 0)
        return false;
    if (converted) {
        // FFmpeg keeps an HLS track's own name (NAME="Original") as its
        // comment: it's the name the lists show.
        for (unsigned i = 0; i < (*fmt)->nb_streams; i++) {
            AVDictionary **tags = &(*fmt)->streams[i]->metadata;
            const AVDictionaryEntry *name = av_dict_get(*tags, "comment", NULL, 0);
            if (name && name->value[0] && !av_dict_get(*tags, "title", NULL, 0))
                av_dict_set(tags, "title", name->value, 0);
        }
        in->skipped_us = hlsLocationBaseNs(location) / 1000;
        int64_t start = streamsStart(*fmt);
        if (start != AV_NOPTS_VALUE)
            in->start_us = start - in->skipped_us;
        if (start != AV_NOPTS_VALUE && (*fmt)->start_time != AV_NOPTS_VALUE && start > (*fmt)->start_time)
            in->sound_lead_us = start - (*fmt)->start_time;
    } else if (!strcmp((*fmt)->iformat->name, "mpegts")) {
        // A server's conversion kept on the card (its parts joined) keeps the
        // conversion's times, which don't begin at 0: its picture's start is 0.
        int64_t start = streamsStart(*fmt);
        if (start != AV_NOPTS_VALUE && start > 0)
            in->start_us = start;
    }
    return true;
}

void mediaInputClose(MediaInput *in)
{
    if (in->io) {
        av_freep(&in->io->buffer);
        avio_context_free(&in->io);
    }
    hlsCloseInput(in->playlist);
    in->playlist = NULL;
    mediaFileClose(in->file);
    in->file = NULL;
}

void mediaFileClose(MediaFile *f)
{
    if (!f)
        return;
    if (f->file)
        fclose(f->file);
    remoteDetach(f);
    free(f);
}

int mediaFileRead(void *opaque, uint8_t *buf, int size)
{
    MediaFile *f = opaque;
    if (f->remote)
        return remoteRead(f, buf, size);
    size_t n = fread(buf, 1, (size_t)size, f->file);
    return n > 0 ? (int)n : AVERROR_EOF;
}

static int64_t fileSeek(FILE *file, int64_t offset, int whence)
{
    if (whence & AVSEEK_SIZE) {
        long here = ftell(file);
        fseek(file, 0, SEEK_END);
        long size = ftell(file);
        fseek(file, here, SEEK_SET);
        return size;
    }
    return fseek(file, (long)offset, whence & ~AVSEEK_FORCE) == 0 ? ftell(file) : -1;
}

int64_t mediaFileSeek(void *opaque, int64_t offset, int whence)
{
    MediaFile *f = opaque;
    if (f->file)
        return fileSeek(f->file, offset, whence);
    pthread_mutex_lock(&f->remote->lock);
    int64_t total = f->remote->total;
    pthread_mutex_unlock(&f->remote->lock);
    if (whence & AVSEEK_SIZE)
        return total;
    int kind = whence & ~AVSEEK_FORCE;
    int64_t base = kind == SEEK_SET ? 0 : kind == SEEK_CUR ? f->position : total;
    if (offset + base < 0)
        return -1;
    f->position = offset + base;
    return f->position;
}
