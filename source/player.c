#include "player.h"

#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "subs.h"

#define AUDIO_WAIT_NS 2000000000ull  // How long the picture waits for the sound to start before going alone.
#define SWITCH_LEAD_NS 300000000ll   // A new sound track joins this far ahead of the picture.
#define JOIN_SLACK_NS 20000000ll     // Ready this little behind the picture still joins.
#define MS_NS 1000000ll

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
    pthread_mutex_t swap_lock;
    bool swap_done;            // Under swap_lock.
    Audio *swap_old, *swap_new;
    int swap_track;
    char swap_error[160];
    bool swap_failed;          // Taken by playerTakeAudioError.

    // Subtitles: an .srt next to the video (track 0, when there is one), then the file's own.
    bool has_external;
    SubTrack external;
    char external_label[64];
    int subtitle;  // The track shown, -1 for none.
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
    snprintf(p->external_label, sizeof(p->external_label), "%s%s", language ? language : "Subtitles",
             " (.srt file)");
}

static void openSubtitles(Player *p, const char *path)
{
    char srt[512];
    p->has_external = subsFindFor(path, srt, sizeof(srt)) && subsLoadSrtFile(&p->external, srt);
    if (p->has_external)
        labelExternal(p, srt);
    // An .srt was put there to be seen; else a track the file marks default.
    int preferred = videoSubtitlePreferred(p->video);
    p->subtitle = p->has_external ? 0 : preferred;
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
    pthread_mutex_destroy(&p->swap_lock);
    audioClose(p->audio);
    videoClose(p->video);
    subsFree(&p->external);
    free(p);
}

int playerSubtitleCount(const Player *p)
{
    return (p->has_external ? 1 : 0) + videoSubtitleCount(p->video);
}

const char *playerSubtitleLabel(const Player *p, int track)
{
    if (p->has_external && track == 0)
        return p->external_label;
    return videoSubtitleLabel(p->video, track - (p->has_external ? 1 : 0));
}

int playerSubtitle(const Player *p)
{
    return p->subtitle;
}

void playerSetSubtitle(Player *p, int track)
{
    p->subtitle = track >= 0 && track < playerSubtitleCount(p) ? track : -1;
}

bool playerSubtitleText(Player *p, char *out, size_t size)
{
    if (p->subtitle < 0)
        return false;
    int64_t at = p->position_ns - (int64_t)p->subtitle_delay_ms * MS_NS;
    if (p->has_external && p->subtitle == 0)
        return subsAt(&p->external, at, out, size);
    return videoSubtitleAt(p->video, p->subtitle - (p->has_external ? 1 : 0), at, out, size);
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

const GfxVideoFrame *playerUpdate(Player *p, uint64_t now_ns)
{
    if (p->swapping)
        finishSwap(p);
    if (p->holding) {
        // The first frame (after opening or a seek) is ready: time starts from
        // it, and the sound with it, so the picture isn't late from the start.
        int64_t first;
        if (videoNextPts(p->video, &first)) {
            p->holding = false;
            p->first_frame_ns = first;
            p->position_ns = first;
            p->base_pos_ns = first;
            p->base_now_ns = now_ns;
            p->waiting_since_ns = now_ns;
            if (p->audio)
                audioSetPaused(p->audio, p->paused);
        }
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
        } else if (audioFinished(p->audio) ||
                   (!p->paused && p->waiting_since_ns && now_ns - p->waiting_since_ns > AUDIO_WAIT_NS)) {
            useOwnClock(p, now_ns);  // The sound ended, or never came: the picture goes on alone.
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
    if (p->audio && !p->holding && !p->joining)
        audioSetPaused(p->audio, p->paused);
    if (!p->paused) {
        p->base_pos_ns = p->position_ns;  // The console's clock carries on from where it stopped.
        p->base_now_ns = now_ns;
    }
}

bool playerPaused(const Player *p)
{
    return p->paused;
}

void playerSeekBy(Player *p, int64_t delta_ns, uint64_t now_ns)
{
    int64_t duration = videoDuration(p->video), target = p->position_ns + delta_ns;
    if (duration > 0 && target > duration - 1000000000)
        target = duration - 1000000000;  // Not past the end: a second before it.
    if (target < 0)
        target = 0;
    videoSeek(p->video, target);
    p->position_ns = target;
    p->base_pos_ns = target;
    p->base_now_ns = now_ns;
    p->holding = true;  // Until the picture has a frame from the target.
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
    playerSeekBy(p, target_ns - p->position_ns, now_ns);
}

// How far ahead of the picture sound joins: the same wait at any speed.
static int64_t joinLead(const Player *p)
{
    return playbackScaled(SWITCH_LEAD_NS, p->speed);
}

void playerSetSpeed(Player *p, int permille, uint64_t now_ns)
{
    if (permille == p->speed)
        return;
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
    audioSetPaused(p->audio, true);
    audioSeek(p->audio, p->position_ns + joinLead(p));
    p->joining = true;
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
    return p->position_ns;
}

int64_t playerDuration(const Player *p)
{
    return videoDuration(p->video);
}

bool playerFinished(Player *p, char *error, size_t error_size)
{
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
             "paused=%d holding=%d own_clock=%d joining=%d swapping=%d pos=%.3f audio=%s clock=%s%.3f "
             "next_frame=%s%.3f finished=%d%s%s",
             p->paused, p->holding, p->own_clock, p->joining, p->swapping, p->position_ns / 1e9,
             p->audio ? (audioFinished(p->audio) ? "ended" : "open") : "none", clock ? "" : "invalid ", heard / 1e9,
             have_next ? "" : "none ", next / 1e9, finished, error[0] ? " " : "", error);
}

int playerAudioTrackCount(const Player *p)
{
    return videoAudioTrackCount(p->video);
}

const char *playerAudioTrackName(const Player *p, int track)
{
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

void playerSetAudioTrack(Player *p, int track, uint64_t now_ns)
{
    if (p->swapping || track < 0 || track >= playerAudioTrackCount(p))
        return;
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
    audioSeek(p->audio, p->position_ns + joinLead(p));
    p->joining = true;
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
    if (start + JOIN_SLACK_NS < p->position_ns)
        audioSeek(p->audio, p->position_ns + joinLead(p));
    else if (p->position_ns >= start)
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
