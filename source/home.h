// The Home screen's rows, worked out from the library and where videos
// were left: Continue watching, Recently added, Series, Films; or, for the
// Showcase style (homeshow.c), Continue watching, Favorites, Films, Series
// and a row for each of the most common genres; or, for the IPTV list's
// Showcase (homeiptv.c), its rows of channels, then an Xtream account's
// films and series. Empty rows are left out. Pure logic.
#pragma once

#include "iptvhome.h"
#include "library.h"
#include "resume.h"
#include "watched.h"

#define HOME_ROW_MAX 16    // Cards per row of the library's.
#define HOME_ROW_ITEMS 40  // Room in a row: the IPTV list's rows of channels have more.

typedef enum {
    HOME_CONTINUE,  // Videos started and not finished, the latest first; then each
                    // series' next episode after the one watched last ("Up next").
    HOME_FAVORITES, // Series and films marked as favorites (on a server).
    HOME_RECENT,    // Films and series, the newest files first.
    HOME_SERIES,    // By name.
    HOME_FILMS,     // By title (Showcase: the newest first, as its Series).
    HOME_GENRE,     // Showcase: films and series of one genre, the newest first.
    // The IPTV list's Showcase: its rows of channels (HomeRow.channels),
    HOME_CHANNELS_RECENT,     // the channels watched last,
    HOME_CHANNELS_FAVORITES,  // the favorite ones,
    HOME_CHANNELS,            // one group's (its name in genre), or all of them;
    HOME_CATEGORY,            // and an Xtream account's films of one category (its name in genre).
    HOME_KIND_COUNT,
} HomeRowKind;

#define HOME_GENRE_ROWS 8     // Showcase: rows of genres, the most common first.
#define HOME_ROWS_MAX 128     // The IPTV list's groups and categories past this get no row.
#define HOME_GENRE_NAME_MAX 64

typedef struct {
    HomeRowKind kind;
    LibraryRef items[HOME_ROW_ITEMS];  // A row of channels': index is a channel of the IPTV list.
    bool up_next[HOME_ROW_ITEMS];  // Continue watching: an episode not started, next in its series.
    int count;
    char genre[HOME_GENRE_NAME_MAX];  // HOME_GENRE's, HOME_CHANNELS' group, HOME_CATEGORY's.
    bool wide;      // Showcase: wide pictures across (as the classic style's cards), not upright posters.
    bool channels;  // Its items are the IPTV list's channels, not the library's.
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
// A row's title: its kind's, or its genre, group or category.
const char *homeRowName(const HomeRow *row);
// Continue watching into row (emptied first): videos started, then each series' next episode.
void homeContinueRow(HomeRow *row, const Library *lib, const ResumeList *resume, const Watched *watched);

// The Showcase style's rows (homeshow.c).
void homeComputeShowcase(Home *home, const Library *lib, const ResumeList *resume, const Watched *watched);
// Marks its rows wide or not for tiles (a HomeTiles). Rows of channels are always wide.
void homeSetTiles(Home *home, int tiles);
// A series' genres: its first episode's that has some, or NULL.
const char *homeShowGenres(const Library *lib, int show);
// A series' summary, from the one of its episodes that keeps it, or NULL.
const char *homeShowOverview(const Library *lib, int show);

// The IPTV list's Showcase (homeiptv.c): channels' rows of its Home (Recently watched, Favorite
// channels, each group, as iptvHomeBuild made them), then lib's films and series (an Xtream
// account's, the newest first) and a row for each of film_groups (its film categories) with films.
void homeComputeIptv(Home *home, const IptvHome *channels, const IptvList *list, const Library *lib,
                     const IptvGroup *film_groups, int film_group_count);
// The row's place in another Home of the same source: the row of the same kind and name, -1 if none.
int homeFindRow(const Home *home, const HomeRow *row);
