// Anti-aliased text with the embedded Noto Sans fonts (Latin, Greek and
// Cyrillic). Glyphs are rendered once per size and cached.
#pragma once

#include <stdbool.h>

#include "gfx.h"

typedef enum {
    FONT_REGULAR,
    FONT_BOLD,
    FONT_SUBTITLE,  // Text subtitles: the bold one, or a font file chosen for them.
    FONT_FACE_COUNT,
} FontFace;

// Loads the embedded fonts, plus the console's fonts for Japanese, Chinese
// and Korean when available. False only if the embedded fonts fail.
bool fontInit(void);
void fontExit(void);
// Text subtitles in the font of a .ttf or .otf file (NULL: the built-in bold
// one). Characters it hasn't come from the built-in fonts. False if the file
// can't be read as a font: the built-in one stays.
bool fontSetSubtitleFile(const char *path);

// How many console fallback fonts loaded (0 if none: CJK shows as '?').
int fontFallbackCount(void);
// True if every character of the text is in the face or a fallback (none would show as '?').
bool fontCanDraw(FontFace face, const char *utf8);

int fontTextWidth(FontFace face, int px, const char *utf8);
// Draws text with its line box starting at y. Returns the width drawn.
int fontDraw(FontFace face, int px, int x, int y, const char *utf8, Color color);
// Draws text cut to max_width, ending in an ellipsis when it does not fit.
void fontDrawClipped(FontFace face, int px, int x, int y, int max_width, const char *utf8, Color color);

// Draws text from x with the middle of its capital letters at cy, so it sits
// centered in a pill or next to an icon. Returns the width drawn.
int fontDrawMiddle(FontFace face, int px, int x, float cy, const char *utf8, Color color);
// Draws text centered on (cx, cy) by its ink: for letters on buttons.
void fontDrawCentered(FontFace face, int px, float cx, float cy, const char *utf8, Color color);
