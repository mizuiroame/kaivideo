// The Home screen's rows, worked out from the library and where videos
// were left: Continue watching, Recently added, Series, Films. Empty rows
// are left out. Pure logic.
#pragma once

#include "library.h"
#include "resume.h"

#define HOME_ROW_MAX 16  // Cards per row.

typedef enum {
    HOME_CONTINUE,  // Videos started and not finished, the latest first.
    HOME_RECENT,    // Films and series, the newest files first.
    HOME_SERIES,    // By name.
    HOME_FILMS,     // By title.
    HOME_KIND_COUNT,
} HomeRowKind;

typedef struct {
    HomeRowKind kind;
    LibraryRef items[HOME_ROW_MAX];
    int count;
} HomeRow;

typedef struct {
    HomeRow rows[HOME_KIND_COUNT];
    int count;  // Rows with something in them.
} Home;

void homeCompute(Home *home, const Library *lib, const ResumeList *resume);
const char *homeRowTitle(HomeRowKind kind);
