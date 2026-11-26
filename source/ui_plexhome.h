// Plex Home's profile picker over the library, as Plex's own "Who's
// watching?": each profile's avatar and name, a lock on the ones with a PIN;
// and the PIN pad, big enough for fingers. Touch targets go to uiLibraryTap
// (LIBHIT_PROFILE, LIBHIT_PIN_KEY); the help line is the app's.
#pragma once

#include <stdbool.h>

#include "plexhome.h"

#define UI_PROFILE_COLUMNS 5
#define UI_PIN_KEYS 12     // 1-9, then Delete, 0, Cancel.
#define UI_PIN_KEY_DELETE 9
#define UI_PIN_KEY_ZERO 10
#define UI_PIN_KEY_CANCEL 11
#define UI_PIN_KEY_COLUMNS 3

// The profiles; current: the uuid of the one in use; busy: a line saying
// what's under way instead of the profiles (reading them, switching).
void uiDrawProfiles(const PlexHomeUser *users, int count, int cursor, const char *current, const char *busy);
// The PIN pad for user: digits typed so far (shown as dots), the key under
// the cursor, and a message under the dots (a wrong PIN), or "" for none.
// checking: the PIN is being checked (the keys are dimmed).
void uiDrawPinPad(const PlexHomeUser *user, int digits, int cursor, const char *message, bool checking);
// The key's digit ('0'-'9'), or 0 for Delete and Cancel.
char uiPinKeyDigit(int key);
