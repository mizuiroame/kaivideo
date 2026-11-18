// Where the cursor is on the library's screens: the tabs (Home, Films,
// Series, Search, Files), the card under it on each, a series' page with its
// seasons, and the search results. Pure logic; ui_library.c draws it.
#pragma once

#include <stdbool.h>

#include "home.h"
#include "library.h"

#define LIBVIEW_COLS 5          // Cards across, on Films and Series.
#define LIBVIEW_GRID_ROWS 3     // Rows of cards in view.
#define LIBVIEW_HOME_COLS 5     // Cards in view across a Home row.
#define LIBVIEW_HOME_ROWS 3     // Home rows in view.
#define LIBVIEW_LIST_ROWS 8     // Episodes or search results in view.
#define LIBVIEW_RESULTS 200
#define LIBVIEW_SEASONS 100  // Seasons are numbered below 100.
#define LIBVIEW_QUERY_MAX 64

typedef enum {
    TAB_HOME,
    TAB_FILMS,
    TAB_SERIES,
    TAB_SEARCH,
    TAB_FILES,
    TAB_COUNT,
} LibTab;

typedef enum { GRID_FILMS, GRID_SERIES, GRID_COUNT } LibGrid;

typedef struct {
    int cursor, first;  // The item under the cursor, the first in view.
} Scroll;

typedef struct {
    LibTab tab;
    int home_row, home_first_row;
    Scroll home_cols[HOME_KIND_COUNT];  // Each Home row's place (by row index on screen).
    Scroll grid[GRID_COUNT];            // first is a row of cards here.
    // A series' page, over the tab it was opened from (-1: none).
    int show;
    int details;  // The item whose details are open over the screen, or -1.
    char show_name[NAMING_MAX];  // To find it again after a new scan.
    int season;  // An index in its seasons.
    Scroll episodes;
    // Search.
    char query[LIBVIEW_QUERY_MAX];
    LibraryRef results[LIBVIEW_RESULTS];
    int result_count;
    Scroll results_scroll;
} LibView;

void libviewInit(LibView *v);
// Changes tab by delta (wrapping), closing a series' page.
void libviewNextTab(LibView *v, int delta);

// Home: dx moves across the row, dy between rows.
void libviewHomeMove(LibView *v, const Home *home, int dx, int dy);
// Films or Series: moves the cursor in a grid of count cards.
void libviewGridMove(LibView *v, LibGrid grid, int count, int dx, int dy);

// The seasons of a series (their numbers, in order). Returns how many.
int libviewSeasons(const Library *lib, int show, int *out, int max);
// The episodes of its season'th season: where they start in its episodes, and how many.
void libviewSeasonRange(const Library *lib, int show, int season, int *first, int *count);
// Opens a series' page on its first season, or the one of episode item if
// it's one of its episodes (-1 for none), with the cursor on it.
void libviewOpenShow(LibView *v, const Library *lib, int show, int item);
// On a series' page: dx changes season, dy moves through its episodes.
void libviewShowMove(LibView *v, const Library *lib, int dx, int dy);
// The item under the cursor on a series' page, or -1.
int libviewShowItem(const LibView *v, const Library *lib);

// Searches for query and puts the cursor on the first result.
void libviewSearch(LibView *v, const Library *lib, const char *query);
void libviewResultsMove(LibView *v, int dy);

// Keeps a library's cursors within it after it changed (a new scan).
void libviewClamp(LibView *v, const Library *lib, const Home *home);

// What A opens on the current screen (not Files): false if nothing.
bool libviewSelected(const LibView *v, const Library *lib, const Home *home, LibraryRef *out);
