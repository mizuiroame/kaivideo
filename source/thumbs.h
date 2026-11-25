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
// The same, THUMB_LARGE_W x THUMB_LARGE_H, for showing it big. A few are kept at a time.
const uint32_t *thumbGetLarge(const char *path, int64_t size);
// A server's or addon's poster (art: an item's art or series_art, or a server video's location),
// THUMB_POSTER_W x THUMB_POSTER_H, upright; NULL while there's none (yet). Not for a file's frames.
const uint32_t *thumbGetPoster(const char *art);
// True if that art has no poster.
bool thumbPosterIsNone(const char *art);
// True if the video has no picture: it can't be decoded (or reading it failed).
bool thumbIsNone(const char *path, int64_t size);
// Videos that got no picture are looked at again: the server may answer now.
void thumbsRetryMissing(void);
// Once a frame: takes the pictures made meanwhile.
void thumbsUpdate(void);
// While a video plays, nothing is made (the decoder is the player's).
void thumbsPause(bool paused);
