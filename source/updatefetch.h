// The update check's network side (update.h, updaterelease.h): asking GitHub
// for the latest release, and installing it, on a thread of its own.
#pragma once

#include <stdbool.h>

#include "update.h"

typedef enum {
    UPDATE_IDLE,         // Nothing asked since KaiVideo started.
    UPDATE_CHECKING,
    UPDATE_CURRENT,      // This is the latest version.
    UPDATE_FOUND,        // A newer one: status.tag.
    UPDATE_CHECK_FAILED, // GitHub couldn't be asked, or its answer wasn't usable.
    UPDATE_DOWNLOADING,  // Installing status.tag.
    UPDATE_INSTALLED,    // status.tag is in place: it runs from the next start.
    UPDATE_INSTALL_FAILED,
} UpdateState;

typedef struct {
    UpdateState state;
    UpdatePlace place;
    char tag[UPDATE_TAG_MAX];  // The newer version, for FOUND and after.
    char error[256];           // Why it failed, in the language in use.
    float progress;            // While DOWNLOADING: 0 to 1 (0 until the size is known).
    unsigned serial;           // Counts changes, so the screen notices each one once.
} UpdateStatus;

// nro_path: argv[0], where KaiVideo runs from.
void updateFetchInit(const char *nro_path);
// Starts asking GitHub. False if a check or an install is running.
bool updateFetchCheck(void);
// Installs the newer version found. False if there is none, KaiVideo is
// Brewstore's (or of an unknown place), or something is running.
bool updateFetchInstall(void);
void updateFetchStatus(UpdateStatus *out);
// Stops what's running (its download is cancelled) and waits for it.
void updateFetchExit(void);
