// Films and episodes watched to the end (or marked by hand), kept in
// watched.txt: one path per line, the one watched last at the bottom. The
// order says which episode of a series was watched last, for "Up next".
// Pure logic over a file.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define WATCHED_MAX 5000
#define WATCHED_PATH_MAX 512

typedef struct {
    char (*paths)[WATCHED_PATH_MAX];  // The oldest first.
    size_t count;
} Watched;

void watchedInit(Watched *w);
void watchedFree(Watched *w);
// Reads file into w (empty if there's none).
void watchedLoad(Watched *w, const char *file);
bool watchedSave(const Watched *w, const char *file);

bool watchedHas(const Watched *w, const char *path);
// When path was watched, among the others: higher is later; -1 if it wasn't.
int watchedOrder(const Watched *w, const char *path);
// Marks path watched (now: it goes to the end) or not.
void watchedSet(Watched *w, const char *path, bool watched);
