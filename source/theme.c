#include "theme.h"

#define RGB(hex) GFX_RGB(((hex) >> 16) & 0xFF, ((hex) >> 8) & 0xFF, (hex) & 0xFF)

static const char *const THEME_NAMES[THEME_COUNT] = {"Dark", "Black", "Midnight", "Plum"};
// bg, surface, surface_hi, text, dim, faint, track.
static const unsigned THEMES[THEME_COUNT][7] = {
    {0x121214, 0x1C1C21, 0x2C2C34, 0xF2F2F5, 0xA4A4B0, 0x5E5E6A, 0x404048},
    {0x000000, 0x121214, 0x24242A, 0xF2F2F5, 0xA0A0AA, 0x5A5A64, 0x3A3A42},
    {0x0D1120, 0x161C2E, 0x252D46, 0xF0F2F8, 0xA2AAC2, 0x5A6382, 0x343D5A},
    {0x161018, 0x211824, 0x342638, 0xF5F0F5, 0xB4A4B8, 0x6C5C72, 0x46384C},
};
static const char *const ACCENT_NAMES[THEME_ACCENT_COUNT] = {"Pink", "Blue", "Green", "Purple", "Orange", "Red"};
static const unsigned ACCENTS[THEME_ACCENT_COUNT] = {0xFF6B9A, 0x4DA3FF, 0x3DD68C, 0xA78BFA, 0xFF9F45, 0xFF5C5C};

UiColors g_colors = {
    RGB(0x121214), RGB(0x1C1C21), RGB(0x2C2C34), RGB(0xF2F2F5), RGB(0xA4A4B0),
    RGB(0x5E5E6A), RGB(0x404048), RGB(0xFF6B9A), RGB(0xFF8A5C),
};

const char *themeName(int theme)
{
    return theme >= 0 && theme < THEME_COUNT ? THEME_NAMES[theme] : THEME_NAMES[0];
}

const char *themeAccentName(int accent)
{
    return accent >= 0 && accent < THEME_ACCENT_COUNT ? ACCENT_NAMES[accent] : ACCENT_NAMES[0];
}

Color themeAccentColor(int accent)
{
    return RGB(ACCENTS[accent >= 0 && accent < THEME_ACCENT_COUNT ? accent : 0]);
}

void themeApply(int theme, int accent)
{
    const unsigned *t = THEMES[theme >= 0 && theme < THEME_COUNT ? theme : 0];
    g_colors.bg = RGB(t[0]);
    g_colors.surface = RGB(t[1]);
    g_colors.surface_hi = RGB(t[2]);
    g_colors.text = RGB(t[3]);
    g_colors.dim = RGB(t[4]);
    g_colors.faint = RGB(t[5]);
    g_colors.track = RGB(t[6]);
    g_colors.accent = RGB(ACCENTS[accent >= 0 && accent < THEME_ACCENT_COUNT ? accent : 0]);
}
