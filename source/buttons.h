// Console buttons. The host builds (preview) get the same values as libnx.
#pragma once

#ifdef __SWITCH__
#include <switch.h>
#else
#define BIT(n) (1ull << (n))
enum {
    HidNpadButton_A = BIT(0),
    HidNpadButton_B = BIT(1),
    HidNpadButton_X = BIT(2),
    HidNpadButton_Y = BIT(3),
    HidNpadButton_L = BIT(6),
    HidNpadButton_R = BIT(7),
    HidNpadButton_ZL = BIT(8),
    HidNpadButton_ZR = BIT(9),
    HidNpadButton_Plus = BIT(10),
    HidNpadButton_Minus = BIT(11),
    HidNpadButton_Left = BIT(12),
    HidNpadButton_Up = BIT(13),
    HidNpadButton_Right = BIT(14),
    HidNpadButton_Down = BIT(15),
    HidNpadButton_StickLLeft = BIT(16),
    HidNpadButton_StickLUp = BIT(17),
    HidNpadButton_StickLRight = BIT(18),
    HidNpadButton_StickLDown = BIT(19),
    HidNpadButton_StickRLeft = BIT(20),
    HidNpadButton_StickRUp = BIT(21),
    HidNpadButton_StickRRight = BIT(22),
    HidNpadButton_StickRDown = BIT(23),
};
#endif

#define BUTTONS_UP (HidNpadButton_Up | HidNpadButton_StickLUp)
#define BUTTONS_DOWN (HidNpadButton_Down | HidNpadButton_StickLDown)
#define BUTTONS_LEFT (HidNpadButton_Left | HidNpadButton_StickLLeft)
#define BUTTONS_RIGHT (HidNpadButton_Right | HidNpadButton_StickLRight)
// The d-pad's left and right move a list by a page (the sticks go a row at a
// time: a stick pushed a little sideways jumped pages).
#define BUTTONS_PAGE_UP HidNpadButton_Left
#define BUTTONS_PAGE_DOWN HidNpadButton_Right
// Stick directions, which drift and are never "let go" exactly.
#define BUTTONS_STICKS (HidNpadButton_StickLLeft | HidNpadButton_StickLUp | HidNpadButton_StickLRight | \
                        HidNpadButton_StickLDown | HidNpadButton_StickRLeft | HidNpadButton_StickRUp | \
                        HidNpadButton_StickRRight | HidNpadButton_StickRDown)
#define BUTTONS_REPEATING (BUTTONS_UP | BUTTONS_DOWN | BUTTONS_LEFT | BUTTONS_RIGHT | HidNpadButton_StickRUp | \
                           HidNpadButton_StickRDown | HidNpadButton_StickRLeft | HidNpadButton_StickRRight)

// In a menu or panel the right stick moves like the d-pad, as in KaiPlayer and
// KaiRadio: there it has nothing else to do (paging long lists and the video
// under them keep it).
static inline unsigned long long buttonsRightStickAsDpad(unsigned long long pressed)
{
    static const unsigned long long MAP[][2] = {
        {HidNpadButton_StickRUp, HidNpadButton_Up},
        {HidNpadButton_StickRDown, HidNpadButton_Down},
        {HidNpadButton_StickRLeft, HidNpadButton_Left},
        {HidNpadButton_StickRRight, HidNpadButton_Right},
    };
    for (unsigned i = 0; i < sizeof(MAP) / sizeof(MAP[0]); i++)
        if (pressed & MAP[i][0])
            pressed = (pressed & ~MAP[i][0]) | MAP[i][1];
    return pressed;
}
