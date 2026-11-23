#include "folderlist.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define LINE_MAX_BYTES 2048
#define HREF_MAX 4096
#define SECONDS_PER_DAY 86400
#define DAY_AHEAD (2 * SECONDS_PER_DAY)  // A Unix date without a year this far ahead was last year's.

static int64_t daysFromCivil(int64_t y, int m, int d)  // Days since 1970-01-01 (Howard Hinnant's).
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static int64_t secondsOf(int year, int month, int day, int hour, int minute, int second)
{
    if (year < 1970 || year > 9999 || month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 || second < 0 || second > 60)
        return 0;
    return daysFromCivil(year, month, day) * SECONDS_PER_DAY + hour * 3600 + minute * 60 + second;
}

static int yearOf(int64_t seconds)
{
    int year = 1970;
    while (year < 9999 && secondsOf(year + 1, 1, 1, 0, 0, 0) <= seconds)
        year++;
    return year;
}

// Adds name (length bytes) unless it's "." or ".." or can't be kept. False only if memory runs out.
static bool addNamed(DavListing *l, const char *name, size_t length, bool is_dir, int64_t size, int64_t modified)
{
    DavEntry e;
    memset(&e, 0, sizeof(e));
    if (!length || length >= sizeof(e.name) || memchr(name, '/', length) || (length == 1 && name[0] == '.') ||
        (length == 2 && name[0] == '.' && name[1] == '.'))
        return true;
    memcpy(e.name, name, length);
    if (!davNameIsPlain(e.name))
        return true;
    e.is_dir = is_dir;
    e.size = is_dir ? -1 : size;
    e.modified = modified;
    return davListingAdd(l, &e);
}

// Calls each line (without "\r\n"), up to LINE_MAX_BYTES; longer ones are skipped.
typedef bool (*LineFn)(const char *line, void *user);

static bool eachLine(const char *text, LineFn fn, void *user)
{
    char line[LINE_MAX_BYTES];
    for (const char *p = text; *p;) {
        size_t n = strcspn(p, "\r\n");
        if (n < sizeof(line)) {
            memcpy(line, p, n);
            line[n] = '\0';
            if (!fn(line, user))
                return false;
        }
        p += n;
        p += strspn(p, "\r\n");
    }
    return true;
}

// --- MLSD ---

typedef struct {
    DavListing l;
    bool any, ok;
} Lines;

// "20261121003855" or "20261121003855.123".
static int64_t mlsdTime(const char *v, size_t n)
{
    int y, mo, d, h, mi, s;
    if (n < 14 || sscanf(v, "%4d%2d%2d%2d%2d%2d", &y, &mo, &d, &h, &mi, &s) != 6)
        return 0;
    return secondsOf(y, mo, d, h, mi, s);
}

static bool mlsdLine(const char *line, void *user)
{
    Lines *ls = user;
    const char *space = strchr(line, ' ');
    if (!space || !strchr(line, '=') || strchr(line, '=') > space)
        return true;
    bool known = false, is_dir = false;
    int64_t size = -1, modified = 0;
    for (const char *fact = line; fact < space;) {
        size_t n = strcspn(fact, ";");
        if (fact + n > space)
            n = (size_t)(space - fact);
        const char *eq = memchr(fact, '=', n);
        if (eq) {
            size_t key = (size_t)(eq - fact), vn = n - key - 1;
            const char *v = eq + 1;
            if (key == 4 && !strncasecmp(fact, "type", 4)) {
                known = (vn == 4 && !strncasecmp(v, "file", 4)) || (vn == 3 && !strncasecmp(v, "dir", 3));
                is_dir = vn == 3;
            } else if (key == 4 && !strncasecmp(fact, "size", 4)) {
                char *end;
                long long value = strtoll(v, &end, 10);
                size = end > v && value >= 0 ? value : -1;
            } else if (key == 6 && !strncasecmp(fact, "modify", 6)) {
                modified = mlsdTime(v, vn);
            }
        }
        fact += n + (fact[n] == ';');
    }
    ls->any = true;
    if (known)
        ls->ok = addNamed(&ls->l, space + 1, strlen(space + 1), is_dir, size, modified);
    return ls->ok;
}

static bool finish(Lines *ls, bool listing, DavEntry **out, size_t *count)
{
    if (!ls->ok || !listing) {
        free(ls->l.items);
        return false;
    }
    *out = ls->l.items;
    *count = ls->l.count;
    return true;
}

bool folderParseMlsd(const char *text, DavEntry **out, size_t *count)
{
    *out = NULL;
    *count = 0;
    Lines ls = {.ok = true};
    if (!text)
        return false;
    eachLine(text, mlsdLine, &ls);
    return finish(&ls, ls.any || !text[strspn(text, " \r\n")], out, count);
}

// --- LIST ---

typedef struct {
    Lines lines;
    int64_t now;
} ListState;

static int monthOf(const char *word)
{
    static const char MONTHS[] = "janfebmaraprmayjunjulaugsepoctnovdec";
    if (strlen(word) != 3)
        return 0;
    char lower[4] = {(char)tolower((unsigned char)word[0]), (char)tolower((unsigned char)word[1]),
                     (char)tolower((unsigned char)word[2]), 0};
    const char *at = strstr(MONTHS, lower);
    return at && (at - MONTHS) % 3 == 0 ? (int)(at - MONTHS) / 3 + 1 : 0;
}

// The next word of *p (spaces skipped), copied into out; *p moves past it. False at the end.
static bool nextWord(const char **p, char *out, size_t size)
{
    *p += strspn(*p, " \t");
    size_t n = strcspn(*p, " \t");
    if (!n)
        return false;
    snprintf(out, size, "%.*s", (int)(n < size ? n : size - 1), *p);
    *p += n;
    return true;
}

// "-rw-r--r-- 1 user group 1234 Nov 21 00:38 name": the month, then its day and
// a time or a year, find the date whatever comes before it; the size is the word before.
static bool unixLine(const char *line, ListState *st)
{
    char kind = line[0];
    if (!strchr("-dl", kind) || strlen(line) < 10)
        return false;
    const char *p = line;
    char words[16][64];
    int count = 0;
    const char *after[16];
    while (count < 16 && nextWord(&p, words[count], sizeof(words[0]))) {
        after[count] = p;
        int m = count >= 2 ? monthOf(words[count - 2]) : 0;
        if (m && count >= 3 && isdigit((unsigned char)words[count - 1][0])) {
            const char *date = words[count];
            int day = atoi(words[count - 1]), hour = 0, minute = 0, year = 0;
            bool timed = sscanf(date, "%d:%d", &hour, &minute) == 2 && strchr(date, ':');
            if (!timed) {
                hour = minute = 0;  // sscanf took the year as an hour.
                if (sscanf(date, "%d", &year) != 1)
                    return false;
            }
            if (timed) {
                year = yearOf(st->now);
                if (secondsOf(year, m, day, hour, minute, 0) > st->now + DAY_AHEAD)
                    year--;
            }
            char *end;
            long long size = strtoll(words[count - 3], &end, 10);
            const char *name = after[count];
            if (*name == ' ')
                name++;  // One space before the name; more are part of it.
            size_t n = strlen(name);
            if (kind == 'l') {  // "link -> target": the link's name, a file as far as can be told.
                const char *arrow = strstr(name, " -> ");
                if (arrow)
                    n = (size_t)(arrow - name);
            }
            st->lines.any = true;
            st->lines.ok = addNamed(&st->lines.l, name, n, kind == 'd', *end ? -1 : size,
                                    secondsOf(year, m, day, hour, minute, 0));
            return true;
        }
        count++;
    }
    return false;
}

// IIS: "11-21-26  12:38AM       <DIR>          Folder" or "...  1234 name".
static bool dosLine(const char *line, ListState *st)
{
    int mo, d, y, h, mi, consumed = 0;
    char half[3] = "";
    if (sscanf(line, "%d-%d-%d %d:%d%2[AaPp]%*[Mm]%n", &mo, &d, &y, &h, &mi, half, &consumed) < 6 || !consumed)
        return false;
    const char *p = line + consumed;
    char word[32];
    if (!nextWord(&p, word, sizeof(word)))
        return false;
    bool is_dir = !strcasecmp(word, "<DIR>");
    char *end;
    long long size = is_dir ? -1 : strtoll(word, &end, 10);
    if (!is_dir && *end)
        return false;
    p += strspn(p, " \t");
    if (y < 100)
        y += y < 70 ? 2000 : 1900;
    if (h == 12)
        h = 0;
    if (tolower((unsigned char)half[0]) == 'p')
        h += 12;
    st->lines.any = true;
    st->lines.ok = addNamed(&st->lines.l, p, strlen(p), is_dir, size, secondsOf(y, mo, d, h, mi, 0));
    return true;
}

static bool listLine(const char *line, void *user)
{
    ListState *st = user;
    if (!unixLine(line, st))
        dosLine(line, st);
    return st->lines.ok;
}

bool folderParseFtpList(const char *text, int64_t now, DavEntry **out, size_t *count)
{
    *out = NULL;
    *count = 0;
    if (!text)
        return false;
    ListState st = {.lines = {.ok = true}, .now = now};
    eachLine(text, listLine, &st);
    // Nothing at all is an empty folder; text with no line read isn't a listing.
    return finish(&st.lines, st.lines.any || !text[strspn(text, " \r\n\t")] || !strncasecmp(text, "total ", 6),
                  out, count);
}

// --- A web server's index ---

// Where needle first is in text, ignoring case (strcasestr isn't everywhere). NULL if it isn't.
static const char *findCase(const char *text, const char *needle)
{
    size_t n = strlen(needle);
    for (; *text; text++)
        if (!strncasecmp(text, needle, n))
            return text;
    return NULL;
}

// The next href's value from *p, entities like &amp; decoded; *p moves past it. False when there's no more.
static bool nextHref(const char **p, char *out, size_t size)
{
    for (const char *s = *p; (s = findCase(s, "href")) != NULL; s += 4) {
        const char *v = s + 4;
        v += strspn(v, " \t\r\n");
        if (*v != '=')
            continue;
        v++;
        v += strspn(v, " \t\r\n");
        char quote = *v == '"' || *v == '\'' ? *v++ : 0;
        size_t n = quote ? strcspn(v, quote == '"' ? "\"" : "'") : strcspn(v, " \t\r\n>");
        if (quote && v[n] != quote)
            return false;
        *p = v + n;
        size_t o = 0;
        for (size_t i = 0; i < n && o + 1 < size;) {
            static const struct {
                const char *entity;
                char c;
            } ENTITIES[] = {{"&amp;", '&'}, {"&#38;", '&'}, {"&quot;", '"'}, {"&#39;", '\''}, {"&apos;", '\''}};
            bool done = false;
            for (size_t k = 0; k < sizeof(ENTITIES) / sizeof(ENTITIES[0]) && !done; k++) {
                size_t m = strlen(ENTITIES[k].entity);
                if (i + m <= n && !strncasecmp(v + i, ENTITIES[k].entity, m)) {
                    out[o++] = ENTITIES[k].c;
                    i += m;
                    done = true;
                }
            }
            if (!done)
                out[o++] = v[i++];
        }
        out[o] = '\0';
        return true;
    }
    return false;
}

static bool seen(const DavListing *l, const char *name)
{
    for (size_t i = 0; i < l->count; i++)
        if (!strcmp(l->items[i].name, name))
            return true;
    return false;
}

// What an href names in the folder: false for links elsewhere.
static bool hrefEntry(const char *href, const char *folder_url, const char *folder, DavEntry *e)
{
    memset(e, 0, sizeof(*e));
    e->size = -1;
    if (!href[0] || strchr("?#", href[0]) || !strncasecmp(href, "mailto:", 7) || !strncasecmp(href, "javascript:", 11))
        return false;
    char clean[HREF_MAX];
    snprintf(clean, sizeof(clean), "%s", href);
    clean[strcspn(clean, "?#")] = '\0';
    size_t n = strlen(clean);
    e->is_dir = n && clean[n - 1] == '/';
    if (strstr(clean, "://")) {  // A whole address: this server's only.
        if (!davSameServer(clean, folder_url))
            return false;
        return davEntryOfHref(clean, folder, e);
    }
    if (clean[0] == '/')
        return davEntryOfHref(clean, folder, e);
    while (n && clean[n - 1] == '/')
        clean[--n] = '\0';
    if (!strncmp(clean, "./", 2))
        memmove(clean, clean + 2, strlen(clean + 2) + 1);
    if (!clean[0] || strchr(clean, '/') || !davPercentDecode(clean) || strchr(clean, '/'))
        return false;  // Deeper, up ("../"), or a name with a decoded '/'.
    n = strlen(clean);
    if (!n || n >= sizeof(e->name) || !strcmp(clean, ".") || !strcmp(clean, "..") || !davNameIsPlain(clean))
        return false;
    memcpy(e->name, clean, n + 1);
    return true;
}

bool folderParseHtmlIndex(const char *html, const char *folder_url, DavEntry **out, size_t *count)
{
    *out = NULL;
    *count = 0;
    char folder[HREF_MAX];
    if (!html || !findCase(html, "<a") || !davHrefPath(folder_url, folder, sizeof(folder)))
        return false;
    DavListing l = {0};
    char href[HREF_MAX];
    bool ok = true;
    for (const char *p = html; ok && nextHref(&p, href, sizeof(href));) {
        DavEntry e;
        if (hrefEntry(href, folder_url, folder, &e) && !seen(&l, e.name))
            ok = davListingAdd(&l, &e);
    }
    if (!ok) {
        free(l.items);
        return false;
    }
    *out = l.items;
    *count = l.count;
    return true;
}
