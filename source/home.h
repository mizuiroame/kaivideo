// The Home screen's rows, worked out from the library and where videos
// were left: Continue watching, Recently added, Series, Films; or, for the
// Showcase style (homeshow.c), Continue watching, Favorites, Films, Series
// and a row for each of the most common genres. Empty rows are left out.
// Pure logic.
#pragma once

#include "library.h"
#include "resume.h"
#include "watched.h"

#define HOME_ROW_MAX 16  // Cards per row.

typedef enum {
    HOME_CONTINUE,  // Videos started and not finished, the latest first; then each
                    // series' next episode after the one watched last ("Up next").
    HOME_FAVORITES, // Series and films marked as favorites (on a server).
    HOME_RECENT,    // Films and series, the newest files first.
    HOME_SERIES,    // By name.
    HOME_FILMS,     // By title (Showcase: the newest first, as its Series).
    HOME_GENRE,     // Showcase: films and series of one genre, the newest first.
    HOME_KIND_COUNT,
} HomeRowKind;

#define HOME_GENRE_ROWS 8                          // Showcase: rows of genres, the most common first.
#define HOME_ROWS_MAX (HOME_KIND_COUNT + HOME_GENRE_ROWS)
#define HOME_GENRE_NAME_MAX 48

typedef struct {
    HomeRowKind kind;
    LibraryRef items[HOME_ROW_MAX];
    bool up_next[HOME_ROW_MAX];  // Continue watching: an episode not started, next in its series.
    int count;
    char genre[HOME_GENRE_NAME_MAX];  // HOME_GENRE's.
    bool wide;  // Showcase: wide pictures across (as the classic style's cards), not upright posters.
} HomeRow;

// Showcase: how its rows' tiles look.
typedef enum {
    HOME_TILES_MIXED,      // Continue watching wide (where it was left shows), the other rows posters.
    HOME_TILES_POSTERS,    // Upright posters, as DVD cases, in every row.
    HOME_TILES_LANDSCAPE,  // Wide pictures in every row.
    HOME_TILES_COUNT,
} HomeTiles;

typedef struct {
    HomeRow rows[HOME_ROWS_MAX];
    int count;  // Rows with something in them.
} Home;

// resume and watched may be NULL.
void homeCompute(Home *home, const Library *lib, const ResumeList *resume, const Watched *watched);
const char *homeRowTitle(HomeRowKind kind);
// A row's title: its kind's, or its genre.
const char *homeRowName(const HomeRow *row);
// Continue watching into row (emptied first): videos started, then each series' next episode.
void homeContinueRow(HomeRow *row, const Library *lib, const ResumeList *resume, const Watched *watched);

// The Showcase style's rows (homeshow.c).
void homeComputeShowcase(Home *home, const Library *lib, const ResumeList *resume, const Watched *watched);
// Marks its rows wide or not for tiles (a HomeTiles).
void homeSetTiles(Home *home, int tiles);
// A series' genres: its first episode's that has some, or NULL.
const char *homeShowGenres(const Library *lib, int show);
// A series' summary, from the one of its episodes that keeps it, or NULL.
const char *homeShowOverview(const Library *lib, int show);
