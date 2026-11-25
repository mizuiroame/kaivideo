#include "nfo.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "xmlread.h"

#define FIELD_MAX NFO_PLOT_MAX  // A field's text while read (the plot's is the longest kept).
#define GENRE_FIELDS 8          // <genre> tags looked at; more are left out.
#define YEAR_MIN 1870
#define YEAR_MAX 2200
#define RUNTIME_MAX 1000        // Minutes: more is a mistake (or seconds).
#define GENRE_NAME_MAX 64       // One genre's name, as Home keeps it (HOME_GENRE_NAME_MAX).

typedef enum {
    F_NONE, F_TITLE, F_YEAR, F_PREMIERED, F_AIRED, F_SEASON, F_EPISODE, F_RUNTIME, F_GENRE, F_PLOT, F_OUTLINE,
} Field;

static Field fieldOf(const char *tag)
{
    static const struct { const char *tag; Field field; } FIELDS[] = {
        {"title", F_TITLE},     {"year", F_YEAR},       {"premiered", F_PREMIERED}, {"aired", F_AIRED},
        {"season", F_SEASON},   {"episode", F_EPISODE}, {"runtime", F_RUNTIME},     {"genre", F_GENRE},
        {"plot", F_PLOT},       {"outline", F_OUTLINE},
    };
    for (size_t i = 0; i < sizeof(FIELDS) / sizeof(FIELDS[0]); i++)
        if (!strcasecmp(tag, FIELDS[i].tag))
            return FIELDS[i].field;
    return F_NONE;
}

static NfoKind kindOf(const char *tag)
{
    if (!strcasecmp(tag, "movie"))
        return NFO_MOVIE;
    if (!strcasecmp(tag, "tvshow"))
        return NFO_TVSHOW;
    if (!strcasecmp(tag, "episodedetails"))
        return NFO_EPISODE;
    return NFO_NONE;
}

// The length of the valid UTF-8 character at s (n bytes left), or 0 if it isn't one.
static size_t utf8Length(const unsigned char *s, size_t n)
{
    size_t want = s[0] < 0x80 ? 1 : (s[0] & 0xE0) == 0xC0 ? 2 : (s[0] & 0xF0) == 0xE0 ? 3
                                  : (s[0] & 0xF8) == 0xF0 ? 4 : 0;
    if (!want || want > n || (want == 2 && s[0] < 0xC2))
        return 0;
    for (size_t i = 1; i < want; i++)
        if ((s[i] & 0xC0) != 0x80)
            return 0;
    return want;
}

// Into out (size bytes): text on one line, its runs of white space (and control characters) as one
// space, trimmed; a byte that isn't UTF-8 (a Latin-1 file) as '?'. Characters are never cut.
static void clean(const char *text, char *out, size_t size)
{
    size_t n = 0, left = strlen(text);
    const unsigned char *s = (const unsigned char *)text;
    bool space = false;
    while (left && size) {
        size_t len = utf8Length(s, left);
        if (len == 1 && (*s < 0x20 || *s == 0x7F || *s == ' ')) {
            space = n > 0;
            s++, left--;
            continue;
        }
        size_t put = len ? len : 1;
        if (n + (space ? 1 : 0) + put >= size)
            break;
        if (space)
            out[n++] = ' ';
        space = false;
        if (len)
            memcpy(out + n, s, len);
        else
            out[n] = '?';
        n += put;
        s += put;
        left -= put;
    }
    if (size)
        out[n] = '\0';
}

static int number(const char *text, int min, int max)
{
    while (*text == ' ')
        text++;
    char *end;
    long v = strtol(text, &end, 10);
    return end != text && v >= min && v <= max ? (int)v : -1;
}

// "2019-03-08" or "2019": its year, 0 if none.
static int yearIn(const char *text)
{
    int y = number(text, YEAR_MIN, YEAR_MAX);
    return y > 0 ? y : 0;
}

// Adds one <genre>'s names ("Drama", or "Drama / Comedy", "Drama, Comedy") to the joined list, each once.
static void addGenres(char *genres, size_t size, int *kept, const char *text)
{
    const char *p = text;
    while (*p && *kept < GENRES_EACH_MAX) {
        size_t len = strcspn(p, ",/|");
        const char *start = p;
        p += len;
        if (*p)
            p++;
        while (len && *start == ' ')
            start++, len--;
        while (len && start[len - 1] == ' ')
            len--;
        if (!len || len >= GENRE_NAME_MAX)
            continue;
        char one[GENRE_NAME_MAX];
        memcpy(one, start, len);
        one[len] = '\0';
        char existing[GENRE_NAME_MAX];
        bool dup = false;
        for (int i = 0; !dup && genresAt(genres, i, existing, sizeof(existing)); i++)
            dup = !strcasecmp(existing, one);
        size_t used = strlen(genres);
        if (dup || used + len + (used ? 2 : 0) >= size)
            continue;
        snprintf(genres + used, size - used, "%s%s", used ? ", " : "", one);
        (*kept)++;
    }
}

typedef struct {
    NfoInfo *out;
    char outline[NFO_PLOT_MAX];
    int genre_fields, genres_kept;
    int premiered_year;
} Reading;

static void take(Reading *r, Field field, const char *raw)
{
    char text[FIELD_MAX];
    clean(raw, text, sizeof(text));
    NfoInfo *out = r->out;
    switch (field) {
    case F_TITLE:
        if (!out->title[0])
            clean(text, out->title, sizeof(out->title));
        break;
    case F_YEAR:
        if (!out->year)
            out->year = yearIn(text);
        break;
    case F_PREMIERED:
    case F_AIRED:
        if (!r->premiered_year)
            r->premiered_year = yearIn(text);
        break;
    case F_SEASON:
        if (out->season < 0)
            out->season = number(text, 0, 9999);
        break;
    case F_EPISODE:
        if (out->episode < 0)
            out->episode = number(text, 0, 99999);
        break;
    case F_RUNTIME:
        if (!out->runtime_min) {
            int m = number(text, 1, RUNTIME_MAX);
            out->runtime_min = m > 0 ? m : 0;
        }
        break;
    case F_GENRE:
        if (r->genre_fields++ < GENRE_FIELDS)
            addGenres(out->genres, sizeof(out->genres), &r->genres_kept, text);
        break;
    case F_PLOT:
        if (!out->plot[0])
            memcpy(out->plot, text, strlen(text) + 1);
        break;
    case F_OUTLINE:
        if (!r->outline[0])
            memcpy(r->outline, text, strlen(text) + 1);
        break;
    case F_NONE:
        break;
    }
}

bool nfoParse(const char *text, size_t length, NfoInfo *out)
{
    memset(out, 0, sizeof(*out));
    out->season = out->episode = -1;
    if (!text)
        return false;
    Reading *r = calloc(1, sizeof(Reading));
    char *value = malloc(FIELD_MAX);
    if (!r || !value) {
        free(r);
        free(value);
        return false;
    }
    r->out = out;
    XmlReader x;
    xmlBegin(&x, text, length);
    int depth = 0;           // 1: in the root, 2: in one of its fields.
    Field field = F_NONE;    // The root's field being read.
    size_t value_length = 0;
    bool done = false;
    for (XmlToken t; !done && (t = xmlNext(&x)) != XML_END;) {
        switch (t) {
        case XML_OPEN:
            if (depth == 0) {
                out->kind = kindOf(x.name);
                done = out->kind == NFO_NONE;  // Not one of the three: nothing to read.
            } else if (depth == 1) {
                field = fieldOf(x.name);
                value_length = 0;
                value[0] = '\0';
            }
            depth++;
            break;
        case XML_CLOSE:
            if (depth == 2 && field != F_NONE)
                take(r, field, value);
            if (depth == 2)
                field = F_NONE;
            if (depth > 0)
                depth--;
            done = depth == 0;  // The first film, series or episode only (a file may hold several).
            break;
        case XML_TEXT:
            if (depth == 2 && field != F_NONE)
                value_length = xmlTextAppend(&x, value, FIELD_MAX, value_length);
            break;
        case XML_END:
            break;
        }
    }
    if (depth == 2 && field != F_NONE)  // Cut off inside a field: what came of it.
        take(r, field, value);
    if (!out->plot[0])
        memcpy(out->plot, r->outline, strlen(r->outline) + 1);
    if (!out->year)
        out->year = r->premiered_year;
    free(value);
    free(r);
    if (out->kind == NFO_NONE) {
        memset(out, 0, sizeof(*out));
        out->season = out->episode = -1;
        return false;
    }
    return true;
}
