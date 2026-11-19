// The Live TV tab: the server's channels, each with what's on now (and how
// far into it) and what's next.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "libview.h"
#include "livetv.h"

// now_s: the time (UTC); utc_offset_s: the console's clock's distance from it.
// loading: the guide is being read for the first time.
void uiDrawLiveTab(const LibView *v, const LiveGuide *guide, int64_t now_s, int64_t utc_offset_s, bool clock_24h,
                   bool loading);
