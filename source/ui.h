// What KaiVideo draws over the 1280x720 layout: the folder list to choose a
// video, and the bar over a playing one. And where a tap lands on them.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "browser.h"
#include "player.h"
#include "resume.h"

#define UI_LIST_Y 76
#define UI_ROW_H 58
#define UI_ROWS 10

// The first row shown so that the cursor stays in view.
size_t uiFirstVisible(size_t cursor, size_t count);

// The folder list, with how far each video has been watched; message (an
// error, for a while) replaces the help line.
void uiDrawBrowser(const Browser *b, const ResumeList *resume, const char *message);
// The bar over a playing video, and the close button above it; note, if
// any, is said above the bar ("Resumed at 12:34").
void uiDrawPlayerBar(const Player *p, const char *name, const char *note);

typedef enum {
    TAP_NOTHING,
    TAP_UP,        // The path at the top: the folder above.
    TAP_ROW,       // A row: *row is its index.
    TAP_CLOSE,     // The player's close button.
    TAP_PAUSE,     // The player's play/pause button.
    TAP_SEEK,      // The progress bar: *fraction is where (0-1).
    TAP_PICTURE,   // Anywhere else on a playing video.
} TapTarget;

TapTarget uiTapBrowser(const Browser *b, int x, int y, size_t *row);
TapTarget uiTapPlayer(int x, int y, double *fraction);
