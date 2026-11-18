// Console buttons drawn as badges, and help lines built from them.
#pragma once

#include "gfx.h"

// A badge for a console button: a circle for A, B, X, Y, + and −, a rounded
// tab for the shoulder buttons, and a d-pad for directions. key is one of
// "A" "B" "X" "Y" "L" "R" "ZL" "ZR" "+" "-" "RS" "<>" "^v" "^" "v" "<" ">".
int uiButtonWidth(const char *key);
// Draws the badge from x, centered on cy.
void uiDrawButton(int x, float cy, const char *key, Color bg, Color fg);
// The same, centered on cx.
void uiDrawButtonCentered(float cx, float cy, const char *key, Color bg, Color fg);

// A help line such as "[A] open  [B] back  [L][R] track": badges in
// brackets, items separated by two spaces. Items that don't fit in
// max_width are left out.
void uiDrawHelp(int x, float cy, int max_width, const char *help);
