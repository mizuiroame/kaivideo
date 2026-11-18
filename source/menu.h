// A drop-down list of choices over the player (sound tracks, subtitles):
// the row under the cursor and the rows in view. A few fixed rows can follow
// the list (the subtitles' size and color), always in view. Pure logic;
// ui.c draws it.
#pragma once

#include <stdbool.h>

#define MENU_ROWS 7  // List rows in view; more scroll.

typedef struct {
    bool open;
    int count;   // All rows: the list's, then the fixed ones.
    int fixed;   // Rows after the list that don't scroll.
    int cursor;
    int first;   // The first list row in view.
} Menu;

// Opens with count rows (the last fixed of them fixed) and the cursor on
// current (kept within the rows).
void menuOpen(Menu *m, int count, int fixed, int current);
void menuClose(Menu *m);
// Moves the cursor by delta rows (stopping at the ends), keeping it in view.
void menuMove(Menu *m, int delta);
// List rows in view (at most MENU_ROWS), not counting the fixed ones.
int menuVisible(const Menu *m);
// True when the cursor is on one of the fixed rows.
bool menuOnFixed(const Menu *m);
