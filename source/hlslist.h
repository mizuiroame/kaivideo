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
} HlsPlaylist;

// From a media playlist found at base. The renamed list starts at the segment
// holding start_ns, so nothing before it has to be made. False if it has no segments.
bool hlsPlaylistParse(const char *playlist, const char *base, int64_t start_ns, HlsPlaylist *out);
void hlsPlaylistFree(HlsPlaylist *p);
// The segments holding length_ns of video from from_ns on; past the end, the last one.
void hlsPlaylistSpan(const HlsPlaylist *p, int64_t from_ns, int64_t length_ns, int *first, int *last);
// The segment number of a name made by hlsPlaylistParse, or -1.
int hlsLocalSegment(const char *url);
