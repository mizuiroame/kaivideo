#include "ui_buttons.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "font.h"
#include "ui_style.h"

#define BADGE_SIZE 22     // Circle diameter and d-pad side.
#define TAB_H 20          // Shoulder button tab height.
#define TAB_PAD 7
#define BADGE_TEXT 13
#define TAB_TEXT 12
#define BADGE_GAP 3       // Between badges of one item ("[L][R]").
#define LABEL_GAP 7       // Between the badges and the label.
#define ITEM_GAP 24       // Between help items.
#define HELP_TEXT 18
#define KEY_MAX 8

static bool isShoulder(const char *key)
{
    return strcmp(key, "L") == 0 || strcmp(key, "R") == 0 || strcmp(key, "ZL") == 0 || strcmp(key, "ZR") == 0;
}

static bool isDirection(const char *key)
{
    return key[0] && strchr("<>^v", key[0]) != NULL;
}

int uiButtonWidth(const char *key)
{
    if (isShoulder(key))
        return fontTextWidth(FONT_BOLD, TAB_TEXT, key) + 2 * TAB_PAD;
    return BADGE_SIZE;
}

// A small triangle pointing in a direction (dx, dy), centered on (cx, cy).
static void drawArrow(float cx, float cy, float dx, float dy, Color color)
{
    const float length = 4.5f, half = 4.0f;
    float tip_x = cx + dx * length / 2.0f, tip_y = cy + dy * length / 2.0f;
    float base_x = cx - dx * length / 2.0f, base_y = cy - dy * length / 2.0f;
    gfxTriangle(tip_x, tip_y, base_x - dy * half, base_y + dx * half, base_x + dy * half, base_y - dx * half, color);
}

static void drawDirections(float cx, float cy, const char *key, Color fg)
{
    const float offset = 4.5f;
    bool pair = key[1] != '\0';
    if (strchr(key, '<'))
        drawArrow(cx - (pair ? offset : 0.0f), cy, -1, 0, fg);
    if (strchr(key, '>'))
        drawArrow(cx + (pair ? offset : 0.0f), cy, 1, 0, fg);
    if (strchr(key, '^'))
        drawArrow(cx, cy - (pair ? offset : 0.0f), 0, -1, fg);
    if (strchr(key, 'v'))
        drawArrow(cx, cy + (pair ? offset : 0.0f), 0, 1, fg);
}

void uiDrawButton(int x, float cy, const char *key, Color bg, Color fg)
{
    int w = uiButtonWidth(key);
    float cx = (float)x + (float)w / 2.0f;
    if (isShoulder(key)) {
        int top = (int)lroundf(cy - TAB_H / 2.0f);
        gfxFillRoundRect(x, top, w, TAB_H, 6, bg);
        fontDrawCentered(FONT_BOLD, TAB_TEXT, cx, (float)top + TAB_H / 2.0f, key, fg);
        return;
    }
    int top = (int)lroundf(cy - BADGE_SIZE / 2.0f);
    float mid = (float)top + BADGE_SIZE / 2.0f;
    if (isDirection(key)) {
        gfxFillRoundRect(x, top, BADGE_SIZE, BADGE_SIZE, 6, bg);
        drawDirections(cx, mid, key, fg);
        return;
    }
    gfxFillCircle(x + BADGE_SIZE / 2, top + BADGE_SIZE / 2, BADGE_SIZE / 2, bg);
    if (strcmp(key, "+") == 0 || strcmp(key, "-") == 0) {
        gfxLine(cx - 4.5f, mid, cx + 4.5f, mid, 2.0f, fg);
        if (key[0] == '+')
            gfxLine(cx, mid - 4.5f, cx, mid + 4.5f, 2.0f, fg);
        return;
    }
    fontDrawCentered(FONT_BOLD, strlen(key) > 1 ? 10 : BADGE_TEXT, cx, mid, key, fg);
}

void uiDrawButtonCentered(float cx, float cy, const char *key, Color bg, Color fg)
{
    uiDrawButton((int)lroundf(cx - uiButtonWidth(key) / 2.0f), cy, key, bg, fg);
}

// Reads "[KEY]" at *p into key and moves past it. False if *p isn't one.
static bool readKey(const char **p, char key[KEY_MAX])
{
    if (**p != '[')
        return false;
    const char *end = strchr(*p, ']');
    if (!end || end - *p - 1 >= KEY_MAX)
        return false;
    snprintf(key, KEY_MAX, "%.*s", (int)(end - *p - 1), *p + 1);
    *p = end + 1;
    return true;
}

// Measures or draws one help item (badges then label); returns its width.
static int helpItem(const char *item, size_t len, int x, float cy, bool draw)
{
    char text[128], key[KEY_MAX];
    snprintf(text, sizeof(text), "%.*s", (int)len, item);
    const char *p = text;
    int w = 0;
    while (readKey(&p, key)) {
        if (draw)
            uiDrawButton(x + w, cy, key, COLOR_SURFACE_HI, COLOR_TEXT);
        w += uiButtonWidth(key) + BADGE_GAP;
        while (*p == ' ')
            p++;
    }
    if (*p) {
        w += LABEL_GAP - BADGE_GAP;
        w += draw ? fontDrawMiddle(FONT_REGULAR, HELP_TEXT, x + w, cy, p, COLOR_DIM)
                  : fontTextWidth(FONT_REGULAR, HELP_TEXT, p);
    }
    return w;
}

void uiDrawHelp(int x, float cy, int max_width, const char *help)
{
    int used = 0;
    while (*help) {
        const char *end = strstr(help, "  ");
        size_t len = end ? (size_t)(end - help) : strlen(help);
        int w = helpItem(help, len, x + used, cy, false);
        if (used + w > max_width)
            return;
        helpItem(help, len, x + used, cy, true);
        used += w + ITEM_GAP;
        help += len;
        while (*help == ' ')
            help++;
    }
}
