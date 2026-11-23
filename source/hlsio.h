// A video the server converts, as HLS: its segments are fetched ahead by one
// thread and kept in memory, and the picture's and the sound's FFmpeg readers
// both take them from there. One at a time.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <libavformat/avformat.h>

#include "streamheaders.h"

#define HLS_LOCATION "jellyfin-converted:"
// A sound track converted on its own, beside the video's conversion: a
// second session, so changing track doesn't make the picture again.
#define HLS_AUDIO_LOCATION "jellyfin-converted-audio:"

// Either session's location.
bool hlsIsLocation(const char *path);
// Reads the playlists from the server and starts fetching, from the segment
// holding start_ns. False, with a reason, if it can't. Reading the playlists
// stops when *cancel (may be NULL) becomes true.
bool hlsStart(const char *master_url, const char *authorization, int64_t start_ns, const volatile bool *cancel,
              char *error, size_t error_size);
// Live TV straight from a channel's own HLS source (no sign-in is sent to it),
// at the best variant within max_bitrate (0: no limit). False soon if it
// doesn't answer, so the server can convert the channel instead.
// The same, taking any https certificate when any_certificate (an IPTV list with Certificate check off),
// and sending the headers its list or addon asks for with every request (stream may be NULL).
bool hlsStartDirectAny(const char *url, long long max_bitrate, bool any_certificate, const StreamHeaders *stream,
                       const volatile bool *cancel, char *error, size_t error_size);
// The last hlsStartDirect failed because what came isn't HLS (a stream sent as it is, say).
bool hlsDirectWasNotHls(void);
bool hlsStartDirect(const char *url, long long max_bitrate, const volatile bool *cancel, char *error,
                    size_t error_size);
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
// The sound-alone session (HLS_AUDIO_LOCATION): a server's conversion of one
// track, read from the segment holding start_ns. Its stop.
bool hlsStartAudio(const char *url, const char *authorization, int64_t start_ns, const volatile bool *cancel,
                   char *error, size_t error_size);
void hlsStopAudio(void);
// hlsBaseNs of the session a location names.
int64_t hlsLocationBaseNs(const char *location);
// Opens the running stream of location's session into *fmt (allocated, with
// what the caller set on it, such as an interrupt_callback: while it says so,
// a reader stops waiting for a segment; fmt's opaque is taken). *playlist is
// to be closed with hlsCloseInput after the format.
bool hlsOpenInput(AVFormatContext **fmt, AVIOContext **playlist, const char *location);
void hlsCloseInput(AVIOContext *playlist);

// Subtitles of a channel's own stream (WebVTT lists FFmpeg doesn't read):
// how many, and each one's name and language code.
int hlsSubtitleCount(void);
const char *hlsSubtitleName(int track);
const char *hlsSubtitleLanguage(int track);
// Starts fetching a track's segments near what plays (-1: none).
void hlsSelectSubtitle(int track);
// Where playback is (0: the stream's start), so the subtitles fetched are
// those of what shows, not of where the readers have got to ahead of it.
void hlsSetPlayback(int64_t position_ns);
// The next WebVTT segment of the chosen track (data to be freed); false if none waits.
bool hlsTakeSubtitle(char **data, size_t *size);
