// The pictures on the library's cards, as the UI asks for them: read from
// <config>/thumbs when they were made before, or asked of thumbgen.h. A few
// dozen stay in memory; the least recently drawn go first.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "thumbgen.h"

// folder: where the JPEGs are kept (made if missing).
void thumbsInit(const char *folder);
void thumbsExit(void);

// The picture of the video at path (of size bytes), THUMB_W x THUMB_H
// pixels (0xAABBGGRR), or NULL while there's none (yet). Asks for it to be
// made, and reads at most a couple from the card per frame.
const uint32_t *thumbGet(const char *path, int64_t size);
// Once a frame: takes the pictures made meanwhile.
void thumbsUpdate(void);
// While a video plays, nothing is made (the decoder is the player's).
void thumbsPause(bool paused);
