// KaiVideo's colors (the theme's, theme.h) and margins, shared by the
// player's and the library's screens.
#pragma once

#include "gfx.h"
#include "theme.h"

#define MARGIN 32

#define COLOR_BG (g_colors.bg)
#define COLOR_SURFACE (g_colors.surface)
#define COLOR_SURFACE_HI (g_colors.surface_hi)
#define COLOR_TEXT (g_colors.text)
#define COLOR_DIM (g_colors.dim)
#define COLOR_FAINT (g_colors.faint)
#define COLOR_TRACK (g_colors.track)
#define COLOR_ACCENT (g_colors.accent)
#define COLOR_ERROR (g_colors.error)
// The sources' own colors, as the servers have theirs (uiServerColor): the library's pill and Settings' dots.
#define UI_NETWORK_COLOR GFX_RGB(0x2E, 0xC4, 0xB6)
#define UI_IPTV_COLOR GFX_RGB(0xF2, 0x8C, 0x28)
#define UI_ADDONS_COLOR GFX_RGB(0x4C, 0x8D, 0xF6)
