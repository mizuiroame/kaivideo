// Moving around the library's screens.
#include <stdio.h>
#include <string.h>

#include "check.h"
#include "libview.h"

static void buildLibrary(Library *lib)
{
    libraryInit(lib, "sdmc:/V");
    char path[128];
    for (int i = 1; i <= 12; i++) {  // 12 films.
        snprintf(path, sizeof(path), "sdmc:/V/Film %02d (2001).mkv", i);
        libraryAdd(lib, path, 1, i);
    }
    for (int s = 1; s <= 3; s++)  // A series: 3 seasons of 10.
        for (int e = 1; e <= 10; e++) {
            snprintf(path, sizeof(path), "sdmc:/V/Show/Show.S%02dE%02d.mkv", s, e);
            libraryAdd(lib, path, 1, 100);
        }
    libraryBuild(lib);
}

// A server's libraries are tabs of their own, each a grid of its films and series.
static void testCollectionTabs(void)
{
    static Library lib;
    libraryInit(&lib, "jellyfin:");
    int films = libraryAddCollection(&lib, "Películas", COLLECTION_FILMS);
    int shows = libraryAddCollection(&lib, "Programas", COLLECTION_SERIES);
    char path[64];
    for (int i = 0; i < 7; i++) {
        VideoName n = {.year = 2000 + i};
        snprintf(n.title, sizeof(n.title), "Film %d", i);
        snprintf(path, sizeof(path), "jellyfin:f%d/f%d", i, i);
        libraryAddRemote(&lib, path, &n, 1, i, &(LibraryRemote){.in_collection = films + 1});
    }
    for (int s = 0; s < 2; s++) {
        VideoName n = {.is_episode = true, .season = 1, .episode = 1};
        snprintf(n.show, sizeof(n.show), "Show %d", s);
        snprintf(path, sizeof(path), "jellyfin:e%d/e%d", s, s);
        libraryAddRemote(&lib, path, &n, 1, 1, &(LibraryRemote){.in_collection = shows + 1});
    }
    libraryBuild(&lib);
    CHECK(lib.collections[films].count == 7 && lib.collections[shows].count == 2);

    static LibView v;
    libviewInit(&v);
    libviewSetTabs(&v, &lib, true, false);
    CHECK(v.tab_count == 5 && v.tabs[1].kind == TAB_COLLECTION && v.tabs[1].collection == films &&
          v.tabs[2].collection == shows && v.tabs[3].kind == TAB_SEARCH && v.tabs[4].kind == TAB_FILES);
    libviewNextTab(&v, 1);
    CHECK(v.tab == TAB_COLLECTION && v.collection == films && libviewGrid(&v) == GRID_COLLECTIONS + films);
    int grid = libviewGrid(&v);
    CHECK(libviewGridCount(&v, &lib, grid) == 7);
    libviewGridMove(&v, grid, 7, 1, 1);  // To the second row, second card: the 7th.
    CHECK(v.grid[grid].cursor == LIBVIEW_COLS + 1 || v.grid[grid].cursor == 6);
    static Home home;
    homeCompute(&home, &lib, NULL, NULL);
    LibraryRef ref;
    CHECK(libviewSelected(&v, &lib, &home, &ref) && !ref.is_show &&
          ref.index == lib.collections[films].refs[v.grid[grid].cursor].index);
    libviewNextTab(&v, 1);
    CHECK(v.collection == shows && libviewSelected(&v, &lib, &home, &ref) && ref.is_show);
    libviewGoToTab(&v, 0);
    CHECK(v.tab == TAB_HOME && libviewGrid(&v) == -1);
    libviewNextTab(&v, -1);  // Wraps to the last: Files.
    CHECK(v.tab == TAB_FILES);
    v.tab = TAB_SEARCH;      // Set by name: next goes on from there.
    libviewNextTab(&v, -1);
    CHECK(v.tab == TAB_COLLECTION && v.collection == shows);

    // With Live TV, its tab comes after the libraries.
    libviewSetTabs(&v, &lib, true, true);
    CHECK(v.tab_count == 6 && v.tabs[3].kind == TAB_LIVE && v.tabs[4].kind == TAB_SEARCH);
    libviewGoToTab(&v, 3);
    libviewLiveMove(&v, 20, 9);
    CHECK(v.tab == TAB_LIVE && libviewGrid(&v) == -1 && v.live.cursor == 9 && v.live.first == 9 - LIBVIEW_LIVE_ROWS + 1);
    libviewLiveMove(&v, 3, 0);  // Fewer channels now: the cursor stays on one.
    CHECK(v.live.cursor == 2 && v.live.first == 0);

    // The card's tabs, and back: a tab that's gone falls back to Home.
    libviewSetTabs(&v, &lib, false, false);
    CHECK(v.tab_count == 5 && v.tabs[1].kind == TAB_FILMS && v.tab == TAB_HOME);
    libraryFree(&lib);
}

void testLibView(void)
{
    testCollectionTabs();
    static Library lib;
    buildLibrary(&lib);
    static LibView v;
    libviewInit(&v);
    CHECK(v.tab == TAB_HOME && v.show == -1);
    libviewNextTab(&v, -1);
    CHECK(v.tab == TAB_FILES);
    libviewNextTab(&v, 2);
    CHECK(v.tab == TAB_FILMS);

    // The grid of 12 films, 5 across: down twice reaches the short last row.
    libviewGridMove(&v, GRID_FILMS, 12, 4, 0);
    CHECK(v.grid[GRID_FILMS].cursor == 4);
    libviewGridMove(&v, GRID_FILMS, 12, 0, 1);
    libviewGridMove(&v, GRID_FILMS, 12, 0, 1);
    CHECK(v.grid[GRID_FILMS].cursor == 11);
    libviewGridMove(&v, GRID_FILMS, 12, 1, 0);  // Past the end: stays.
    CHECK(v.grid[GRID_FILMS].cursor == 11);
    LibraryRef ref;
    static Home home;
    homeCompute(&home, &lib, NULL, NULL);
    CHECK(libviewSelected(&v, &lib, &home, &ref) && !ref.is_show && ref.index == lib.films[11]);

    // A series' page: seasons, and the episodes of each.
    int seasons[8];
    CHECK(libviewSeasons(&lib, 0, seasons, 8) == 3 && seasons[2] == 3);
    int first, count;
    libviewSeasonRange(&lib, 0, 1, &first, &count);
    CHECK(first == 10 && count == 10);
    int item = lib.shows[0].episodes[24];  // S03E05.
    libviewOpenShow(&v, &lib, 0, item);
    CHECK(v.show == 0 && v.season == 2 && v.episodes.cursor == 4 && libviewShowItem(&v, &lib) == item);
    libviewShowMove(&v, &lib, 0, 20);  // Down past the end: the last.
    CHECK(v.episodes.cursor == 9 && v.episodes.first == 10 - LIBVIEW_LIST_ROWS);
    libviewShowMove(&v, &lib, -1, 0);  // Another season starts at its top.
    CHECK(v.season == 1 && v.episodes.cursor == 0 && v.episodes.first == 0);
    CHECK(libviewSelected(&v, &lib, &home, &ref) && ref.index == lib.shows[0].episodes[10]);
    // A new scan: the open series is found again by name, its season kept in range.
    v.season = 7;
    libviewClamp(&v, &lib, &home);
    CHECK(v.show == 0 && v.season == 2);
    libviewNextTab(&v, 1);
    CHECK(v.show == -1);

    // Home: rows and their own places.
    v.tab = TAB_HOME;
    libviewHomeMove(&v, &home, 0, 1);  // Recently added -> Series.
    libviewHomeMove(&v, &home, 3, 0);
    CHECK(v.home_row == 1 && v.home_cols[1].cursor == 0);  // One series: stays on it.
    libviewHomeMove(&v, &home, 0, 1);
    libviewHomeMove(&v, &home, 9, 0);
    CHECK(v.home_row == 2 && v.home_cols[2].cursor == 9 && v.home_cols[2].first == 9 - LIBVIEW_HOME_COLS + 1);
    CHECK(libviewSelected(&v, &lib, &home, &ref) && ref.index == lib.films[9]);

    // Search.
    v.tab = TAB_SEARCH;
    libviewSearch(&v, &lib, "film 1");
    CHECK(v.result_count == 3);  // Film 10, 11, 12.
    libviewResultsMove(&v, 5);
    CHECK(v.results_scroll.cursor == 2);

    // A smaller library afterwards: the cursors stay inside it.
    libraryFree(&lib);
    libraryInit(&lib, "sdmc:/V");
    libraryAdd(&lib, "sdmc:/V/Only (2000).mkv", 1, 1);
    libraryBuild(&lib);
    homeCompute(&home, &lib, NULL, NULL);
    v.show = 0;
    libviewClamp(&v, &lib, &home);
    CHECK(v.show == -1 && v.grid[GRID_FILMS].cursor == 0 && v.result_count == 0 && v.home_row <= 1);
    libraryFree(&lib);
}
