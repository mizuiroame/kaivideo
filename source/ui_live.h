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
// dvr: the server records: a Recordings pill (L, LIBHIT_DVR_RECORDINGS) over the channels, and a Record pill
// (R, LIBHIT_DVR_RECORD) for the program under the cursor. Programs it will record have a red dot.
void uiDrawLiveTab(const LibView *v, const LiveGuide *guide, int64_t now_s, int64_t utc_offset_s, bool clock_24h,
                   bool loading, const char *group, bool iptv, int favorite, bool dvr);
// Where the guide's hours are drawn across: from x, w wide (a finger along them moves through time).
int uiLiveGridX(void);
int uiLiveGridW(void);
// The choices for a program over the guide: its title, a line under it (when, which channel), and
// count rows (cursor highlighted). A tap on one is LIBHIT_LIVE_CHOICE (a: it), outside LIBHIT_CLOSE.
void uiDrawLiveChoices(const char *title, const char *line, const char *const *labels, int count, int cursor);
// The Recordings list's top: "< title" (a tap: LIBHIT_CLOSE, back to the guide) and a summary on the right.
void uiDrawRecordingsTop(const char *title, const char *summary);
#define UI_GROUP_ROWS 10  // Rows of a list of groups in view (a page of them).

// The IPTV list's groups over the guide: row 0 is all its channels. cursor and chosen are rows.
void uiDrawLiveGroups(const IptvList *list, int cursor, int chosen);
// The same for any groups (an Xtream account's categories): title over them, row 0 is all of them
// (all, with all_count in it), then each group with its count.
void uiDrawGroupList(const char *title, const char *all, int all_count, const IptvGroup *groups, int count,
                     int cursor, int chosen);
// A group's pill w wide at x, y: the Y button, its name and a chevron; a tap is LIBHIT_LIVE_GROUPS.
void uiDrawGroupPill(int x, int y, int w, const char *group);
