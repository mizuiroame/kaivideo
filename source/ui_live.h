// The Live TV tab: the server's channels, each with what's on now (and how
// far into it) and what's next.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "iptv.h"
#include "libview.h"
#include "livetv.h"

// now_s: the time (UTC); utc_offset_s: the console's clock's distance from it.
// loading: the guide is being read for the first time. group: the IPTV
// list's group shown (a pill that lists them), or NULL. iptv: there's a list.
// favorite: whether the IPTV channel under the cursor is a favorite (an R pill, LIBHIT_FAVORITE), -1 for none.
void uiDrawLiveTab(const LibView *v, const LiveGuide *guide, int64_t now_s, int64_t utc_offset_s, bool clock_24h,
                   bool loading, const char *group, bool iptv, int favorite);
// The IPTV list's groups over the guide: row 0 is all its channels. cursor and chosen are rows.
void uiDrawLiveGroups(const IptvList *list, int cursor, int chosen);
