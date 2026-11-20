#include "libview.h"

#include <stdio.h>
#include <string.h>

static int clamp(int value, int low, int high)
{
    return value < low ? low : value > high ? high : value;
}

// Keeps cursor in [0, count) and in view of visible items from first. A
// button moved it: the view rests on a row again.
static void keepInView(Scroll *s, int count, int visible)
{
    s->offset = 0;
    s->cursor = count > 0 ? clamp(s->cursor, 0, count - 1) : 0;
    if (s->cursor < s->first)
        s->first = s->cursor;
    if (s->cursor >= s->first + visible)
        s->first = s->cursor - visible + 1;
    s->first = clamp(s->first, 0, count > visible ? count - visible : 0);
}

static const LibTab CARD_TABS[] = {TAB_HOME, TAB_FILMS, TAB_SERIES, TAB_SEARCH, TAB_FILES};

static void addTab(LibView *v, LibTab kind, int collection)
{
    if (v->tab_count < LIBVIEW_TABS)
        v->tabs[v->tab_count++] = (LibTabRef){kind, collection};
}

void libviewSetTabs(LibView *v, const Library *lib, bool server, bool live, bool downloads)
{
    v->tab_count = 0;
    if (server && (lib->collection_count > 0 || live)) {
        addTab(v, TAB_HOME, -1);
        for (int i = 0; i < lib->collection_count; i++)
            addTab(v, TAB_COLLECTION, i);
        if (live)
            addTab(v, TAB_LIVE, -1);
        addTab(v, TAB_SEARCH, -1);
    } else {
        for (size_t i = 0; i < sizeof(CARD_TABS) / sizeof(CARD_TABS[0]) - 1; i++)  // Files comes last.
            addTab(v, CARD_TABS[i], -1);
    }
    if (downloads)
        addTab(v, TAB_DOWNLOADS, -1);
    addTab(v, TAB_FILES, -1);
    if (libviewTabIndex(v) < 0) {
        v->tab = TAB_HOME;
        v->collection = -1;
        v->show = -1;
    }
}

void libviewInit(LibView *v)
{
    memset(v, 0, sizeof(*v));
    v->show = -1;
    v->details = -1;
    v->collection = -1;
    for (size_t i = 0; i < sizeof(CARD_TABS) / sizeof(CARD_TABS[0]); i++)
        addTab(v, CARD_TABS[i], -1);
}

int libviewTabIndex(const LibView *v)
{
    for (int i = 0; i < v->tab_count; i++)
        if (v->tabs[i].kind == v->tab && (v->tab != TAB_COLLECTION || v->tabs[i].collection == v->collection))
            return i;
    return -1;
}

void libviewGoToTab(LibView *v, int index)
{
    if (index < 0 || index >= v->tab_count)
        return;
    v->tab = v->tabs[index].kind;
    v->collection = v->tabs[index].collection;
    v->show = -1;
}

bool libviewShowTab(LibView *v, LibTab kind)
{
    for (int i = 0; i < v->tab_count; i++)
        if (v->tabs[i].kind == kind) {
            libviewGoToTab(v, i);
            return true;
        }
    return false;
}

void libviewNextTab(LibView *v, int delta)
{
    int at = libviewTabIndex(v);
    if (v->tab_count == 0)
        return;
    libviewGoToTab(v, (((at < 0 ? 0 : at) + delta) % v->tab_count + v->tab_count) % v->tab_count);
}

int libviewGrid(const LibView *v)
{
    switch (v->tab) {
    case TAB_FILMS: return GRID_FILMS;
    case TAB_SERIES: return GRID_SERIES;
    case TAB_COLLECTION:
        return v->collection >= 0 && v->collection < LIBRARY_COLLECTIONS ? GRID_COLLECTIONS + v->collection : -1;
    default: return -1;
    }
}

int libviewGridCount(const LibView *v, const Library *lib, int grid)
{
    (void)v;
    if (grid == GRID_FILMS)
        return (int)lib->film_count;
    if (grid == GRID_SERIES)
        return (int)lib->show_count;
    int c = grid - GRID_COLLECTIONS;
    return c >= 0 && c < lib->collection_count ? (int)lib->collections[c].count : 0;
}

LibraryRef libviewGridRef(const LibView *v, const Library *lib, int grid, int index)
{
    (void)v;
    if (grid == GRID_FILMS)
        return (LibraryRef){false, lib->films[index]};
    if (grid == GRID_SERIES)
        return (LibraryRef){true, index};
    return lib->collections[grid - GRID_COLLECTIONS].refs[index];
}

// --- Home ---

void libviewHomeMove(LibView *v, const Home *home, int dx, int dy)
{
    if (home->count == 0)
        return;
    if (dy)
        v->home_offset = 0;
    v->home_row = clamp(v->home_row + dy, 0, home->count - 1);
    if (v->home_row < v->home_first_row)
        v->home_first_row = v->home_row;
    if (v->home_row >= v->home_first_row + LIBVIEW_HOME_ROWS)
        v->home_first_row = v->home_row - LIBVIEW_HOME_ROWS + 1;
    Scroll *cols = &v->home_cols[v->home_row];
    cols->cursor += dx;
    keepInView(cols, home->rows[v->home_row].count, LIBVIEW_HOME_COLS);
}

// --- Following a finger ---

bool libviewHomeScrollPx(LibView *v, const Home *home, int px)
{
    Scroll s = {v->home_row, v->home_first_row, v->home_offset};
    bool moved = scrollPx(&s, px, LIBVIEW_HOME_ROW_PX, LIBVIEW_HOME_ROWS, 1, home->count);
    v->home_row = s.cursor;
    v->home_first_row = s.first;
    v->home_offset = s.offset;
    return moved;
}

bool libviewHomeRowScrollPx(LibView *v, const Home *home, int row, int px)
{
    if (row < 0 || row >= home->count)
        return false;
    return scrollPx(&v->home_cols[row], px, LIBVIEW_HOME_COL_PX, LIBVIEW_HOME_COLS, 1, home->rows[row].count);
}

// --- Grids ---

void libviewGridMove(LibView *v, LibGrid grid, int count, int dx, int dy)
{
    Scroll *s = &v->grid[grid];
    s->offset = 0;
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
    v->episodes = (Scroll){0, 0, 0};
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
        v->episodes = (Scroll){0, 0, 0};
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

// --- Live TV ---

void libviewLiveMove(LibView *v, int count, int dy)
{
    v->live.cursor += dy;
    keepInView(&v->live, count, LIBVIEW_LIVE_ROWS);
}

// --- Search ---

void libviewSearch(LibView *v, const Library *lib, const char *query)
{
    snprintf(v->query, sizeof(v->query), "%s", query);
    v->result_count = (int)librarySearch(lib, query, v->results, LIBVIEW_RESULTS);
    v->results_scroll = (Scroll){0, 0, 0};
}

void libviewResultsMove(LibView *v, int dy)
{
    v->results_scroll.cursor += dy;
    keepInView(&v->results_scroll, v->result_count, LIBVIEW_LIST_ROWS);
}

// --- After a scan ---

void libviewClamp(LibView *v, const Library *lib, const Home *home)
{
    if (v->details >= 0 && (size_t)v->details >= lib->count)
        v->details = -1;
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
    for (int grid = 0; grid < GRID_COUNT; grid++)
        libviewGridMove(v, (LibGrid)grid, libviewGridCount(v, lib, grid), 0, 0);
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
    case TAB_SERIES:
    case TAB_COLLECTION: {
        int grid = libviewGrid(v);
        if (grid < 0 || v->grid[grid].cursor >= libviewGridCount(v, lib, grid))
            return false;
        *out = libviewGridRef(v, lib, grid, v->grid[grid].cursor);
        return true;
    }
    case TAB_SEARCH:
        if (v->results_scroll.cursor >= v->result_count)
            return false;
        *out = v->results[v->results_scroll.cursor];
        return true;
    default:
        return false;
    }
}
