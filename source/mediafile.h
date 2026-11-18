// What FFmpeg reads a video from: a file on the card, or one on a Jellyfin
// server ("jellyfin:..." locations), fetched ahead by a thread of its own so
// the network's pauses don't reach the picture.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct MediaFile MediaFile;

// NULL if it can't be opened.
MediaFile *mediaFileOpen(const char *location);
void mediaFileClose(MediaFile *f);

// Makes every read from the network that's waiting, or comes, on the files open
// now give up at once: for closing a video or cancelling its opening. Files
// opened afterwards aren't touched.
void mediaFileInterruptAll(void);
// True if reading stopped because the network failed (not at the file's end).
bool mediaFileFailed(const MediaFile *f);

// For avio_alloc_context, with the MediaFile as opaque.
int mediaFileRead(void *opaque, uint8_t *buf, int size);
int64_t mediaFileSeek(void *opaque, int64_t offset, int whence);
