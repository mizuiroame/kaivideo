// The Downloads panel over the library (+ menu > Downloads).
#pragma once

#include <stdbool.h>

#include "scroll.h"

// A video's download, as its mark shows it.
typedef enum { UI_DL_NONE, UI_DL_QUEUED, UI_DL_ACTIVE, UI_DL_PAUSED, UI_DL_FAILED, UI_DL_DONE } UiDownloadMark;

// The mark of a download, centered at cx, cy, radius r: a filled circle with
// an arrow when it's on the card; a ring that fills as it comes (fraction 0
// to 1) while it downloads; faint while it waits; two bars when paused; red
// when it failed. Nothing for UI_DL_NONE.
void uiDrawDownloadMark(float cx, float cy, float r, UiDownloadMark mark, double fraction);

// The header's downloads pill, ending at right: an arrow and what it says
// ("2 · 42%"). Answers a tap with LIBHIT_DOWNLOADS. Returns its left edge.
int uiDrawDownloadsPill(int right, const char *label, double fraction);

#define UI_DOWNLOAD_ROWS 6     // Rows in view.
#define UI_DOWNLOAD_ROW_H 72

typedef struct {
    const char *title;   // "Paper Lanterns (1998)".
    const char *detail;  // "Rook · S01E02 · Pawns", or the server's name.
    const char *state;   // "Downloading · 1.2 of 3.4 GB", "On the card · 3.4 GB", "Failed: ...".
    double progress;     // 0 to 1; -1 for no bar.
    bool done, failed;
} UiDownloadRow;

// The rows (count of them, scrolled as s says, the cursor's highlighted),
// the one whose delete was pressed once (confirm, -1 for none) in red, a
// line at the top (summary), and empty when there are none. Touch targets:
// LIBHIT_DOWNLOAD_ROW and LIBHIT_DOWNLOAD_DELETE (a: the row), LIBHIT_CLOSE outside.
void uiDrawDownloads(const UiDownloadRow *rows, int count, const Scroll *s, int confirm, const char *summary,
                     const char *empty);
