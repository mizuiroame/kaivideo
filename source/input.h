// Turns held buttons into repeated presses, for scrolling lists and seeking.
#pragma once

#include <stdint.h>

#define INPUT_MAX_BUTTONS 64

typedef struct {
    uint64_t repeat_mask;  // Buttons that auto-repeat.
    uint64_t next_fire_ns[INPUT_MAX_BUTTONS];
} InputRepeat;

void inputRepeatInit(InputRepeat *r, uint64_t repeat_mask);

// Returns the buttons that count as pressed this frame: those just pressed,
// plus repeating ones held past the initial delay.
uint64_t inputRepeatUpdate(InputRepeat *r, uint64_t down, uint64_t held, uint64_t now_ns);
