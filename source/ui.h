// What KaiVideo draws over the 1280x720 layout: the folder list to choose a
// video, and the bar over a playing one. And where a tap lands on them.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "browser.h"
#include "menu.h"
#include "player.h"
#include "resume.h"
#include "settings.h"

#define UI_HEADER_Y 72  // The folder list: its path, under the tab bar.
#define UI_LIST_Y 128
#define UI_ROW_H 58
#define UI_ROWS 9
#define UI_MENU_ROW_H 48  // A row of the player's drop-down lists.
#define UI_PLAYER_BAR_H 120  // The player's bar along the bottom.


// The folder list under the tab bar, with how far each video has been
// watched (the help line under it is the app's).
typedef enum {
    BROWSE_FILES,      // The Files tab.
    BROWSE_PICK,       // Choosing the library folder: a title, Use and Cancel buttons.
    BROWSE_PICK_FIRST, // The same the first time, without Cancel.
} BrowseMode;
void uiDrawBrowser(const Browser *b, const ResumeList *resume, BrowseMode mode);
// The bar over a playing video, and the close button above it; note, if
// any, is said above the bar ("Resumed at 12:34").
// scrub: where a finger drags the progress bar (0-1), shown instead of the
// playing position; -1 when it isn't.
void uiDrawPlayerBar(const Player *p, const char *name, const char *note, double scrub);
// Where x falls along the progress bar (0-1): dragging it.
double uiSeekFraction(int x);
// Subtitle text ('\n' between lines), centered at the bottom with an
// outline, wrapped to the screen; above the bar when it shows.
// The loading symbol, turned by angle (radians), over whatever is there.
void uiDrawSpinner(float cx, float cy, float angle);
// The same with a line under it, over the video.
void uiDrawWaiting(float angle, const char *text);
// The screen while a video opens.
void uiDrawLoading(const char *name, float angle, bool cancelling);
void uiDrawSubtitles(const char *text, bool bar_shown);
// The subtitles' text size (layout pixels), color (0xRRGGBB), and a dark
// box behind them instead of an outline.
void uiSetSubtitleStyle(int px, unsigned rgb, bool box);
// How far (layout pixels) subtitles go up while the player's bar shows.
int uiSubtitleBarLift(void);
// A paused video: a big play symbol in the middle (a tap there plays on).
void uiDrawPausedSymbol(void);

typedef enum {
    UI_MENU_AUDIO,      // The sound tracks.
    UI_MENU_SUBTITLES,  // "Off", the subtitle tracks, then the options that apply to them.
    UI_MENU_PLAYBACK,   // The chapters, then the speed and the sync delays.
} UiMenuKind;

// The drop-down list above the bar's Playback, Audio or CC button, with the
// chapter or track playing (or shown) checked and the cursor's row lit.
// config: the subtitles' options' values (fixed rows), or NULL.
void uiDrawTrackMenu(const Player *p, UiMenuKind kind, const Menu *m, const Config *config);

typedef enum {
    TAP_NOTHING,
    TAP_UP,        // The path at the top: the folder above.
    TAP_USE_FOLDER, // Choosing the library folder: use this one.
    TAP_CANCEL,     // Choosing the library folder: keep the one there was.
    TAP_ROW,       // A row: *row is its index.
    TAP_CLOSE,     // The player's close button.
    TAP_PAUSE,     // The player's play/pause button.
    TAP_SEEK,      // The progress bar: *fraction is where (0-1).
    TAP_SUBTITLES, // The player's CC button (the list of subtitles).
    TAP_AUDIO,     // The player's Audio button (the list of sound tracks).
    TAP_PLAYBACK,  // The player's Playback button (chapters, speed, delays).
    TAP_PICTURE,   // Anywhere else on a playing video.
    TAP_MENU_ROW,  // A row of the open drop-down list: *row is its index.
    TAP_MENU_BOX,  // The list, but not a row (its title).
    TAP_OUTSIDE,   // Outside the open list: it closes.
} TapTarget;

// Taps on the tab bar (y < UI_HEADER_Y) are the library's, but picking's button.
TapTarget uiTapBrowser(const Browser *b, int x, int y, BrowseMode mode, size_t *row);
// has_subtitles, has_tracks: the CC and Audio buttons are there.
TapTarget uiTapPlayer(int x, int y, bool has_subtitles, bool has_tracks, double *fraction);
TapTarget uiTapTrackMenu(UiMenuKind kind, const Menu *m, int x, int y, int *row);
// A tap on one of the list's option rows: -1 on its left half, 1 on its right.
int uiTapOptionDelta(UiMenuKind kind, const Menu *m, int x);
