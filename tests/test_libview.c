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

void testLibView(void)
{
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
    homeCompute(&home, &lib, NULL);
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
    homeCompute(&home, &lib, NULL);
    v.show = 0;
    libviewClamp(&v, &lib, &home);
    CHECK(v.show == -1 && v.grid[GRID_FILMS].cursor == 0 && v.result_count == 0 && v.home_row <= 1);
    libraryFree(&lib);
}
