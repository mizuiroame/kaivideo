// Plex Home without talking to plex.tv: the profiles of an account's Home
// (GET /api/v2/home/users), the token a switch gives (POST
// /api/v2/home/users/<uuid>/switch), what a refused switch means, and which of
// the profile's servers to use. Pure logic over strings; plexhomesync.c talks.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "plex.h"

#define PLEX_HOME_USERS_MAX 16  // Plex allows 15 profiles in a Home.
#define PLEX_HOME_PIN_DIGITS 4
#define PLEX_HOME_PATH_MAX 192

typedef struct {
    char uuid[PLEX_ID_MAX];
    long long id;
    char title[PLEX_NAME_MAX];
    char thumb[PLEX_URI_MAX];  // Its avatar's address (plex.tv), or "".
    bool admin;                // The Home's owner.
    bool protected_pin;        // Switching to it asks for its PIN.
    bool restricted;           // A managed profile (a child's, say).
    bool guest;
} PlexHomeUser;

// Why a switch failed (or didn't).
typedef enum {
    PLEX_SWITCH_OK,
    PLEX_SWITCH_WRONG_PIN,    // The PIN isn't the profile's.
    PLEX_SWITCH_TOO_MANY,     // Too many wrong PINs: plex.tv waits a while.
    PLEX_SWITCH_GONE,         // The profile isn't in the Home any more.
    PLEX_SWITCH_REFUSED,      // Any other refusal.
    PLEX_SWITCH_UNREACHABLE,  // No answer from plex.tv.
    PLEX_SWITCH_NO_SERVER,    // The server in use isn't shared with the profile, or doesn't answer it.
} PlexSwitchResult;

// The profiles of a Home, its owner first, guests left out. Returns how many
// (at most max), -1 if it can't be read. An account without a Home gives one.
int plexHomeUsersParse(const char *json, PlexHomeUser *out, int max);
// The token in a switch's answer ("authToken", or the older "authenticationToken").
bool plexHomeSwitchToken(const char *json, char *token, size_t size);
// What a refused switch's HTTP status (0: no answer) says.
PlexSwitchResult plexHomeSwitchError(long status);
// The text to show for a result (translated), "" for PLEX_SWITCH_OK.
const char *plexHomeSwitchMessage(PlexSwitchResult result);
// True for a PIN as Plex takes it: PLEX_HOME_PIN_DIGITS digits.
bool plexHomePinValid(const char *pin);
// The switch's path on plex.tv, without the client's query. pin: "" for none.
// False if uuid or pin can't be one.
bool plexHomeSwitchPath(const char *uuid, const char *pin, char *out, size_t size);
// Which of the profile's servers to use: the one in use before (server_id),
// -1 if the profile hasn't it; with no server before (""), the first
// (plexResourcesParse puts the owned ones first), -1 if none.
int plexHomePickServer(const PlexServer *servers, int count, const char *server_id);
// The index of the profile with that uuid, -1 if none.
int plexHomeFind(const PlexHomeUser *users, int count, const char *uuid);
// The name a profile goes by in KaiVideo: its account's user name (a full
// Plex account in the Home), else its title (a managed profile has no user name).
void plexHomeName(const char *username, const char *title, char *out, size_t size);
