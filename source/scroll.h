// Where a list is scrolled to, and moving it by pixels as a finger does.
// Pure logic, shared by the library's views, the file list and the player's lists.
#pragma once

#include <stdbool.h>

typedef struct {
    int cursor, first;  // The item under the cursor, the first in view.
    int offset;         // Pixels into the first row in view: a finger left it between rows (0 on one).
} Scroll;

// A finger moves a list's content by px (positive: up, showing later rows; for
// a row across, left), row_h pixels a row and cols items a row, visible rows
// in view. It stops at the ends, and the cursor stays on a row mostly in view.
// False if it was at an end already, or got there.
bool scrollPx(Scroll *s, int px, int row_h, int visible, int cols, int count);
