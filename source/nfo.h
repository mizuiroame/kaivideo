// What a Kodi-style .nfo file (as Jellyfin, Emby and tinyMediaManager write
// them too) says of a film (<movie>), a series (<tvshow>) or an episode
// (<episodedetails>): title, year, plot, genres, length, season and episode.
// Tolerant: a broken or cut file gives what could be read; a file that isn't
// one of those (a bare web address) gives nothing. Pure logic.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "genres.h"
#include "naming.h"

#define NFO_MAX_BYTES (256 * 1024)  // What's read of a file at most: past it, the start is enough.
#define NFO_PLOT_MAX 1000           // What's kept of a plot, in bytes (cut whole characters).

typedef enum { NFO_NONE, NFO_MOVIE, NFO_TVSHOW, NFO_EPISODE } NfoKind;

typedef struct {
    NfoKind kind;
    char title[NAMING_MAX];       // "" if none.
    int year;                     // 0 if unknown (from <year>, else <premiered> or <aired>).
    int season, episode;          // An episode's; -1 if unknown.
    int runtime_min;              // 0 if unknown.
    char genres[GENRES_TEXT_MAX]; // "Drama, Comedy" (at most GENRES_EACH_MAX), "" for none.
    char plot[NFO_PLOT_MAX];      // Its <plot>, else its <outline>; on one line.
} NfoInfo;

// Reads text (length bytes, no NUL needed). False (out->kind NFO_NONE) if it isn't a film's, series' or
// episode's. Every text in out is valid UTF-8 on one line, without tabs.
bool nfoParse(const char *text, size_t length, NfoInfo *out);
