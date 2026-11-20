// The Settings pages' rows, the subtitle options of the player's Subtitles
// list, and the choices kept in settings.txt. Pure logic.
#pragma once

#include <stdbool.h>
#include <stdio.h>

#include "jellyfin.h"

#define SETTINGS_NAME_MAX 128
#define SETTINGS_PAGE_MAX_ROWS 7

typedef enum {
    SETTING_LIBRARY,       // Opens the folder picker.
    SETTING_RESCAN,        // Looks for videos again.
    SETTING_THUMBNAILS,
    SETTING_AUTOPLAY,      // Play the next episode when one ends.
    SETTING_OPEN_DISPLAY,  // Opens the Display page.
    SETTING_ABOUT,         // Opens the About page.
    SETTING_THEME,
    SETTING_ACCENT,
    SETTING_BACKGROUND,
    SETTING_BLUR,
    SETTING_CLOCK,
    SETTING_OPEN_JELLYFIN,  // Opens the Media server page (Jellyfin or Plex).
    SETTING_JF_SERVER,      // Types the server's address.
    SETTING_JF_CODE,        // Signs in with Quick Connect.
    SETTING_JF_PASSWORD,    // Signs in with a user name and password.
    SETTING_JF_SIGN_OUT,    // Signs out.
    SETTING_JF_QUALITY,     // The most a video may use, from the server.
    SETTING_JF_FIND,        // Looks for servers on the network.
    SETTING_JF_TEST,        // Tries the server, step by step.
    SETTING_JF_CERTIFICATE, // Whether an https server's certificate must be trusted.
    SETTING_SERVER_KIND,    // Jellyfin or Plex.
    SETTING_PLEX_CODE,      // Signs in with a code typed at plex.tv/link.
    SETTING_PLEX_SERVER,    // The Plex server in use (shown, not chosen).
    SETTING_COUNT,
} SettingRow;

// Settings opens on the main page; Display holds the looks.
typedef enum { SETTINGS_PAGE_MAIN, SETTINGS_PAGE_DISPLAY, SETTINGS_PAGE_JELLYFIN, SETTINGS_PAGE_COUNT } SettingsPage;

typedef enum { SUBTITLES_SMALL, SUBTITLES_MEDIUM, SUBTITLES_LARGE, SUBTITLES_SIZE_COUNT } SubtitleSize;
typedef enum { SUBTITLE_OUTLINE, SUBTITLE_BOX, SUBTITLE_STYLE_COUNT } SubtitleStyle;
#define SUBTITLE_COLOR_COUNT 4

// The player's Subtitles list ends with these, changed with left and right.
typedef enum { SUBOPT_SIZE, SUBOPT_COLOR, SUBOPT_STYLE, SUBOPT_FONT, SUBOPT_COUNT } SubtitleOption;
#define SUBTITLE_FONTS_FOLDER "fonts"  // In the config folder: .ttf and .otf files for subtitles.

typedef struct {
    int theme, accent;
    char background[SETTINGS_NAME_MAX];  // A file in <library>/Backgrounds, or "" for none.
    int blur;                            // BackgroundBlur.
    bool thumbnails;                     // Pictures from the videos on their cards.
    bool clock_24h;                      // The header's clock: 21:47 or 9:47 PM.
    bool autoplay;                       // When an episode ends, the next one plays (after a countdown).
    bool use_jellyfin;                   // The library comes from the media server.
    int server_kind;                     // ServerKind: the one Settings' Media server page is for.
    int server_view;                     // ServerKind: the one the library shows (X goes through them).
    int jf_quality;                      // An index in the quality list; 0 is Auto.
    bool jf_check_certificate;           // Off: an https server's own certificate is taken too.
    int subtitles;                       // SubtitleSize.
    int subtitle_color;                  // White, Yellow, Cyan, Green.
    int subtitle_style;                  // SubtitleStyle.
    char subtitle_font[SETTINGS_NAME_MAX];  // A file in config/fonts, or "" for the built-in font.
} Config;

void configDefaults(Config *c);
// Reads a "key=value" line of settings.txt into c. False if it isn't one of its keys.
bool configReadLine(Config *c, const char *line);
void configWrite(const Config *c, FILE *f);

// The rows of a page, in order. Returns how many. signed_in: to the media
// server, and kind: which one it is, decide what its page offers.
int settingsPageRows(SettingsPage page, bool signed_in, ServerKind kind, SettingRow rows[SETTINGS_PAGE_MAX_ROWS]);
const char *settingsPageTitle(SettingsPage page);
const char *settingLabel(SettingRow row);
// Rows changed with left and right (the others open something with A).
bool settingIsChoice(SettingRow row);
// Steps a choice by delta, wrapping (not the background: it needs the list of images).
void settingStep(Config *c, SettingRow row, int delta);
// The row's value as shown ("Dark", "Medium"); "" for rows that open a page.
void settingValue(const Config *c, SettingRow row, char *out, size_t size);

// The most bits per second to ask the server for, with this quality.
long long settingQualityBitrate(const Config *c);
// How many of the options (from the first) a list offers: subtitles with their
// own look take only the size.
int subtitleOptionCount(bool color_applies);
const char *subtitleOptionLabel(SubtitleOption option);
void subtitleOptionValue(const Config *c, SubtitleOption option, char *out, size_t size);
// Steps an option (not the font: it needs the list of font files).
void subtitleOptionStep(Config *c, SubtitleOption option, int delta);
// The subtitles' text size in pixels of the 1280x720 layout, and color (0xRRGGBB).
int settingSubtitlePx(const Config *c);
// The size chosen against Medium: for subtitles that bring their own sizes.
double settingSubtitleScale(const Config *c);
unsigned settingSubtitleColor(const Config *c);
