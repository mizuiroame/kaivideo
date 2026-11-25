// Plex Home's profiles, on a thread of their own so the screens stay live:
// reading who's in the account's Home, and switching to one of them (with its
// PIN), which gives the profile's own token and its token for the server in
// use. A switch that works replaces the Plex session (jfsync.c), and the
// library is read again as that profile sees it. One thing at a time.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "plexhome.h"

typedef enum {
    PLEX_HOME_IDLE,
    PLEX_HOME_LOADING,
    PLEX_HOME_LOADED,       // users/count hold the Home's profiles.
    PLEX_HOME_LOAD_FAILED,
    PLEX_HOME_SWITCHING,
    PLEX_HOME_SWITCHED,     // The profile is in use.
    PLEX_HOME_SWITCH_FAILED,  // result says why; the profile before stays.
} PlexHomeState;

typedef struct {
    PlexHomeState state;
    PlexHomeUser users[PLEX_HOME_USERS_MAX];
    int count;
    PlexSwitchResult result;
    char message[160];                // Why it failed, to show ("" if it didn't).
    char switched_to[PLEX_NAME_MAX];  // The name the profile switched to goes by.
} PlexHomeStatus;

// Reads the Home's profiles with the Plex session's token. False if something
// is under way or Plex isn't signed in.
bool plexHomeStartLoad(void);
// Switches to a profile read by the last load. pin: its PIN, "" for none (kept
// only until it's sent). False if something is under way.
bool plexHomeStartSwitch(const PlexHomeUser *user, const char *pin);
bool plexHomeBusy(void);
// What came of it: the state, and the profiles read.
void plexHomeGetStatus(PlexHomeStatus *out);
// Waits for what's under way, at exit.
void plexHomeStop(void);
