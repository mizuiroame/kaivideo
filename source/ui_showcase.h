// Home's Showcase style: a large picture of the film or series under the
// cursor (fading into the background, with its title, year, length or
// seasons, genres and summary) over rows of tiles that scroll sideways:
// upright posters drawn as flat DVD cases, or wide pictures (HomeRow.wide).
#pragma once

#include <stdbool.h>

#include "home.h"
#include "library.h"
#include "libview.h"
#include "resume.h"

// Whether the library's screen shows Home as the Showcase now.
bool uiShowcaseShown(const LibView *v, const Library *lib, const Home *home, bool folder_set);
// The large picture, behind the header: drawn before it.
void uiShowcaseDrawBackdrop(const LibView *v, const Library *lib, const Home *home);
// The title, its details and the rows, under the header.
void uiShowcaseDraw(const LibView *v, const Library *lib, const Home *home, const ResumeList *resume);
