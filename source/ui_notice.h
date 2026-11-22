// The notice bubble (notice.h): at the top, centered, over everything.
#pragma once

#include <stdbool.h>

#include "notice.h"

// Draws n slid in by slide (0 out of sight above, 1 in place).
void uiDrawNotice(const Notice *n, float slide);
// True when (x, y) is on the bubble drawn last (a tap there sends it away).
bool uiNoticeAt(int x, int y);
