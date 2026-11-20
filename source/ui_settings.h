// Settings and About, drawn over the library's screens (after them, so
// their touch targets are on top; a tap outside closes them).
#pragma once

#include "settings.h"

// cursor: the selected row (a SettingRow on page). library_root: "" if
// none chosen. version: KaiVideo's, for About.
void uiDrawSettings(const Config *c, SettingsPage page, int cursor, const char *library_root, const char *version);
void uiDrawAbout(const char *version);
// What the Jellyfin rows say: the server's address, and who is signed in ("" for nobody).
void uiSettingsSetJellyfin(const char *server, const char *user);
// Signing in: the code to approve on another device ("" while it's asked for).
// The code to approve: in Jellyfin's Quick Connect, or at plex.tv/link.
// linked: Plex took the code, and the server is being found.
void uiDrawSignInCode(bool plex, const char *code, const char *server, bool linked);
