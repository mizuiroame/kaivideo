// Genres as the servers and addons name them ("Drama, Comedy"), for Home's
// Showcase rows: read from their answers, and kept by series for the
// episodes that come without their own. Pure logic.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <jansson.h>

#define GENRES_TEXT_MAX 96  // "Drama, Comedy, Thriller": what's kept of an item's.
#define GENRES_ID_MAX 48    // A series' id on its server.
#define GENRES_EACH_MAX 3   // Genres kept of one item.

// The names in a JSON array: strings (Jellyfin's Genres, an addon's genres), or objects whose key
// holds the name (Plex's Genre: [{"tag": "Drama"}], key "tag"; NULL for strings). At most
// GENRES_EACH_MAX, joined by ", " into out ("" for none).
void genresJoin(const json_t *array, const char *key, char *out, size_t size);

// Each series' genres and summary, by its id.
typedef struct {
    char id[GENRES_ID_MAX];
    char genres[GENRES_TEXT_MAX];
    char *overview;  // Or NULL.
    bool given;      // Its summary went to one of its episodes already.
} GenreEntry;

typedef struct {
    GenreEntry *entries;
    int count, capacity;
    bool sorted;
} GenreTable;

void genreTableFree(GenreTable *t);
// Keeps a series' genres and summary (either may be "" or NULL; with neither it's skipped). False if
// out of memory or the id is too long.
bool genreTableAdd(GenreTable *t, const char *id, const char *genres, const char *overview);
// A series' place in the table, or -1. The first look after adding sorts it.
int genreTableFind(GenreTable *t, const char *id);
// Its genres (NULL for none), at a place found.
const char *genreTableGenres(const GenreTable *t, int at);
// Its summary the first time it's asked for (its first episode keeps it for the series), then NULL.
const char *genreTableTakeOverview(GenreTable *t, int at);

// The genre at index in "Drama, Comedy" (0: "Drama"), into out. False past the last.
bool genresAt(const char *genres, int index, char *out, size_t size);
