// Where the cursor is on the library's screens: the tabs (Home, Films,
// Series, Search, Files), the card under it on each, a series' page with its
// seasons, and the search results. Pure logic; ui_library.c draws it.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "home.h"
#include "library.h"
#include "scroll.h"

#define LIBVIEW_COLS 5          // Cards across, on Films and Series.
#define LIBVIEW_GRID_ROWS 3     // Rows of cards in view.
#define LIBVIEW_HOME_COLS 5     // Cards in view across a Home row.
#define LIBVIEW_HOME_ROWS 3     // Home rows in view.
#define LIBVIEW_LIST_ROWS 8     // Episodes or search results in view.
// Their sizes on screen, which a finger moves them by.
#define LIBVIEW_HOME_ROW_PX 196  // A Home row, its title and cards.
#define LIBVIEW_HOME_COL_PX 222  // A Home card and the gap after it.
#define LIBVIEW_GRID_ROW_PX 186  // A row of cards on Films and Series.
#define LIBVIEW_LIST_ROW_PX 58   // An episode or a search result.
#define LIBVIEW_LIVE_ROW_PX 76   // A channel in Live TV's guide.
#define LIBVIEW_RESULTS 200
#define LIBVIEW_SEASONS 100  // Seasons are numbered below 100.
#define LIBVIEW_QUERY_MAX 64

typedef enum {
    TAB_HOME,
    TAB_FILMS,
    TAB_SERIES,
    TAB_SEARCH,
    TAB_FILES,
    TAB_COLLECTION,  // One of a server's libraries (LibView.collection).
    TAB_LIVE,        // A server's Live TV channels.
    TAB_DOWNLOADS,   // The downloads: on their way, on the card, deleted from it.
    TAB_COUNT,
} LibTab;

#define LIBVIEW_TABS (LIBRARY_COLLECTIONS + 6)
#define LIBVIEW_LIVE_ROWS 6  // Channels in view.

typedef struct {
    LibTab kind;
    int collection;  // TAB_COLLECTION's library, -1 for the others.
} LibTabRef;

// The grids: Films, Series, then one per server library from GRID_COLLECTIONS.
typedef enum { GRID_FILMS, GRID_SERIES, GRID_COLLECTIONS, GRID_COUNT = GRID_COLLECTIONS + LIBRARY_COLLECTIONS } LibGrid;



// How many of Home's rows and cards fit in view, and their sizes (which a finger moves them by).
typedef struct {
    int rows, cols;      // In view.
    int row_px, col_px;  // A row with its title, and a card with the gap after it.
    int wide_cols, wide_col_px;  // The same for a row of wide pictures (HomeRow.wide).
} LibHomeLayout;

// The Showcase style's: one row in view under its large picture, with posters across.
#define LIBVIEW_SHOWCASE_ROWS 1
#define LIBVIEW_SHOWCASE_COLS 9
#define LIBVIEW_SHOWCASE_ROW_PX 226
#define LIBVIEW_SHOWCASE_COL_PX 132
#define LIBVIEW_SHOWCASE_WIDE_COLS 4
#define LIBVIEW_SHOWCASE_WIDE_COL_PX 262

typedef struct {
    LibTab tab;
    int collection;               // The library shown, when tab is TAB_COLLECTION.
    LibTabRef tabs[LIBVIEW_TABS]; // The tabs, in order (libviewSetTabs).
    int tab_count;
    int home_row, home_first_row;
    int home_offset;  // Pixels into Home's first row in view (Scroll.offset's).
    Scroll home_cols[HOME_ROWS_MAX];    // Each Home row's place (by row index on screen).
    LibHomeLayout home_layout;          // How Home's rows are laid out (libviewSetHomeShowcase),
    bool showcase;                      // as the Showcase style's.
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
    Scroll live;      // The Live TV channel under the cursor.
    bool iptv;        // The IPTV list's view: its Home is its channels' (app_iptvhome.c).
    // Films and Series showing one category (an Xtream account's): its films' item indexes and its
    // series' show indexes, the app's (libviewSetGridOnly). NULL: all of them.
    const int *only[GRID_COLLECTIONS];
    int only_count[GRID_COLLECTIONS];
    int64_t live_at;  // And the time in its guide (livetv.h): 0 for now.
} LibView;

// Starts on Home, with the card's tabs, its rows laid out as the classic style's.
void libviewInit(LibView *v);
// Home's rows laid out as the Showcase style's (true) or the classic one's.
void libviewSetHomeShowcase(LibView *v, bool showcase);
// The tabs for this library: with a server's libraries, Home, each of them,
// Live TV (live: the server has channels), Search and Files; else Home, Films,
// Series, Search and Files. Downloads goes before Files when asked for. A tab
// that's gone leaves Home showing.
void libviewSetTabs(LibView *v, const Library *lib, bool server, bool live, bool downloads);
// The IPTV list's view: its Home (rows of channels), its Live TV guide, an Xtream account's
// Films, Series and Search (when it has them), and Files.
void libviewSetIptvTabs(LibView *v, const Library *lib);
// Films or Series (grid) shows only these (item indexes for Films, show indexes for Series; kept by
// the caller while they show), or all of them again with NULL. The other views' tabs undo it.
void libviewSetGridOnly(LibView *v, LibGrid grid, const int *refs, int count);
// Shows a tab of that kind (not a server's library). False if there's none.
bool libviewShowTab(LibView *v, LibTab kind);
// Live TV: moves the cursor through count channels.
void libviewLiveMove(LibView *v, int count, int dy);
// Changes tab by delta (wrapping), closing a series' page.
void libviewNextTab(LibView *v, int delta);
// Goes to the index'th tab (a tap on it), closing a series' page.
void libviewGoToTab(LibView *v, int index);
// The index of the tab showing in tabs, -1 if it isn't there.
int libviewTabIndex(const LibView *v);

// The grid the current tab shows, -1 if it isn't one.
int libviewGrid(const LibView *v);
// How many cards a grid has, and one of them.
int libviewGridCount(const LibView *v, const Library *lib, int grid);
LibraryRef libviewGridRef(const LibView *v, const Library *lib, int grid, int index);

// How many of a Home row's cards fit in view, and a card's size with its gap.
int libviewHomeCols(const LibView *v, const Home *home, int row, int *col_px);
// Home: dx moves across the row, dy between rows.
void libviewHomeMove(LibView *v, const Home *home, int dx, int dy);

// A finger moves Home's rows (up and down), or one of them (across), as scrollPx does.
bool libviewHomeScrollPx(LibView *v, const Home *home, int px);
bool libviewHomeRowScrollPx(LibView *v, const Home *home, int row, int px);
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
