// ASS and SSA subtitles with their styles (fonts, colors, positions, effects),
// drawn by libass into an image to put over the video. Events may be added from
// the decoding thread while the window's thread renders.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ASSSUBS_DEFAULT_FAMILY "Noto Sans"  // Used when a font a line asks for isn't there.

typedef struct AssSubs AssSubs;

typedef struct {
    const char *name;
    const void *data;
    size_t size;
} AssFont;

typedef struct {
    int x, y, w, h;          // Where it goes in the rendered area; w is 0 when nothing shows.
    const uint32_t *pixels;  // w x h, 0xAABBGGRR. Valid until the next render.
    unsigned id;             // Changes when the pixels do.
} AssPicture;

// What a picture is rendered for.
typedef struct {
    int width, height;    // The area, in pixels.
    double font_scale;    // Font sizes are multiplied by this.
    double lift_percent;  // Lines not placed by the file go up this much of the height.
} AssArea;

// For a video_w x video_h video, with these fonts (they're copied). NULL if it can't start.
AssSubs *assSubsCreate(int video_w, int video_h, const AssFont *fonts, int font_count);
void assSubsClose(AssSubs *a);

// A track from its header (the [Script Info] and styles): its number, or -1.
int assSubsAddTrack(AssSubs *a, const char *header, size_t size);
// One line as the file holds it ("ReadOrder,Layer,Style,..."). The same line again is ignored.
void assSubsAddEvent(AssSubs *a, int track, const char *data, size_t size, int64_t start_ms, int64_t duration_ms);

// The track at time_ms. False only if the track doesn't exist.
bool assSubsRender(AssSubs *a, int track, int64_t time_ms, const AssArea *area, AssPicture *out);
