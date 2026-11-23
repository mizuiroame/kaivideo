// The notice bubbles (notice.h): at the top, centered, over everything, newest on top.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "notice.h"

// Draws the bubbles noticeVisible gave.
void uiDrawNotices(const NoticeView *views, int count);
// The id of the bubble drawn last at (x, y), fully in (a tap there sends it away); 0 for none.
uint64_t uiNoticeAt(int x, int y);
