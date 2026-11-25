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
// The made-up large picture of a video while the Showcase scenes draw, else NULL.
const uint32_t *fakeLarge(const char *path);
