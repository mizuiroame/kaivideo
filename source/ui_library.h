// What the library's screens draw: the tab bar, Home's rows of cards, the
// Films and Series grids, a series' page, Search, and the first-run choice
// of a folder. Each draw remembers where its touch targets went, and
// uiLibraryTap answers from them.
#pragma once

#include "home.h"
#include "library.h"
#include "libview.h"
#include "resume.h"

#define UI_TAB_BAR_H 68

typedef enum {
    LIBHIT_NONE,
    LIBHIT_TAB,         // a: the tab.
    LIBHIT_SETTINGS,    // The tab bar's Settings button.
    LIBHIT_HOME_CARD,   // a: the Home row (on screen), b: the card in it.
    LIBHIT_GRID_CARD,   // a: the card.
    LIBHIT_BACK,        // A series' page: back to where it was opened from.
    LIBHIT_SEASON,      // a: the season (an index in its seasons).
    LIBHIT_EPISODE,     // a: the episode (an index in the season).
    LIBHIT_SEARCH_BOX,  // Type a search.
    LIBHIT_RESULT,      // a: the result.
    LIBHIT_SETTING,     // a: a row of Settings (A on it).
    LIBHIT_SETTING_PREV, // a: a choice's left arrow.
    LIBHIT_SETTING_NEXT, // a: a choice's right arrow.
    LIBHIT_PANEL,       // Settings or About, but not a row: nothing.
    LIBHIT_PAGE_BACK,   // A Settings page's Back: the main page.
    LIBHIT_CLOSE,       // Outside Settings or About, or their back arrow: closes.
} LibHitKind;

typedef struct {
    LibHitKind kind;
    int a, b;
} LibHit;

// Clears the screen to the background image, or the theme's color.
void uiDrawBackground(void);
// Cards show pictures from the videos (thumbs.h) when on.
void uiLibrarySetThumbnails(bool on);
// The tab bar, with status (a scan's progress, or NULL) left of the
// Settings button. The Files tab draws the folder list under it.
void uiDrawTabBar(LibTab active, const char *status);
// The current tab (not Files) or a series' page, under the tab bar, which
// it draws too. folder_set: false before a library folder is chosen.
void uiDrawLibrary(const LibView *v, const Library *lib, const Home *home, const ResumeList *resume,
                   bool folder_set, const char *status, const char *message);

// A message for a while at the bottom (an error), over everything.
void uiDrawToast(const char *message);
// Where a tap lands on what was drawn last.
LibHit uiLibraryTap(int x, int y);
// Adds a touch target to what was drawn last (ui_settings.c's).
void uiLibraryAddHit(int x, int y, int w, int h, LibHitKind kind, int a, int b);
