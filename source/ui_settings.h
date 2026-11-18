// Settings and About, drawn over the library's screens (after them, so
// their touch targets are on top; a tap outside closes them).
#pragma once

#include "settings.h"

// cursor: the selected row (a SettingRow on page). library_root: "" if
// none chosen. version: KaiVideo's, for About.
void uiDrawSettings(const Config *c, SettingsPage page, int cursor, const char *library_root, const char *version);
void uiDrawAbout(const char *version);
