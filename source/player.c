#include "player.h"

#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "hlsio.h"
#include "jellyfin.h"
#include "jfsync.h"
#include "subs.h"

#define AUDIO_WAIT_NS 2000000000ull  // How long the picture waits for the sound to start before going alone.
#define SWITCH_LEAD_NS 300000000ll   // A new sound track joins this far ahead of the picture.
#define REMOTE_LEAD_NS 2000000000ll  // Over the network it takes longer to get ready.
#define MAX_LEAD_NS 16000000000ll    // Each time it arrives late, the lead doubles up to this.
#define SWITCH_GAP_NS 700000000ll    // A new track starting further on than this is sought instead.
#define SWITCH_WAIT_NS 10000000000ull // The picture waits this long for a new track, at most.
#define JOIN_SLACK_NS 20000000ll     // Ready this little behind the picture still joins.
#define CUSHION_WAIT_NS 3000000000ull // A converted video waits this long, at most, for some video ahead.
#define LIVE_CUSHION_WAIT_NS 30000000000ull  // Live TV waits longer: its next segments are still being made.
#define MS_NS 1000000ll
#define EXTERNAL_MAX (JF_PLAN_SUBS + 1)
#define REOPEN_SETTLE_NS 600000000ull  // Jumps closer together than this count as one.
#define PREFETCH_AFTER_FRAMES 1200   // About 20 s in, the other subtitle tracks are got ready.

// Subtitles that aren't inside what's being read: an .srt next to the video,
// or a track the server gives (fetched when first chosen).
typedef enum { EXT_READY, EXT_REMOTE, EXT_LOADING, EXT_FAILED } ExtState;

typedef struct {
    char label[JF_TRACK_NAME_MAX];
    SubTrack track;
    ExtState state;     // Under ext_lock.
    int stream_index;   // The server's number.
} ExtSub;

struct Player {
    Video *video;
    Audio *audio;
    char path[512];
    int audio_track;         // The sound track chosen (video.h's numbering), -1 before one is.
    char audio_info[160];
    bool paused;
    int speed;               // Thousandths (playback.h).
    int audio_delay_ms, subtitle_delay_ms;
    bool holding;            // Waiting for the picture's first frame (opening, a seek): the sound waits too.
    int64_t position_ns;     // Where playback is, in the file's time.
    int64_t first_frame_ns;  // The first frame since opening or the last seek.
    uint64_t hold_since_ns;     // When holding began.
    uint64_t first_frame_at_ns; // When that frame came; 0 until it has.
    bool start_noted;           // The last start's times are waiting to be logged.
    int start_ms, cushion_ms;   // Until playing, and the part spent gathering video.
    // The console's clock, when it's the one: position = base_pos + (now - base_now).
    bool own_clock;
    int64_t base_pos_ns;
    uint64_t base_now_ns;
    uint64_t waiting_since_ns;  // Since when the sound's clock has been awaited (start, seek).

    // Changing the sound track: a thread closes the old one and opens the
    // new, while the picture goes on by the console's clock. Then the new
    // one waits, ready, for the picture to reach it (joining).
    pthread_t swap_thread;
    bool swapping, joining;
    int64_t join_lead_ns;      // How far ahead sound is asked to be ready (at normal speed).
    int join_retries;          // Times it arrived late, for the log.
    // Changing track on the open file: the picture waits for the new sound.
    bool switching, switch_sought;
    uint64_t switch_since_ns;
    int last_switch_ms;        // How long the last one took, for the log.
    pthread_mutex_t swap_lock;
    bool swap_done;            // Under swap_lock.
    Audio *swap_old, *swap_new;
    int swap_track;
    char swap_error[160];
    bool swap_failed;          // Taken by playerTakeAudioError.

    // Subtitles: those outside the file first, then the file's own.
    ExtSub ext[EXTERNAL_MAX];
    int ext_count;
    pthread_mutex_t ext_lock;
    pthread_t ext_thread;
    bool ext_thread_started;
    bool ext_thread_done;   // Under ext_lock.
    int ext_loading;
    int subtitle;  // The track shown, -1 for none.

    // From the server.
    char location[LIBRARY_PATH_MAX];
    bool converted;
    JfTrack plan_audio[JF_PLAN_AUDIO];
    int plan_audio_count;
    bool audio_restart;
    int audio_restart_index;
    int64_t base_ns;           // Where a converted stream begins.
    bool reopen_wanted;
    int64_t reopen_at_ns;
    uint64_t reopen_since_ns;  // The last jump that asked for it.
    volatile bool ext_cancel;  // Closing: subtitles on their way are dropped.
    int frames_seen;
};

// The .srt's label: its language from "name.es.srt", and that it's a file.
static void labelExternal(Player *p, const char *srt_path)
{
    char lang[16] = "";
    const char *name = strrchr(srt_path, '/');
    name = name ? name + 1 : srt_path;
    const char *ext = strrchr(name, '.'), *dot = ext;
    while (dot > name && dot[-1] != '.')
        dot--;
    if (dot > name && ext - dot >= 2 && ext - dot <= 3) {  // "name.es.srt": "es" ("My.Movie.srt" has none).
        memcpy(lang, dot, (size_t)(ext - dot));
        lang[ext - dot] = '\0';
    }
    const char *language = subsLanguageName(lang);
    snprintf(p->ext[0].label, sizeof(p->ext[0].label), "%s%s", language ? language : "Subtitles", " (.srt file)");
}

static void openSubtitles(Player *p, const char *path)
{
    char srt[512];
    if (subsFindFor(path, srt, sizeof(srt)) && subsLoadSrtFile(&p->ext[0].track, srt)) {
        p->ext_count = 1;
        p->ext[0].state = EXT_READY;
        labelExternal(p, srt);
    }
    // An .srt was put there to be seen; else a track the file marks default.
    int preferred = videoSubtitlePreferred(p->video);
    p->subtitle = p->ext_count ? 0 : preferred;
}

Player *playerOpen(const char *path, uint64_t now_ns, char *error, size_t error_size)
{
    Player *p = calloc(1, sizeof(*p));
    if (!p) {
        snprintf(error, error_size, "Out of memory.");
        return NULL;
    }
    p->video = videoOpen(path, error, error_size);
    if (!p->video) {
        free(p);
        return NULL;
    }
    snprintf(p->path, sizeof(p->path), "%s", path);
    p->speed = PLAYBACK_PERMILLE;
    pthread_mutex_init(&p->ext_lock, NULL);
    p->join_lead_ns = jfIsLocation(path) || !strncmp(path, "jellyfin-", 9) ? REMOTE_LEAD_NS : SWITCH_LEAD_NS;
    pthread_mutex_init(&p->swap_lock, NULL);
    char why[160];
    p->audio = audioOpen(path, -1, why, sizeof(why));
    p->audio_track = p->audio ? audioTrack(p->audio) : -1;
    if (p->audio)
        snprintf(p->audio_info, sizeof(p->audio_info), "%s", audioDescription(p->audio));
    else
        snprintf(p->audio_info, sizeof(p->audio_info), "%s", why[0] ? why : "No sound");
    p->own_clock = !p->audio;
    p->holding = true;  // The sound starts with the first frame (it opens paused).
    p->hold_since_ns = now_ns;
    p->waiting_since_ns = now_ns;
    openSubtitles(p, path);
    return p;
}

void playerClose(Player *p)
{
    if (!p)
        return;
    if (p->swapping) {
        pthread_join(p->swap_thread, NULL);
        audioClose(p->swap_new);
    }
    p->ext_cancel = true;
    if (p->ext_thread_started)
        pthread_join(p->ext_thread, NULL);
    pthread_mutex_destroy(&p->swap_lock);
    pthread_mutex_destroy(&p->ext_lock);
    audioClose(p->audio);
    videoClose(p->video);
    for (int i = 0; i < p->ext_count; i++)
        subsFree(&p->ext[i].track);
    free(p);
}

int playerSubtitleCount(const Player *p)
{
    return p->ext_count + videoSubtitleCount(p->video);
}

const char *playerSubtitleLabel(const Player *p, int track)
{
    if (track >= 0 && track < p->ext_count)
        return p->ext[track].label;
    return videoSubtitleLabel(p->video, track - p->ext_count);
}

int playerSubtitle(const Player *p)
{
    return p->subtitle;
}

static void *fetchExternal(void *arg)
{
    Player *p = arg;
    ExtSub *e = &p->ext[p->ext_loading];
    NetResponse answer;
    SubTrack track = {0};
    bool ok = jfSyncFetchSubtitle(p->location, e->stream_index, &p->ext_cancel, &answer);
    if (ok) {
        ok = subsParseSrt(&track, answer.data, answer.size);
        netResponseFree(&answer);
    }
    pthread_mutex_lock(&p->ext_lock);
    e->track = track;
    e->state = ok ? EXT_READY : EXT_FAILED;
    p->ext_thread_done = true;
    pthread_mutex_unlock(&p->ext_lock);
    return NULL;
}

// Gets the chosen track from the server if it's still there; one at a time.
// The next track to fetch: the one chosen, or later on any other. -1 if none.
static int externalToFetch(Player *p)
{
    if (p->subtitle >= 0 && p->subtitle < p->ext_count && p->ext[p->subtitle].state == EXT_REMOTE)
        return p->subtitle;
    if (++p->frames_seen < PREFETCH_AFTER_FRAMES)
        return -1;
    for (int i = 0; i < p->ext_count; i++)
        if (p->ext[i].state == EXT_REMOTE)
            return i;
    return -1;
}

static void updateExternal(Player *p)
{
    pthread_mutex_lock(&p->ext_lock);
    bool done = p->ext_thread_done;
    int next = externalToFetch(p);
    pthread_mutex_unlock(&p->ext_lock);
    if (p->ext_thread_started && done) {
        pthread_join(p->ext_thread, NULL);
        p->ext_thread_started = false;
        p->ext_thread_done = false;
    }
    if (next < 0 || p->ext_thread_started)
        return;
    p->ext_loading = next;
    p->ext[next].state = EXT_LOADING;
    p->ext_thread_started = pthread_create(&p->ext_thread, NULL, fetchExternal, p) == 0;
    if (!p->ext_thread_started)
        p->ext[next].state = EXT_FAILED;
}

void playerSetSubtitle(Player *p, int track)
{
    p->subtitle = track >= 0 && track < playerSubtitleCount(p) ? track : -1;
    if (p->subtitle >= 0 && p->subtitle < p->ext_count) {
        pthread_mutex_lock(&p->ext_lock);
        if (p->ext[p->subtitle].state == EXT_FAILED)  // Asked for again: one more try.
            p->ext[p->subtitle].state = EXT_REMOTE;
        pthread_mutex_unlock(&p->ext_lock);
    }
}

void playerAttachPlan(Player *p, const JfPlan *plan, const char *location)
{
    snprintf(p->location, sizeof(p->location), "%s", location);
    p->converted = !plan->direct;
    if (p->converted) {
        p->base_ns = hlsBaseNs();
        memcpy(p->plan_audio, plan->audio, sizeof(p->plan_audio));
        p->plan_audio_count = plan->audio_count;
        p->audio_track = plan->audio_current;
    }
    bool had_file_track = p->subtitle >= 0;
    for (int i = 0; i < plan->sub_count && p->ext_count < EXTERNAL_MAX; i++) {
        const JfTrack *t = &plan->subs[i];
        if (!p->converted && !t->external)
            continue;  // It's inside the file being played.
        ExtSub *e = &p->ext[p->ext_count];
        snprintf(e->label, sizeof(e->label), "%s", t->name);
        e->stream_index = t->index;
        e->state = EXT_REMOTE;
        if (i == plan->sub_default && (p->converted || !had_file_track))
            p->subtitle = p->ext_count;
        p->ext_count++;
    }
    if (!p->converted && had_file_track && p->subtitle >= 0)  // The file's tracks moved down the list.
        p->subtitle += p->ext_count;
}

bool playerIsConverted(const Player *p)
{
    return p->converted;
}

bool playerTakeReopen(Player *p, uint64_t now_ns, int64_t *at_ns)
{
    if (!p->reopen_wanted || now_ns - p->reopen_since_ns < REOPEN_SETTLE_NS)
        return false;
    p->reopen_wanted = false;
    *at_ns = p->reopen_at_ns;
    return true;
}

bool playerTakeAudioRestart(Player *p, int *stream_index)
{
    bool wanted = p->audio_restart;
    p->audio_restart = false;
    *stream_index = p->audio_restart_index;
    return wanted;
}

// The time subtitles are at: the delay moves them later.
static int64_t subtitleTime(const Player *p)
{
    return p->position_ns - (int64_t)p->subtitle_delay_ms * MS_NS;
}

bool playerSubtitleText(Player *p, char *out, size_t size)
{
    if (p->subtitle < 0)
        return false;
    int64_t at = subtitleTime(p);
    if (p->subtitle < p->ext_count) {
        pthread_mutex_lock(&p->ext_lock);
        bool ready = p->ext[p->subtitle].state == EXT_READY;
        pthread_mutex_unlock(&p->ext_lock);
        return ready && subsAt(&p->ext[p->subtitle].track, at, out, size);
    }
    return videoSubtitleAt(p->video, p->subtitle - p->ext_count, at, out, size);
}

// A track of the list, in the video's numbering (the .srt comes first in the list).
static int videoTrackOf(const Player *p, int track)
{
    return track - p->ext_count;
}

bool playerSubtitleColorApplies(const Player *p)
{
    if (p->subtitle >= 0)
        return !videoSubtitleStyled(p->video, videoTrackOf(p, p->subtitle));
    for (int i = 0; i < playerSubtitleCount(p); i++)
        if (!videoSubtitleStyled(p->video, videoTrackOf(p, i)))
            return true;
    return false;
}

bool playerSubtitlePicture(Player *p, const AssArea *area, AssPicture *out)
{
    int track = videoTrackOf(p, p->subtitle);
    if (p->subtitle < 0 || track < 0)
        return false;
    return videoSubtitlePicture(p->video, track, subtitleTime(p), area, out);
}

// Makes the console's clock the one from here, carrying on from position.
static void useOwnClock(Player *p, uint64_t now_ns)
{
    p->own_clock = true;
    p->base_pos_ns = p->position_ns;
    p->base_now_ns = now_ns;
}

static void finishSwap(Player *p);
static void join(Player *p);
static void startJoining(Player *p);
static void updateSwitch(Player *p, uint64_t now_ns);

// True when playing can start: the first frame is here (it shows from then
// on), and a converted video has some seconds ahead, so it doesn't stop again
// right away.
static bool readyToStart(Player *p, uint64_t now_ns)
{
    if (!p->first_frame_at_ns) {
        int64_t first;
        if (!videoNextPts(p->video, &first))
            return false;
        p->first_frame_ns = first;
        p->first_frame_at_ns = now_ns;
    }
    uint64_t most = hlsIsLive() ? LIVE_CUSHION_WAIT_NS : CUSHION_WAIT_NS;
    return !p->converted || hlsCushionReady(p->first_frame_ns) || now_ns - p->first_frame_at_ns > most;
}

// Holding ends: time starts from the first frame, and the sound with it, so
// the picture isn't late from the start.
static void startFromFirstFrame(Player *p, uint64_t now_ns)
{
    p->holding = false;
    p->position_ns = p->first_frame_ns;
    p->base_pos_ns = p->first_frame_ns;
    p->base_now_ns = now_ns;
    p->waiting_since_ns = now_ns;
    p->start_ms = (int)((now_ns - p->hold_since_ns) / 1000000);
    p->cushion_ms = (int)((now_ns - p->first_frame_at_ns) / 1000000);
    p->start_noted = true;
    p->first_frame_at_ns = 0;
    if (p->audio)
        audioSetPaused(p->audio, p->paused);
}

bool playerTakeStartTimes(Player *p, int *start_ms, int *cushion_ms)
{
    bool noted = p->start_noted;
    p->start_noted = false;
    *start_ms = p->start_ms;
    *cushion_ms = p->cushion_ms;
    return noted;
}

const GfxVideoFrame *playerUpdate(Player *p, uint64_t now_ns)
{
    updateExternal(p);
    if (p->swapping)
        finishSwap(p);
    if (p->audio && audioTakeSwitchError(p->audio, p->swap_error, sizeof(p->swap_error))) {
        p->swap_failed = true;  // The old track goes on: the lists say so.
        p->audio_track = audioTrack(p->audio);
    }
    if (p->switching) {
        updateSwitch(p, now_ns);
    } else if (p->holding) {
        if (readyToStart(p, now_ns))
            startFromFirstFrame(p, now_ns);
    } else if (p->own_clock) {
        if (!p->paused)
            p->position_ns = p->base_pos_ns + playbackScaled((int64_t)(now_ns - p->base_now_ns), p->speed);
        if (p->joining)
            join(p);
    } else {
        int64_t heard;
        if (audioClock(p->audio, &heard)) {
            p->position_ns = heard;
            p->waiting_since_ns = 0;
            p->speed = audioSpeed(p->audio);  // Normal if the one asked for couldn't be set up.
        } else if (audioFinished(p->audio)) {
            useOwnClock(p, now_ns);  // The sound ended: the picture goes on alone.
        } else if (!p->paused && p->waiting_since_ns && now_ns - p->waiting_since_ns > AUDIO_WAIT_NS) {
            useOwnClock(p, now_ns);  // It's late: the picture goes on, and it joins when it can.
            startJoining(p);
        }
    }
    // Sound later than the picture: the picture is that far ahead of what's heard.
    // Never before the first frame: after a seek it shows at once, even with
    // the sound ahead of the picture.
    int64_t delay_ns = p->audio_track >= 0 ? (int64_t)p->audio_delay_ms * MS_NS : 0;
    int64_t picture_ns = p->position_ns + delay_ns;
    return videoFrameAt(p->video, picture_ns > p->first_frame_ns ? picture_ns : p->first_frame_ns);
}

void playerTogglePause(Player *p, uint64_t now_ns)
{
    p->paused = !p->paused;
    // While holding or joining, the sound stays paused until the picture's ready for it.
    if (p->audio && !p->holding && !p->joining && !p->switching)
        audioSetPaused(p->audio, p->paused);
    if (!p->paused) {
        p->base_pos_ns = p->position_ns;  // The console's clock carries on from where it stopped.
        p->base_now_ns = now_ns;
        if (p->waiting_since_ns)
            p->waiting_since_ns = now_ns;  // The sound couldn't start while paused.
    }
}

bool playerWaitingForPicture(const Player *p)
{
    return p->holding || p->switching;
}

bool playerPaused(const Player *p)
{
    return p->paused;
}

void playerSeekBy(Player *p, int64_t delta_ns, uint64_t now_ns)
{
    int64_t duration = videoDuration(p->video), target = playerPosition(p) + delta_ns;
    if (duration > 0 && target > duration - 1000000000)
        target = duration - 1000000000;  // Not past the end: a second before it.
    if (target < 0)
        target = 0;
    if (p->converted && target < p->base_ns) {  // That part wasn't asked of the server.
        p->reopen_wanted = true;
        p->reopen_at_ns = target;
        p->reopen_since_ns = now_ns;
        return;
    }
    p->reopen_wanted = false;
    videoSeek(p->video, target);
    p->switching = false;
    p->position_ns = target;
    p->first_frame_ns = target;
    p->base_pos_ns = target;
    p->base_now_ns = now_ns;
    p->holding = true;  // Until the picture has a frame from the target.
    p->hold_since_ns = now_ns;
    p->first_frame_at_ns = 0;
    if (p->audio) {
        // The sound follows, held until then, and is the clock again (even if it had ended).
        audioSetPaused(p->audio, true);
        audioSeek(p->audio, target);
        p->own_clock = false;
        p->joining = false;
        p->waiting_since_ns = now_ns;
    }
}

void playerSeekTo(Player *p, int64_t target_ns, uint64_t now_ns)
{
    playerSeekBy(p, target_ns - playerPosition(p), now_ns);
}

// How far ahead of the picture sound joins: the same wait at any speed.
static int64_t joinLead(const Player *p)
{
    return playbackScaled(p->join_lead_ns, p->speed);
}

// The sound gets ready a little ahead of the picture, paused, to start there.
static void startJoining(Player *p)
{
    audioSetPaused(p->audio, true);
    audioSeek(p->audio, p->position_ns + joinLead(p));
    p->waiting_since_ns = 0;
    p->joining = true;
}

void playerSetSpeed(Player *p, int permille, uint64_t now_ns)
{
    if (permille == p->speed)
        return;
    p->switching = false;
    if (!p->holding)
        useOwnClock(p, now_ns);  // The picture goes on by the console's clock meanwhile.
    p->speed = permille;
    if (!p->audio)
        return;  // No sound, or a new track on its way: it takes the speed when it comes.
    audioSetSpeed(p->audio, permille);
    if (p->holding) {
        audioSeek(p->audio, p->position_ns);  // Waits for the first frame, as usual.
        return;
    }
    startJoining(p);
}

int playerSpeed(const Player *p)
{
    return p->speed;
}

void playerSetAudioDelay(Player *p, int delay_ms)
{
    p->audio_delay_ms = delay_ms;
}

int playerAudioDelay(const Player *p)
{
    return p->audio_delay_ms;
}

void playerSetSubtitleDelay(Player *p, int delay_ms)
{
    p->subtitle_delay_ms = delay_ms;
}

int playerSubtitleDelay(const Player *p)
{
    return p->subtitle_delay_ms;
}

int playerChapterCount(const Player *p)
{
    int count;
    videoChapters(p->video, &count);
    return count;
}

const char *playerChapterName(const Player *p, int chapter)
{
    int count;
    const PlaybackChapter *chapters = videoChapters(p->video, &count);
    return chapter >= 0 && chapter < count ? chapters[chapter].title : "";
}

int64_t playerChapterStart(const Player *p, int chapter)
{
    int count;
    const PlaybackChapter *chapters = videoChapters(p->video, &count);
    return chapter >= 0 && chapter < count ? chapters[chapter].start_ns : 0;
}

int playerChapter(const Player *p)
{
    int count;
    const PlaybackChapter *chapters = videoChapters(p->video, &count);
    return playbackChapterAt(chapters, count, p->position_ns);
}

int64_t playerPosition(const Player *p)
{
    return p->reopen_wanted ? p->reopen_at_ns : p->position_ns;  // Where it's going, on the bar too.
}

int64_t playerDuration(const Player *p)
{
    return videoDuration(p->video);
}

bool playerFinished(Player *p, char *error, size_t error_size)
{
    if (p->audio && audioReadFailed(p->audio)) {
        snprintf(error, error_size, "The connection to the server was lost.");
        return true;
    }
    return videoFinished(p->video, error, error_size);
}

const char *playerVideoInfo(const Player *p)
{
    return videoDescription(p->video);
}

const char *playerAudioInfo(const Player *p)
{
    return p->audio_info;
}

void playerSize(const Player *p, int *width, int *height, double *aspect)
{
    videoSize(p->video, width, height, aspect);
}

VideoStats playerStats(const Player *p)
{
    return videoStats(p->video);
}

void playerDescribe(Player *p, char *out, size_t size)
{
    int64_t heard = 0, next = 0;
    bool clock = p->audio && audioClock(p->audio, &heard), have_next = videoNextPts(p->video, &next);
    char error[64] = "";
    bool finished = videoFinished(p->video, error, sizeof(error));
    snprintf(out, size,
             "paused=%d holding=%d own_clock=%d joining=%d (late %d) switching=%d (last %d ms, sought %d) swapping=%d "
             "pos=%.3f audio=%s clock=%s%.3f next_frame=%s%.3f finished=%d%s%s",
             p->paused, p->holding, p->own_clock, p->joining, p->join_retries, p->switching, p->last_switch_ms,
             p->switch_sought, p->swapping, p->position_ns / 1e9,
             p->audio ? (audioFinished(p->audio) ? "ended" : "open") : "none", clock ? "" : "invalid ", heard / 1e9,
             have_next ? "" : "none ", next / 1e9, finished, error[0] ? " " : "", error);
}

int playerAudioTrackCount(const Player *p)
{
    return p->converted ? p->plan_audio_count : videoAudioTrackCount(p->video);
}

const char *playerAudioTrackName(const Player *p, int track)
{
    if (p->converted)
        return track >= 0 && track < p->plan_audio_count ? p->plan_audio[track].name : "";
    return videoAudioTrackName(p->video, track);
}

int playerAudioTrack(const Player *p)
{
    return p->audio_track;
}

static void *swapThread(void *arg)
{
    Player *p = arg;
    audioClose(p->swap_old);  // Fades out and plays out what it had queued.
    char error[160];
    Audio *a = audioOpen(p->path, p->swap_track, error, sizeof(error));  // Paused: it gets ready.
    pthread_mutex_lock(&p->swap_lock);
    p->swap_new = a;
    snprintf(p->swap_error, sizeof(p->swap_error), "%s", a ? "" : error[0] ? error : "No sound");
    p->swap_done = true;
    pthread_mutex_unlock(&p->swap_lock);
    return NULL;
}

// The new track, on the open file: it starts when it's ready, and the picture with it.
static void updateSwitch(Player *p, uint64_t now_ns)
{
    int64_t start;
    if (audioFinished(p->audio)) {
        p->switching = false;
        useOwnClock(p, now_ns);
    } else if (now_ns - p->switch_since_ns > SWITCH_WAIT_NS) {
        p->switching = false;
        useOwnClock(p, now_ns);
        startJoining(p);
    } else if (!p->paused && audioReadyAt(p->audio, &start)) {
        if (start > p->position_ns + SWITCH_GAP_NS && !p->switch_sought) {
            p->switch_sought = true;  // Its sound for here was already passed in the file.
            audioSeek(p->audio, p->position_ns);
            return;
        }
        if (start > p->position_ns)
            p->position_ns = start;
        p->switching = false;
        p->own_clock = false;
        p->waiting_since_ns = now_ns;
        p->last_switch_ms = (int)((now_ns - p->switch_since_ns) / 1000000);
        snprintf(p->audio_info, sizeof(p->audio_info), "%s", audioDescription(p->audio));
        audioSetPaused(p->audio, false);
    }
}

void playerSetAudioTrack(Player *p, int track, uint64_t now_ns)
{
    if (p->swapping || track < 0 || track >= playerAudioTrackCount(p))
        return;
    if (p->converted) {  // The server has to make the video with that sound.
        p->audio_restart = true;
        p->audio_restart_index = p->plan_audio[track].index;
        return;
    }
    if (p->audio) {
        audioSetPaused(p->audio, true);
        audioSwitchTrack(p->audio, track, p->position_ns);
        p->audio_track = track;
        p->joining = false;
        snprintf(p->audio_info, sizeof(p->audio_info), "Changing the sound track");
        if (p->holding) {  // Opening or a jump is under way: the sound follows it as usual.
            audioSeek(p->audio, p->position_ns);
            p->own_clock = false;
            return;
        }
        p->switching = true;
        p->switch_sought = false;
        p->switch_since_ns = now_ns;
        return;
    }
    // The picture goes on alone by the console's clock while the sound changes.
    if (!p->own_clock && !p->holding)
        useOwnClock(p, now_ns);
    p->own_clock = true;
    p->joining = false;
    p->swap_old = p->audio;
    p->audio = NULL;
    p->swap_track = track;
    p->swap_done = false;
    p->audio_track = track;
    snprintf(p->audio_info, sizeof(p->audio_info), "Changing the sound track");
    if (pthread_create(&p->swap_thread, NULL, swapThread, p) != 0) {
        audioClose(p->swap_old);  // Can't happen in practice; the video goes on silent.
        snprintf(p->audio_info, sizeof(p->audio_info), "The sound thread can't start.");
        return;
    }
    p->swapping = true;
}

static void finishSwap(Player *p)
{
    pthread_mutex_lock(&p->swap_lock);
    bool done = p->swap_done;
    pthread_mutex_unlock(&p->swap_lock);
    if (!done)
        return;
    pthread_join(p->swap_thread, NULL);
    p->swapping = false;
    p->audio = p->swap_new;
    p->swap_new = NULL;
    if (!p->audio) {
        snprintf(p->audio_info, sizeof(p->audio_info), "%s", p->swap_error);
        p->swap_failed = true;
        return;
    }
    snprintf(p->audio_info, sizeof(p->audio_info), "%s", audioDescription(p->audio));
    audioSetSpeed(p->audio, p->speed);
    if (p->holding) {
        // A seek is under way: the sound waits for its first frame, as usual.
        audioSeek(p->audio, p->position_ns);
        p->own_clock = false;
        return;
    }
    startJoining(p);
}

// The new sound is ready from some time on: it starts when the picture gets
// there, and its clock leads from then. Ready too late: it tries further on.
static void join(Player *p)
{
    int64_t heard, start;
    if (audioClock(p->audio, &heard)) {
        p->joining = false;
        p->own_clock = false;
        p->waiting_since_ns = 0;
        return;
    }
    if (audioFinished(p->audio)) {
        p->joining = false;  // No sound left from here: the picture ends on its own.
        return;
    }
    if (p->paused || !audioReadyAt(p->audio, &start))
        return;
    if (start + JOIN_SLACK_NS < p->position_ns) {  // Late: further ahead this time.
        p->join_lead_ns = p->join_lead_ns * 2 < MAX_LEAD_NS ? p->join_lead_ns * 2 : MAX_LEAD_NS;
        p->join_retries++;
        audioSeek(p->audio, p->position_ns + joinLead(p));
    } else if (p->position_ns >= start)
        audioSetPaused(p->audio, false);
}

bool playerTakeAudioError(Player *p, char *out, size_t size)
{
    if (!p->swap_failed)
        return false;
    p->swap_failed = false;
    snprintf(out, size, "%s", p->swap_error);
    return true;
}
