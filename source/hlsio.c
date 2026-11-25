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
#include "lang.h"
#include "mediafile.h"
#include "net.h"
#include "tsstream.h"

#define SLOTS 24
#define READERS 16            // The picture's and the sound's readers, each on any of the lists.
#define AHEAD 4               // Segments kept ready from where each reads.
#define AHEAD_SUBTITLES 2     // Subtitles are small, and there may be several languages.
#define CUSHION_NS 4000000000ll  // Video in memory before playback starts or goes on from a jump.
#define LIVE_MARGIN_NS 2000000000ll  // Live TV: this much more than a segment ahead before starting.
#define IO_BUFFER (64 * 1024)
#define PLAYLIST_MAX (8u * 1024 * 1024)
#define SEGMENT_MAX (96u * 1024 * 1024)
#define SUBTITLE_SEGMENT_MAX (2u * 1024 * 1024)  // A WebVTT segment is a few KB.
#define HELD_MAX (256u * 1024 * 1024)  // Segments kept in memory by a session (a reader's own next one aside).
#define MASTER_MAX (16u * 1024)
#define PROBE_BYTES (8 * 1024 * 1024)
#define PROBE_US (15 * 1000000ll)
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
    char *init;             // fMP4: its initialization section, once fetched.
    size_t init_size;
} MediaList;

// List 0 is the video's (and, from a server, the only one); a channel's own
// master list can add its other sound tracks and subtitles as lists 1 on.
typedef struct {
    bool running;
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t wake;
    NetClient *net;
    char authorization[512];
    char origin_url[HLS_URL_MAX];  // The first list's address; the header only goes to its origin.
    NetProblem problem;           // Why a list couldn't be fetched, while starting,
    long status;                  // the server's status then,
    bool not_hls;                 // or the first answer wasn't an HLS list at all.
    bool any_certificate;         // Its addresses' certificates aren't checked (an IPTV list's choice).
    StreamHeaders stream;         // The headers its list or addon asks for, sent with every request.
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
    size_t largest;               // The biggest segment fetched yet: what one more may take (hlsMayFetch).
    int first_segment;            // The first video segment read, -1 before one is: playback's 0.
    int64_t playback_ns;          // Where playback is (hlsSetPlayback), -1 until told.
} HlsSession;

static HlsSession g_main, g_audio;  // The video's, and a sound track converted on its own.

#define VIDEO (ses->lists[0].list)

bool hlsIsLocation(const char *path)
{
    return strncmp(path, HLS_LOCATION, sizeof(HLS_LOCATION) - 1) == 0 ||
           strncmp(path, HLS_AUDIO_LOCATION, sizeof(HLS_AUDIO_LOCATION) - 1) == 0;
}

// --- What to fetch ---

static int aheadOf(HlsSession *ses, int list)
{
    return ses->lists[list].list.subtitles ? AHEAD_SUBTITLES : AHEAD;
}

static int findSlot(HlsSession *ses, int list, int index)
{
    for (int i = 0; i < SLOTS; i++)
        if (ses->slots[i].state != SLOT_EMPTY && ses->slots[i].list == list && ses->slots[i].index == index)
            return i;
    return -1;
}

static bool wanted(HlsSession *ses, int list, int index)
{
    for (int r = 0; r < READERS; r++) {
        const Reader *reader = &ses->readers[r];
        if (reader->owner && reader->list == list && index >= reader->segment &&
            index < reader->segment + aheadOf(ses, list))
            return true;
    }
    return false;
}

// The nearest missing segment to any reader: false if there's none.
static bool nextSegment(HlsSession *ses, int *list, int *index)
{
    for (int distance = 0; distance < AHEAD; distance++)
        for (int r = 0; r < READERS; r++) {
            const Reader *reader = &ses->readers[r];
            if (!reader->owner || distance >= aheadOf(ses, reader->list))
                continue;
            int at = reader->segment + distance;
            if (at < ses->lists[reader->list].list.count && findSlot(ses, reader->list, at) < 0) {
                *list = reader->list;
                *index = at;
                return true;
            }
        }
    return false;
}

static int freeSlot(HlsSession *ses)
{
    int victim = -1;
    for (int i = 0; i < SLOTS; i++) {
        if (ses->slots[i].state == SLOT_EMPTY)
            return i;
        if (ses->slots[i].state == SLOT_READY && !wanted(ses, ses->slots[i].list, ses->slots[i].index) &&
            (victim < 0 || ses->slots[i].index < ses->slots[victim].index))
            victim = i;
    }
    return victim;
}

// --- The thread ---

// The server's sign-in, or none: a channel's own source never gets it.
static const char *authorization(const HlsSession *ses, const char *url)
{
    return ses->authorization[0] && hlsSameOrigin(url, ses->origin_url) ? ses->authorization : NULL;
}

static bool fetchSegment(HlsSession *ses, int list, int index, NetResponse *r)
{
    NetRequest request = {.url = ses->lists[list].list.urls[index],
                          .authorization = authorization(ses, ses->lists[list].list.urls[index]),
                          .cancel = &ses->abort_fetch, .stall_s = SEGMENT_STALL_S,
                          .any_certificate = ses->any_certificate,
                          .stream = &ses->stream};
    size_t max = ses->lists[list].list.subtitles ? SUBTITLE_SEGMENT_MAX : SEGMENT_MAX;
    for (int attempt = 0; attempt < FETCH_TRIES && !ses->abort_fetch; attempt++) {
        if (netFetchText(ses->net, &request, max, r))
            return true;
        if (!ses->abort_fetch)
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

static bool getText(HlsSession *ses, const char *url, NetResponse *r);

// The earliest segment any reader of a list is at, -1 if none reads it.
static int earliestReader(HlsSession *ses, int list)
{
    int at = -1;
    for (int r = 0; r < READERS; r++)
        if (ses->readers[r].owner && ses->readers[r].list == list && (at < 0 || ses->readers[r].segment < at))
            at = ses->readers[r].segment;
    return at;
}

// A live list's text keeps only what's near its readers: a channel's list
// can hold hours, and FFmpeg reads the whole text each time. Subtitles are
// read by KaiVideo itself, from the segments' addresses: they need none.
static void trimLive(HlsSession *ses, int list)
{
    HlsPlaylist *p = &ses->lists[list].list;
    if (p->ended)
        return;
    if (p->subtitles) {
        hlsPlaylistTrim(p, p->count);
        return;
    }
    int reader = earliestReader(ses, list);
    if (reader >= 0)
        hlsPlaylistTrim(p, reader - LIVE_KEEP_BEHIND);
}

// Live lists: read them again, and take what was added (segments keep their numbers). Under the lock.
static void reread(HlsSession *ses)
{
    for (int i = 0; i < ses->list_count && !ses->stop; i++) {
        bool unchosen = ses->lists[i].list.subtitles && i != ses->subtitle_list;  // Read again only while shown.
        if (!ses->lists[i].loaded || ses->lists[i].list.ended || unchosen)
            continue;
        ses->abort_fetch = false;  // Left set by a fetch called off; stopping sets it again.
        char url[HLS_URL_MAX];
        snprintf(url, sizeof(url), "%s", ses->lists[i].url);
        pthread_mutex_unlock(&ses->lock);
        NetResponse media;
        bool ok = getText(ses, url, &media);
        pthread_mutex_lock(&ses->lock);
        if (!ok)
            continue;
        // New segments go at the end, by their numbers: a channel's own list
        // drops its old ones as it goes, the server's keeps them all.
        int added = hlsPlaylistAppend(&ses->lists[i].list, media.data, url);
        netResponseFree(&media);
        trimLive(ses, i);
        if (i == 0 && added > 0)
            ses->grew_ns = monotonicNs();
        if (added >= 0)
            pthread_cond_broadcast(&ses->wake);
    }
    ses->next_reread_ns = monotonicNs() + REREAD_NS;  // From when this one ended: long lists take a while.
    if (!VIDEO.ended && monotonicNs() - ses->grew_ns > LIVE_STALL_NS) {
        ses->failed = true;  // Readers waiting for more give up: playback ends, and says so.
        pthread_cond_broadcast(&ses->wake);
    }
}

// Waits for something to do; a growing list is read again on time. Under the lock.
static void waitForWork(HlsSession *ses)
{
    if (VIDEO.ended) {
        pthread_cond_wait(&ses->wake, &ses->lock);
        return;
    }
    struct timespec until;
    clock_gettime(CLOCK_REALTIME, &until);
    uint64_t now = monotonicNs(), left = ses->next_reread_ns > now ? ses->next_reread_ns - now : 0;
    until.tv_sec += (time_t)(left / NS_PER_S);
    until.tv_nsec += (long)(left % NS_PER_S);
    if (until.tv_nsec >= NS_PER_S) {
        until.tv_sec++;
        until.tv_nsec -= NS_PER_S;
    }
    pthread_cond_timedwait(&ses->wake, &ses->lock, &until);
}

// --- Subtitles of a channel's own ---

// The segment being shown, by media sequence: playback's time 0 is the start
// of the first segment read (readers run ahead of what shows). -1 before it's known.
static int64_t playbackSequence(HlsSession *ses)
{
    if (ses->first_segment < 0 || ses->first_segment >= VIDEO.count || ses->playback_ns < 0)
        return -1;
    int first, last;
    hlsPlaylistSpan(&VIDEO, VIDEO.starts_ns[ses->first_segment] + ses->playback_ns, 1, &first, &last);
    return VIDEO.sequence + first;
}

void hlsSetPlayback(int64_t position_ns)
{
    HlsSession *ses = &g_main;
    if (!ses->running)
        return;
    pthread_mutex_lock(&ses->lock);
    ses->playback_ns = position_ns;
    pthread_mutex_unlock(&ses->lock);
}

// Reads the chosen subtitles' list, the first time. Under the lock.
static void loadSubtitleList(HlsSession *ses, int list, unsigned choice)
{
    char url[HLS_URL_MAX];
    snprintf(url, sizeof(url), "%s", hlsMasterListUrl(&ses->master, list));
    ses->abort_fetch = false;
    pthread_mutex_unlock(&ses->lock);
    NetResponse r;
    HlsPlaylist fresh;
    bool ok = getText(ses, url, &r);
    if (ok) {
        ok = hlsPlaylistParseList(r.data, url, 0, list, true, &fresh);
        netResponseFree(&r);
    }
    if (ok)
        hlsPlaylistTrim(&fresh, fresh.count);  // No text needed: KaiVideo reads its segments itself.
    pthread_mutex_lock(&ses->lock);
    MediaList *m = &ses->lists[list];
    if (ok && choice == ses->subtitle_choice) {
        hlsPlaylistFree(&m->list);
        m->list = fresh;
        snprintf(m->url, sizeof(m->url), "%s", url);
        m->loaded = true;
        ses->subtitle_next = -1;
    } else if (ok) {
        hlsPlaylistFree(&fresh);  // Another was chosen meanwhile.
    } else if (choice == ses->subtitle_choice) {
        ses->subtitle_list = 0;  // It can't be had: none shows.
    }
}

// Fetches the chosen subtitles' next segment, near the picture's (by media
// sequence), for the player to take. False if there's nothing to do. Under the lock.
static bool subtitleWork(HlsSession *ses)
{
    int list = ses->subtitle_list;
    unsigned choice = ses->subtitle_choice;
    if (!list || ses->stop)
        return false;
    MediaList *m = &ses->lists[list];
    if (!m->loaded) {
        loadSubtitleList(ses, list, choice);
        return true;
    }
    int64_t sequence = playbackSequence(ses);
    if (sequence < 0)
        return false;
    int at = (int)(sequence - m->list.sequence), from = at - SUBTITLES_BEHIND > 0 ? at - SUBTITLES_BEHIND : 0;
    if (ses->subtitle_next < from)
        ses->subtitle_next = from;  // Placed, or caught up after falling behind.
    int index = ses->subtitle_next;
    if (index >= m->list.count || index > at + SUBTITLES_AHEAD || ses->subtitle_queued >= SUBTITLE_QUEUE)
        return false;
    char url[HLS_URL_MAX];
    snprintf(url, sizeof(url), "%s", m->list.urls[index]);
    ses->abort_fetch = false;
    pthread_mutex_unlock(&ses->lock);
    NetResponse r;
    NetRequest request = {.url = url, .authorization = authorization(ses, url), .cancel = &ses->abort_fetch,
                          .timeout_s = PLAYLIST_TIMEOUT_S, .any_certificate = ses->any_certificate,
                          .stream = &ses->stream};
    bool ok = netFetchText(ses->net, &request, PLAYLIST_MAX, &r);
    pthread_mutex_lock(&ses->lock);
    if (choice != ses->subtitle_choice) {
        if (ok)
            netResponseFree(&r);
        return true;
    }
    ses->subtitle_next = index + 1;  // One that failed is skipped: the next lines matter more.
    if (ok) {
        ses->subtitle_queue[ses->subtitle_queued].data = r.data;
        ses->subtitle_queue[ses->subtitle_queued].size = r.size;
        ses->subtitle_queued++;
    }
    return true;
}

// The subtitles of what shows now haven't come: they go before the picture's
// segments further ahead. Under the lock.
static bool subtitleUrgent(HlsSession *ses)
{
    if (!ses->subtitle_list || ses->subtitle_queued >= SUBTITLE_QUEUE)
        return false;
    const MediaList *m = &ses->lists[ses->subtitle_list];
    if (!m->loaded)
        return true;
    int64_t sequence = playbackSequence(ses);
    if (sequence < 0)
        return false;
    int at = (int)(sequence - m->list.sequence);
    return ses->subtitle_next <= at + SUBTITLES_AHEAD && ses->subtitle_next < m->list.count;
}

static void dropSubtitleQueue(HlsSession *ses)
{
    for (int i = 0; i < ses->subtitle_queued; i++)
        free(ses->subtitle_queue[i].data);
    ses->subtitle_queued = 0;
}

static const HlsRendition *subtitleRendition(HlsSession *ses, int track)
{
    if (!ses->running || track < 0 || track >= ses->subtitle_count)
        return NULL;
    for (int i = 0; i < ses->master.count; i++)
        if (ses->master.renditions[i].list == ses->subtitle_lists[track])
            return &ses->master.renditions[i];
    return NULL;
}

int hlsSubtitleCount(void)
{
    HlsSession *ses = &g_main;
    return ses->running ? ses->subtitle_count : 0;
}

const char *hlsSubtitleName(int track)
{
    HlsSession *ses = &g_main;
    const HlsRendition *r = subtitleRendition(ses, track);
    return r ? r->name : "";
}

const char *hlsSubtitleLanguage(int track)
{
    HlsSession *ses = &g_main;
    const HlsRendition *r = subtitleRendition(ses, track);
    return r ? r->language : "";
}

void hlsSelectSubtitle(int track)
{
    HlsSession *ses = &g_main;
    if (!ses->running)
        return;
    pthread_mutex_lock(&ses->lock);
    int list = track >= 0 && track < ses->subtitle_count ? ses->subtitle_lists[track] : 0;
    if (list != ses->subtitle_list) {
        ses->subtitle_list = list;
        ses->subtitle_choice++;
        ses->subtitle_next = -1;
        dropSubtitleQueue(ses);
        pthread_cond_broadcast(&ses->wake);
    }
    pthread_mutex_unlock(&ses->lock);
}

bool hlsTakeSubtitle(char **data, size_t *size)
{
    HlsSession *ses = &g_main;
    if (!ses->running)
        return false;
    pthread_mutex_lock(&ses->lock);
    bool any = ses->subtitle_queued > 0;
    if (any) {
        *data = ses->subtitle_queue[0].data;
        *size = ses->subtitle_queue[0].size;
        memmove(&ses->subtitle_queue[0], &ses->subtitle_queue[1], (size_t)(ses->subtitle_queued - 1) * sizeof(ses->subtitle_queue[0]));
        ses->subtitle_queued--;
        pthread_cond_broadcast(&ses->wake);  // Room for the next.
    }
    pthread_mutex_unlock(&ses->lock);
    return any;
}

// --- The fetching thread ---

// The bytes of the segments kept.
static size_t heldBytes(const HlsSession *ses)
{
    size_t held = 0;
    for (int i = 0; i < SLOTS; i++)
        if (ses->slots[i].state == SLOT_READY)
            held += ses->slots[i].size;
    return held;
}

// Whether a reader of the list is at that segment (it waits for it now).
static bool readerAt(const HlsSession *ses, int list, int index)
{
    for (int r = 0; r < READERS; r++)
        if (ses->readers[r].owner && ses->readers[r].list == list && ses->readers[r].segment == index)
            return true;
    return false;
}

// Lets go of segments no reader wants any more (the oldest first) while one more as big as the largest
// wouldn't fit. Under the lock.
static void dropUnwanted(HlsSession *ses)
{
    while (!hlsMayFetch(heldBytes(ses), ses->largest, HELD_MAX, false)) {
        int victim = -1;
        for (int i = 0; i < SLOTS; i++)
            if (ses->slots[i].state == SLOT_READY && !wanted(ses, ses->slots[i].list, ses->slots[i].index) &&
                (victim < 0 || ses->slots[i].index < ses->slots[victim].index))
                victim = i;
        if (victim < 0)
            return;
        free(ses->slots[victim].data);
        ses->slots[victim] = (Slot){.state = SLOT_EMPTY};
    }
}

// Whether a segment may be fetched now, as far as memory goes. Under the lock.
static bool roomFor(HlsSession *ses, int list, int index)
{
    dropUnwanted(ses);
    return hlsMayFetch(heldBytes(ses), ses->largest, HELD_MAX, readerAt(ses, list, index));
}

static void *fetchThread(void *arg)
{
    HlsSession *ses = arg;
    pthread_mutex_lock(&ses->lock);
    while (!ses->stop) {
        if (!VIDEO.ended && monotonicNs() >= ses->next_reread_ns) {
            reread(ses);
            continue;
        }
        if (!ses->failed && subtitleUrgent(ses) && subtitleWork(ses))
            continue;
        int list = 0, index = -1;
        int slot = !ses->failed && nextSegment(ses, &list, &index) && roomFor(ses, list, index) ? freeSlot(ses) : -1;
        if (slot < 0) {
            if (!ses->failed && subtitleWork(ses))
                continue;
            waitForWork(ses);
            continue;
        }
        free(ses->slots[slot].data);
        ses->slots[slot] = (Slot){.list = list, .index = index, .state = SLOT_LOADING};
        ses->fetch_list = list;
        ses->fetch_index = index;
        ses->abort_fetch = false;
        pthread_mutex_unlock(&ses->lock);
        NetResponse r;
        bool ok = fetchSegment(ses, list, index, &r);
        pthread_mutex_lock(&ses->lock);
        bool aborted = ses->abort_fetch;
        ses->fetch_index = -1;
        ses->slots[slot].state = ok ? SLOT_READY : SLOT_EMPTY;
        ses->slots[slot].data = ok ? r.data : NULL;
        ses->slots[slot].size = ok ? r.size : 0;
        if (ok && r.size > ses->largest)
            ses->largest = r.size;
        if (!ok && !aborted && list == 0)
            ses->failed = true;  // A track of its own that fails leaves the picture playing.
        pthread_cond_broadcast(&ses->wake);
    }
    pthread_mutex_unlock(&ses->lock);
    return NULL;
}

// --- Starting ---

static bool getText(HlsSession *ses, const char *url, NetResponse *r)
{
    // While starting, the caller's cancel cuts it short; afterwards, stopping does.
    const volatile bool *cancel = !ses->running && ses->start_cancel ? ses->start_cancel : &ses->abort_fetch;
    NetRequest request = {.url = url, .authorization = authorization(ses, url), .cancel = cancel,
                          .timeout_s = ses->playlist_timeout_s, .any_certificate = ses->any_certificate,
                          .stream = &ses->stream};
    bool ok = netFetchText(ses->net, &request, PLAYLIST_MAX, r);
    if (!ok && !ses->running) {  // Starting: why, for the message.
        ses->problem = r->problem;
        ses->status = r->status;
    }
    return ok;
}

// Reads list number list (-1: the only one) from url into ses->lists.
static bool loadList(HlsSession *ses, int slot, int list, bool subtitles, const char *url, int64_t start_ns)
{
    NetResponse media;
    if (!getText(ses, url, &media))
        return false;
    MediaList *m = &ses->lists[slot];
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
static void loadRenditions(HlsSession *ses)
{
    static HlsMaster local;  // What FFmpeg is told of.
    local = ses->master;
    local.count = 0;
    ses->list_count = ses->master.list_count;
    for (int i = 0; i < ses->master.count; i++) {
        const HlsRendition *r = &ses->master.renditions[i];
        if (r->type == HLS_RENDITION_SUBTITLES) {
            ses->subtitle_lists[ses->subtitle_count++] = r->list;
            continue;
        }
        if (r->list)
            loadList(ses, r->list, r->list, false, r->url, 0);
        if (r->list == 0 || ses->lists[r->list].loaded)
            local.renditions[local.count++] = *r;
    }
    ses->master_text = malloc(MASTER_MAX);
    if (ses->master_text && hlsLocalMaster(&local, ses->master_text, MASTER_MAX)) {
        ses->master_size = strlen(ses->master_text);
    } else {
        free(ses->master_text);
        ses->master_text = NULL;
    }
}

// The media playlist behind the master one (its best variant) into list 0,
// with the variant's own sound tracks and subtitles, if it has any, as more
// lists. A list that has segments already is the media one itself.
static bool readPlaylists(HlsSession *ses, const char *master_url, int64_t start_ns)
{
    NetResponse first;
    if (!getText(ses, master_url, &first))
        return false;
    HlsMaster *master = &ses->master;
    bool is_master = hlsMasterParse(first.data, master_url, ses->max_bitrate, master);
    bool several = is_master && master->list_count > 1;
    bool ok;
    ses->list_count = 1;
    if (!is_master) {  // Already the one with the segments.
        ok = hlsPlaylistParse(first.data, master_url, start_ns, &VIDEO);
        ses->not_hls = !ok && strncmp(first.data, "#EXTM3U", 7) != 0;  // A plain stream, or a page.
        if (ok && !VIDEO.ended)  // Live: what FFmpeg is told of starts near the end.
            hlsPlaylistTrim(&VIDEO, VIDEO.count - LIVE_KEEP);
        snprintf(ses->lists[0].url, sizeof(ses->lists[0].url), "%s", master_url);
        ses->lists[0].loaded = ok;
        netResponseFree(&first);
    } else {
        netResponseFree(&first);
        ok = loadList(ses, 0, several ? 0 : -1, false, master->variant_url, start_ns);
        if (ok && several)
            loadRenditions(ses);
    }
    ses->base_ns = VIDEO.base_ns;
    ses->grew_ns = monotonicNs();
    ses->next_reread_ns = monotonicNs() + REREAD_NS;
    return ok;
}

int64_t hlsBaseNs(void)
{
    HlsSession *ses = &g_main;
    return ses->running ? ses->base_ns : 0;
}

static void freeLists(HlsSession *ses)
{
    for (int i = 0; i < HLS_LISTS_MAX; i++) {
        hlsPlaylistFree(&ses->lists[i].list);
        free(ses->lists[i].init);
        ses->lists[i].init = NULL;
    }
    free(ses->master_text);
    ses->master_text = NULL;
}

static void stopSession(HlsSession *ses);

static bool g_next_any_certificate;  // For the session start() makes (it clears the session first).
static StreamHeaders g_next_stream;   // The same.
static bool g_last_not_hls;         // The last direct start found something that isn't HLS.

bool hlsDirectWasNotHls(void)
{
    return g_last_not_hls;
}

static bool start(HlsSession *ses, const char *master_url, const char *auth, long long max_bitrate, long timeout_s,
                  int64_t start_ns, const volatile bool *cancel)
{
    stopSession(ses);
    memset(ses, 0, sizeof(*ses));
    pthread_mutex_init(&ses->lock, NULL);
    pthread_cond_init(&ses->wake, NULL);
    ses->start_cancel = cancel;
    snprintf(ses->authorization, sizeof(ses->authorization), "%s", auth ? auth : "");
    snprintf(ses->origin_url, sizeof(ses->origin_url), "%s", master_url);
    ses->any_certificate = g_next_any_certificate;
    ses->stream = g_next_stream;
    ses->max_bitrate = max_bitrate;
    ses->playlist_timeout_s = timeout_s;
    ses->fetch_index = -1;
    ses->first_segment = -1;
    ses->playback_ns = -1;
    ses->interrupts = mediaFileInterrupts();
    ses->net = netClientOpen();
    bool ok = ses->net && readPlaylists(ses, master_url, start_ns);
    ses->running = ok && pthread_create(&ses->thread, NULL, fetchThread, ses) == 0;
    if (!ses->running) {
        freeLists(ses);
        netClientClose(ses->net);
        ses->net = NULL;
        pthread_cond_destroy(&ses->wake);
        pthread_mutex_destroy(&ses->lock);
    }
    return ses->running;
}

bool hlsStartDirect(const char *url, long long max_bitrate, const volatile bool *cancel, char *error,
                    size_t error_size)
{
    return hlsStartDirectAny(url, max_bitrate, false, NULL, cancel, error, error_size);
}

bool hlsStartDirectAny(const char *url, long long max_bitrate, bool any_certificate, const StreamHeaders *stream,
                       const volatile bool *cancel, char *error, size_t error_size)
{
    HlsSession *ses = &g_main;
    g_last_not_hls = false;
    g_next_any_certificate = any_certificate;
    if (stream)
        g_next_stream = *stream;
    bool ok = start(ses, url, NULL, max_bitrate, DIRECT_TIMEOUT_S, 0, cancel);
    g_next_any_certificate = false;
    memset(&g_next_stream, 0, sizeof(g_next_stream));
    if (ok)
        return true;
    long status = ses->status;
    g_last_not_hls = ses->not_hls;
    switch (ses->not_hls ? NET_OK : ses->problem) {
    case NET_NO_NAME:
    case NET_NO_CONNECT:
    case NET_TIMEOUT: snprintf(error, error_size, "%s", T("The channel doesn't answer: it may be off the air.")); break;
    case NET_CERTIFICATE:
        snprintf(error, error_size, "%s", T("The channel's server certificate can't be trusted."));
        break;
    case NET_SECURE: snprintf(error, error_size, "%s", T("The channel's secure connection failed.")); break;
    case NET_STATUS:
        if (status == 404 || status == 410)
            snprintf(error, error_size, "%s", T("The channel isn't at that address any more."));
        else if (status == 401 || status == 403 || status == 451)
            snprintf(error, error_size, "%s", T("The channel refuses to play here (it may play only in its country)."));
        else
            snprintf(error, error_size, T("The channel's server answered %ld."), status);
        break;
    default:
        snprintf(error, error_size, "%s",
                 ses->not_hls ? T("This channel isn't sent as HLS (.m3u8): KaiVideo can't play it yet.")
                              : T("The channel's own stream can't be read."));
        break;
    }
    return false;
}

bool hlsStart(const char *master_url, const char *authorization, int64_t start_ns, const volatile bool *cancel,
              char *error, size_t error_size)
{
    HlsSession *ses = &g_main;
    bool ok = start(ses, master_url, authorization, 0, PLAYLIST_TIMEOUT_S, start_ns, cancel);
    if (!ok)
        snprintf(error, error_size, "%s", T("The server didn't start converting this video."));
    return ok;
}

bool hlsStartAudio(const char *url, const char *authorization, int64_t start_ns, const volatile bool *cancel,
                   char *error, size_t error_size)
{
    bool ok = start(&g_audio, url, authorization, 0, PLAYLIST_TIMEOUT_S, start_ns, cancel);
    if (!ok)
        snprintf(error, error_size, "%s", T("The server didn't start converting this sound track."));
    return ok;
}

void hlsStop(void)
{
    stopSession(&g_main);
    tsStreamStop();  // A channel sent as it is stops with the video too.
}

void hlsStopAudio(void)
{
    stopSession(&g_audio);
}

static HlsSession *sessionOf(const char *location)
{
    return !strncmp(location, HLS_AUDIO_LOCATION, sizeof(HLS_AUDIO_LOCATION) - 1) ? &g_audio : &g_main;
}

int64_t hlsLocationBaseNs(const char *location)
{
    HlsSession *ses = sessionOf(location);
    return ses->running ? ses->base_ns : 0;
}

static void stopSession(HlsSession *ses)
{
    if (!ses->running)
        return;
    pthread_mutex_lock(&ses->lock);
    ses->stop = true;
    ses->abort_fetch = true;
    pthread_cond_broadcast(&ses->wake);
    pthread_mutex_unlock(&ses->lock);
    pthread_join(ses->thread, NULL);
    ses->running = false;
    for (int i = 0; i < SLOTS; i++)
        free(ses->slots[i].data);
    dropSubtitleQueue(ses);
    freeLists(ses);
    netClientClose(ses->net);
    pthread_cond_destroy(&ses->wake);
    pthread_mutex_destroy(&ses->lock);
}

bool hlsFailed(void)
{
    HlsSession *ses = &g_main;
    if (!ses->running)
        return false;
    pthread_mutex_lock(&ses->lock);
    bool failed = ses->failed;
    pthread_mutex_unlock(&ses->lock);
    return failed;
}

bool hlsIsLive(void)
{
    HlsSession *ses = &g_main;
    if (!ses->running)
        return false;
    pthread_mutex_lock(&ses->lock);
    bool live = !VIDEO.ended;
    pthread_mutex_unlock(&ses->lock);
    return live;
}

bool hlsCushionReady(int64_t from_ns)
{
    HlsSession *ses = &g_main;
    if (!ses->running)
        return true;
    pthread_mutex_lock(&ses->lock);
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
        int slot = findSlot(ses, 0, i);
        ready = slot >= 0 && ses->slots[slot].state == SLOT_READY;
    }
    ready = ready || ses->failed;
    pthread_mutex_unlock(&ses->lock);
    return ready;
}

double hlsSecondsReady(int64_t from_ns)
{
    HlsSession *ses = &g_main;
    if (!ses->running)
        return -1;
    pthread_mutex_lock(&ses->lock);
    int first, last;
    hlsPlaylistSpan(&VIDEO, from_ns, 1, &first, &last);
    int64_t until = from_ns;
    for (int i = first; i < VIDEO.count; i++) {
        int slot = findSlot(ses, 0, i);
        if (slot < 0 || ses->slots[slot].state != SLOT_READY)
            break;
        until = i + 1 < VIDEO.count ? VIDEO.starts_ns[i + 1] : until;  // The last one's end isn't known.
    }
    pthread_mutex_unlock(&ses->lock);
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

static void noteReader(HlsSession *ses, const void *owner, int list, int segment)
{
    if (list == 0 && ses->first_segment < 0)
        ses->first_segment = segment;  // Where FFmpeg's times, and so playback's, begin.
    int r = 0;
    while (r < READERS - 1 && ses->readers[r].owner && (ses->readers[r].owner != owner || ses->readers[r].list != list))
        r++;
    ses->readers[r] = (Reader){owner, list, segment};
    if (ses->fetch_index >= 0 && !wanted(ses, ses->fetch_list, ses->fetch_index))
        ses->abort_fetch = true;
    pthread_cond_broadcast(&ses->wake);
}

// True if the reader has something better to do than wait. Its callback takes
// the reader's own lock, so ours isn't held meanwhile.
static bool readerInterrupted(HlsSession *ses, const AVFormatContext *s)
{
    if (!s->interrupt_callback.callback)
        return false;
    pthread_mutex_unlock(&ses->lock);
    bool yes = s->interrupt_callback.callback(s->interrupt_callback.opaque) != 0;
    pthread_mutex_lock(&ses->lock);
    return yes;
}

// A copy of the segment, waiting for it. NULL if it won't be had, or the
// reader was interrupted. Under the lock.
static char *waitForSegment(HlsSession *ses, const AVFormatContext *s, int list, int index, size_t *size)
{
    // The picture's reader also reads the subtitles: one late must not hold it up.
    uint64_t give_up = ses->lists[list].list.subtitles ? monotonicNs() + SUBTITLE_WAIT_NS : 0;
    for (;;) {
        int slot = findSlot(ses, list, index);
        if (slot >= 0 && ses->slots[slot].state == SLOT_READY) {
            char *copy = malloc(ses->slots[slot].size ? ses->slots[slot].size : 1);
            if (copy)
                memcpy(copy, ses->slots[slot].data, ses->slots[slot].size);
            *size = ses->slots[slot].size;
            return copy;
        }
        if (ses->stop || ses->failed || mediaFileInterrupts() != ses->interrupts || readerInterrupted(ses, s))
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
        pthread_cond_timedwait(&ses->wake, &ses->lock, &until);
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

// An fMP4 list's initialization section: fetched once (the lock let go meanwhile), then copied. NULL if
// it can't be had. Under the lock.
static char *initSection(HlsSession *ses, int list, size_t *size)
{
    MediaList *m = &ses->lists[list];
    if (!m->init && m->list.map_url) {
        char url[HLS_URL_MAX];
        snprintf(url, sizeof(url), "%s", m->list.map_url);
        pthread_mutex_unlock(&ses->lock);
        NetRequest request = {.url = url, .authorization = authorization(ses, url), .cancel = &ses->abort_fetch,
                              .stall_s = SEGMENT_STALL_S, .any_certificate = ses->any_certificate,
                          .stream = &ses->stream};
        NetResponse r;
        memset(&r, 0, sizeof(r));
        NetClient *client = netClientOpen();  // Its own: the fetching thread uses the session's meanwhile.
        bool ok = client && netFetchText(client, &request, SEGMENT_MAX, &r);
        netClientClose(client);
        pthread_mutex_lock(&ses->lock);
        if (ok && !m->init) {
            m->init = r.data;  // Kept: the response's text is ours.
            m->init_size = r.size;
            r.data = NULL;
        }
        netResponseFree(&r);
    }
    if (!m->init)
        return NULL;
    *size = m->init_size;
    return copyText(m->init, m->init_size);
}

static int ioOpen(AVFormatContext *s, AVIOContext **pb, const char *url, int flags, AVDictionary **options)
{
    (void)flags;
    (void)options;
    HlsSession *ses = s->opaque;  // hlsOpenInput put it there.
    char *data = NULL;
    size_t size = 0;
    int list, index;
    pthread_mutex_lock(&ses->lock);
    if (!strcmp(url, HLS_LOCAL_INDEX)) {  // The master list, or the only list.
        bool master = ses->master_text != NULL;
        size = master ? ses->master_size : VIDEO.size;
        data = copyText(master ? ses->master_text : VIDEO.text, size);
    } else if (hlsLocalName(url, &list, &index) && list >= 0 && list < ses->list_count && ses->lists[list].loaded) {
        HlsPlaylist *p = &ses->lists[list].list;
        if (index == HLS_INIT_INDEX) {
            data = initSection(ses, list, &size);
        } else if (index < 0) {
            size = p->size;
            data = copyText(p->text, size);
        } else if (index < p->count) {
            noteReader(ses, s, list, index);
            data = waitForSegment(ses, s, list, index, &size);
        }
    }
    pthread_mutex_unlock(&ses->lock);
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

bool hlsOpenInput(AVFormatContext **fmt, AVIOContext **playlist, const char *location)
{
    HlsSession *ses = sessionOf(location);
    (*fmt)->opaque = ses;  // For ioOpen, which FFmpeg calls with the context.
    if (!ses->running || ioOpen(*fmt, playlist, HLS_LOCAL_INDEX, 0, NULL) < 0)
        return false;
    (*fmt)->pb = *playlist;
    (*fmt)->flags |= AVFMT_FLAG_CUSTOM_IO;
    // FFmpeg's HLS reader looks at each list's stream with these (by default
    // 16 KB and 4 s). A conversion begun mid-video with picture subtitles drawn
    // on it starts its sound seconds before its picture: looking only that far,
    // the picture's size stays unknown and nothing shows. Reading stops as
    // soon as every track is known, so other videos don't start later.
    (*fmt)->probesize = PROBE_BYTES;
    (*fmt)->max_analyze_duration = PROBE_US;
    (*fmt)->io_open = ioOpen;
    (*fmt)->io_close2 = ioClose;
    return avformat_open_input(fmt, HLS_LOCAL_INDEX, av_find_input_format("hls"), NULL) >= 0;
}

void hlsCloseInput(AVIOContext *playlist)
{
    ioClose(NULL, playlist);
}
