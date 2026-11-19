// A video the server converts, as HLS: its segments are fetched ahead by one
// thread and kept in memory, and the picture's and the sound's FFmpeg readers
// both take them from there. One at a time.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <libavformat/avformat.h>

#define HLS_LOCATION "jellyfin-converted:"

bool hlsIsLocation(const char *path);
// Reads the playlists from the server and starts fetching, from the segment
// holding start_ns. False, with a reason, if it can't.
bool hlsStart(const char *master_url, const char *authorization, int64_t start_ns, char *error, size_t error_size);
// Live TV straight from a channel's own HLS source (no sign-in is sent to it),
// at the best variant within max_bitrate (0: no limit). False soon if it
// doesn't answer, so the server can convert the channel instead.
bool hlsStartDirect(const char *url, long long max_bitrate, char *error, size_t error_size);
// Where the stream begins in the video: earlier places need a new start.
int64_t hlsBaseNs(void);
void hlsStop(void);
// True once segments couldn't be had from the server.
bool hlsFailed(void);
// True if the next few seconds from from_ns are here, so playing from there
// won't run out at once. Also true when waiting longer is pointless.
bool hlsCushionReady(int64_t from_ns);
// True while the stream is live TV: its list grows as the server makes it.
bool hlsIsLive(void);
// Seconds of video in memory from from_ns on, without a gap; -1 if no stream runs.
double hlsSecondsReady(int64_t from_ns);
// Opens the running stream into *fmt (allocated, with what the caller set on
// it, such as an interrupt_callback: while it says so, a reader stops waiting
// for a segment). *playlist is to be closed with hlsCloseInput after the format.
bool hlsOpenInput(AVFormatContext **fmt, AVIOContext **playlist);
void hlsCloseInput(AVIOContext *playlist);
