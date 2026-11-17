#include "subs.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define SRT_FILE_MAX (8u << 20)

void subsFree(SubTrack *t)
{
    for (size_t i = 0; i < t->count; i++)
        free(t->cues[i].text);
    free(t->cues);
    t->cues = NULL;
    t->count = t->capacity = 0;
}

void subsClean(char *text)
{
    char *out = text;
    for (const char *p = text; *p;) {
        if (*p == '{') {  // An ASS override block: {\i1}, {\an8}...
            const char *close = strchr(p, '}');
            if (close) {
                p = close + 1;
                continue;
            }
        }
        if (*p == '<') {  // An HTML-ish tag: <i>, </font>...
            const char *close = strchr(p, '>');
            if (close && close - p < 64) {
                p = close + 1;
                continue;
            }
        }
        if (p[0] == '\\' && (p[1] == 'N' || p[1] == 'n')) {
            *out++ = '\n';
            p += 2;
            continue;
        }
        if (p[0] == '\\' && p[1] == 'h') {  // ASS's hard space.
            *out++ = ' ';
            p += 2;
            continue;
        }
        if (*p == '\r') {
            p++;
            continue;
        }
        *out++ = *p++;
    }
    *out = '\0';
    // Spaces at the ends of lines, and empty lines at the start and end.
    char *w = text;
    for (char *line = text; *line;) {
        char *end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);
        size_t start = 0;
        while (start < len && (line[start] == ' ' || line[start] == '\t'))
            start++;
        while (len > start && (line[len - 1] == ' ' || line[len - 1] == '\t'))
            len--;
        if (len > start) {
            if (w != text)
                *w++ = '\n';
            memmove(w, line + start, len - start);
            w += len - start;
        }
        if (!end)
            break;
        line = end + 1;
    }
    *w = '\0';
}

bool subsAdd(SubTrack *t, int64_t start_ns, int64_t end_ns, const char *text)
{
    if (!text || end_ns <= start_ns)
        return false;
    char *copy = strdup(text);
    if (!copy)
        return false;
    subsClean(copy);
    if (!copy[0]) {
        free(copy);
        return false;
    }
    // Where it goes, by start time; the same line again (a seek re-read it) is left out.
    size_t at = t->count;
    while (at > 0 && t->cues[at - 1].start_ns > start_ns)
        at--;
    for (size_t i = at; i > 0 && t->cues[i - 1].start_ns == start_ns; i--)
        if (strcmp(t->cues[i - 1].text, copy) == 0) {
            free(copy);
            return true;
        }
    if (t->count == t->capacity) {
        size_t grown = t->capacity ? t->capacity * 2 : 256;
        SubCue *cues = realloc(t->cues, grown * sizeof(SubCue));
        if (!cues) {
            free(copy);
            return false;
        }
        t->cues = cues;
        t->capacity = grown;
    }
    memmove(&t->cues[at + 1], &t->cues[at], (t->count - at) * sizeof(SubCue));
    t->cues[at] = (SubCue){start_ns, end_ns, copy};
    t->count++;
    return true;
}

// "01:02:03,456" (or with a dot) into nanoseconds; false if it isn't a time.
static bool parseTime(const char *s, int64_t *out, const char **end)
{
    int h, m, sec, ms, n = 0;
    if (sscanf(s, "%d:%d:%d%*[,.]%d%n", &h, &m, &sec, &ms, &n) != 4 || n == 0 || m > 59 || sec > 59 || h < 0)
        return false;
    *out = (((int64_t)h * 60 + m) * 60 + sec) * 1000000000 + (int64_t)ms * 1000000;
    *end = s + n;
    return true;
}

bool subsParseSrt(SubTrack *t, const char *data, size_t size)
{
    char *copy = malloc(size + 1);
    if (!copy)
        return false;
    memcpy(copy, data, size);
    copy[size] = '\0';
    char *p = copy;
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
        p += 3;
    size_t before = t->count;
    while (*p) {
        // Find the next "start --> end" line; what's before it (the cue's number) doesn't matter.
        char *arrow = strstr(p, "-->");
        if (!arrow)
            break;
        char *line = arrow;
        while (line > p && line[-1] != '\n')
            line--;
        int64_t start, end;
        const char *after;
        char *next_line = strchr(arrow, '\n');
        while (*line == ' ' || *line == '\t')
            line++;
        bool ok = parseTime(line, &start, &after);
        const char *q = arrow + 3;
        while (*q == ' ' || *q == '\t')
            q++;
        ok = ok && parseTime(q, &end, &after);
        if (!next_line)
            break;
        // The text: the lines after, up to an empty line.
        char *text = next_line + 1, *stop = text;
        while (*stop) {
            char *nl = strchr(stop, '\n');
            char *line_end = nl ? nl : stop + strlen(stop);
            bool empty = true;
            for (char *c = stop; c < line_end; c++)
                if (!isspace((unsigned char)*c))
                    empty = false;
            if (empty)
                break;
            stop = nl ? nl + 1 : line_end;
        }
        char saved = *stop;
        *stop = '\0';
        if (ok)
            subsAdd(t, start, end, text);
        *stop = saved;
        p = stop;
        if (*p == '\0')
            break;
        p++;
    }
    free(copy);
    return t->count > before;
}

bool subsLoadSrtFile(SubTrack *t, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    char *data = NULL;
    long size = -1;
    if (fseek(f, 0, SEEK_END) == 0)
        size = ftell(f);
    if (size > 0 && (unsigned long)size <= SRT_FILE_MAX && fseek(f, 0, SEEK_SET) == 0) {
        data = malloc((size_t)size);
        if (data && fread(data, 1, (size_t)size, f) != (size_t)size) {
            free(data);
            data = NULL;
        }
    }
    fclose(f);
    bool ok = data && subsParseSrt(t, data, (size_t)size);
    free(data);
    return ok;
}

bool subsAt(const SubTrack *t, int64_t pos_ns, char *out, size_t size)
{
    size_t used = 0;
    out[0] = '\0';
    for (size_t i = 0; i < t->count && t->cues[i].start_ns <= pos_ns; i++) {
        if (pos_ns >= t->cues[i].end_ns)
            continue;
        int n = snprintf(out + used, size - used, "%s%s", used ? "\n" : "", t->cues[i].text);
        if (n < 0 || (size_t)n >= size - used)
            break;
        used += (size_t)n;
    }
    return used > 0;
}

const char *subsAssText(const char *ass)
{
    const char *p = ass;
    for (int commas = 0; commas < 8; commas++) {
        p = strchr(p, ',');
        if (!p)
            return ass;  // Not an ASS line: all of it is text.
        p++;
    }
    return p;
}

const char *subsLanguageName(const char *code)
{
    static const struct {
        const char *codes, *name;  // The codes: 639-2 (both forms) and 639-1, space-separated.
    } LANGUAGES[] = {
        {"eng en", "English"},     {"spa es", "Spanish"},     {"fre fra fr", "French"},
        {"ger deu de", "German"},  {"ita it", "Italian"},     {"por pt", "Portuguese"},
        {"jpn ja", "Japanese"},    {"chi zho zh", "Chinese"}, {"kor ko", "Korean"},
        {"rus ru", "Russian"},     {"ara ar", "Arabic"},      {"dut nld nl", "Dutch"},
        {"pol pl", "Polish"},      {"tur tr", "Turkish"},     {"swe sv", "Swedish"},
        {"nor nob no nb", "Norwegian"}, {"dan da", "Danish"}, {"fin fi", "Finnish"},
        {"hin hi", "Hindi"},       {"cat ca", "Catalan"},     {"gre ell el", "Greek"},
        {"heb he", "Hebrew"},      {"ukr uk", "Ukrainian"},   {"cze ces cs", "Czech"},
        {"hun hu", "Hungarian"},   {"rum ron ro", "Romanian"}, {"tha th", "Thai"},
        {"vie vi", "Vietnamese"},  {"ind id", "Indonesian"},  {"may msa ms", "Malay"},
    };
    if (!code || !code[0])
        return NULL;
    size_t len = strlen(code);
    for (size_t i = 0; i < sizeof(LANGUAGES) / sizeof(LANGUAGES[0]); i++) {
        for (const char *p = LANGUAGES[i].codes; *p;) {
            size_t n = strcspn(p, " ");
            if (n == len && strncasecmp(p, code, n) == 0)
                return LANGUAGES[i].name;
            p += n;
            while (*p == ' ')
                p++;
        }
    }
    return code;
}

bool subsFindFor(const char *video_path, char *out, size_t size)
{
    char folder[512], base[256];
    const char *slash = strrchr(video_path, '/');
    if (!slash || (size_t)(slash - video_path) >= sizeof(folder))
        return false;
    memcpy(folder, video_path, (size_t)(slash - video_path));
    folder[slash - video_path] = '\0';
    snprintf(base, sizeof(base), "%s", slash + 1);
    char *dot = strrchr(base, '.');
    if (dot)
        *dot = '\0';
    size_t base_len = strlen(base);
    // A video at the root leaves "sdmc:": the folder to open is "sdmc:/".
    char dir_path[520];
    snprintf(dir_path, sizeof(dir_path), "%s%s", folder, folder[0] && folder[strlen(folder) - 1] == ':' ? "/" : "");
    DIR *dir = opendir(dir_path);
    if (!dir)
        return false;
    char best[256] = "";
    struct dirent *e;
    while ((e = readdir(dir)) != NULL) {
        const char *name = e->d_name, *ext = strrchr(name, '.');
        if (!ext || strcasecmp(ext, ".srt") != 0 || strncmp(name, base, base_len) != 0)
            continue;
        if ((size_t)(ext - name) == base_len) {  // "name.srt": the one.
            snprintf(best, sizeof(best), "%s", name);
            break;
        }
        if (name[base_len] == '.' && (!best[0] || strcmp(name, best) < 0))
            snprintf(best, sizeof(best), "%s", name);  // "name.es.srt": the first by name.
    }
    closedir(dir);
    if (!best[0])
        return false;
    int n = snprintf(out, size, "%s/%s", folder, best);
    return n > 0 && (size_t)n < size;
}
