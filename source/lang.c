#include "lang.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NAME_KEY "language"

typedef struct {
    const char *english, *text;
} Pair;

typedef struct Language {
    struct Language *before;  // The one loaded before it, kept.
    char name[LANG_NAME_MAX];
    char *buffer;  // The file's text, cut into lines in place.
    Pair *slots;   // Open addressing, by the English's hash.
    size_t slot_count;
} Language;

static const Language *volatile g_language;  // NULL: English.
static Language *g_loaded;                    // Every one loaded, the last first.

static uint32_t hashOf(const char *s)
{
    uint32_t h = 2166136261u;  // FNV-1a.
    for (; *s; s++)
        h = (h ^ (unsigned char)*s) * 16777619u;
    return h;
}

const char *langText(const char *english)
{
    const Language *l = g_language;
    if (!l || !english || !english[0])
        return english;
    for (size_t i = hashOf(english) & (l->slot_count - 1);; i = (i + 1) & (l->slot_count - 1)) {
        if (!l->slots[i].english)
            return english;
        if (!strcmp(l->slots[i].english, english))
            return l->slots[i].text;
    }
}

const char *langName(void)
{
    const Language *l = g_language;
    return l ? l->name : "English";
}

void langUseEnglish(void)
{
    g_language = NULL;  // The one before stays: a reader may still be in it.
}

// The next placeholder from *s on ("%d", "%-5.2lf"), or false. "%%" isn't one.
static bool nextPlaceholder(const char **s, char *out, size_t size)
{
    for (const char *p = *s; (p = strchr(p, '%')) != NULL;) {
        if (p[1] == '%') {
            p += 2;
            continue;
        }
        size_t n = strspn(p + 1, "-+ #0123456789.*hlLqjzt");
        size_t len = n + 2 < size ? n + 2 : size - 1;  // The '%', its flags, and its letter.
        memcpy(out, p, len);
        out[len] = '\0';
        *s = p + n + 2 <= p + strlen(p) ? p + n + 2 : p + strlen(p);
        return p[n + 1] != '\0';
    }
    return false;
}

void langFreeAll(void)
{
    g_language = NULL;
    while (g_loaded) {
        Language *l = g_loaded;
        g_loaded = l->before;
        free(l->slots);
        free(l->buffer);
        free(l);
    }
}

bool langSamePlaceholders(const char *a, const char *b)
{
    char pa[32], pb[32];
    for (;;) {
        bool more_a = nextPlaceholder(&a, pa, sizeof(pa)), more_b = nextPlaceholder(&b, pb, sizeof(pb));
        if (more_a != more_b)
            return false;
        if (!more_a)
            return true;
        if (strcmp(pa, pb) != 0 || strchr(pa, 'n'))  // %n writes: never from a translation.
            return false;
    }
}

// "\n" in a line becomes a line break, in place.
static void unescape(char *s)
{
    char *to = s;
    for (const char *from = s; *from; from++) {
        if (from[0] == '\\' && from[1] == 'n') {
            *to++ = '\n';
            from++;
        } else {
            *to++ = *from;
        }
    }
    *to = '\0';
}

// The file's lines, in place: line breaks (and a '\r' before them) become ends.
static size_t splitLines(char *buffer, char **lines, size_t max)
{
    size_t n = 0;
    for (char *p = buffer; *p && n < max;) {
        char *end = strchr(p, '\n');
        if (end)
            *end = '\0';
        size_t len = strlen(p);
        if (len && p[len - 1] == '\r')
            p[len - 1] = '\0';
        lines[n++] = p;
        if (!end)
            break;
        p = end + 1;
    }
    return n;
}

static void put(Language *l, const char *english, const char *text)
{
    for (size_t i = hashOf(english) & (l->slot_count - 1);; i = (i + 1) & (l->slot_count - 1))
        if (!l->slots[i].english || !strcmp(l->slots[i].english, english)) {
            l->slots[i] = (Pair){english, text};
            return;
        }
}

// The "language = name" line among the first ones. False if there's none.
static bool readName(char **lines, size_t count, char *name, size_t size)
{
    for (size_t i = 0; i < count; i++) {
        const char *line = lines[i];
        if (line[0] == '#' || !line[0])
            continue;
        const char *eq = strchr(line, '=');
        size_t key = strspn(line, "abcdefghijklmnopqrstuvwxyz");
        if (!eq || key != strlen(NAME_KEY) || strncmp(line, NAME_KEY, key) != 0)
            return false;
        snprintf(name, size, "%s", eq + 1 + strspn(eq + 1, " "));
        return name[0] != '\0';
    }
    return false;
}

bool langLoadText(const char *text, size_t size)
{
    Language *l = calloc(1, sizeof(Language));
    char *buffer = l ? malloc(size + 1) : NULL;
    size_t max_lines = size / 2 + 2;
    char **lines = buffer ? malloc(max_lines * sizeof(char *)) : NULL;
    if (!lines) {
        free(buffer);
        free(l);
        return false;
    }
    memcpy(buffer, text, size);
    buffer[size] = '\0';
    size_t count = splitLines(buffer, lines, max_lines);
    l->slot_count = 64;
    while (l->slot_count < count * 2)
        l->slot_count *= 2;
    l->slots = calloc(l->slot_count, sizeof(Pair));
    if (!l->slots || !readName(lines, count, l->name, sizeof(l->name))) {
        free(l->slots);
        free(lines);
        free(buffer);
        free(l);
        return false;
    }
    // Past the name: pairs, an English line then its translation, blank lines between.
    size_t i = 0;
    while (i < count && (lines[i][0] == '#' || !lines[i][0] || strchr(lines[i], '=') == NULL))
        i++;
    for (i++; i + 1 < count; i++) {
        if (!lines[i][0] || lines[i][0] == '#')
            continue;
        if (!lines[i + 1][0] || lines[i + 1][0] == '#')
            continue;  // Its translation is missing: the English stays.
        unescape(lines[i]);
        unescape(lines[i + 1]);
        if (langSamePlaceholders(lines[i], lines[i + 1]))
            put(l, lines[i], lines[i + 1]);
        i++;
    }
    l->buffer = buffer;
    free(lines);
    l->before = g_loaded;
    g_loaded = l;
    g_language = l;  // The one before stays: a reader may still be in it.
    return true;
}
