// A Live TV channel's own HLS master list: the variant KaiVideo plays and the
// sound tracks and subtitles that go with it in lists of their own, and the
// master list (with KaiVideo's own names) that FFmpeg reads for them all.
// Pure logic.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "hlslist.h"

#define HLS_LISTS_MAX 8          // The variant's list (0) and those of its tracks.
#define HLS_RENDITIONS_MAX 12
#define HLS_NAME_MAX 64
#define HLS_LANGUAGE_MAX 16
#define HLS_CODECS_MAX 128

typedef enum { HLS_RENDITION_AUDIO, HLS_RENDITION_SUBTITLES } HlsRenditionType;

typedef struct {
    HlsRenditionType type;
    char name[HLS_NAME_MAX];
    char language[HLS_LANGUAGE_MAX];
    char url[HLS_URL_MAX];  // "" for a sound track inside the variant itself.
    int list;               // Its list, 0 when it has none of its own.
    bool is_default;
} HlsRendition;

typedef struct {
    char variant_url[HLS_URL_MAX];
    long long bandwidth;
    char codecs[HLS_CODECS_MAX];
    HlsRendition renditions[HLS_RENDITIONS_MAX];  // The variant's own groups only.
    int count;
    int list_count;  // The variant's list and one for each rendition with its own.
} HlsMaster;

// The best variant of a master list found at base (as hlsBestVariant chooses,
// within max_bitrate) and its sound tracks and subtitles. False if playlist
// isn't a master list.
bool hlsMasterParse(const char *playlist, const char *base, long long max_bitrate, HlsMaster *out);
// The address of a list: 0 is the variant's.
const char *hlsMasterListUrl(const HlsMaster *m, int list);
bool hlsMasterListIsSubtitles(const HlsMaster *m, int list);
// The master list FFmpeg reads, naming list n HLS_LOCAL_PREFIX "n/index.m3u8".
// False if it doesn't fit in size.
bool hlsLocalMaster(const HlsMaster *m, char *out, size_t size);
