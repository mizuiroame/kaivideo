#include "watched.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void watchedInit(Watched *w)
{
    w->paths = NULL;
    w->count = 0;
}

void watchedFree(Watched *w)
{
    free(w->paths);
    watchedInit(w);
}

static bool ensure(Watched *w)
{
    if (!w->paths)
        w->paths = malloc(WATCHED_MAX * sizeof(*w->paths));
    return w->paths != NULL;
}

int watchedOrder(const Watched *w, const char *path)
{
    for (size_t i = w->count; i-- > 0;)  // The latest are the likeliest asked about.
        if (strcmp(w->paths[i], path) == 0)
            return (int)i;
    return -1;
}

bool watchedHas(const Watched *w, const char *path)
{
    return watchedOrder(w, path) >= 0;
}

void watchedSet(Watched *w, const char *path, bool watched)
{
    int at = watchedOrder(w, path);
    if (at >= 0) {  // Out of its place: back at the end, or gone.
        memmove(&w->paths[at], &w->paths[at + 1], (w->count - (size_t)at - 1) * sizeof(*w->paths));
        w->count--;
    }
    if (!watched || strlen(path) >= WATCHED_PATH_MAX || strpbrk(path, "\r\n") || !ensure(w))
        return;
    if (w->count == WATCHED_MAX) {  // The oldest goes.
        memmove(&w->paths[0], &w->paths[1], (WATCHED_MAX - 1) * sizeof(*w->paths));
        w->count--;
    }
    snprintf(w->paths[w->count++], WATCHED_PATH_MAX, "%s", path);
}

void watchedLoad(Watched *w, const char *file)
{
    w->count = 0;
    FILE *f = fopen(file, "r");
    if (!f)
        return;
    char line[WATCHED_PATH_MAX + 2];
    while (fgets(line, sizeof(line), f)) {
        if (!strchr(line, '\n') && !feof(f)) {  // Too long: skip the whole line.
            int ch;
            while ((ch = fgetc(f)) != EOF && ch != '\n') {
            }
            continue;
        }
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0])
            watchedSet(w, line, true);
    }
    fclose(f);
}

bool watchedSave(const Watched *w, const char *file)
{
    FILE *f = fopen(file, "w");
    if (!f)
        return false;
    for (size_t i = 0; i < w->count; i++)
        fprintf(f, "%s\n", w->paths[i]);
    return fclose(f) == 0;
}
