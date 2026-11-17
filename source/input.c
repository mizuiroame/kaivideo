#include "input.h"

#include <string.h>

#define REPEAT_DELAY_NS 350000000ull
#define REPEAT_INTERVAL_NS 70000000ull

void inputRepeatInit(InputRepeat *r, uint64_t repeat_mask)
{
    memset(r, 0, sizeof(*r));
    r->repeat_mask = repeat_mask;
}

uint64_t inputRepeatUpdate(InputRepeat *r, uint64_t down, uint64_t held, uint64_t now_ns)
{
    uint64_t pressed = down;
    for (int i = 0; i < INPUT_MAX_BUTTONS; i++) {
        uint64_t bit = 1ull << i;
        if (!(r->repeat_mask & bit))
            continue;
        if (down & bit) {
            r->next_fire_ns[i] = now_ns + REPEAT_DELAY_NS;
        } else if ((held & bit) && now_ns >= r->next_fire_ns[i]) {
            pressed |= bit;
            r->next_fire_ns[i] = now_ns + REPEAT_INTERVAL_NS;
        }
    }
    return pressed;
}
