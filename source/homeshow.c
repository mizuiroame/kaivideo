// Home's Showcase rows: Continue watching, Favorites, Films and Series (the
// newest first), then a row for each of the most common genres.
#include "home.h"

#include <stdlib.h>
#include <string.h>

#include "genres.h"

#define DISTINCT_MAX 64       // Genres counted; more are left out.
#define GENRE_MIN_ITEMS 2     // A genre with fewer films and series gets no row.

typedef struct {
    char name[HOME_GENRE_NAME_MAX];
    int count;
} GenreCount;

const char *homeShowGenres(const Library *lib, int show)
{
    const LibraryShow *s = &lib->shows[show];
    for (int i = 0; i < s->count; i++)
        if (lib->items[s->episodes[i]].genres)
            return lib->items[s->episodes[i]].genres;
    return NULL;
}

const char *homeShowOverview(const Library *lib, int show)
{
    const LibraryShow *s = &lib->shows[show];
    for (int i = 0; i < s->count; i++)
        if (lib->items[s->episodes[i]].series_overview)
            return lib->items[s->episodes[i]].series_overview;
    return NULL;
}

static const char *refGenres(const Library *lib, LibraryRef ref)
{
    return ref.is_show ? homeShowGenres(lib, ref.index) : lib->items[ref.index].genres;
}

static bool hasGenre(const char *genres, const char *name)
{
    char one[HOME_GENRE_NAME_MAX];
    for (int i = 0; genresAt(genres, i, one, sizeof(one)); i++)
        if (!strcmp(one, name))
            return true;
    return false;
}

static void countGenres(const char *genres, GenreCount *counts, int *distinct)
{
    char one[HOME_GENRE_NAME_MAX];
    for (int i = 0; genresAt(genres, i, one, sizeof(one)); i++) {
        int at = 0;
        while (at < *distinct && strcmp(counts[at].name, one))
            at++;
        if (at == *distinct) {
            if (*distinct == DISTINCT_MAX)
                continue;
            memcpy(counts[at].name, one, sizeof(one));
            counts[at].count = 0;
            (*distinct)++;
        }
        counts[at].count++;
    }
}

// The most films and series first; the same count by name, so the order stays put.
static int compareCounts(const void *a, const void *b)
{
    const GenreCount *x = a, *y = b;
    if (x->count != y->count)
        return y->count - x->count;
    return strcmp(x->name, y->name);
}

// One kind of the newest-first refs (films, or series) into a row.
static void addRecent(Home *home, const Library *lib, HomeRowKind kind, bool shows)
{
    HomeRow *row = &home->rows[home->count];
    memset(row, 0, sizeof(*row));
    row->kind = kind;
    for (size_t i = 0; i < lib->recent_count && row->count < HOME_ROW_MAX; i++)
        if (lib->recent[i].is_show == shows)
            row->items[row->count++] = lib->recent[i];
    home->count += row->count > 0;
}

static void addGenreRows(Home *home, const Library *lib)
{
    GenreCount *counts = calloc(DISTINCT_MAX, sizeof(GenreCount));
    if (!counts)
        return;
    int distinct = 0;
    for (size_t i = 0; i < lib->recent_count; i++)
        countGenres(refGenres(lib, lib->recent[i]), counts, &distinct);
    qsort(counts, (size_t)distinct, sizeof(GenreCount), compareCounts);
    for (int g = 0; g < distinct && g < HOME_GENRE_ROWS && counts[g].count >= GENRE_MIN_ITEMS; g++) {
        HomeRow *row = &home->rows[home->count];
        memset(row, 0, sizeof(*row));
        row->kind = HOME_GENRE;
        memcpy(row->genre, counts[g].name, sizeof(row->genre));
        for (size_t i = 0; i < lib->recent_count && row->count < HOME_ROW_MAX; i++)
            if (hasGenre(refGenres(lib, lib->recent[i]), row->genre))
                row->items[row->count++] = lib->recent[i];
        home->count += row->count > 0;
    }
    free(counts);
}

void homeComputeShowcase(Home *home, const Library *lib, const ResumeList *resume, const Watched *watched)
{
    memset(home, 0, sizeof(*home));
    homeContinueRow(&home->rows[0], lib, resume, watched);
    home->count += home->rows[0].count > 0;

    HomeRow *row = &home->rows[home->count];
    memset(row, 0, sizeof(*row));
    row->kind = HOME_FAVORITES;
    row->count = (int)libraryFavorites(lib, row->items, HOME_ROW_MAX);
    home->count += row->count > 0;

    addRecent(home, lib, HOME_FILMS, false);
    addRecent(home, lib, HOME_SERIES, true);
    addGenreRows(home, lib);
}

void homeSetTiles(Home *home, int tiles)
{
    for (int r = 0; r < home->count; r++)
        home->rows[r].wide = tiles == HOME_TILES_LANDSCAPE ||
                             (tiles != HOME_TILES_POSTERS && home->rows[r].kind == HOME_CONTINUE);
}
