// Settings and About, drawn over the library's screens (after them, so
// their touch targets are on top; a tap outside closes them).
#pragma once

#include "settings.h"

// cursor: the selected row (SettingRow). library_root: "" if none chosen.
void uiDrawSettings(const Config *c, int cursor, const char *library_root);
void uiDrawAbout(const char *version);
