#include "libview.h"

#include <stdio.h>
#include <string.h>

static int clamp(int value, int low, int high)
{
    return value < low ? low : value > high ? high : value;
}

// Keeps cursor in [0, count) and in view of visible items from first.
static void keepInView(Scroll *s, int count, int visible)
{
    s->cursor = count > 0 ? clamp(s->cursor, 0, count - 1) : 0;
    if (s->cursor < s->first)
        s->first = s->cursor;
    if (s->cursor >= s->first + visible)
        s->first = s->cursor - visible + 1;
    s->first = clamp(s->first, 0, count > visible ? count - visible : 0);
}

void libviewInit(LibView *v)
{
    memset(v, 0, sizeof(*v));
    v->show = -1;
}

void libviewNextTab(LibView *v, int delta)
{
    v->tab = (LibTab)(((int)v->tab + delta + TAB_COUNT) % TAB_COUNT);
    v->show = -1;
}

// --- Home ---

void libviewHomeMove(LibView *v, const Home *home, int dx, int dy)
{
    if (home->count == 0)
        return;
    v->home_row = clamp(v->home_row + dy, 0, home->count - 1);
    if (v->home_row < v->home_first_row)
        v->home_first_row = v->home_row;
    if (v->home_row >= v->home_first_row + LIBVIEW_HOME_ROWS)
        v->home_first_row = v->home_row - LIBVIEW_HOME_ROWS + 1;
    Scroll *cols = &v->home_cols[v->home_row];
    cols->cursor += dx;
    keepInView(cols, home->rows[v->home_row].count, LIBVIEW_HOME_COLS);
}

// --- Grids ---

void libviewGridMove(LibView *v, LibGrid grid, int count, int dx, int dy)
{
    Scroll *s = &v->grid[grid];
    if (count == 0) {
        s->cursor = s->first = 0;
        return;
    }
    int next = s->cursor + dx + dy * LIBVIEW_COLS;
    if (dy > 0 && next >= count && s->cursor / LIBVIEW_COLS < (count - 1) / LIBVIEW_COLS)
        next = count - 1;  // Down onto a short last row.
    if (next >= 0 && next < count)
        s->cursor = next;
    s->cursor = clamp(s->cursor, 0, count - 1);
    int row = s->cursor / LIBVIEW_COLS, rows = (count + LIBVIEW_COLS - 1) / LIBVIEW_COLS;
    if (row < s->first)
        s->first = row;
    if (row >= s->first + LIBVIEW_GRID_ROWS)
        s->first = row - LIBVIEW_GRID_ROWS + 1;
    s->first = clamp(s->first, 0, rows > LIBVIEW_GRID_ROWS ? rows - LIBVIEW_GRID_ROWS : 0);
}

// --- A series' page ---

int libviewSeasons(const Library *lib, int show, int *out, int max)
{
    if (show < 0 || (size_t)show >= lib->show_count)
        return 0;
    const LibraryShow *s = &lib->shows[show];
    int n = 0;
    for (int i = 0; i < s->count && n < max; i++) {
        int season = lib->items[s->episodes[i]].name.season;
        if (n == 0 || out[n - 1] != season)
            out[n++] = season;
    }
    return n;
}

void libviewSeasonRange(const Library *lib, int show, int season, int *first, int *count)
{
    *first = *count = 0;
    int seasons[LIBVIEW_SEASONS];
    int n = libviewSeasons(lib, show, seasons, LIBVIEW_SEASONS);
    if (season < 0 || season >= n)
        return;
    const LibraryShow *s = &lib->shows[show];
    int i = 0;
    while (i < s->count && lib->items[s->episodes[i]].name.season != seasons[season])
        i++;
    *first = i;
    while (i < s->count && lib->items[s->episodes[i]].name.season == seasons[season])
        i++;
    *count = i - *first;
}

void libviewOpenShow(LibView *v, const Library *lib, int show, int item)
{
    v->show = show;
    snprintf(v->show_name, sizeof(v->show_name), "%s", lib->shows[show].name);
    v->season = 0;
    v->episodes = (Scroll){0, 0};
    int seasons[LIBVIEW_SEASONS];
    int n = libviewSeasons(lib, show, seasons, LIBVIEW_SEASONS);
    for (int season = 0; item >= 0 && season < n; season++) {
        int first, count;
        libviewSeasonRange(lib, show, season, &first, &count);
        for (int e = 0; e < count; e++)
            if (lib->shows[show].episodes[first + e] == item) {
                v->season = season;
                v->episodes.cursor = e;
            }
    }
    libviewShowMove(v, lib, 0, 0);
}

void libviewShowMove(LibView *v, const Library *lib, int dx, int dy)
{
    int seasons[LIBVIEW_SEASONS];
    int n = libviewSeasons(lib, v->show, seasons, LIBVIEW_SEASONS);
    int next = n > 0 ? clamp(v->season + dx, 0, n - 1) : 0;
    if (next != v->season)
        v->episodes = (Scroll){0, 0};
    v->season = next;
    int first, count;
    libviewSeasonRange(lib, v->show, v->season, &first, &count);
    v->episodes.cursor += dy;
    keepInView(&v->episodes, count, LIBVIEW_LIST_ROWS);
}

int libviewShowItem(const LibView *v, const Library *lib)
{
    int first, count;
    libviewSeasonRange(lib, v->show, v->season, &first, &count);
    if (v->episodes.cursor >= count)
        return -1;
    return lib->shows[v->show].episodes[first + v->episodes.cursor];
}

// --- Search ---

void libviewSearch(LibView *v, const Library *lib, const char *query)
{
    snprintf(v->query, sizeof(v->query), "%s", query);
    v->result_count = (int)librarySearch(lib, query, v->results, LIBVIEW_RESULTS);
    v->results_scroll = (Scroll){0, 0};
}

void libviewResultsMove(LibView *v, int dy)
{
    v->results_scroll.cursor += dy;
    keepInView(&v->results_scroll, v->result_count, LIBVIEW_LIST_ROWS);
}

// --- After a scan ---

void libviewClamp(LibView *v, const Library *lib, const Home *home)
{
    // The open series may have moved in the list, or be gone.
    if (v->show >= 0) {
        int found = -1;
        for (size_t i = 0; i < lib->show_count && found < 0; i++)
            if (strcmp(lib->shows[i].name, v->show_name) == 0)
                found = (int)i;
        v->show = found;
    }
    if (v->show >= 0)
        libviewShowMove(v, lib, 0, 0);
    v->home_row = home->count ? clamp(v->home_row, 0, home->count - 1) : 0;
    v->home_first_row = clamp(v->home_first_row, 0, v->home_row);
    for (int r = 0; r < home->count; r++)
        keepInView(&v->home_cols[r], home->rows[r].count, LIBVIEW_HOME_COLS);
    libviewGridMove(v, GRID_FILMS, (int)lib->film_count, 0, 0);
    libviewGridMove(v, GRID_SERIES, (int)lib->show_count, 0, 0);
    if (v->query[0]) {
        char query[LIBVIEW_QUERY_MAX];
        snprintf(query, sizeof(query), "%s", v->query);
        int cursor = v->results_scroll.cursor;
        libviewSearch(v, lib, query);
        libviewResultsMove(v, cursor);
    }
}

bool libviewSelected(const LibView *v, const Library *lib, const Home *home, LibraryRef *out)
{
    if (v->show >= 0) {
        int item = libviewShowItem(v, lib);
        *out = (LibraryRef){false, item};
        return item >= 0;
    }
    switch (v->tab) {
    case TAB_HOME:
        if (v->home_row >= home->count || v->home_cols[v->home_row].cursor >= home->rows[v->home_row].count)
            return false;
        *out = home->rows[v->home_row].items[v->home_cols[v->home_row].cursor];
        return true;
    case TAB_FILMS:
        if ((size_t)v->grid[GRID_FILMS].cursor >= lib->film_count)
            return false;
        *out = (LibraryRef){false, lib->films[v->grid[GRID_FILMS].cursor]};
        return true;
    case TAB_SERIES:
        if ((size_t)v->grid[GRID_SERIES].cursor >= lib->show_count)
            return false;
        *out = (LibraryRef){true, v->grid[GRID_SERIES].cursor};
        return true;
    case TAB_SEARCH:
        if (v->results_scroll.cursor >= v->result_count)
            return false;
        *out = v->results[v->results_scroll.cursor];
        return true;
    default:
        return false;
    }
}
