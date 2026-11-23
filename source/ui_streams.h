// The streams an addon gave for a film or episode, to choose one: each with
// its addon, its name ("1080p") and what it says of itself (the file, size).
#pragma once

#include <stdbool.h>

#include "stremio.h"

#define UI_STREAM_ROWS 7

// title: the film or episode. loading: still asking. note: why some addons failed, or NULL.
// Taps: LIBHIT_STREAM_ROW (a: the stream), LIBHIT_CLOSE (outside, or its back arrow).
void uiDrawStreams(const char *title, const StremioStream *streams, int count, int cursor, int first, bool loading,
                   const char *note);
