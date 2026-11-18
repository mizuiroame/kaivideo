#include "naming.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TOKENS 48
#define TOKEN_MAX 96

typedef struct {
    char text[MAX_TOKENS][TOKEN_MAX];
    int count;
} Tokens;

// Release tags that end a title wherever they are.
static const char *const STRONG_TAGS[] = {
    "4k", "uhd", "bluray", "blu-ray", "bdrip", "brrip", "bdremux", "remux", "webrip", "web-dl", "webdl", "hdtv",
    "dvdrip", "hdrip", "x264", "x265", "h264", "h265", "hevc", "avc", "10bit", "8bit", "hdr", "hdr10", "hdr10+",
    "dovi", "nf", "amzn", "dsnp", "hmax", "atvp", "truehd",
};
// Words that are tags after a year, a strong tag or SxxEyy, and may be
// part of a title before ("Charlotte's Web").
static const char *const WEAK_TAGS[] = {
    "web", "dv", "dual", "multi", "complete", "proper", "repack", "extended", "remastered", "unrated", "uncut",
    "imax", "latino", "french", "eng", "english", "spanish", "jap", "atmos", "aac", "aac2", "ac3", "dts", "dd5",
    "ddp5", "ma", "internal", "limited", "ita", "esp", "spa", "lat", "sub", "subs", "dub",
};

static bool inList(const char *word, const char *const *list, size_t count)
{
    for (size_t i = 0; i < count; i++)
        if (strcmp(word, list[i]) == 0)
            return true;
    return false;
}

static bool allDigits(const char *s, size_t min, size_t max)
{
    size_t n = strlen(s);
    if (n < min || n > max)
        return false;
    for (size_t i = 0; i < n; i++)
        if (!isdigit((unsigned char)s[i]))
            return false;
    return true;
}

// The token lowercased, without parentheses around it, and up to a '-'
// when cut is set ("x265-ELiTE" -> "x265").
static void normalized(const char *token, char *out, size_t size, bool cut)
{
    size_t n = 0;
    for (const char *p = token; *p && n + 1 < size; p++) {
        if (*p == '(' || *p == ')')
            continue;
        if (cut && *p == '-' && n > 0)
            break;
        out[n++] = (char)tolower((unsigned char)*p);
    }
    out[n] = '\0';
}

static int tagKind(const char *token)  // 0 none, 1 weak, 2 strong.
{
    char word[TOKEN_MAX], whole[TOKEN_MAX];
    normalized(token, word, sizeof(word), true);
    normalized(token, whole, sizeof(whole), false);
    size_t n = strlen(word);
    // 1080p, 720p, 2160p, 1080i.
    if (n >= 4 && n <= 5 && (word[n - 1] == 'p' || word[n - 1] == 'i')) {
        word[n - 1] = '\0';
        bool resolution = allDigits(word, 3, 4);
        word[n - 1] = 'p';
        if (resolution)
            return 2;
    }
    if (inList(word, STRONG_TAGS, sizeof(STRONG_TAGS) / sizeof(STRONG_TAGS[0])) ||
        inList(whole, STRONG_TAGS, sizeof(STRONG_TAGS) / sizeof(STRONG_TAGS[0])))
        return 2;
    if (inList(word, WEAK_TAGS, sizeof(WEAK_TAGS) / sizeof(WEAK_TAGS[0])))
        return 1;
    return 0;
}

static int yearOf(const char *token)
{
    char word[TOKEN_MAX];
    normalized(token, word, sizeof(word), false);
    if (!allDigits(word, 4, 4))
        return 0;
    int year = atoi(word);
    return year >= 1900 && year <= 2099 ? year : 0;
}

// SxxEyy (S01E01, s1e2, S01E01E02) or 1x04: the season and episode.
static bool episodeMarker(const char *token, int *season, int *episode)
{
    char word[TOKEN_MAX];
    normalized(token, word, sizeof(word), true);
    char *end;
    if (word[0] == 's' && isdigit((unsigned char)word[1])) {
        long s = strtol(word + 1, &end, 10);
        if (*end == 'e' && isdigit((unsigned char)end[1]) && s < 100) {
            long e = strtol(end + 1, &end, 10);
            if ((*end == '\0' || *end == 'e') && e < 1000) {
                *season = (int)s;
                *episode = (int)e;
                return true;
            }
        }
        return false;
    }
    if (isdigit((unsigned char)word[0])) {
        long s = strtol(word, &end, 10);
        if (*end == 'x' && s < 100 && end - word <= 2 && allDigits(end + 1, 2, 3)) {
            *season = (int)s;
            *episode = atoi(end + 1);
            return true;
        }
    }
    return false;
}

// A file's name (without its folders and extension) or a folder's, with
// what isn't part of the name dropped: [groups], {tags}, a leading
// "www.site.org -", and dots and underscores between words. Split into words.
static void tokenize(const char *path, bool is_file, Tokens *t)
{
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    char name[512];
    snprintf(name, sizeof(name), "%s", base);
    char *dot = is_file ? strrchr(name, '.') : NULL;
    if (dot && dot != name)
        *dot = '\0';
    char *p = name;
    if (strncasecmp(p, "www.", 4) == 0) {
        p += strcspn(p, " -");
        p += strspn(p, " -");
    }
    char clean[512];
    size_t n = 0;
    int depth = 0;
    for (; *p && n + 1 < sizeof(clean); p++) {
        if (*p == '[' || *p == '{') {
            depth++;
        } else if ((*p == ']' || *p == '}') && depth > 0) {
            depth--;
            clean[n++] = ' ';
        } else if (depth == 0) {
            clean[n++] = (*p == '.' || *p == '_') ? ' ' : *p;
        }
    }
    clean[n] = '\0';
    t->count = 0;
    for (char *word = strtok(clean, " \t"); word && t->count < MAX_TOKENS; word = strtok(NULL, " \t"))
        snprintf(t->text[t->count++], TOKEN_MAX, "%s", word);
}

// Words from..to-1 joined by spaces, without dashes at either end.
static void join(const Tokens *t, int from, int to, char *out, size_t size)
{
    while (from < to && strcmp(t->text[from], "-") == 0)
        from++;
    while (to > from && strcmp(t->text[to - 1], "-") == 0)
        to--;
    out[0] = '\0';
    size_t n = 0;
    for (int i = from; i < to; i++)
        n += (size_t)snprintf(out + n, n < size ? size - n : 0, "%s%s", i > from ? " " : "", t->text[i]);
    // "Name -" or "- Name" glued to a word.
    size_t len = strlen(out);
    while (len > 0 && (out[len - 1] == '-' || out[len - 1] == ' '))
        out[--len] = '\0';
}

// "PrairieRoad" -> "Prairie Road": one glued word gets its spaces back.
static void splitGlued(char *name, size_t size)
{
    if (strchr(name, ' '))
        return;
    char out[NAMING_MAX];
    size_t n = 0;
    for (size_t i = 0; name[i] && n + 2 < sizeof(out); i++) {
        if (i > 0 && isupper((unsigned char)name[i]) && islower((unsigned char)name[i - 1]))
            out[n++] = ' ';
        out[n++] = name[i];
    }
    out[n] = '\0';
    snprintf(name, size, "%s", out);
}

// Where the name's tags begin: the first strong tag, or a weak one after a
// year, a strong tag or an episode marker (from start on).
static int tagsStart(const Tokens *t, int start, bool marker_seen)
{
    for (int i = start; i < t->count; i++) {
        int kind = tagKind(t->text[i]);
        if (kind == 2 || (kind == 1 && marker_seen))
            return i;
        if (i > 0 && yearOf(t->text[i]))
            marker_seen = true;
    }
    return t->count;
}

// The last year in words 1..end-1 (the first word stays part of the title).
static int lastYear(const Tokens *t, int end, int *at)
{
    for (int i = end - 1; i > 0; i--) {
        int year = yearOf(t->text[i]);
        if (year) {
            *at = i;
            return year;
        }
    }
    return 0;
}

// --- Folders ---

// The name of the folder at depth levels above the file ("" if that's the
// root or above it).
static void folderName(const char *path, const char *root, int levels, char *out, size_t size)
{
    out[0] = '\0';
    size_t root_len = strlen(root);
    while (root_len > 0 && root[root_len - 1] == '/')
        root_len--;
    const char *end = strrchr(path, '/');
    for (int i = 0; end && i < levels; i++) {
        if (end <= path + root_len && strncmp(path, root, root_len) == 0)
            return;  // The root, or above it.
        const char *start = end;
        while (start > path && start[-1] != '/')
            start--;
        if (i == levels - 1) {
            snprintf(out, size, "%.*s", (int)(end - start), start);
            return;
        }
        end = start - 1;
    }
}

// Word i says a season: "S02", "S04e01-22", or "Season 2", "Temporada 1",
// "Staffel 3", "Saison 4" (two words). Its number.
static bool seasonWord(const Tokens *t, int i, int *season)
{
    char word[TOKEN_MAX];
    normalized(t->text[i], word, sizeof(word), true);
    if (word[0] == 's' && isdigit((unsigned char)word[1])) {
        char *end;
        long s = strtol(word + 1, &end, 10);
        if ((*end == '\0' || *end == 'e') && s < 100) {
            *season = (int)s;
            return true;
        }
    }
    static const char *const WORDS[] = {"season", "temporada", "staffel", "saison"};
    for (size_t w = 0; w < sizeof(WORDS) / sizeof(WORDS[0]); w++) {
        if (strcmp(word, WORDS[w]) == 0 && i + 1 < t->count && allDigits(t->text[i + 1], 1, 3)) {
            *season = atoi(t->text[i + 1]);
            return true;
        }
    }
    return false;
}

// A folder's name as a series' name: up to a season ("North Peak Season 2
// (1080p x265)" -> "North Peak") or the tags, without a year after it
// ("Coach Mabel (2020)" -> "Coach Mabel"). "" for a folder that's only a
// season ("Season 2"). True if it names a season, and which.
static bool cleanFolder(const char *folder, char *out, size_t size, int *season)
{
    static Tokens t;  // Large: kept off the stack.
    tokenize(folder, false, &t);
    int cut = tagsStart(&t, 0, false);
    bool has_season = false;
    for (int i = 0; i < cut; i++) {
        if (seasonWord(&t, i, season)) {
            has_season = true;
            cut = i;
        }
    }
    if (cut > 1 && yearOf(t.text[cut - 1]))
        cut--;
    join(&t, 0, cut, out, size);
    return has_season;
}

// "Title (2017)": the title and year.
static bool filmFolder(const char *name, char *title, size_t size, int *year)
{
    size_t n = strlen(name);
    if (n < 8 || name[n - 1] != ')' || name[n - 6] != '(' || name[n - 7] != ' ')
        return false;
    char digits[5];
    memcpy(digits, name + n - 5, 4);
    digits[4] = '\0';
    if (!allDigits(digits, 4, 4))
        return false;
    *year = atoi(digits);
    snprintf(title, size, "%.*s", (int)(n - 7), name);
    return true;
}

// True if the folder's name holds the series' name from the file ("Sky
// Pilots - The Long War" holds "LongWar").
static bool folderNamesSeries(const char *folder, const char *parsed)
{
    char folder_key[NAMING_MAX], parsed_key[NAMING_MAX];
    namingKey(folder, folder_key, sizeof(folder_key));
    namingKey(parsed, parsed_key, sizeof(parsed_key));
    return folder_key[0] && (!parsed_key[0] || strstr(folder_key, parsed_key));
}

// --- The parse ---

static void parseEpisode(const Tokens *t, int marker, VideoName *out)
{
    // The series: the words before the marker, without a year after them.
    int show_end = marker;
    while (show_end > 0 && strcmp(t->text[show_end - 1], "-") == 0)
        show_end--;
    if (show_end > 1 && yearOf(t->text[show_end - 1]))
        show_end--;
    join(t, 0, show_end, out->show, sizeof(out->show));
    int title_from = marker + 1;
    join(t, title_from, tagsStart(t, title_from, true), out->title, sizeof(out->title));
}

void namingParse(const char *path, const char *root, VideoName *out)
{
    memset(out, 0, sizeof(*out));
    char parent[NAMING_MAX], above[NAMING_MAX], series_folder[NAMING_MAX] = "";
    folderName(path, root, 1, parent, sizeof(parent));
    folderName(path, root, 2, above, sizeof(above));
    // The series' folder: the parent ("North Peak Season 2 (1080p)" as
    // "North Peak"), or the one above a folder that's only a season.
    int folder_season = 0, above_season = 0;
    bool in_season = parent[0] && cleanFolder(parent, series_folder, sizeof(series_folder), &folder_season);
    if (!series_folder[0] && above[0])
        cleanFolder(above, series_folder, sizeof(series_folder), &above_season);
    static Tokens t;  // Large: kept off the stack.
    tokenize(path, true, &t);

    int marker = -1;
    for (int i = 0; i < t.count && marker < 0; i++)
        if (episodeMarker(t.text[i], &out->season, &out->episode))
            marker = i;
    int cut = tagsStart(&t, 0, false);
    if (marker >= 0) {
        parseEpisode(&t, marker, out);
        out->is_episode = true;
    } else if (cut >= 3 && strcmp(t.text[cut - 2], "-") == 0 && allDigits(t.text[cut - 1], 1, 3)) {
        // "Series - 05", as anime is often named.
        out->is_episode = true;
        out->season = 1;
        out->episode = atoi(t.text[cut - 1]);
        join(&t, 0, cut - 2, out->show, sizeof(out->show));
    } else if (in_season && t.count > 0 && allDigits(t.text[0], 1, 3)) {
        // "Season 1/03 - Lamentis".
        out->is_episode = true;
        out->season = folder_season;
        out->episode = atoi(t.text[0]);
        join(&t, 1, cut, out->title, sizeof(out->title));
    }

    if (out->is_episode) {
        if (in_season && !out->season)
            out->season = folder_season;
        if (series_folder[0] && folderNamesSeries(series_folder, out->show))
            snprintf(out->show, sizeof(out->show), "%s", series_folder);
        else
            splitGlued(out->show, sizeof(out->show));
        if (out->show[0])
            return;
        // "S01E02.mkv" alone: no series to put it in, so a film by its name.
        memset(out, 0, sizeof(*out));
        join(&t, 0, t.count, out->title, sizeof(out->title));
        return;
    }
    if (parent[0] && filmFolder(parent, out->title, sizeof(out->title), &out->year))
        return;
    int year_at = cut;
    out->year = lastYear(&t, cut, &year_at);
    join(&t, 0, out->year ? year_at : cut, out->title, sizeof(out->title));
    if (!out->title[0])  // Nothing but tags: the name as it is.
        join(&t, 0, t.count, out->title, sizeof(out->title));
}

// --- Keys ---

// Latin-1 letters with accents (U+00C0 to U+00FF) as plain ones; 0 for
// the signs in that range.
static char plainLatin1(unsigned code)
{
    static const char MAP[] = "aaaaaaaceeeeiiiidnooooo0ouuuuyts"   // U+00C0-U+00DF
                              "aaaaaaaceeeeiiiidnooooo0ouuuuyty";  // U+00E0-U+00FF
    char c = MAP[code - 0xC0];
    return c == '0' ? 0 : c;
}

void namingKey(const char *name, char *out, size_t size)
{
    if (size == 0)
        return;
    size_t n = 0;
    const unsigned char *p = (const unsigned char *)name;
    while (*p && n + 1 < size) {
        if (isalnum(*p)) {
            out[n++] = (char)tolower(*p);
            p++;
        } else if (*p == 0xC3 && p[1] >= 0x80 && p[1] <= 0xBF) {  // U+00C0-U+00FF.
            char c = plainLatin1(0xC0 + (p[1] - 0x80));
            if (c)
                out[n++] = c;
            p += 2;
        } else if (*p >= 0xC0) {  // Other scripts: kept, whole characters only.
            size_t len = *p >= 0xF0 ? 4 : *p >= 0xE0 ? 3 : 2;
            size_t have = 1;
            while (have < len && (p[have] & 0xC0) == 0x80)
                have++;
            if (have == len && n + len >= size)
                break;  // It doesn't fit.
            if (have == len) {
                memcpy(out + n, p, len);
                n += len;
            }
            p += have;
        } else {
            p++;
        }
    }
    out[n] = '\0';
}
