#include "player.h"

#include <stdio.h>
#include <stdlib.h>

#include "audio.h"

#define AUDIO_WAIT_NS 2000000000ull  // How long the picture waits for the sound to start before going alone.

struct Player {
    Video *video;
    Audio *audio;
    char audio_info[160];
    bool paused;
    bool started;            // The first frame has been shown.
    int64_t position_ns;     // Where playback is, in the file's time.
    // The console's clock, when it's the one: position = base_pos + (now - base_now).
    bool own_clock;
    int64_t base_pos_ns;
    uint64_t base_now_ns;
    uint64_t waiting_since_ns;  // Since when the sound's clock has been awaited (start, seek).
};

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
    p->waiting_since_ns = now_ns;
    return p;
}

void playerClose(Player *p)
{
    if (!p)
        return;
    audioClose(p->audio);
    videoClose(p->video);
    free(p);
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
    if (!p->started) {
        // Show the first frame as soon as there is one; time starts from it.
        int64_t first;
        if (videoNextPts(p->video, &first)) {
            p->started = true;
            p->position_ns = first;
            p->base_pos_ns = first;
            p->base_now_ns = now_ns;
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
    if (p->audio)
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
    if (p->audio) {
        // The sound follows, and is the clock again (even if it had ended).
        audioSeek(p->audio, target);
        p->own_clock = false;
        p->waiting_since_ns = now_ns;
    }
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
