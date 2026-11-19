#include "hlsio.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <libavformat/avio.h>
#include <libavutil/mem.h>

#include "hlslist.h"
#include "hlsmaster.h"
#include "mediafile.h"
#include "net.h"

#define SLOTS 24
#define READERS 16            // The picture's and the sound's readers, each on any of the lists.
#define AHEAD 4               // Segments kept ready from where each reads.
#define AHEAD_SUBTITLES 2     // Subtitles are small, and there may be several languages.
#define CUSHION_NS 4000000000ll  // Video in memory before playback starts or goes on from a jump.
#define LIVE_MARGIN_NS 2000000000ll  // Live TV: this much more than a segment ahead before starting.
#define IO_BUFFER (64 * 1024)
#define PLAYLIST_MAX (8u * 1024 * 1024)
#define SEGMENT_MAX (96u * 1024 * 1024)
#define MASTER_MAX (16u * 1024)
#define PLAYLIST_TIMEOUT_S 30L
#define DIRECT_TIMEOUT_S 8L   // A channel's own source: if it doesn't answer soon, the server converts instead.
#define SEGMENT_STALL_S 40L   // The server may need a while to start converting.
#define FETCH_TRIES 3
#define RETRY_WAIT_NS 500000000L
#define WAIT_STEP_NS 50000000L
#define NS_PER_S 1000000000L
#define REREAD_NS 3000000000ull  // A growing list is read again this often (its segments are ~8 s).
#define LIVE_STALL_NS 30000000000ull  // A live list that stops growing this long: the channel stopped.
#define SUBTITLE_WAIT_NS 1500000000ull  // A subtitle segment not here by then is skipped.
#define SUBTITLE_QUEUE 8      // Subtitle segments fetched and not yet taken by the player.
#define SUBTITLES_BEHIND 1    // Subtitle segments fetched from before the picture's.
#define SUBTITLES_AHEAD 3     // And this many after it, before the picture's own: they're small, and
                              // live captions run late, so shown earlier (a delay below 0) they must be here.
#define LIVE_KEEP 6           // A live list starts with this many of its last segments (FFmpeg starts 3 from the end).
#define LIVE_KEEP_BEHIND 2    // And then keeps this many behind its earliest reader.

typedef enum { SLOT_EMPTY, SLOT_LOADING, SLOT_READY } SlotState;

typedef struct {
    int list, index;
    SlotState state;
    char *data;
    size_t size;
} Slot;

typedef struct {
    const void *owner;   // Its AVFormatContext.
    int list, segment;
} Reader;

typedef struct {
    HlsPlaylist list;
    char url[HLS_URL_MAX];  // Read again while it grows (live TV).
    bool loaded;
} MediaList;

// List 0 is the video's (and, from a server, the only one); a channel's own
// master list can add its other sound tracks and subtitles as lists 1 on.
static struct {
    bool running;
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t wake;
    NetClient *net;
    char authorization[512];
    MediaList lists[HLS_LISTS_MAX];
    int list_count;
    char *master_text;            // What FFmpeg opens first when there are several lists, else NULL.
    size_t master_size;
    Slot slots[SLOTS];
    Reader readers[READERS];
    int fetch_list, fetch_index;  // The segment being fetched, index -1 for none.
    unsigned interrupts;
    volatile bool abort_fetch;
    bool stop, failed;
    long long max_bitrate;        // For choosing a master list's variant; 0: no limit.
    long playlist_timeout_s;
    const volatile bool *start_cancel;  // The caller's, while starting; may be NULL.
    int64_t base_ns;              // The video list's start: set at start, read without the lock.
    uint64_t grew_ns;             // When the live video list last grew.
    uint64_t next_reread_ns;
    // Subtitles of a channel's own (FFmpeg's HLS reader takes none): their
    // lists are read only once chosen, and their segments handed over as they come.
    HlsMaster master;
    int subtitle_lists[HLS_LISTS_MAX];  // Each subtitle track's list.
    int subtitle_count;
    int subtitle_list;            // The chosen one's list, 0 for none.
    unsigned subtitle_choice;     // Counts choices: a segment fetched for an older one is dropped.
    int subtitle_next;            // Its next segment to fetch, -1 until placed by the picture.
    struct {
        char *data;
        size_t size;
    } subtitle_queue[SUBTITLE_QUEUE];
    int subtitle_queued;
    int first_segment;            // The first video segment read, -1 before one is: playback's 0.
    int64_t playback_ns;          // Where playback is (hlsSetPlayback), -1 until told.
} g;

#define VIDEO (g.lists[0].list)

bool hlsIsLocation(const char *path)
{
    return strncmp(path, HLS_LOCATION, sizeof(HLS_LOCATION) - 1) == 0;
}

// --- What to fetch ---

static int aheadOf(int list)
{
    return g.lists[list].list.subtitles ? AHEAD_SUBTITLES : AHEAD;
}

static int findSlot(int list, int index)
{
    for (int i = 0; i < SLOTS; i++)
        if (g.slots[i].state != SLOT_EMPTY && g.slots[i].list == list && g.slots[i].index == index)
            return i;
    return -1;
}

static bool wanted(int list, int index)
{
    for (int r = 0; r < READERS; r++) {
        const Reader *reader = &g.readers[r];
        if (reader->owner && reader->list == list && index >= reader->segment &&
            index < reader->segment + aheadOf(list))
            return true;
    }
    return false;
}

// The nearest missing segment to any reader: false if there's none.
static bool nextSegment(int *list, int *index)
{
    for (int distance = 0; distance < AHEAD; distance++)
        for (int r = 0; r < READERS; r++) {
            const Reader *reader = &g.readers[r];
            if (!reader->owner || distance >= aheadOf(reader->list))
                continue;
            int at = reader->segment + distance;
            if (at < g.lists[reader->list].list.count && findSlot(reader->list, at) < 0) {
                *list = reader->list;
                *index = at;
                return true;
            }
        }
    return false;
}

static int freeSlot(void)
{
    int victim = -1;
    for (int i = 0; i < SLOTS; i++) {
        if (g.slots[i].state == SLOT_EMPTY)
            return i;
        if (g.slots[i].state == SLOT_READY && !wanted(g.slots[i].list, g.slots[i].index) &&
            (victim < 0 || g.slots[i].index < g.slots[victim].index))
            victim = i;
    }
    return victim;
}

// --- The thread ---

// The server's sign-in, or none: a channel's own source never gets it.
static const char *authorization(void)
{
    return g.authorization[0] ? g.authorization : NULL;
}

static bool fetchSegment(int list, int index, NetResponse *r)
{
    NetRequest request = {.url = g.lists[list].list.urls[index], .authorization = authorization(),
                          .cancel = &g.abort_fetch, .stall_s = SEGMENT_STALL_S};
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

// The earliest segment any reader of a list is at, -1 if none reads it.
static int earliestReader(int list)
{
    int at = -1;
    for (int r = 0; r < READERS; r++)
        if (g.readers[r].owner && g.readers[r].list == list && (at < 0 || g.readers[r].segment < at))
            at = g.readers[r].segment;
    return at;
}

// A live list's text keeps only what's near its readers: a channel's list
// can hold hours, and FFmpeg reads the whole text each time. Subtitles are
// read by KaiVideo itself, from the segments' addresses: they need none.
static void trimLive(int list)
{
    HlsPlaylist *p = &g.lists[list].list;
    if (p->ended)
        return;
    if (p->subtitles) {
        hlsPlaylistTrim(p, p->count);
        return;
    }
    int reader = earliestReader(list);
    if (reader >= 0)
        hlsPlaylistTrim(p, reader - LIVE_KEEP_BEHIND);
}

// Live lists: read them again, and take what was added (segments keep their numbers). Under the lock.
static void reread(void)
{
    for (int i = 0; i < g.list_count && !g.stop; i++) {
        bool unchosen = g.lists[i].list.subtitles && i != g.subtitle_list;  // Read again only while shown.
        if (!g.lists[i].loaded || g.lists[i].list.ended || unchosen)
            continue;
        g.abort_fetch = false;  // Left set by a fetch called off; stopping sets it again.
        char url[HLS_URL_MAX];
        snprintf(url, sizeof(url), "%s", g.lists[i].url);
        pthread_mutex_unlock(&g.lock);
        NetResponse media;
        bool ok = getText(url, &media);
        pthread_mutex_lock(&g.lock);
        if (!ok)
            continue;
        // New segments go at the end, by their numbers: a channel's own list
        // drops its old ones as it goes, the server's keeps them all.
        int added = hlsPlaylistAppend(&g.lists[i].list, media.data, url);
        netResponseFree(&media);
        trimLive(i);
        if (i == 0 && added > 0)
            g.grew_ns = monotonicNs();
        if (added >= 0)
            pthread_cond_broadcast(&g.wake);
    }
    g.next_reread_ns = monotonicNs() + REREAD_NS;  // From when this one ended: long lists take a while.
    if (!VIDEO.ended && monotonicNs() - g.grew_ns > LIVE_STALL_NS) {
        g.failed = true;  // Readers waiting for more give up: playback ends, and says so.
        pthread_cond_broadcast(&g.wake);
    }
}

// Waits for something to do; a growing list is read again on time. Under the lock.
static void waitForWork(void)
{
    if (VIDEO.ended) {
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

// --- Subtitles of a channel's own ---

// The segment being shown, by media sequence: playback's time 0 is the start
// of the first segment read (readers run ahead of what shows). -1 before it's known.
static int64_t playbackSequence(void)
{
    if (g.first_segment < 0 || g.first_segment >= VIDEO.count || g.playback_ns < 0)
        return -1;
    int first, last;
    hlsPlaylistSpan(&VIDEO, VIDEO.starts_ns[g.first_segment] + g.playback_ns, 1, &first, &last);
    return VIDEO.sequence + first;
}

void hlsSetPlayback(int64_t position_ns)
{
    if (!g.running)
        return;
    pthread_mutex_lock(&g.lock);
    g.playback_ns = position_ns;
    pthread_mutex_unlock(&g.lock);
}

// Reads the chosen subtitles' list, the first time. Under the lock.
static void loadSubtitleList(int list, unsigned choice)
{
    char url[HLS_URL_MAX];
    snprintf(url, sizeof(url), "%s", hlsMasterListUrl(&g.master, list));
    g.abort_fetch = false;
    pthread_mutex_unlock(&g.lock);
    NetResponse r;
    HlsPlaylist fresh;
    bool ok = getText(url, &r);
    if (ok) {
        ok = hlsPlaylistParseList(r.data, url, 0, list, true, &fresh);
        netResponseFree(&r);
    }
    if (ok)
        hlsPlaylistTrim(&fresh, fresh.count);  // No text needed: KaiVideo reads its segments itself.
    pthread_mutex_lock(&g.lock);
    MediaList *m = &g.lists[list];
    if (ok && choice == g.subtitle_choice) {
        hlsPlaylistFree(&m->list);
        m->list = fresh;
        snprintf(m->url, sizeof(m->url), "%s", url);
        m->loaded = true;
        g.subtitle_next = -1;
    } else if (ok) {
        hlsPlaylistFree(&fresh);  // Another was chosen meanwhile.
    } else if (choice == g.subtitle_choice) {
        g.subtitle_list = 0;  // It can't be had: none shows.
    }
}

// Fetches the chosen subtitles' next segment, near the picture's (by media
// sequence), for the player to take. False if there's nothing to do. Under the lock.
static bool subtitleWork(void)
{
    int list = g.subtitle_list;
    unsigned choice = g.subtitle_choice;
    if (!list || g.stop)
        return false;
    MediaList *m = &g.lists[list];
    if (!m->loaded) {
        loadSubtitleList(list, choice);
        return true;
    }
    int64_t sequence = playbackSequence();
    if (sequence < 0)
        return false;
    int at = (int)(sequence - m->list.sequence), from = at - SUBTITLES_BEHIND > 0 ? at - SUBTITLES_BEHIND : 0;
    if (g.subtitle_next < from)
        g.subtitle_next = from;  // Placed, or caught up after falling behind.
    int index = g.subtitle_next;
    if (index >= m->list.count || index > at + SUBTITLES_AHEAD || g.subtitle_queued >= SUBTITLE_QUEUE)
        return false;
    char url[HLS_URL_MAX];
    snprintf(url, sizeof(url), "%s", m->list.urls[index]);
    g.abort_fetch = false;
    pthread_mutex_unlock(&g.lock);
    NetResponse r;
    NetRequest request = {.url = url, .authorization = authorization(), .cancel = &g.abort_fetch,
                          .timeout_s = PLAYLIST_TIMEOUT_S};
    bool ok = netFetchText(g.net, &request, PLAYLIST_MAX, &r);
    pthread_mutex_lock(&g.lock);
    if (choice != g.subtitle_choice) {
        if (ok)
            netResponseFree(&r);
        return true;
    }
    g.subtitle_next = index + 1;  // One that failed is skipped: the next lines matter more.
    if (ok) {
        g.subtitle_queue[g.subtitle_queued].data = r.data;
        g.subtitle_queue[g.subtitle_queued].size = r.size;
        g.subtitle_queued++;
    }
    return true;
}

// The subtitles of what shows now haven't come: they go before the picture's
// segments further ahead. Under the lock.
static bool subtitleUrgent(void)
{
    if (!g.subtitle_list || g.subtitle_queued >= SUBTITLE_QUEUE)
        return false;
    const MediaList *m = &g.lists[g.subtitle_list];
    if (!m->loaded)
        return true;
    int64_t sequence = playbackSequence();
    if (sequence < 0)
        return false;
    int at = (int)(sequence - m->list.sequence);
    return g.subtitle_next <= at + SUBTITLES_AHEAD && g.subtitle_next < m->list.count;
}

static void dropSubtitleQueue(void)
{
    for (int i = 0; i < g.subtitle_queued; i++)
        free(g.subtitle_queue[i].data);
    g.subtitle_queued = 0;
}

static const HlsRendition *subtitleRendition(int track)
{
    if (!g.running || track < 0 || track >= g.subtitle_count)
        return NULL;
    for (int i = 0; i < g.master.count; i++)
        if (g.master.renditions[i].list == g.subtitle_lists[track])
            return &g.master.renditions[i];
    return NULL;
}

int hlsSubtitleCount(void)
{
    return g.running ? g.subtitle_count : 0;
}

const char *hlsSubtitleName(int track)
{
    const HlsRendition *r = subtitleRendition(track);
    return r ? r->name : "";
}

const char *hlsSubtitleLanguage(int track)
{
    const HlsRendition *r = subtitleRendition(track);
    return r ? r->language : "";
}

void hlsSelectSubtitle(int track)
{
    if (!g.running)
        return;
    pthread_mutex_lock(&g.lock);
    int list = track >= 0 && track < g.subtitle_count ? g.subtitle_lists[track] : 0;
    if (list != g.subtitle_list) {
        g.subtitle_list = list;
        g.subtitle_choice++;
        g.subtitle_next = -1;
        dropSubtitleQueue();
        pthread_cond_broadcast(&g.wake);
    }
    pthread_mutex_unlock(&g.lock);
}

bool hlsTakeSubtitle(char **data, size_t *size)
{
    if (!g.running)
        return false;
    pthread_mutex_lock(&g.lock);
    bool any = g.subtitle_queued > 0;
    if (any) {
        *data = g.subtitle_queue[0].data;
        *size = g.subtitle_queue[0].size;
        memmove(&g.subtitle_queue[0], &g.subtitle_queue[1], (size_t)(g.subtitle_queued - 1) * sizeof(g.subtitle_queue[0]));
        g.subtitle_queued--;
        pthread_cond_broadcast(&g.wake);  // Room for the next.
    }
    pthread_mutex_unlock(&g.lock);
    return any;
}

// --- The fetching thread ---

static void *fetchThread(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&g.lock);
    while (!g.stop) {
        if (!VIDEO.ended && monotonicNs() >= g.next_reread_ns) {
            reread();
            continue;
        }
        if (!g.failed && subtitleUrgent() && subtitleWork())
            continue;
        int list = 0, index = -1;
        int slot = !g.failed && nextSegment(&list, &index) ? freeSlot() : -1;
        if (slot < 0) {
            if (!g.failed && subtitleWork())
                continue;
            waitForWork();
            continue;
        }
        free(g.slots[slot].data);
        g.slots[slot] = (Slot){.list = list, .index = index, .state = SLOT_LOADING};
        g.fetch_list = list;
        g.fetch_index = index;
        g.abort_fetch = false;
        pthread_mutex_unlock(&g.lock);
        NetResponse r;
        bool ok = fetchSegment(list, index, &r);
        pthread_mutex_lock(&g.lock);
        bool aborted = g.abort_fetch;
        g.fetch_index = -1;
        g.slots[slot].state = ok ? SLOT_READY : SLOT_EMPTY;
        g.slots[slot].data = ok ? r.data : NULL;
        g.slots[slot].size = ok ? r.size : 0;
        if (!ok && !aborted && list == 0)
            g.failed = true;  // A track of its own that fails leaves the picture playing.
        pthread_cond_broadcast(&g.wake);
    }
    pthread_mutex_unlock(&g.lock);
    return NULL;
}

// --- Starting ---

static bool getText(const char *url, NetResponse *r)
{
    // While starting, the caller's cancel cuts it short; afterwards, stopping does.
    const volatile bool *cancel = !g.running && g.start_cancel ? g.start_cancel : &g.abort_fetch;
    NetRequest request = {.url = url, .authorization = authorization(), .cancel = cancel,
                          .timeout_s = g.playlist_timeout_s};
    return netFetchText(g.net, &request, PLAYLIST_MAX, r);
}

// Reads list number list (-1: the only one) from url into g.lists.
static bool loadList(int slot, int list, bool subtitles, const char *url, int64_t start_ns)
{
    NetResponse media;
    if (!getText(url, &media))
        return false;
    MediaList *m = &g.lists[slot];
    bool ok = hlsPlaylistParseList(media.data, url, start_ns, list, subtitles, &m->list);
    netResponseFree(&media);
    if (ok && !m->list.ended)  // Live: what FFmpeg is told of starts near the end.
        hlsPlaylistTrim(&m->list, m->list.count - LIVE_KEEP);
    snprintf(m->url, sizeof(m->url), "%s", url);
    m->loaded = ok;
    return ok;
}

// The master list's other sound tracks, as lists 1 on: those that can't be
// read are left out of the master list FFmpeg gets. Subtitles stay out of it
// (FFmpeg's HLS reader takes none): their lists are read once chosen.
static void loadRenditions(void)
{
    static HlsMaster local;  // What FFmpeg is told of.
    local = g.master;
    local.count = 0;
    g.list_count = g.master.list_count;
    for (int i = 0; i < g.master.count; i++) {
        const HlsRendition *r = &g.master.renditions[i];
        if (r->type == HLS_RENDITION_SUBTITLES) {
            g.subtitle_lists[g.subtitle_count++] = r->list;
            continue;
        }
        if (r->list)
            loadList(r->list, r->list, false, r->url, 0);
        if (r->list == 0 || g.lists[r->list].loaded)
            local.renditions[local.count++] = *r;
    }
    g.master_text = malloc(MASTER_MAX);
    if (g.master_text && hlsLocalMaster(&local, g.master_text, MASTER_MAX)) {
        g.master_size = strlen(g.master_text);
    } else {
        free(g.master_text);
        g.master_text = NULL;
    }
}

// The media playlist behind the master one (its best variant) into list 0,
// with the variant's own sound tracks and subtitles, if it has any, as more
// lists. A list that has segments already is the media one itself.
static bool readPlaylists(const char *master_url, int64_t start_ns)
{
    NetResponse first;
    if (!getText(master_url, &first))
        return false;
    HlsMaster *master = &g.master;
    bool is_master = hlsMasterParse(first.data, master_url, g.max_bitrate, master);
    bool several = is_master && master->list_count > 1;
    bool ok;
    g.list_count = 1;
    if (!is_master) {  // Already the one with the segments.
        ok = hlsPlaylistParse(first.data, master_url, start_ns, &VIDEO);
        if (ok && !VIDEO.ended)  // Live: what FFmpeg is told of starts near the end.
            hlsPlaylistTrim(&VIDEO, VIDEO.count - LIVE_KEEP);
        snprintf(g.lists[0].url, sizeof(g.lists[0].url), "%s", master_url);
        g.lists[0].loaded = ok;
        netResponseFree(&first);
    } else {
        netResponseFree(&first);
        ok = loadList(0, several ? 0 : -1, false, master->variant_url, start_ns);
        if (ok && several)
            loadRenditions();
    }
    g.base_ns = VIDEO.base_ns;
    g.grew_ns = monotonicNs();
    g.next_reread_ns = monotonicNs() + REREAD_NS;
    return ok;
}

int64_t hlsBaseNs(void)
{
    return g.running ? g.base_ns : 0;
}

static void freeLists(void)
{
    for (int i = 0; i < HLS_LISTS_MAX; i++)
        hlsPlaylistFree(&g.lists[i].list);
    free(g.master_text);
    g.master_text = NULL;
}

static bool start(const char *master_url, const char *auth, long long max_bitrate, long timeout_s, int64_t start_ns,
                  const volatile bool *cancel)
{
    hlsStop();
    memset(&g, 0, sizeof(g));
    pthread_mutex_init(&g.lock, NULL);
    pthread_cond_init(&g.wake, NULL);
    g.start_cancel = cancel;
    snprintf(g.authorization, sizeof(g.authorization), "%s", auth ? auth : "");
    g.max_bitrate = max_bitrate;
    g.playlist_timeout_s = timeout_s;
    g.fetch_index = -1;
    g.first_segment = -1;
    g.playback_ns = -1;
    g.interrupts = mediaFileInterrupts();
    g.net = netClientOpen();
    bool ok = g.net && readPlaylists(master_url, start_ns);
    g.running = ok && pthread_create(&g.thread, NULL, fetchThread, NULL) == 0;
    if (!g.running) {
        freeLists();
        netClientClose(g.net);
        g.net = NULL;
        pthread_cond_destroy(&g.wake);
        pthread_mutex_destroy(&g.lock);
    }
    return g.running;
}

bool hlsStartDirect(const char *url, long long max_bitrate, const volatile bool *cancel, char *error,
                    size_t error_size)
{
    bool ok = start(url, NULL, max_bitrate, DIRECT_TIMEOUT_S, 0, cancel);
    if (!ok)
        snprintf(error, error_size, "The channel's own stream can't be read.");
    return ok;
}

bool hlsStart(const char *master_url, const char *authorization, int64_t start_ns, const volatile bool *cancel,
              char *error, size_t error_size)
{
    bool ok = start(master_url, authorization, 0, PLAYLIST_TIMEOUT_S, start_ns, cancel);
    if (!ok)
        snprintf(error, error_size, "The server didn't start converting this video.");
    return ok;
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
    dropSubtitleQueue();
    freeLists();
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
    bool live = !VIDEO.ended;
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
    bool live = !VIDEO.ended;
    int64_t span = live ? VIDEO.longest_ns + LIVE_MARGIN_NS : CUSHION_NS;
    int first, last;
    hlsPlaylistSpan(&VIDEO, from_ns, span, &first, &last);
    if (last > first + AHEAD - 1)
        last = first + AHEAD - 1;  // Only this many are fetched ahead.
    bool ready = !live || from_ns + span <= VIDEO.end_ns;
    for (int i = first; i <= last && ready; i++) {
        int slot = findSlot(0, i);
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
    hlsPlaylistSpan(&VIDEO, from_ns, 1, &first, &last);
    int64_t until = from_ns;
    for (int i = first; i < VIDEO.count; i++) {
        int slot = findSlot(0, i);
        if (slot < 0 || g.slots[slot].state != SLOT_READY)
            break;
        until = i + 1 < VIDEO.count ? VIDEO.starts_ns[i + 1] : until;  // The last one's end isn't known.
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

static void noteReader(const void *owner, int list, int segment)
{
    if (list == 0 && g.first_segment < 0)
        g.first_segment = segment;  // Where FFmpeg's times, and so playback's, begin.
    int r = 0;
    while (r < READERS - 1 && g.readers[r].owner && (g.readers[r].owner != owner || g.readers[r].list != list))
        r++;
    g.readers[r] = (Reader){owner, list, segment};
    if (g.fetch_index >= 0 && !wanted(g.fetch_list, g.fetch_index))
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
static char *waitForSegment(const AVFormatContext *s, int list, int index, size_t *size)
{
    // The picture's reader also reads the subtitles: one late must not hold it up.
    uint64_t give_up = g.lists[list].list.subtitles ? monotonicNs() + SUBTITLE_WAIT_NS : 0;
    for (;;) {
        int slot = findSlot(list, index);
        if (slot >= 0 && g.slots[slot].state == SLOT_READY) {
            char *copy = malloc(g.slots[slot].size ? g.slots[slot].size : 1);
            if (copy)
                memcpy(copy, g.slots[slot].data, g.slots[slot].size);
            *size = g.slots[slot].size;
            return copy;
        }
        if (g.stop || g.failed || mediaFileInterrupts() != g.interrupts || readerInterrupted(s))
            return NULL;
        if (give_up && monotonicNs() >= give_up)
            return NULL;  // FFmpeg goes on to the next subtitle segment.
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

// A copy of text, for FFmpeg to read.
static char *copyText(const char *text, size_t size)
{
    char *data = malloc(size + 1);
    if (data)
        memcpy(data, text, size);
    return data;
}

static int ioOpen(AVFormatContext *s, AVIOContext **pb, const char *url, int flags, AVDictionary **options)
{
    (void)flags;
    (void)options;
    char *data = NULL;
    size_t size = 0;
    int list, index;
    pthread_mutex_lock(&g.lock);
    if (!strcmp(url, HLS_LOCAL_INDEX)) {  // The master list, or the only list.
        bool master = g.master_text != NULL;
        size = master ? g.master_size : VIDEO.size;
        data = copyText(master ? g.master_text : VIDEO.text, size);
    } else if (hlsLocalName(url, &list, &index) && list >= 0 && list < g.list_count && g.lists[list].loaded) {
        HlsPlaylist *p = &g.lists[list].list;
        if (index < 0) {
            size = p->size;
            data = copyText(p->text, size);
        } else if (index < p->count) {
            noteReader(s, list, index);
            data = waitForSegment(s, list, index, &size);
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
