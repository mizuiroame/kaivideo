// The Showcase's tiles (ui_showcase.c): a flat DVD case with the poster on
// its cover, or a wide picture with the name under it; and the focus frame
// both wear, cut at two corners as Mizuiro's seal.
#pragma once

#include <stdbool.h>

#include "library.h"
#include "resume.h"

#define TILE_CASE_W 116   // An upright case, about a DVD's shape.
#define TILE_CASE_H 174
#define TILE_WIDE_W 246   // A wide picture, and its two lines under it.
#define TILE_WIDE_H 138
#define TILE_WIDE_TEXT_H 40

// Tiles are drawn between top and bottom only (and the clip is left there): call before drawing them.
void uiTilesSetRegion(int top, int bottom);
// The art of a server's or addon's poster for ref (its series' for an episode), or NULL (a file on
// the card or the network folder without a folder picture: its frame stands in).
const char *uiTilePosterArt(const Library *lib, LibraryRef ref);
// The name a tile shows: a film's title, a series' name (an episode's too).
const char *uiTileName(const Library *lib, LibraryRef ref);

// A case at (x, y): its poster, or a frame of the video under its name. progress: how far it was
// watched (0-1), -1 if not started. seen: a watched mark.
void uiDrawCaseTile(const Library *lib, LibraryRef ref, int x, int y, bool focused, double progress, bool seen);
// A wide tile at (x, y), its name and a line under it; tag: "UP NEXT" or such over the picture, or NULL.
void uiDrawWideTile(const Library *lib, LibraryRef ref, int x, int y, bool focused, double progress, bool seen,
                    const char *tag);
// The focus frame around a w x h tile at (x, y).
void uiDrawTileFocus(int x, int y, int w, int h);
