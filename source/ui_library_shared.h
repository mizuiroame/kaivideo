// Drawing pieces the library's screens share between ui_library.c and
// ui_details.c: pictures of films and series, and how far they were watched.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "font.h"
#include "gfx.h"
#include "library.h"
#include "resume.h"

// Cards show pictures from the videos (uiLibrarySetThumbnails).
bool uiLibThumbnailsOn(void);
// Watched to the end, or marked so.
bool uiLibSeen(const Library *lib, int item);
// How far it was watched (0-1), or -1 if it wasn't started.
double uiLibWatched(const Library *lib, const ResumeList *resume, int item);
// The card's picture of a film, episode or series, or NULL if there's none yet.
const uint32_t *uiLibPicture(const Library *lib, LibraryRef ref);
// An image of image_w x image_h drawn into a layout box with rounded corners.
void uiLibDrawImage(const uint32_t *pixels, int image_w, int image_h, int x, int y, int w, int h, int radius);
// A card's picture (thumbs.h's size) the same way.
void uiLibDrawPicture(const uint32_t *pixels, int x, int y, int w, int h, int radius);
// The two colors of a card without a picture, from its name.
void uiLibCardColors(const char *name, Color *top, Color *bottom);
// Text wrapped at spaces downward from top, at most max_lines lines.
void uiLibDrawWrapped(FontFace face, int px, int x, int top, int w, int line_h, int max_lines, const char *text,
                      Color color);
// "S01E03 · Lamentia", or "Season 1, episode 3" without a title.
void uiLibEpisodeLabel(const LibraryItem *item, char *out, size_t size);
