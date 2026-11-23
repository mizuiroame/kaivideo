// A live stream sent as it is over HTTP (an IPTV channel's MPEG-TS, not
// HLS): a thread downloads it into a window that the player's picture and
// sound read from, each at its own pace; nothing can seek in it. One at a time.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <libavformat/avformat.h>

#include "streamheaders.h"

#define TS_LOCATION "kaivideo-stream:"  // What the player opens for it.
#define TS_WINDOW (8u << 20)            // Kept from the slowest reader on.

typedef enum {
    TS_STARTED,    // It sends MPEG-TS: open TS_LOCATION.
    TS_PLAYLIST,   // It sends an HLS list after all.
    TS_FAILED,     // See error.
} TsStart;

// Connects to url and waits for its first bytes (cancel cuts it short), sending the
// headers its list or addon asks for (stream may be NULL).
TsStart tsStreamStart(const char *url, bool any_certificate, const StreamHeaders *stream,
                      const volatile bool *cancel, char *error, size_t error_size);
void tsStreamStop(void);
bool tsStreamIsLocation(const char *location);
// A reader of the stream for FFmpeg (fmt comes allocated). False if it isn't running.
bool tsStreamOpenInput(AVFormatContext **fmt, AVIOContext **io);
void tsStreamCloseInput(AVIOContext *io);
