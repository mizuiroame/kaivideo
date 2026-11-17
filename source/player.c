#include "player.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "subs.h"

#define AUDIO_WAIT_NS 2000000000ull  // How long the picture waits for the sound to start before going alone.

struct Player {
    Video *video;
    Audio *audio;
    char audio_info[160];
    bool paused;
    bool holding;            // Waiting for the picture's first frame (opening, a seek): the sound waits too.
    int64_t position_ns;     // Where playback is, in the file's time.
    // The console's clock, when it's the one: position = base_pos + (now - base_now).
    bool own_clock;
    int64_t base_pos_ns;
    uint64_t base_now_ns;
    uint64_t waiting_since_ns;  // Since when the sound's clock has been awaited (start, seek).

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
    char why[160];
    p->audio = audioOpen(path, why, sizeof(why));
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
    if (p->has_external && p->subtitle == 0)
        return subsAt(&p->external, p->position_ns, out, size);
    return videoSubtitleAt(p->video, p->subtitle - (p->has_external ? 1 : 0), p->position_ns, out, size);
}

// Makes the console's clock the one from here, carrying on from position.
static void useOwnClock(Player *p, uint64_t now_ns)
{
    p->own_clock = true;
    p->base_pos_ns = p->position_ns;
    p->base_now_ns = now_ns;
}

const GfxVideoFrame *playerUpdate(Player *p, uint64_t now_ns)
{
    if (p->holding) {
        // The first frame (after opening or a seek) is ready: time starts from
        // it, and the sound with it, so the picture isn't late from the start.
        int64_t first;
        if (videoNextPts(p->video, &first)) {
            p->holding = false;
            p->position_ns = first;
            p->base_pos_ns = first;
            p->base_now_ns = now_ns;
            p->waiting_since_ns = now_ns;
            if (p->audio)
                audioSetPaused(p->audio, p->paused);
        }
    } else if (p->own_clock) {
        if (!p->paused)
            p->position_ns = p->base_pos_ns + (int64_t)(now_ns - p->base_now_ns);
    } else {
        int64_t heard;
        if (audioClock(p->audio, &heard)) {
            p->position_ns = heard;
            p->waiting_since_ns = 0;
        } else if (audioFinished(p->audio) ||
                   (!p->paused && p->waiting_since_ns && now_ns - p->waiting_since_ns > AUDIO_WAIT_NS)) {
            useOwnClock(p, now_ns);  // The sound ended, or never came: the picture goes on alone.
        }
    }
    return videoFrameAt(p->video, p->position_ns);
}

void playerTogglePause(Player *p, uint64_t now_ns)
{
    p->paused = !p->paused;
    if (p->audio && !p->holding)  // While holding, the sound stays paused until the picture's ready.
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
        p->waiting_since_ns = now_ns;
    }
}

void playerSeekTo(Player *p, int64_t target_ns, uint64_t now_ns)
{
    playerSeekBy(p, target_ns - p->position_ns, now_ns);
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
