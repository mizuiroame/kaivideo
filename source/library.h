// The videos in the library folder and the folders in it, as films and
// series: found on the card by libraryScan, kept between runs in a cache
// file, and sorted and grouped by libraryBuild. Pure logic over the file
// system.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dav.h"
#include "naming.h"

#define LIBRARY_PATH_MAX 512
#define LIBRARY_DEPTH 8  // Folders deeper than this aren't looked in.
// An item's art "poster:<address>": a poster from the web, cropped to fill a card (thumbgen.c).
#define LIBRARY_POSTER_PREFIX "poster:"

typedef struct {
    char *path;
    VideoName name;
    int64_t size;
    int64_t added;  // The file's time (seconds since 1970), 0 if unknown.
    int show;       // Its series (an index in shows), or -1 for a film.
    // For a video on a server (the name then comes from it, not from the path):
    bool named;
    char *overview;      // Its summary, or NULL.
    char *series_art;    // Where its series' picture comes from, or NULL.
    char *art;           // Where its own picture comes from (thumbs.h's key), or NULL: from its file.
    int64_t runtime_ns;  // 0 if unknown.
    int blocked;         // Why the console can't play it (JfPlayability), 0 if it can.
    bool favorite;       // A film marked so, or an episode of a series marked so.
    int collection;      // The server's library it's in (an index in collections), -1 for none.
} LibraryItem;

// What a server says of a video besides its name.
typedef struct {
    const char *overview, *series_art, *art;
    int64_t runtime_ns;
    int blocked;
    bool favorite;
    int in_collection;  // Its library's index in collections plus 1; 0 for none.
} LibraryRemote;

typedef struct {
    char name[NAMING_MAX];  // The way most of its files spell it.
    int *episodes;          // Item indexes, by season and episode.
    int count;
    int seasons;            // How many different seasons.
    int64_t added;          // Its newest episode's time.
    bool favorite;
    int collection;         // Its first episode's.
} LibraryShow;

// A film or episode (an item), or a series.
typedef struct {
    bool is_show;
    int index;
} LibraryRef;

// A library on the server, as its tab shows it.
#define LIBRARY_COLLECTIONS 8
#define LIBRARY_COLLECTION_NAME 48
typedef enum { COLLECTION_MIXED, COLLECTION_FILMS, COLLECTION_SERIES } CollectionKind;
typedef struct {
    char name[LIBRARY_COLLECTION_NAME];
    CollectionKind kind;
    LibraryRef *refs;  // Made by libraryBuild: its series by name, then its films by title.
    size_t count;
} LibraryCollection;

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
    LibraryCollection collections[LIBRARY_COLLECTIONS];  // A server's libraries; none on the card.
    int collection_count;
} Library;

void libraryInit(Library *lib, const char *root);
void libraryFree(Library *lib);

// Adds a video (not yet sorted or grouped). False if out of memory.
bool libraryAdd(Library *lib, const char *path, int64_t size, int64_t added);
// Adds a video that comes with its name.
bool libraryAddRemote(Library *lib, const char *path, const VideoName *name, int64_t size, int64_t added,
                      const LibraryRemote *remote);
// Names, sorts and groups what was added.
void libraryBuild(Library *lib);

// Finds the videos under lib->root (emptying it first) and builds it.
// False if the folder can't be read, or *cancel was set meanwhile.
bool libraryScan(Library *lib, const volatile bool *cancel);

#define LIBRARY_NETWORK_FOLDERS 2000  // A network folder's scan asks for at most this many folders.
#define LIBRARY_NETWORK_VIDEOS 20000

// The same for a network folder (lib->root "dav:/"), listed through list
// (davList's shape: it asks the server, one folder at a time). False if its
// top can't be listed, or *cancel was set.
bool libraryScanNetwork(Library *lib, bool (*list)(const char *path, DavEntry **out, size_t *count, char *why,
                                                   size_t why_size),
                        const volatile bool *cancel);

// The cache: the root on the first line, then "size<TAB>time<TAB>path".
bool librarySave(const Library *lib, const char *file);
// Reads a cache (emptying lib first) and builds it. False if there's none.
bool libraryLoad(Library *lib, const char *file);

// The item with this path, or -1.
int libraryFind(const Library *lib, const char *path);
// The episode after item in its series (the next season's first after a
// season's last), or -1: the last one, or a film.
int libraryNextEpisode(const Library *lib, int item);

// Adds one of the server's libraries. Its index, or -1 (no name, or too many).
int libraryAddCollection(Library *lib, const char *name, CollectionKind kind);
// What a library's tab shows (built): its series by name, then its films by
// title. Returns how many (at most max).
size_t libraryCollectionRefs(const Library *lib, int collection, LibraryRef *out, size_t max);
// Favorite series by name, then favorite films by title. Returns how many (at most max).
size_t libraryFavorites(const Library *lib, LibraryRef *out, size_t max);
// Marks a film, or a series and its episodes.
void librarySetFavorite(Library *lib, LibraryRef ref, bool favorite);

// Series, films and episodes whose names hold the query (compared by
// namingKey): series first, then films, then episodes. Returns how many
// (at most max).
size_t librarySearch(const Library *lib, const char *query, LibraryRef *out, size_t max);
