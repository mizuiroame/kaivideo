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
    LIBHIT_FOLDER,      // The tab bar's Folder button: choose the library folder.
    LIBHIT_HOME_CARD,   // a: the Home row (on screen), b: the card in it.
    LIBHIT_GRID_CARD,   // a: the card.
    LIBHIT_BACK,        // A series' page: back to where it was opened from.
    LIBHIT_SEASON,      // a: the season (an index in its seasons).
    LIBHIT_EPISODE,     // a: the episode (an index in the season).
    LIBHIT_SEARCH_BOX,  // Type a search.
    LIBHIT_RESULT,      // a: the result.
} LibHitKind;

typedef struct {
    LibHitKind kind;
    int a, b;
} LibHit;

// The tab bar, with status (a scan's progress, or NULL) left of the
// Folder button. The Files tab draws the folder list under it.
void uiDrawTabBar(LibTab active, const char *status);
// The current tab (not Files) or a series' page, under the tab bar, which
// it draws too. folder_set: false before a library folder is chosen.
void uiDrawLibrary(const LibView *v, const Library *lib, const Home *home, const ResumeList *resume,
                   bool folder_set, const char *status, const char *message);

// Where a tap lands on what was drawn last.
LibHit uiLibraryTap(int x, int y);
