// What the Showcase's files share (ui_showcase.c draws the large picture and
// the rows; ui_showcase_live.c a channel's text, and the blur of a logo's or
// poster's colors with the picture shown whole on it).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "artcolors.h"
#include "gfx.h"
#include "ui_library.h"
#include "ui_showcase.h"

// The large picture: 16:9 at the top right, behind the header, fading into the background.
#define HERO_X 440
#define HERO_W (GFX_WIDTH - HERO_X)
#define HERO_H (HERO_W * 9 / 16)
// The text beside it.
#define TEXT_W 560
#define SUMMARY_W 520
#define KICKER_Y 84
#define TITLE_Y 104
#define TITLE_PX 40
#define TITLE_LINE 46
#define INFO_PX 18
#define SUMMARY_PX 18
#define SUMMARY_LINE 26
#define SUMMARY_LINES 3
#define SUMMARY_COLOR GFX_RGB(0xD4, 0xD6, 0xDE)
#define BUTTON_H 40
#define BUTTON_Y 296          // At the lowest: higher when the text above is short.
#define BUTTON_GAP 22
#define ROWS_Y 356            // The rows under it.

// A picture shown whole on a blur of its colors (a channel's logo card, a poster): where it goes.
#define PLATE_CX 946          // Its middle across,
#define PLATE_WIDE_W 368      // a logo card's size (16:9),
#define PLATE_WIDE_Y 96
#define PLATE_POSTER_W 168    // a poster's (2:3).
#define PLATE_POSTER_Y 78

float uiShowcaseSmooth(float t);
// "a  ·  b": part after what's in out, if any.
void uiShowcaseAddPart(char *out, size_t size, const char *part);
// The kicker over the title (the brand's short line before it), and the title (one or two lines):
// returns the y under the title.
int uiShowcaseDrawTitle(const char *kicker, const char *title);
// A pill: button (as "A") and label, filled (primary) or quiet. Returns its width; hit: what a tap does.
int uiShowcaseDrawPill(int x, int y, const char *button, const char *label, bool primary, LibHitKind hit);
// How many lines of SUMMARY_W text takes, up to max.
int uiShowcaseSummaryLines(const char *text, int max);

// The blur: the grid's colors smoothly over the large picture's place.
void uiShowcaseDrawBlur(const ArtColors *colors);
// The picture shown whole: w x h pixels (a logo card THUMB_W x THUMB_H, or a poster), or the name on its
// colors when pixels is NULL; upright: a poster's place. alpha: how far it has faded in (0-255).
void uiShowcaseDrawPlate(const uint32_t *pixels, int w, int h, bool upright, const char *name, unsigned alpha);

// A channel's text beside the large picture: its name, group, what's on now (with how far into it)
// and next, and its buttons.
void uiShowcaseDrawChannelText(const UiShowcaseChannels *ch, int channel);
