// Home's Showcase scenes of the preview (showcase_scenes.c).
#pragma once

#include <stdint.h>

#include "library.h"
#include "resume.h"
#include "watched.h"

typedef struct {
    const Library *sd;  // The sample library, as found on the card.
    const ResumeList *resume;
    const Watched *watched;
    void (*begin)(void);
    void (*save)(const char *name);
    int *problems;
} ShowcaseCtx;

void showcaseScenes(const ShowcaseCtx *c);
// Its helpers, for the IPTV list's scenes (iptv_scenes.c): frames drawn then saved, the taps checked.
#include "home.h"
#include "libview.h"
void showcaseDraw(const ShowcaseCtx *c, const char *name, const LibView *v, const Library *lib, const Home *home,
                  int frames);
void showcaseCheckTaps(const ShowcaseCtx *c, const char *name, const LibView *v, const Home *home);
void showcaseProblem(const ShowcaseCtx *c, const char *name, const char *what);
void showcaseMoveTo(LibView *v, const Home *home, int row, int col);
// The IPTV list's Showcase: an M3U list's channels, an Xtream account's with films and series, and X
// going between it and the other sources (sd, server: their libraries and Homes as last drawn).
void iptvShowcaseScenes(const ShowcaseCtx *c, const Library *sd, const Library *server, const LibView *sd_view,
                        const Home *sd_home);
// A made-up channel logo for an address of https://logo.invalid/, else NULL.
const uint32_t *fakeLogo(const char *path);
// The made-up large picture of a video while the Showcase scenes draw, else NULL.
const uint32_t *fakeLarge(const char *path);
// Home's Showcase for the card with .nfo files and pictures beside its videos (nfo_scenes.c), and a
// server's tiles wearing their downloads' marks.
void nfoShowcaseScenes(const ShowcaseCtx *c);
void downloadShowcaseScenes(const ShowcaseCtx *c, const Library *server);
