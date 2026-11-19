// Trickplay: small pictures of a video the server made every few seconds, on
// sheets (JPEGs) of tile_w x tile_h pictures each. Which picture shows a
// moment, and where it is on its sheet. Pure logic; tpfetch.c gets the sheets.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define TRICKPLAY_MAX_WIDTH 320  // The largest pictures asked for.

typedef struct {
    int width, height;    // A picture's size.
    int tile_w, tile_h;   // Pictures across and down a sheet.
    int count;            // Pictures in all.
    int64_t interval_ns;  // Time between pictures.
} TrickplayInfo;

typedef struct {
    int sheet;  // Which sheet.
    int x, y;   // The picture's top left on it, in pixels.
} TrickplaySpot;

// From an /Items answer holding the video with Fields=Trickplay: the
// pictures of media_source, the largest up to TRICKPLAY_MAX_WIDTH wide.
// False if there are none.
bool trickplayParse(const char *json, const char *media_source, TrickplayInfo *out);
// The picture for pos_ns (the last one past the end). False if there are none.
bool trickplayLocate(const TrickplayInfo *t, int64_t pos_ns, TrickplaySpot *out);
