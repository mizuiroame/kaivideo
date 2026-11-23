// HLS playlists as a Jellyfin server gives them: finding what they point to,
// and rewriting one so its segments have names KaiVideo serves itself. Pure logic.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HLS_URL_MAX 1536
#define HLS_LOCAL_PREFIX "file:kaivideo/"  // Segments become "file:kaivideo/12.ts".
#define HLS_LOCAL_INDEX HLS_LOCAL_PREFIX "index.m3u8"

// ref as seen from base: an absolute address, one from the server's root, or
// one next to base (whose query doesn't count). False if it doesn't fit.
bool hlsResolveUrl(const char *base, const char *ref, char *out, size_t size);
// True when a and b have the same scheme, host and port ("https://h:8920"), compared without
// case. A list may name segments anywhere; the session's Authorization header only goes to
// addresses with the same origin as the list it came from. False for an address with a
// user part ("http://x@h") or without "://".
bool hlsSameOrigin(const char *a, const char *b);
// The first line of a playlist that isn't a tag: the variant of a master list.
// False if there's none.
bool hlsFirstEntry(const char *playlist, char *out, size_t size);

typedef struct {
    char *text;    // The playlist with its segments renamed.
    size_t size;
    char **urls;   // Each segment's real address (all of them).
    int64_t *starts_ns;  // Where each segment starts in the video.
    int count;
    int64_t base_ns;  // Where the renamed list starts in the video.
    bool ended;       // It says it's complete; else (live TV) it grows and is read again.
    int64_t end_ns;          // Where its last segment ends.
    int64_t longest_ns;      // Its longest segment.
    int64_t sequence;        // The media sequence number of urls[0].
    int first;               // The first segment in text.
    int list;                // Its number among a master list's (hlsmaster.h), -1 when alone.
    bool subtitles;          // Its segments are WebVTT.
    char *map_url;           // fMP4: its initialization section (#EXT-X-MAP), which FFmpeg reads first;
                             // its text names it HLS_LOCAL_PREFIX "init.mp4" ("list/init.mp4"). NULL for none.
} HlsPlaylist;

// The variant of a master list that the console plays best: up to 1080p,
// H.264 before HEVC, a picture before sound alone, and the most within
// max_bitrate (0: no limit; if none fits, the lightest). False if playlist
// isn't a master list.
bool hlsBestVariant(const char *playlist, long long max_bitrate, char *out, size_t size);

// From a media playlist found at base. The renamed list starts at the segment
// holding start_ns, so nothing before it has to be made. False if it has no segments.
bool hlsPlaylistParse(const char *playlist, const char *base, int64_t start_ns, HlsPlaylist *out);
// The same for list number list of a master list: its segments are named
// HLS_LOCAL_PREFIX "list/n.ts" (".vtt" for subtitles).
bool hlsPlaylistParseList(const char *playlist, const char *base, int64_t start_ns, int list, bool subtitles,
                          HlsPlaylist *out);
// A live list read again: the segments in it that p doesn't have yet (by
// media sequence, so a list that drops its old segments works too) are added
// at its end, and its text is made again. How many were added, -1 if the
// list can't be read (p unchanged).
int hlsPlaylistAppend(HlsPlaylist *p, const char *playlist, const char *base);
// Leaves the segments before first out of p's text (a live list read for
// hours); they keep their numbers. False if out of memory.
bool hlsPlaylistTrim(HlsPlaylist *p, int first);
void hlsPlaylistFree(HlsPlaylist *p);
// The segments holding length_ns of video from from_ns on; past the end, the last one.
void hlsPlaylistSpan(const HlsPlaylist *p, int64_t from_ns, int64_t length_ns, int *first, int *last);
// The segment number of a name made by hlsPlaylistParse, or -1.
int hlsLocalSegment(const char *url);
// A local name: the list (0 for a list alone) and segment, -1 for the
// list's own "list/index.m3u8", or HLS_INIT_INDEX for its "init.mp4". False if it isn't one.
#define HLS_INIT_INDEX (-2)
bool hlsLocalName(const char *url, int *list, int *index);
