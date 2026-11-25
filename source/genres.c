#include "genres.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TABLE_FIRST 64

void genresJoin(const json_t *array, const char *key, char *out, size_t size)
{
    if (!size)
        return;
    out[0] = '\0';
    size_t i, used = 0;
    int kept = 0;
    const json_t *entry;
    json_array_foreach(array, i, entry) {
        const json_t *name = key ? json_object_get(entry, key) : entry;
        const char *text = json_string_value(name);
        if (!text || !text[0] || strchr(text, ',') || strpbrk(text, "\t\r\n"))
            continue;  // A comma would split it in two when read back.
        int n = snprintf(out + used, size - used, "%s%s", kept ? ", " : "", text);
        if (n < 0 || (size_t)n >= size - used) {
            out[used] = '\0';  // Doesn't fit whole: left out.
            break;
        }
        used += (size_t)n;
        if (++kept == GENRES_EACH_MAX)
            break;
    }
}

void genreTableFree(GenreTable *t)
{
    for (int i = 0; i < t->count; i++)
        free(t->entries[i].overview);
    free(t->entries);
    memset(t, 0, sizeof(*t));
}

bool genreTableAdd(GenreTable *t, const char *id, const char *genres, const char *overview)
{
    bool has_overview = overview && overview[0];
    if (!id[0] || (!genres[0] && !has_overview))
        return true;
    if (strlen(id) >= GENRES_ID_MAX)
        return false;
    if (t->count == t->capacity) {
        int grown = t->capacity ? t->capacity * 2 : TABLE_FIRST;
        GenreEntry *entries = realloc(t->entries, (size_t)grown * sizeof(GenreEntry));
        if (!entries)
            return false;
        t->entries = entries;
        t->capacity = grown;
    }
    GenreEntry *e = &t->entries[t->count];
    memset(e, 0, sizeof(*e));
    snprintf(e->id, sizeof(e->id), "%s", id);
    snprintf(e->genres, sizeof(e->genres), "%s", genres);
    e->overview = has_overview ? strdup(overview) : NULL;
    t->count++;
    t->sorted = false;
    return true;
}

static int compareEntries(const void *a, const void *b)
{
    return strcmp(((const GenreEntry *)a)->id, ((const GenreEntry *)b)->id);
}

int genreTableFind(GenreTable *t, const char *id)
{
    if (!id || !id[0] || t->count == 0)
        return -1;
    if (!t->sorted) {
        qsort(t->entries, (size_t)t->count, sizeof(GenreEntry), compareEntries);
        t->sorted = true;
    }
    int lo = 0, hi = t->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2, c = strcmp(t->entries[mid].id, id);
        if (c == 0)
            return mid;
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return -1;
}

const char *genreTableGenres(const GenreTable *t, int at)
{
    return at >= 0 && at < t->count && t->entries[at].genres[0] ? t->entries[at].genres : NULL;
}

const char *genreTableTakeOverview(GenreTable *t, int at)
{
    if (at < 0 || at >= t->count || t->entries[at].given)
        return NULL;
    t->entries[at].given = true;
    return t->entries[at].overview;
}

bool genresAt(const char *genres, int index, char *out, size_t size)
{
    const char *p = genres ? genres : "";
    for (int i = 0; *p; i++) {
        while (*p == ' ')
            p++;
        size_t len = strcspn(p, ",");
        size_t trimmed = len;
        while (trimmed > 0 && p[trimmed - 1] == ' ')
            trimmed--;
        if (i == index && trimmed > 0) {
            snprintf(out, size, "%.*s", (int)trimmed, p);
            return true;
        }
        if (i == index)
            return false;
        p += len;
        if (*p == ',')
            p++;
    }
    return false;
}
