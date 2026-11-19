#include "keyboard_system.h"

#include <string.h>

#include <switch.h>

static SystemKeyboardResult showKeyboard(const char *title, const char *initial, bool hidden, char *out, size_t size,
                                         size_t max_length)
{
    SwkbdConfig config;
    if (R_FAILED(swkbdCreate(&config, 0)))
        return SYSTEM_KEYBOARD_UNAVAILABLE;
    swkbdConfigMakePresetDefault(&config);
    swkbdConfigSetType(&config, SwkbdType_All);
    swkbdConfigSetHeaderText(&config, title);
    swkbdConfigSetGuideText(&config, title);
    swkbdConfigSetInitialText(&config, initial ? initial : "");
    swkbdConfigSetStringLenMax(&config, (u32)max_length);
    if (hidden)
        swkbdConfigSetPasswordFlag(&config, 1);

    memset(out, 0, size);
    Result rc = swkbdShow(&config, out, size);
    swkbdClose(&config);
    if (R_SUCCEEDED(rc))
        return SYSTEM_KEYBOARD_OK;
    // libnx reports the user's cancel as a bad library applet exit.
    if (rc == MAKERESULT(Module_Libnx, LibnxError_LibAppletBadExit))
        return SYSTEM_KEYBOARD_CANCELLED;
    return SYSTEM_KEYBOARD_UNAVAILABLE;
}

SystemKeyboardResult systemKeyboardInput(const char *title, const char *initial, char *out, size_t size,
                                         size_t max_length)
{
    return showKeyboard(title, initial, false, out, size, max_length);
}

SystemKeyboardResult systemKeyboardPassword(const char *title, char *out, size_t size, size_t max_length)
{
    return showKeyboard(title, "", true, out, size, max_length);
}
