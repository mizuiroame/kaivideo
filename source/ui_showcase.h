// Home's Showcase style: a large picture of the film or series under the
// cursor (fading into the background, with its title, year, length or
// seasons, genres and summary) over rows of tiles that scroll sideways:
// upright posters drawn as flat DVD cases, or wide pictures (HomeRow.wide).
// The IPTV list's Home is one too: a channel's logo large on a blur of its
// colors, with what's on now and next, over rows of channels, then an
// Xtream account's films and series (a poster without a wide picture shows
// whole, on a blur of its colors).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "home.h"
#include "iptv.h"
#include "library.h"
#include "libview.h"
#include "resume.h"

// A program of a channel's guide, for the large picture and the tiles.
typedef struct {
    const char *title;  // NULL or "" for none known.
    const char *desc;   // Its description, or NULL.
    int64_t start, end; // Seconds since 1970.
} UiProgram;

// The IPTV list whose channels Home's rows of channels are (HomeRow.channels), and its guide.
typedef struct {
    const IptvList *list;
    // What's on channel now and next (titles NULL for none).
    void (*programs)(const void *ctx, int channel, UiProgram *now, UiProgram *next);
    bool (*favorite)(const void *ctx, int channel);
    const void *ctx;
    int64_t now_s, utc_offset_s;
    bool clock_24h;
} UiShowcaseChannels;

// The channels for the frames drawn from now on (copied), or NULL outside the IPTV view.
void uiShowcaseSetChannels(const UiShowcaseChannels *channels);
// Whether the library's screen shows Home as the Showcase now.
bool uiShowcaseShown(const LibView *v, const Library *lib, const Home *home, bool folder_set);
// The large picture, behind the header: drawn before it.
void uiShowcaseDrawBackdrop(const LibView *v, const Library *lib, const Home *home);
// The title, its details and the rows, under the header. A channel's: its favorite button
// (LIBHIT_FAVORITE) beside the one that plays it (LIBHIT_HERO).
void uiShowcaseDraw(const LibView *v, const Library *lib, const Home *home, const ResumeList *resume);
