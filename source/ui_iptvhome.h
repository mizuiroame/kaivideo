// The IPTV view's Home (iptvhome.h): a row of channel cards a group, under
// Recently watched and Favorites, and the favorite button of the channel the
// cursor is on.
#pragma once

#include <stdbool.h>

#include "iptv.h"
#include "iptvhome.h"

#define UI_IPTVHOME_ROWS 3  // Rows in view.
#define UI_IPTVHOME_COLS 5  // Cards in view across a row.
#define UI_IPTVHOME_ROW_PX 196
#define UI_IPTVHOME_COL_PX 222

// What a channel is on now ("" or NULL for nothing known).
typedef const char *(*UiIptvNow)(const void *ctx, int channel);

// loading: the list is being read. Taps: LIBHIT_HOME_CARD (a: row, b: place in it), LIBHIT_FAVORITE.
void uiDrawIptvHome(const IptvHome *h, const IptvHomeView *v, const IptvList *list, const IptvMarks *marks,
                    UiIptvNow now, const void *ctx, bool loading);
