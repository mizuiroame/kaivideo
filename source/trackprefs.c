#include "trackprefs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SERIES_PREFIX "series:"

void trackPrefsInit(TrackPrefs *t)
{
    t->items = NULL;
    t->count = 0;
}

void trackPrefsFree(TrackPrefs *t)
{
    free(t->items);
    trackPrefsInit(t);
}

static int indexOf(const TrackPrefs *t, const char *key)
{
    for (size_t i = 0; i < t->count; i++)
        if (strcmp(t->items[i].key, key) == 0)
            return (int)i;
    return -1;
}

static bool usable(const char *text, size_t max)
{
    return strlen(text) < max && !strpbrk(text, "\t\r\n");
}

// key's entry, moved to the top (made if there's none). NULL if it can't be kept.
static TrackPref *top(TrackPrefs *t, const char *key)
{
    if (!key[0] || !usable(key, TRACKPREFS_KEY_MAX))
        return NULL;
    if (!t->items && !(t->items = calloc(TRACKPREFS_MAX, sizeof(TrackPref))))
        return NULL;
    TrackPref entry;
    memset(&entry, 0, sizeof(entry));
    int at = indexOf(t, key);
    if (at >= 0) {
        entry = t->items[at];
        memmove(&t->items[at], &t->items[at + 1], (t->count - (size_t)at - 1) * sizeof(TrackPref));
        t->count--;
    } else {
        snprintf(entry.key, sizeof(entry.key), "%s", key);
    }
    if (t->count == TRACKPREFS_MAX)
        t->count--;  // The oldest goes.
    memmove(&t->items[1], &t->items[0], t->count * sizeof(TrackPref));
    t->items[0] = entry;
    t->count++;
    return &t->items[0];
}

static void set(TrackPref *p, const char *sound, const char *subtitles)
{
    if (sound && usable(sound, TRACKPREFS_NAME_MAX))
        snprintf(p->sound, sizeof(p->sound), "%s", sound);
    if (subtitles && usable(subtitles, TRACKPREFS_NAME_MAX))
        snprintf(p->subtitles, sizeof(p->subtitles), "%s", subtitles);
}

static void seriesKey(const char *series, char *out, size_t size)
{
    snprintf(out, size, "%s%s", series && series[0] ? SERIES_PREFIX : "", series && series[0] ? series : "");
}

void trackPrefsRemember(TrackPrefs *t, const char *path, const char *series, const char *sound,
                        const char *subtitles)
{
    char key[TRACKPREFS_KEY_MAX];
    seriesKey(series, key, sizeof(key));
    TrackPref *p = key[0] ? top(t, key) : NULL;
    if (p)
        set(p, sound, subtitles);
    p = top(t, path);  // The video's on top: its own choice wins.
    if (p)
        set(p, sound, subtitles);
}

bool trackPrefsFind(const TrackPrefs *t, const char *path, const char *series, const char **sound,
                    const char **subtitles)
{
    *sound = *subtitles = "";
    char key[TRACKPREFS_KEY_MAX];
    seriesKey(series, key, sizeof(key));
    int own = indexOf(t, path), shared = key[0] ? indexOf(t, key) : -1;
    if (own < 0 && shared < 0)
        return false;
    // Each of the two from the video's own choice, else the series'.
    const TrackPref *a = own >= 0 ? &t->items[own] : NULL, *b = shared >= 0 ? &t->items[shared] : NULL;
    *sound = a && a->sound[0] ? a->sound : b ? b->sound : "";
    *subtitles = a && a->subtitles[0] ? a->subtitles : b ? b->subtitles : "";
    return true;
}

void trackPrefsLoad(TrackPrefs *t, const char *file)
{
    t->count = 0;
    FILE *f = fopen(file, "r");
    if (!f)
        return;
    char line[TRACKPREFS_KEY_MAX + 2 * TRACKPREFS_NAME_MAX + 4];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *tab1 = strchr(line, '\t'), *tab2 = tab1 ? strchr(tab1 + 1, '\t') : NULL;
        if (!tab2)
            continue;
        *tab1 = *tab2 = '\0';
        // The file has the latest first: each read one goes under the others.
        size_t before = t->count;
        TrackPref *p = top(t, line);
        if (!p)
            continue;
        set(p, tab1 + 1, tab2 + 1);
        if (t->count > before) {
            TrackPref read = t->items[0];
            memmove(&t->items[0], &t->items[1], (t->count - 1) * sizeof(TrackPref));
            t->items[t->count - 1] = read;
        }
    }
    fclose(f);
}

bool trackPrefsSave(const TrackPrefs *t, const char *file)
{
    FILE *f = fopen(file, "w");
    if (!f)
        return false;
    for (size_t i = 0; i < t->count; i++)
        fprintf(f, "%s\t%s\t%s\n", t->items[i].key, t->items[i].sound, t->items[i].subtitles);
    return fclose(f) == 0;
}
