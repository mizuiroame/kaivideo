#include "home.h"

#include <string.h>

static HomeRow *addRow(Home *home, HomeRowKind kind)
{
    HomeRow *row = &home->rows[home->count];
    memset(row, 0, sizeof(*row));
    row->kind = kind;
    return row;
}

void homeCompute(Home *home, const Library *lib, const ResumeList *resume)
{
    memset(home, 0, sizeof(*home));
    HomeRow *row = addRow(home, HOME_CONTINUE);
    for (size_t i = 0; resume && i < resume->count && row->count < HOME_ROW_MAX; i++) {
        int item = libraryFind(lib, resume->items[i].path);
        if (item >= 0)
            row->items[row->count++] = (LibraryRef){false, item};
    }
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
    case HOME_RECENT: return "Recently added";
    case HOME_SERIES: return "Series";
    case HOME_FILMS: return "Films";
    default: return "";
    }
}
