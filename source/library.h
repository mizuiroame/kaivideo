// The videos in the library folder and the folders in it, as films and
// series: found on the card by libraryScan, kept between runs in a cache
// file, and sorted and grouped by libraryBuild. Pure logic over the file
// system.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "naming.h"

#define LIBRARY_PATH_MAX 512
#define LIBRARY_DEPTH 8  // Folders deeper than this aren't looked in.

typedef struct {
    char *path;
    VideoName name;
    int64_t size;
    int64_t added;  // The file's time (seconds since 1970), 0 if unknown.
    int show;       // Its series (an index in shows), or -1 for a film.
} LibraryItem;

typedef struct {
    char name[NAMING_MAX];  // The way most of its files spell it.
    int *episodes;          // Item indexes, by season and episode.
    int count;
    int seasons;            // How many different seasons.
    int64_t added;          // Its newest episode's time.
} LibraryShow;

// A film or episode (an item), or a series.
typedef struct {
    bool is_show;
    int index;
} LibraryRef;

typedef struct {
    char root[LIBRARY_PATH_MAX];
    LibraryItem *items;
    size_t count, capacity;
    // Made by libraryBuild:
    LibraryShow *shows;  // By name.
    size_t show_count;
    int *films;          // Item indexes, by title.
    size_t film_count;
    LibraryRef *recent;  // Films and series, the newest first.
    size_t recent_count;
} Library;

void libraryInit(Library *lib, const char *root);
void libraryFree(Library *lib);

// Adds a video (not yet sorted or grouped). False if out of memory.
bool libraryAdd(Library *lib, const char *path, int64_t size, int64_t added);
// Names, sorts and groups what was added.
void libraryBuild(Library *lib);

// Finds the videos under lib->root (emptying it first) and builds it.
// False if the folder can't be read, or *cancel was set meanwhile.
bool libraryScan(Library *lib, const volatile bool *cancel);

// The cache: the root on the first line, then "size<TAB>time<TAB>path".
bool librarySave(const Library *lib, const char *file);
// Reads a cache (emptying lib first) and builds it. False if there's none.
bool libraryLoad(Library *lib, const char *file);

// The item with this path, or -1.
int libraryFind(const Library *lib, const char *path);

// Series, films and episodes whose names hold the query (compared by
// namingKey): series first, then films, then episodes. Returns how many
// (at most max).
size_t librarySearch(const Library *lib, const char *query, LibraryRef *out, size_t max);
