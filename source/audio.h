// A video file's sound: read with FFmpeg on its own (the file is opened a
// second time, so sound and picture never wait for each other), decoded,
// turned into the output's 48 kHz stereo and played with audout in a
// thread. It's the clock the picture follows.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Audio Audio;

// The console's audio output: once, for the whole program.
bool audioSystemInit(void);
void audioSystemExit(void);

// Opens path's sound, paused: audioSetPaused(a, false) starts it (once the
// picture is ready, so neither waits for the other). track: which of the
// file's sound tracks (as video.h counts them), or -1 for FFmpeg's pick.
// NULL if there is none (error empty) or it can't be played (error says why).
Audio *audioOpen(const char *path, int track, char *error, size_t error_size);
void audioClose(Audio *a);

// "AAC, 48 kHz, stereo".
const char *audioDescription(const Audio *a);
// Where playback is (the time of what's being heard), in the file's time.
// False before the first sound plays, and right after a seek.
bool audioClock(Audio *a, int64_t *pts_ns);
void audioSetPaused(Audio *a, bool paused);
// Goes to pts_ns: what's queued is dropped, and sound before it is skipped.
void audioSeek(Audio *a, int64_t pts_ns);
// True once everything has been played.
bool audioFinished(Audio *a);
// While paused, the sound gets ready to start: true once it is, with the
// time its sound starts from (after a seek, the target or just past it).
bool audioReadyAt(Audio *a, int64_t *pts_ns);
// The sound track playing (as video.h counts them).
int audioTrack(const Audio *a);
