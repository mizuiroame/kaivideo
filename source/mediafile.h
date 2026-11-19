// What FFmpeg reads a video from: a file on the card, or one on a Jellyfin
// server ("jellyfin:..." locations), fetched ahead by a thread of its own so
// the network's pauses don't reach the picture.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <libavformat/avformat.h>

typedef struct MediaFile MediaFile;

// What a video is read through, for picture or sound: a file, or a converted
// stream (hlsio.h). fmt comes in allocated, with what the caller set on it.
typedef struct {
    MediaFile *file;       // NULL for a converted stream.
    AVIOContext *io;
    AVIOContext *playlist; // A converted stream's.
    int64_t start_us;      // Converted streams don't start at 0: subtract this from their times.
    int64_t skipped_us;    // What a converted stream left out before its start.
    // How far a converted stream's sound leads its picture in each segment (a
    // conversion begun mid-video): FFmpeg's HLS seek, which counts from the
    // earliest track, lands that much late. Seeks ask for that much earlier.
    int64_t sound_lead_us;
} MediaInput;

// Opens location and reads its format. False if it can't be.
bool mediaInputOpen(MediaInput *in, AVFormatContext **fmt, const char *location, int buffer_size);
// After the format is closed.
void mediaInputClose(MediaInput *in);

// NULL if it can't be opened.
MediaFile *mediaFileOpen(const char *location);
void mediaFileClose(MediaFile *f);

// Makes every read from the network that's waiting, or comes, on the files open
// now give up at once: for closing a video or cancelling its opening. Files
// opened afterwards aren't touched.
void mediaFileInterruptAll(void);
// How many interrupts there have been: for other things that wait for the network.
unsigned mediaFileInterrupts(void);
// True if reading stopped because the network failed (not at the file's end).
// f NULL: a converted stream.
bool mediaFileFailed(const MediaFile *f);
// Megabytes of the server's file fetched ahead of its slowest reader, without
// a gap; -1 if none is being read.
double mediaFileMegabytesReady(void);

// For avio_alloc_context, with the MediaFile as opaque.
int mediaFileRead(void *opaque, uint8_t *buf, int size);
int64_t mediaFileSeek(void *opaque, int64_t offset, int whence);
