// Playing a video file's picture: a thread reads it with FFmpeg and decodes
// it with the console's hardware decoder (libounce's VideoDecoder), or with
// FFmpeg on the CPU for what that can't play (HEVC, 10-bit H.264), into a few
// frames that the window's thread shows when their time comes.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "asssubs.h"
#include "gfx.h"
#include "playback.h"

#define VIDEO_QUEUE 3  // Decoded frames waiting to be shown.
#define VIDEO_SUB_TRACKS 8  // Subtitle tracks kept from a file (text, or pictures).
#define VIDEO_AUDIO_TRACKS 8  // Sound tracks that can be chosen.
#define VIDEO_CHAPTERS 99     // Chapters kept from a file.

typedef struct {
    // Counted while it plays, for the log.
    unsigned shown;      // Frames drawn.
    unsigned dropped;    // Decoded but late: skipped to keep up.
    unsigned decoded;    // Out of the decoder.
    unsigned previews;   // Pictures shown while a seek was on its way.
    double copy_ms;      // Average time to take a frame from the decoder.
} VideoStats;

typedef struct Video Video;

// Opens path and starts decoding. NULL with a reason in error if the file
// can't be read or its video can't be decoded.
Video *videoOpen(const char *path, char *error, size_t error_size);
// The same, using at most max_threads threads if it's decoded on the CPU.
Video *videoOpenLimited(const char *path, int max_threads, char *error, size_t error_size);
// Stops the thread and frees everything.
void videoClose(Video *v);

// "H.264 1920x1080, 60 fps".
const char *videoDescription(const Video *v);
// The picture's size and shape: width / height as shown (sample aspect included).
void videoSize(const Video *v, int *width, int *height, double *aspect);

// The frame to show when playback is at media_ns (the file's time): the
// latest whose time has come; on the CPU, during a seek, the nearest picture
// before its target until the target is ready. NULL before the first. The
// frame stays valid until the next call.
const GfxVideoFrame *videoFrameAt(Video *v, int64_t media_ns);
// The time of the next frame waiting to be shown; false if none is decoded yet.
bool videoNextPts(Video *v, int64_t *pts_ns);
// Goes to pts_ns: from the keyframe before it, frames before it aren't shown.
void videoSeek(Video *v, int64_t pts_ns);
// The file's length, or 0 if it doesn't say.
int64_t videoDuration(const Video *v);
// Where playback's time 0 is on the stream's own clock (converted and live
// streams don't start at 0), in ns.
int64_t videoStartNs(const Video *v);
// True once every frame has been shown, or decoding failed (see error).
bool videoFinished(Video *v, char *error, size_t error_size);
VideoStats videoStats(const Video *v);

// The file's subtitle tracks (text, or pictures: PGS, VobSub), decoded as the file is read (so a line
// shows once reading has passed it, which is always by its time).
int videoSubtitleCount(const Video *v);
// The track's title, or its language, or "Track 2".
const char *videoSubtitleLabel(const Video *v, int track);
// The first track marked default or forced, or -1.
int videoSubtitlePreferred(const Video *v);
// The track's text at pos_ns into out ('\n' between lines); false if none.
bool videoSubtitleAt(Video *v, int track, int64_t pos_ns, char *out, size_t size);
// True if the track brings its own look (ASS, or pictures): see videoSubtitlePicture.
bool videoSubtitleStyled(const Video *v, int track);
// The fonts styled subtitles fall back on, kept by the caller for as long as videos open.
void videoSetSubtitleFonts(const AssFont *fonts, int count);
// For a track with its own look (ASS, or pictures): its picture at pos_ns for
// this area goes in out (out->w is 0 when nothing shows).
bool videoSubtitlePicture(Video *v, int track, int64_t pos_ns, const AssArea *area, AssPicture *out);

// The file's chapters, in order of time.
const PlaybackChapter *videoChapters(const Video *v, int *count);

// The file's sound tracks, in the file's order (audio.h opens them by this number).
int videoAudioTrackCount(const Video *v);
// The track's title, or its language, or "Track 2".
const char *videoAudioTrackName(const Video *v, int track);
