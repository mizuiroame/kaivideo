// Playing a video file: its picture (video.h) and sound (audio.h) kept
// together, with pause and seeking. The sound is the clock; without sound
// (or after it ends) the console's clock is.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"
#include "video.h"

typedef struct Player Player;

// Opens path and starts playing. NULL with a reason if the picture can't be
// played; sound that can't be played is noted (playerAudioInfo) and skipped.
Player *playerOpen(const char *path, uint64_t now_ns, char *error, size_t error_size);
void playerClose(Player *p);

// Moves the clock to now_ns and returns the frame to draw (NULL before the first).
const GfxVideoFrame *playerUpdate(Player *p, uint64_t now_ns);

void playerTogglePause(Player *p, uint64_t now_ns);
bool playerPaused(const Player *p);
// Goes delta_ns from where playback is (kept within the file).
void playerSeekBy(Player *p, int64_t delta_ns, uint64_t now_ns);
// Goes to target_ns (kept within the file): a tap on the progress bar.
void playerSeekTo(Player *p, int64_t target_ns, uint64_t now_ns);

int64_t playerPosition(const Player *p);
int64_t playerDuration(const Player *p);
// True once the picture has ended, or failed (error says why).
bool playerFinished(Player *p, char *error, size_t error_size);

const char *playerVideoInfo(const Player *p);
// The sound's format, or why there is none ("No sound", or the reason).
const char *playerAudioInfo(const Player *p);
void playerSize(const Player *p, int *width, int *height, double *aspect);
VideoStats playerStats(const Player *p);
// Its state for the log, when playback seems stuck: "paused=0 holding=1 ...".
void playerDescribe(Player *p, char *out, size_t size);

// Subtitles: an .srt next to the video (the first track, if there is one),
// then the file's text tracks. Opens on the .srt, else a track the file
// marks default, else none.
int playerSubtitleCount(const Player *p);
const char *playerSubtitleLabel(const Player *p, int track);
int playerSubtitle(const Player *p);  // -1 for none.
void playerSetSubtitle(Player *p, int track);
// The text to show now ('\n' between lines); false if none.
bool playerSubtitleText(Player *p, char *out, size_t size);

// Sound tracks: the file's, in its order.
int playerAudioTrackCount(const Player *p);
const char *playerAudioTrackName(const Player *p, int track);
int playerAudioTrack(const Player *p);  // -1 if none plays.
// Plays track instead, from where playback is, without stopping the
// picture (the change takes a moment; asked again meanwhile, it's ignored).
void playerSetAudioTrack(Player *p, int track, uint64_t now_ns);
// True once after a change of track that can't be played (out says why):
// the video goes on silent, another track can be chosen.
bool playerTakeAudioError(Player *p, char *out, size_t size);
