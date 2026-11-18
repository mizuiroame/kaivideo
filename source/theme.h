// KaiVideo's looks: a theme (the dark colors everything is drawn in) and an
// accent color, chosen in Settings. The colors live in g_colors, which
// ui_style.h's COLOR_ names read.
#pragma once

#include "gfx.h"

#define THEME_COUNT 4
#define THEME_ACCENT_COUNT 6

typedef struct {
    Color bg, surface, surface_hi, text, dim, faint, track, accent, error;
} UiColors;

extern UiColors g_colors;

const char *themeName(int theme);
const char *themeAccentName(int accent);
// Sets g_colors to the theme's colors with the accent (out of range: the first).
void themeApply(int theme, int accent);
