// Playing a video file's picture: a thread reads it with FFmpeg and decodes
// it with the console's hardware decoder (libounce's VideoDecoder), into a
// few frames that the window's thread shows when their time comes.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"

#define VIDEO_QUEUE 3  // Decoded frames waiting to be shown.
#define VIDEO_SUB_TRACKS 8  // Text subtitle tracks kept from a file.
#define VIDEO_AUDIO_TRACKS 8  // Sound tracks that can be chosen.

typedef struct {
    // Counted while it plays, for the log.
    unsigned shown;      // Frames drawn.
    unsigned dropped;    // Decoded but late: skipped to keep up.
    unsigned decoded;    // Out of the decoder.
    double copy_ms;      // Average time to take a frame from the decoder.
} VideoStats;

typedef struct Video Video;

// Opens path and starts decoding. NULL with a reason in error if the file
// can't be read or its video isn't H.264, VP9, AV1 or VP8.
Video *videoOpen(const char *path, char *error, size_t error_size);
// Stops the thread and frees everything.
void videoClose(Video *v);

// "H.264 1920x1080, 60 fps".
const char *videoDescription(const Video *v);
// The picture's size and shape: width / height as shown (sample aspect included).
void videoSize(const Video *v, int *width, int *height, double *aspect);

// The frame to show when playback is at media_ns (the file's time): the
// latest whose time has come. NULL before the first. The frame stays valid
// until the next call.
const GfxVideoFrame *videoFrameAt(Video *v, int64_t media_ns);
// The time of the next frame waiting to be shown; false if none is decoded yet.
bool videoNextPts(Video *v, int64_t *pts_ns);
// Goes to pts_ns: from the keyframe before it, frames before it aren't shown.
void videoSeek(Video *v, int64_t pts_ns);
// The file's length, or 0 if it doesn't say.
int64_t videoDuration(const Video *v);
// True once every frame has been shown, or decoding failed (see error).
bool videoFinished(Video *v, char *error, size_t error_size);
VideoStats videoStats(const Video *v);

// The file's text subtitle tracks, decoded as the file is read (so a line
// shows once reading has passed it, which is always by its time).
int videoSubtitleCount(const Video *v);
// The track's title, or its language, or "Track 2".
const char *videoSubtitleLabel(const Video *v, int track);
// The first track marked default or forced, or -1.
int videoSubtitlePreferred(const Video *v);
// The track's text at pos_ns into out ('\n' between lines); false if none.
bool videoSubtitleAt(Video *v, int track, int64_t pos_ns, char *out, size_t size);

// The file's sound tracks, in the file's order (audio.h opens them by this number).
int videoAudioTrackCount(const Video *v);
// The track's title, or its language, or "Track 2".
const char *videoAudioTrackName(const Video *v, int track);
