// What a video file is, from its name and folders: a film ("Paper Moon",
// 1998) or an episode of a series ("Prairie Road", season 9, episode 6,
// "The Long Fence"). Release tags (1080p, x265, WEB-DL, [group]...) are
// dropped. Pure logic over strings.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define NAMING_MAX 128

typedef struct {
    bool is_episode;
    char title[NAMING_MAX];  // The film's title, or the episode's ("" if the name has none).
    char show[NAMING_MAX];   // The series, for an episode.
    int year;                // 0 if unknown.
    int season, episode;     // For an episode; season 1 when the name has only a number.
} VideoName;

// path is the file's full path; root, the library folder it was found in
// (folders above it don't count). A film in a "Title (Year)" folder, or an
// episode in a folder named after its series (or that folder's "Season 2"),
// takes the folder's name, which keeps its punctuation.
void namingParse(const char *path, const char *root, VideoName *out);

// A name to compare and search by: lowercase letters and digits only, with
// accents dropped ("Pokémon: The Movie" -> "pokemonthemovie"); letters of
// other scripts (Japanese...) are kept as they are.
void namingKey(const char *name, char *out, size_t size);
