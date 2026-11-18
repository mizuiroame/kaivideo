// Scans the library folder in a thread of its own, so the screens stay
// live meanwhile (a big card takes seconds).
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "library.h"

// Starts scanning root. False if a scan is running or the thread can't start.
bool libscanStart(const char *root);
// True while scanning; found says how many videos so far.
bool libscanBusy(size_t *found);
// True once, when a scan has finished: out gets its library (the caller
// frees it). ok is false if the folder couldn't be read.
bool libscanTake(Library *out, bool *ok);
// Stops a running scan and waits for it.
void libscanStop(void);
