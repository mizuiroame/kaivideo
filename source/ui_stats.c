#include "ui_stats.h"

#include "font.h"
#include "gfx.h"
#include "lang.h"
#include "ui_style.h"

#define STATS_PX 18
#define STATS_LINE_H 26
#define STATS_PAD 14
#define STATS_TOP 24
#define STATS_ALPHA 190
#define STATS_TITLE T("Statistics")

void uiDrawStats(char lines[][STATS_LINE_MAX], int count)
{
    int width = fontTextWidth(FONT_BOLD, STATS_PX, STATS_TITLE);
    for (int i = 0; i < count; i++) {
        int w = fontTextWidth(FONT_REGULAR, STATS_PX, lines[i]);
        width = w > width ? w : width;
    }
    int box_w = width + 2 * STATS_PAD, box_h = (count + 1) * STATS_LINE_H + 2 * STATS_PAD;
    gfxBlendRect(MARGIN, STATS_TOP, box_w, box_h, GFX_RGB(0, 0, 0), STATS_ALPHA);
    gfxFillRect(MARGIN, STATS_TOP, 4, box_h, COLOR_ACCENT);
    int x = MARGIN + STATS_PAD, y = STATS_TOP + STATS_PAD;  // fontDraw's y is the top of the line.
    fontDraw(FONT_BOLD, STATS_PX, x, y, STATS_TITLE, COLOR_ACCENT);
    for (int i = 0; i < count; i++)
        fontDraw(FONT_REGULAR, STATS_PX, x, y + (i + 1) * STATS_LINE_H, lines[i], GFX_RGB(0xF2, 0xF2, 0xF5));
}
