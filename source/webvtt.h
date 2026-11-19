// WebVTT subtitle segments of a Live TV channel's own HLS stream: their
// lines, put on the video's clock. Pure logic.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "subs.h"

// Adds the lines of one WebVTT segment to t. Their times are moved by the
// segment's X-TIMESTAMP-MAP onto the MPEG-TS clock (90 kHz) the video's
// packets carry, and then back by shift_ns (where the player's time 0 is on
// that clock). How many lines were added; -1 if it isn't WebVTT.
int webvttAdd(SubTrack *t, const char *data, size_t size, int64_t shift_ns);
