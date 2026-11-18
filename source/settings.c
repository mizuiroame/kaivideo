#include "settings.h"

#include <stdlib.h>
#include <string.h>

#include "background.h"
#include "theme.h"

static const char *const LABELS[SETTING_COUNT] = {
    "Library folder", "Look for new videos", "Theme", "Accent color", "Background",
    "Background blur", "Thumbnails", "Subtitle size", "About KaiVideo",
};
static const char *const SUBTITLE_NAMES[SUBTITLES_COUNT] = {"Small", "Medium", "Large"};
static const int SUBTITLE_PX[SUBTITLES_COUNT] = {28, 34, 42};

void configDefaults(Config *c)
{
    memset(c, 0, sizeof(*c));
    c->blur = BLUR_MEDIUM;
    c->thumbnails = true;
    c->subtitles = SUBTITLES_MEDIUM;
}

// A number in 0..count-1, or fallback.
static int readChoice(const char *value, int count, int fallback)
{
    char *end;
    long n = strtol(value, &end, 10);
    return end != value && *end == '\0' && n >= 0 && n < count ? (int)n : fallback;
}

static bool isKey(const char *line, size_t length, const char *key)
{
    return strlen(key) == length && strncmp(line, key, length) == 0;
}

bool configReadLine(Config *c, const char *line)
{
    const char *eq = strchr(line, '=');
    if (!eq)
        return false;
    size_t length = (size_t)(eq - line);
    const char *value = eq + 1;
    if (isKey(line, length, "theme"))
        c->theme = readChoice(value, THEME_COUNT, 0);
    else if (isKey(line, length, "accent"))
        c->accent = readChoice(value, THEME_ACCENT_COUNT, 0);
    else if (isKey(line, length, "background"))  // A name in the Backgrounds folder, never a path.
        snprintf(c->background, sizeof(c->background), "%s", strchr(value, '/') ? "" : value);
    else if (isKey(line, length, "blur"))
        c->blur = readChoice(value, BLUR_COUNT, BLUR_MEDIUM);
    else if (isKey(line, length, "thumbnails"))
        c->thumbnails = strcmp(value, "0") != 0;
    else if (isKey(line, length, "subtitles"))
        c->subtitles = readChoice(value, SUBTITLES_COUNT, SUBTITLES_MEDIUM);
    else
        return false;
    return true;
}

void configWrite(const Config *c, FILE *f)
{
    fprintf(f, "theme=%d\naccent=%d\n", c->theme, c->accent);
    if (c->background[0] && !strpbrk(c->background, "\r\n"))
        fprintf(f, "background=%s\n", c->background);
    fprintf(f, "blur=%d\nthumbnails=%d\nsubtitles=%d\n", c->blur, c->thumbnails ? 1 : 0, c->subtitles);
}

const char *settingLabel(SettingRow row)
{
    return row >= 0 && row < SETTING_COUNT ? LABELS[row] : "";
}

bool settingIsChoice(SettingRow row)
{
    return row == SETTING_THEME || row == SETTING_ACCENT || row == SETTING_BACKGROUND || row == SETTING_BLUR ||
           row == SETTING_THUMBNAILS || row == SETTING_SUBTITLES;
}

static int wrap(int value, int delta, int count)
{
    return ((value + delta) % count + count) % count;
}

void settingStep(Config *c, SettingRow row, int delta)
{
    switch (row) {
    case SETTING_THEME: c->theme = wrap(c->theme, delta, THEME_COUNT); break;
    case SETTING_ACCENT: c->accent = wrap(c->accent, delta, THEME_ACCENT_COUNT); break;
    case SETTING_BLUR: c->blur = wrap(c->blur, delta, BLUR_COUNT); break;
    case SETTING_THUMBNAILS: c->thumbnails = !c->thumbnails; break;
    case SETTING_SUBTITLES: c->subtitles = wrap(c->subtitles, delta, SUBTITLES_COUNT); break;
    default: break;
    }
}

void settingValue(const Config *c, SettingRow row, char *out, size_t size)
{
    switch (row) {
    case SETTING_THEME: snprintf(out, size, "%s", themeName(c->theme)); break;
    case SETTING_ACCENT: snprintf(out, size, "%s", themeAccentName(c->accent)); break;
    case SETTING_BACKGROUND: snprintf(out, size, "%s", c->background[0] ? c->background : "None"); break;
    case SETTING_BLUR: snprintf(out, size, "%s", backgroundBlurName(c->blur)); break;
    case SETTING_THUMBNAILS: snprintf(out, size, "%s", c->thumbnails ? "On" : "Off"); break;
    case SETTING_SUBTITLES:
        snprintf(out, size, "%s", SUBTITLE_NAMES[c->subtitles >= 0 && c->subtitles < SUBTITLES_COUNT ? c->subtitles
                                                                                                   : SUBTITLES_MEDIUM]);
        break;
    default: out[0] = '\0'; break;
    }
}

int settingSubtitlePx(int size)
{
    return SUBTITLE_PX[size >= 0 && size < SUBTITLES_COUNT ? size : SUBTITLES_MEDIUM];
}
