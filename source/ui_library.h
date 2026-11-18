// What the library's screens draw: the tab bar, Home's rows of cards, the
// Films and Series grids, a series' page, Search, and the first-run choice
// of a folder. Each draw remembers where its touch targets went, and
// uiLibraryTap answers from them.
#pragma once

#include "home.h"
#include "library.h"
#include "libview.h"
#include "resume.h"
#include "sysinfo.h"
#include "watched.h"

#define UI_TAB_BAR_H 64
#define UI_FOOTER_Y 664  // The help line below it, as in KaiPlayer.
#define UI_DIM_ALPHA 160

typedef enum {
    LIBHIT_NONE,
    LIBHIT_TAB,         // a: the tab.
    LIBHIT_MENU,        // The header's menu button and name: the + menu.
    LIBHIT_MENU_ITEM,   // a: an item of the + menu.
    LIBHIT_HOME_CARD,   // a: the Home row (on screen), b: the card in it.
    LIBHIT_GRID_CARD,   // a: the card.
    LIBHIT_BACK,        // A series' page: back to where it was opened from.
    LIBHIT_SEASON,      // a: the season (an index in its seasons).
    LIBHIT_EPISODE,     // a: the episode (an index in the season).
    LIBHIT_WATCHED,     // a: the episode whose watched circle it is.
    LIBHIT_SEARCH_BOX,  // Type a search.
    LIBHIT_RESULT,      // a: the result.
    LIBHIT_SETTING,     // a: a row of Settings (A on it).
    LIBHIT_SETTING_PREV, // a: a choice's left arrow.
    LIBHIT_SETTING_NEXT, // a: a choice's right arrow.
    LIBHIT_PANEL,       // Settings or About, but not a row: nothing.
    LIBHIT_PAGE_BACK,   // A Settings page's Back: the main page.
    LIBHIT_CLOSE,       // Outside Settings or About, or their back arrow: closes.
    LIBHIT_UP_NEXT,     // The next episode's card in the player: play it now.
} LibHitKind;

typedef struct {
    LibHitKind kind;
    int a, b;
} LibHit;

// Clears the screen to the background image, or the theme's color.
void uiDrawBackground(void);
// Cards show pictures from the videos (thumbs.h) when on.
void uiLibrarySetThumbnails(bool on);
// What was watched, for the marks on episodes and films (NULL: none).
void uiLibrarySetWatched(const Watched *watched);
// The clock and battery for the header, and how the clock reads.
void uiLibrarySetStatus(const SysInfo *sys, bool clock_24h);
// Forgets the touch targets: each frame starts with none.
void uiLibraryResetHits(void);
// The header: the menu button and name, the tabs between ZL and ZR, and
// status (a scan's progress, or NULL) left of the clock and battery. The
// Files tab draws the folder list under it.
void uiDrawTabBar(LibTab active, const char *status);
// The help line's strip at the bottom (before overlays dim the screen), and
// the help line itself, "[A] open  [B] back" (after them).
void uiDrawFooterStrip(void);
void uiDrawFooterHelp(const char *help);
// The + menu, or a question with its answers: title (the app's name, with
// its version, or a question when version is NULL) and items.
void uiDrawMenu(const char *title, const char *version, const char *const *items, int count, int cursor);
// The current tab (not Files) or a series' page, under the tab bar, which
// it draws too. folder_set: false before a library folder is chosen.
void uiDrawLibrary(const LibView *v, const Library *lib, const Home *home, const ResumeList *resume,
                   bool folder_set, const char *status, const char *message);

// The next episode (item), a card on the right of the player in an
// episode's last seconds: its picture, what's left (fraction_left of the
// time, seconds_left), ZR to play it now, and a close button (LIBHIT_CLOSE).
// Above the bar when it's shown.
void uiDrawUpNextCard(const Library *lib, int item, double fraction_left, int seconds_left, bool bar_shown);
// A message for a while at the bottom (an error), over everything.
void uiDrawToast(const char *message);
// Where a tap lands on what was drawn last.
LibHit uiLibraryTap(int x, int y);
// Adds a touch target to what was drawn last (ui_settings.c's).
void uiLibraryAddHit(int x, int y, int w, int h, LibHitKind kind, int a, int b);
