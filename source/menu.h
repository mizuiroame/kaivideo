// A drop-down list of choices over the player (sound tracks, subtitles):
// the row under the cursor and the rows in view. Pure logic; ui.c draws it.
#pragma once

#include <stdbool.h>

#define MENU_ROWS 7  // Rows in view; more scroll.

typedef struct {
    bool open;
    int count;
    int cursor;
    int first;  // The first row in view.
} Menu;

// Opens with count rows and the cursor on current (kept within the rows).
void menuOpen(Menu *m, int count, int current);
void menuClose(Menu *m);
// Moves the cursor by delta rows (stopping at the ends), keeping it in view.
void menuMove(Menu *m, int delta);
// Rows in view (at most MENU_ROWS).
int menuVisible(const Menu *m);
