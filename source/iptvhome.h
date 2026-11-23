// The IPTV view's Home: rows of channels, as an IPTV app shows them. The
// channels watched last, the favorites, then one row a group. Recents and
// favorites are kept by the channel's name (a list read again renumbers its
// channels). Pure logic; app_iptvhome.c keeps them and ui_iptvhome.c draws them.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "iptv.h"
#include "scroll.h"

#define IPTVHOME_RECENT_MAX 20
#define IPTVHOME_FAVORITES_MAX 200
#define IPTVHOME_ROW_MAX 60                       // Channels in a group's row; the guide has them all.
#define IPTVHOME_ROWS_MAX (2 + IPTV_GROUPS_MAX)  // Recent, Favorites, and each group (or all channels).
#define IPTVHOME_FILE "iptv_marks.txt"           // In KaiVideo's folder.

typedef struct {
    char recent[IPTVHOME_RECENT_MAX][IPTV_NAME_MAX];  // The last watched first.
    int recent_count;
    char favorites[IPTVHOME_FAVORITES_MAX][IPTV_NAME_MAX];  // In the order they were added.
    int favorite_count;
} IptvMarks;

typedef enum { IPTVROW_RECENT, IPTVROW_FAVORITES, IPTVROW_GROUP } IptvRowKind;

typedef struct {
    IptvRowKind kind;
    int group;      // IPTVROW_GROUP's: an index in the list's groups, -1 for all its channels.
    int first;      // Its channels: pool[first .. first + count), indexes in the list.
    int count;
} IptvHomeRow;

typedef struct {
    IptvHomeRow rows[IPTVHOME_ROWS_MAX];
    int count;
    int *pool;
} IptvHome;

// Where the cursor is: rows.cursor the row, cols[row] the channel in it.
typedef struct {
    Scroll rows;
    Scroll cols[IPTVHOME_ROWS_MAX];
} IptvHomeView;

void iptvMarksInit(IptvMarks *m);
// Reads IPTVHOME_FILE's text ("recent\t<name>" and "favorite\t<name>" lines). Unknown lines are left out.
void iptvMarksRead(IptvMarks *m, const char *text);
// Writes it. False if it doesn't fit.
bool iptvMarksWrite(const IptvMarks *m, char *out, size_t size);
// A channel was watched: first in the recents (the oldest goes when they're full).
void iptvMarksWatched(IptvMarks *m, const char *name);
bool iptvMarksIsFavorite(const IptvMarks *m, const char *name);
// Adds it to the favorites, or takes it out. Returns whether it's one now; false too when they're full.
bool iptvMarksToggleFavorite(IptvMarks *m, const char *name);

// The rows for a list: Recent and Favorites when they have channels in it, then
// a row a group with channels (or one of all of them, without groups).
void iptvHomeBuild(IptvHome *h, const IptvList *list, const IptvMarks *m);
void iptvHomeFree(IptvHome *h);
// The channel at a row and place in it, -1 if there's none.
int iptvHomeChannel(const IptvHome *h, int row, int col);

// The cursor: on the first channel of the first row.
void iptvHomeViewReset(IptvHomeView *v);
// dx moves across a row, dy between rows; both keep it in view (rows visible, cols across).
void iptvHomeMove(IptvHomeView *v, const IptvHome *h, int dx, int dy, int rows, int cols);
