#include "ui_notice.h"

#include <stdio.h>
#include <string.h>

#include "font.h"
#include "gfx.h"
#include "ui_library.h"
#include "ui_style.h"

#define NOTICE_W_MAX 760
#define NOTICE_W_MIN 300
#define NOTICE_PAD 18
#define NOTICE_ICON 30
#define NOTICE_GAP 14
#define NOTICE_TEXT_PX 20
#define NOTICE_LINE_H 28
#define NOTICE_TOP (UI_TAB_BAR_H + 10)  // Under the header, as a phone's notification under its status bar.
#define NOTICE_RADIUS 22
#define NOTICE_DONE_COLOR GFX_RGB(0x5C, 0xD6, 0x8A)
#define NOTICE_SHADOW_COLOR GFX_RGB(0x06, 0x06, 0x08)
#define NOTICE_EDGE_COLOR COLOR_FAINT  // The theme's, as the bubble's own colors are.

static int g_x, g_y, g_w, g_h;  // Where the bubble was last drawn, fully in; 0 wide when it wasn't.

// Splits text into at most two lines of max_w: the first ends at the last space
// that fits; the second holds the rest (clipped when drawn). True if two.
static bool wrap(const char *text, int max_w, char *first, char *second, size_t size)
{
    snprintf(first, size, "%s", text);
    second[0] = '\0';
    if (fontTextWidth(FONT_REGULAR, NOTICE_TEXT_PX, text) <= max_w)
        return false;
    size_t cut = 0;
    for (size_t i = 1; text[i] && i < size; i++) {
        if (text[i] != ' ')
            continue;
        snprintf(first, size, "%.*s", (int)i, text);
        if (fontTextWidth(FONT_REGULAR, NOTICE_TEXT_PX, first) > max_w)
            break;
        cut = i;
    }
    if (!cut) {  // No space that fits (a long word, or CJK text): the line is clipped as it is.
        snprintf(first, size, "%s", text);
        return false;
    }
    snprintf(first, size, "%.*s", (int)cut, text);
    snprintf(second, size, "%s", text + cut + 1);
    return true;
}

static void drawIcon(NoticeKind kind, int cx, int cy)
{
    Color color = kind == NOTICE_ERROR ? COLOR_ERROR : kind == NOTICE_DONE ? NOTICE_DONE_COLOR : COLOR_ACCENT;
    gfxFillCircle(cx, cy, NOTICE_ICON / 2, color);
    if (kind == NOTICE_DONE) {  // A tick.
        gfxLine((float)cx - 7, (float)cy + 1, (float)cx - 2, (float)cy + 6, 3.5f, COLOR_BG);
        gfxLine((float)cx - 2, (float)cy + 6, (float)cx + 8, (float)cy - 5, 3.5f, COLOR_BG);
    } else {
        fontDrawCentered(FONT_BOLD, 22, (float)cx, (float)cy, kind == NOTICE_ERROR ? "!" : "i", COLOR_BG);
    }
}

void uiDrawNotice(const Notice *n, float slide)
{
    g_w = 0;
    if (!n || slide <= 0.0f)
        return;
    int text_max = NOTICE_W_MAX - 2 * NOTICE_PAD - NOTICE_ICON - NOTICE_GAP;
    char first[NOTICE_TEXT_MAX], second[NOTICE_TEXT_MAX];
    bool two = wrap(n->text, text_max, first, second, sizeof(first));
    int text_w = fontTextWidth(FONT_REGULAR, NOTICE_TEXT_PX, first);
    if (two) {
        int w2 = fontTextWidth(FONT_REGULAR, NOTICE_TEXT_PX, second);
        text_w = w2 > text_w ? w2 : text_w;
    }
    text_w = text_w > text_max ? text_max : text_w;
    int w = 2 * NOTICE_PAD + NOTICE_ICON + NOTICE_GAP + text_w;
    w = w < NOTICE_W_MIN ? NOTICE_W_MIN : w;
    int h = 2 * NOTICE_PAD - 8 + (two ? 2 : 1) * NOTICE_LINE_H;
    h = h < NOTICE_ICON + 2 * 12 ? NOTICE_ICON + 2 * 12 : h;
    // Eased: quick at first, settling in place (and the same backwards going out).
    float t = 1.0f - slide;
    float eased = 1.0f - t * t * t;
    int x = (GFX_WIDTH - w) / 2;
    int y = (int)((float)NOTICE_TOP - (1.0f - eased) * (float)(NOTICE_TOP + h + 12));

    gfxFillRoundRect(x + 2, y + 6, w, h, NOTICE_RADIUS, NOTICE_SHADOW_COLOR);
    gfxFillRoundRect(x - 1, y - 1, w + 2, h + 2, NOTICE_RADIUS + 1, NOTICE_EDGE_COLOR);
    gfxFillRoundRect(x, y, w, h, NOTICE_RADIUS, COLOR_SURFACE_HI);
    drawIcon(n->kind, x + NOTICE_PAD + NOTICE_ICON / 2, y + h / 2);
    int text_x = x + NOTICE_PAD + NOTICE_ICON + NOTICE_GAP;
    float middle = (float)y + (float)h / 2.0f;
    gfxSetClip(text_x, y, text_max, h);  // A line too long for the bubble is cut at its edge.
    if (two) {
        fontDrawMiddle(FONT_REGULAR, NOTICE_TEXT_PX, text_x, middle - NOTICE_LINE_H / 2.0f, first, COLOR_TEXT);
        fontDrawMiddle(FONT_REGULAR, NOTICE_TEXT_PX, text_x, middle + NOTICE_LINE_H / 2.0f, second, COLOR_TEXT);
    } else {
        fontDrawMiddle(FONT_REGULAR, NOTICE_TEXT_PX, text_x, middle, first, COLOR_TEXT);
    }
    gfxResetClip();
    if (slide >= 1.0f) {  // Fully in: a tap sends it away (sliding, it's already coming or going).
        g_x = x, g_y = y, g_w = w, g_h = h;
    }
}

bool uiNoticeAt(int x, int y)
{
    return g_w > 0 && x >= g_x && x < g_x + g_w && y >= g_y && y < g_y + g_h;
}
