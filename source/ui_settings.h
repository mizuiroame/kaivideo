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
// Who's signed in to each server ("" for none), shown on the main page's rows.
void uiSettingsSetServers(const char *const users[SERVER_KIND_COUNT]);
// What the network folder's rows say: its address and user ("" for none).
// What the network folder's rows show: its address, user, and its two switches.
void uiSettingsSetNetwork(const char *address, const char *user, bool remember, bool allow_unsigned, bool smb);
