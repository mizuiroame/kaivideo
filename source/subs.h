// Subtitles as timed lines of text: read from an .srt file next to the
// video, or added as FFmpeg decodes a text track inside it (SubRip, ASS,
// MP4's mov_text). Shown as plain text: styling tags are left out. Pure logic.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SUBS_CUES_MAX 200000  // Lines kept per track; a film has a few thousand.
#define SUBS_HOURS_MAX 99999

typedef struct {
    int64_t start_ns, end_ns;
    char *text;  // UTF-8, lines separated by '\n'.
} SubCue;

typedef struct {
    SubCue *cues;  // By start time.
    size_t count, capacity;
} SubTrack;

void subsFree(SubTrack *t);

// Adds a line (copied and cleaned of tags), keeping the track in order; the
// same text at the same time again is left out (a track is read again after a seek).
bool subsAdd(SubTrack *t, int64_t start_ns, int64_t end_ns, const char *text);

// Reads SubRip text (UTF-8, with or without a BOM) into t. False if no line could be read.
bool subsParseSrt(SubTrack *t, const char *data, size_t size);
bool subsLoadSrtFile(SubTrack *t, const char *path);

// The text shown at pos_ns (lines on at once joined by '\n'), into out; false if none.
bool subsAt(const SubTrack *t, int64_t pos_ns, char *out, size_t size);

// The text of one of FFmpeg's ASS lines ("ReadOrder,Layer,Style,Name,
// MarginL,MarginR,MarginV,Effect,Text"): the part after the eighth comma.
const char *subsAssText(const char *ass);

// Takes styling out, in place: {\...} ASS tags and <...> HTML ones; "\N"
// and "\n" become line breaks; spaces at the ends of lines go.
void subsClean(char *text);

// The name of a language from its ISO 639 code ("spa", "es" -> "Spanish"), for
// the common ones; any other code comes back as it is, NULL or "" as NULL.
const char *subsLanguageName(const char *code);

// A track's name in a list (subtitles or sound): its title, else its
// language's name, else "Track <number>". "und" counts as no language.
void subsTrackName(char *out, size_t size, const char *title, const char *language, int number);

// The .srt file for a video: "name.srt", else "name.<anything>.srt" (by
// name), in its folder. False if there's none.
bool subsFindFor(const char *video_path, char *out, size_t size);
