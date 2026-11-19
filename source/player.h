// Playing a video file: its picture (video.h) and sound (audio.h) kept
// together, with pause and seeking. The sound is the clock; without sound
// (or after it ends) the console's clock is.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"
#include "jellyfin.h"
#include "playback.h"
#include "video.h"

typedef struct Player Player;

// Opens path and starts playing. NULL with a reason if the picture can't be
// played; sound that can't be played is noted (playerAudioInfo) and skipped.
Player *playerOpen(const char *path, uint64_t now_ns, char *error, size_t error_size);
void playerClose(Player *p);

// What the server said about the video playing (location): its tracks, and
// for a converted one, that a sound track means opening it again.
void playerAttachPlan(Player *p, const JfPlan *plan, const char *location);
bool playerIsConverted(const Player *p);
// True once after another sound track was chosen on a converted video: the
// server's number for it. The video has to be opened again with it.
bool playerTakeAudioRestart(Player *p, int *stream_index);

// True once, when jumps to before where a converted stream begins have stopped
// coming: the video has to be opened again at at_ns.
bool playerTakeReopen(Player *p, uint64_t now_ns, int64_t *at_ns);

// Moves the clock to now_ns and returns the frame to draw (NULL before the first).
const GfxVideoFrame *playerUpdate(Player *p, uint64_t now_ns);

void playerTogglePause(Player *p, uint64_t now_ns);
bool playerPaused(const Player *p);
// True until the first frame after opening or a jump has come.
bool playerWaitingForPicture(const Player *p);
// Goes delta_ns from where playback is (kept within the file).
void playerSeekBy(Player *p, int64_t delta_ns, uint64_t now_ns);
// Goes to target_ns (kept within the file): a tap on the progress bar.
void playerSeekTo(Player *p, int64_t target_ns, uint64_t now_ns);

// The speed, in thousandths (playback.h): the picture goes on while the sound
// changes to it, with its pitch kept. It stays through seeks and track changes.
void playerSetSpeed(Player *p, int permille, uint64_t now_ns);
int playerSpeed(const Player *p);
// The sound's volume in percent (playback.h): 100 as it is, up to 200 louder.
void playerSetVolume(Player *p, int percent);
int playerVolume(const Player *p);
// How the picture fills the screen (playback.h); the screen draws it so.
void playerSetAspect(Player *p, PlaybackAspect aspect);
// The A-B loop of this video (playback.h); the main loop goes back to A at B.
void playerSetLoop(Player *p, PlaybackLoop loop);
PlaybackLoop playerLoop(const Player *p);
PlaybackAspect playerAspect(const Player *p);
// How much later the sound (or, negative, earlier) goes than the picture.
void playerSetAudioDelay(Player *p, int delay_ms);
int playerAudioDelay(const Player *p);
// How much later (or earlier) the subtitles show. A file's own subtitles can
// only come earlier by as far as the file has been read.
void playerSetSubtitleDelay(Player *p, int delay_ms);
int playerSubtitleDelay(const Player *p);

// The file's chapters, in order of time.
int playerChapterCount(const Player *p);
const char *playerChapterName(const Player *p, int chapter);
int64_t playerChapterStart(const Player *p, int chapter);
// The chapter playing, or -1 before the first (or with none).
int playerChapter(const Player *p);

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
// Once after each start or jump, for the log: how long until it played, and
// how much of that went to gathering video ahead.
bool playerTakeStartTimes(Player *p, int *start_ms, int *cushion_ms);

// Subtitles: an .srt next to the video (the first track, if there is one),
// then the file's text tracks. Opens on the .srt, else a track the file
// marks default, else none.
int playerSubtitleCount(const Player *p);
const char *playerSubtitleLabel(const Player *p, int track);
int playerSubtitle(const Player *p);  // -1 for none.
void playerSetSubtitle(Player *p, int track);
// A picture subtitle was chosen (or the one drawn was left): the server's
// number to make the video again with (-1 for none). False if not asked.
bool playerTakeSubtitleRestart(Player *p, int *stream_index);
// The text to show now ('\n' between lines); false if none.
bool playerSubtitleText(Player *p, char *out, size_t size);
// True if the subtitle color and style settings do something: the track shown
// doesn't bring its own look or, with subtitles off, some track doesn't.
bool playerSubtitleColorApplies(const Player *p);
// True if the track shown is drawn with its styles (ASS): out gets its picture
// now for this area (out->w is 0 when nothing shows).
bool playerSubtitlePicture(Player *p, const AssArea *area, AssPicture *out);

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
