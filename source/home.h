// The Home screen's rows, worked out from the library and where videos
// were left: Continue watching, Recently added, Series, Films. Empty rows
// are left out. Pure logic.
#pragma once

#include "library.h"
#include "resume.h"
#include "watched.h"

#define HOME_ROW_MAX 16  // Cards per row.

typedef enum {
    HOME_CONTINUE,  // Videos started and not finished, the latest first; then each
                    // series' next episode after the one watched last ("Up next").
    HOME_RECENT,    // Films and series, the newest files first.
    HOME_SERIES,    // By name.
    HOME_FILMS,     // By title.
    HOME_KIND_COUNT,
} HomeRowKind;

typedef struct {
    HomeRowKind kind;
    LibraryRef items[HOME_ROW_MAX];
    bool up_next[HOME_ROW_MAX];  // Continue watching: an episode not started, next in its series.
    int count;
} HomeRow;

typedef struct {
    HomeRow rows[HOME_KIND_COUNT];
    int count;  // Rows with something in them.
} Home;

// resume and watched may be NULL.
void homeCompute(Home *home, const Library *lib, const ResumeList *resume, const Watched *watched);
const char *homeRowTitle(HomeRowKind kind);
