// The Settings page's rows and the choices kept in settings.txt: theme,
// accent color, background image and its blur, thumbnails and the size of
// subtitles. Pure logic.
#pragma once

#include <stdbool.h>
#include <stdio.h>

#define SETTINGS_NAME_MAX 128

typedef enum {
    SETTING_LIBRARY,     // Opens the folder picker.
    SETTING_RESCAN,      // Looks for videos again.
    SETTING_THEME,
    SETTING_ACCENT,
    SETTING_BACKGROUND,
    SETTING_BLUR,
    SETTING_THUMBNAILS,
    SETTING_SUBTITLES,
    SETTING_ABOUT,       // Opens the About page.
    SETTING_COUNT,
} SettingRow;

typedef enum { SUBTITLES_SMALL, SUBTITLES_MEDIUM, SUBTITLES_LARGE, SUBTITLES_COUNT } SubtitleSize;

typedef struct {
    int theme, accent;
    char background[SETTINGS_NAME_MAX];  // A file in <library>/Backgrounds, or "" for none.
    int blur;                            // BackgroundBlur.
    bool thumbnails;                     // Pictures from the videos on their cards.
    int subtitles;                       // SubtitleSize.
} Config;

void configDefaults(Config *c);
// Reads a "key=value" line of settings.txt into c. False if it isn't one of its keys.
bool configReadLine(Config *c, const char *line);
void configWrite(const Config *c, FILE *f);

const char *settingLabel(SettingRow row);
// Rows changed with left and right (the others open something with A).
bool settingIsChoice(SettingRow row);
// Steps a choice by delta, wrapping (not the background: it needs the list of images).
void settingStep(Config *c, SettingRow row, int delta);
// The row's value as shown ("Dark", "Medium"); "" for rows that open something.
void settingValue(const Config *c, SettingRow row, char *out, size_t size);
// The subtitles' text size for SubtitleSize, in pixels of the 1280x720 layout.
int settingSubtitlePx(int size);
