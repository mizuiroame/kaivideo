#include "home.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static HomeRow *addRow(Home *home, HomeRowKind kind)
{
    HomeRow *row = &home->rows[home->count];
    memset(row, 0, sizeof(*row));
    row->kind = kind;
    return row;
}

static bool showInRow(const Library *lib, const HomeRow *row, int show)
{
    for (int i = 0; i < row->count; i++)
        if (!row->items[i].is_show && lib->items[row->items[i].index].show == show)
            return true;
    return false;
}

static const Library *g_lib;  // qsort and bsearch have no context: the library looked in.

static int compareItemPaths(const void *a, const void *b)
{
    return strcmp(g_lib->items[*(const int *)a].path, g_lib->items[*(const int *)b].path);
}

static int compareKeyPath(const void *key, const void *elem)
{
    return strcmp((const char *)key, g_lib->items[*(const int *)elem].path);
}

// Each series' episode after the one watched last, if it isn't watched,
// started, or its series in the row already: the series watched most
// recently first. The watched list is gone through from its end once, each
// path found by a binary search, so a big library and history stay quick.
static void addUpNext(HomeRow *row, const Library *lib, const ResumeList *resume, const Watched *watched)
{
    int *by_path = malloc((lib->count ? lib->count : 1) * sizeof(int));
    bool *seen = calloc(lib->show_count ? lib->show_count : 1, sizeof(bool));
    if (!by_path || !seen) {
        free(by_path);
        free(seen);
        return;
    }
    for (size_t i = 0; i < lib->count; i++)
        by_path[i] = (int)i;
    g_lib = lib;
    qsort(by_path, lib->count, sizeof(int), compareItemPaths);
    for (size_t w = watched->count; w-- > 0 && row->count < HOME_ROW_MAX;) {
        const int *found = bsearch(watched->paths[w], by_path, lib->count, sizeof(int), compareKeyPath);
        int item = found ? *found : -1;
        int show = item >= 0 ? lib->items[item].show : -1;
        if (show < 0 || seen[show])
            continue;  // Not in the library, a film, or its series' latest already seen.
        seen[show] = true;
        int next = libraryNextEpisode(lib, item);
        while (next >= 0 && watchedHas(watched, lib->items[next].path))
            next = libraryNextEpisode(lib, next);  // Watched further on already: the one after.
        int64_t at;
        if (next < 0 || showInRow(lib, row, show) || (resume && resumeFind(resume, lib->items[next].path, &at, NULL)))
            continue;
        row->up_next[row->count] = true;
        row->items[row->count++] = (LibraryRef){false, next};
    }
    free(by_path);
    free(seen);
}

void homeCompute(Home *home, const Library *lib, const ResumeList *resume, const Watched *watched)
{
    memset(home, 0, sizeof(*home));
    HomeRow *row = addRow(home, HOME_CONTINUE);
    for (size_t i = 0; resume && i < resume->count && row->count < HOME_ROW_MAX; i++) {
        int item = libraryFind(lib, resume->items[i].path);
        if (item >= 0)
            row->items[row->count++] = (LibraryRef){false, item};
    }
    if (watched)
        addUpNext(row, lib, resume, watched);
    home->count += row->count > 0;

    row = addRow(home, HOME_FAVORITES);
    row->count = (int)libraryFavorites(lib, row->items, HOME_ROW_MAX);
    home->count += row->count > 0;

    row = addRow(home, HOME_RECENT);
    for (size_t i = 0; i < lib->recent_count && row->count < HOME_ROW_MAX; i++)
        row->items[row->count++] = lib->recent[i];
    home->count += row->count > 0;

    row = addRow(home, HOME_SERIES);
    for (size_t i = 0; i < lib->show_count && row->count < HOME_ROW_MAX; i++)
        row->items[row->count++] = (LibraryRef){true, (int)i};
    home->count += row->count > 0;

    row = addRow(home, HOME_FILMS);
    for (size_t i = 0; i < lib->film_count && row->count < HOME_ROW_MAX; i++)
        row->items[row->count++] = (LibraryRef){false, lib->films[i]};
    home->count += row->count > 0;
}

const char *homeRowTitle(HomeRowKind kind)
{
    switch (kind) {
    case HOME_CONTINUE: return "Continue watching";
    case HOME_FAVORITES: return "Favorites";
    case HOME_RECENT: return "Recently added";
    case HOME_SERIES: return "Series";
    case HOME_FILMS: return "Films";
    default: return "";
    }
}
