// What a library folder's scan (library.c) takes from the files beside its
// videos: Kodi-style .nfo files (nfo.h) and pictures named after a video
// ("<video>-poster.jpg", "<video>-fanart.jpg", "<video>-thumb.jpg"), or the
// folder's own (movie.nfo, tvshow.nfo, fanart.jpg, backdrop.jpg,
// background.jpg). Read only while scanning, never while drawing.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "library.h"

// The .nfo files and pictures of one folder, by name.
typedef struct {
    char **names;
    size_t count, capacity;
} LibFolderFiles;

// Keeps name if it's an .nfo file or a picture (.jpg, .jpeg, .png; any case).
void libFolderNote(LibFolderFiles *files, const char *name);
void libFolderFree(LibFolderFiles *files);

// For the videos found from start on (folder's own, and its season folders' for what a series' folder
// holds): their .nfo files' names, years, plots, genres and lengths, and their pictures. top: the
// library's own folder (its tvshow.nfo and fanart aren't any one series'). read: how a file is read
// (whole, up to max_bytes); *budget: files it may still read (each one read takes one).
void libnfoApply(Library *lib, size_t start, const char *folder, bool top, const LibFolderFiles *files,
                 LibraryReadFile read, int *budget);

// Reads a file on the card: its first max_bytes at most, NUL-terminated (free it). NULL if it can't.
char *libnfoReadCard(const char *location, size_t max_bytes, size_t *size);

// True if path is right in folder, not in a folder inside it.
bool libDirectlyIn(const char *path, const char *folder);
// True if path is in a season's folder ("Season 01", "S2", "Temporada 3", "Specials": a name with a
// digit, or Specials) right in folder: the series' folder.
bool libInSeasonOf(const char *path, const char *folder);
