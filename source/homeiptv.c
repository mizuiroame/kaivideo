// The IPTV list's Showcase rows: its channels' (Recently watched, Favorite
// channels, one a group), then an Xtream account's Films, Series and one row
// a film category, the newest first.
#include "home.h"

#include <stdio.h>
#include <string.h>

static HomeRow *startRow(Home *home, HomeRowKind kind)
{
    if (home->count >= HOME_ROWS_MAX)
        return NULL;
    HomeRow *row = &home->rows[home->count];
    memset(row, 0, sizeof(*row));
    row->kind = kind;
    return row;
}

static void endRow(Home *home, const HomeRow *row)
{
    if (row && row->count > 0)
        home->count++;
}

static void addChannels(Home *home, const IptvHome *channels, const IptvList *list)
{
    for (int r = 0; r < channels->count; r++) {
        const IptvHomeRow *from = &channels->rows[r];
        HomeRowKind kind = from->kind == IPTVROW_RECENT      ? HOME_CHANNELS_RECENT
                           : from->kind == IPTVROW_FAVORITES ? HOME_CHANNELS_FAVORITES
                                                              : HOME_CHANNELS;
        HomeRow *row = startRow(home, kind);
        if (!row)
            return;
        row->channels = true;
        if (kind == HOME_CHANNELS && from->group >= 0 && from->group < list->group_count)
            snprintf(row->genre, sizeof(row->genre), "%s", list->groups[from->group].name);
        for (int c = 0; c < from->count && row->count < HOME_ROW_ITEMS; c++) {
            int channel = iptvHomeChannel(channels, r, c);
            if (channel >= 0 && channel < list->count)
                row->items[row->count++] = (LibraryRef){false, channel};
        }
        endRow(home, row);
    }
}

// The newest films (shows false) or series, of one category (group, -1 for any).
static void addRecent(Home *home, const Library *lib, HomeRowKind kind, bool shows, int group)
{
    HomeRow *row = startRow(home, kind);
    if (!row)
        return;
    for (size_t i = 0; i < lib->recent_count && row->count < HOME_ROW_MAX; i++) {
        LibraryRef ref = lib->recent[i];
        if (ref.is_show != shows)
            continue;
        int of = shows ? lib->shows[ref.index].group : lib->items[ref.index].group;
        if (group < 0 || of == group)
            row->items[row->count++] = ref;
    }
    endRow(home, row);
}

void homeComputeIptv(Home *home, const IptvHome *channels, const IptvList *list, const Library *lib,
                     const IptvGroup *film_groups, int film_group_count)
{
    memset(home, 0, sizeof(*home));
    addChannels(home, channels, list);
    addRecent(home, lib, HOME_FILMS, false, -1);
    addRecent(home, lib, HOME_SERIES, true, -1);
    for (int g = 0; g < film_group_count; g++) {
        size_t before = (size_t)home->count;
        addRecent(home, lib, HOME_CATEGORY, false, g);
        if ((size_t)home->count > before)
            snprintf(home->rows[before].genre, sizeof(home->rows[before].genre), "%s", film_groups[g].name);
    }
}

int homeFindRow(const Home *home, const HomeRow *row)
{
    for (int r = 0; r < home->count; r++)
        if (home->rows[r].kind == row->kind && home->rows[r].channels == row->channels &&
            !strcmp(home->rows[r].genre, row->genre))
            return r;
    return -1;
}
