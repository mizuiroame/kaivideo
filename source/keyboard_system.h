// The console's own software keyboard (swkbd), as OunceShell uses it on
// Switch 2: for typing a search.
#pragma once

#include <stddef.h>

typedef enum {
    SYSTEM_KEYBOARD_OK,
    SYSTEM_KEYBOARD_CANCELLED,
    SYSTEM_KEYBOARD_UNAVAILABLE,
} SystemKeyboardResult;

// Shows the keyboard and waits for it. On OK, out holds the text.
SystemKeyboardResult systemKeyboardInput(const char *title, const char *initial, char *out,
                                         size_t size, size_t max_length);
// The same for a password: what's typed isn't shown.
SystemKeyboardResult systemKeyboardPassword(const char *title, char *out, size_t size, size_t max_length);
