#include "downloads.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "browser.h"
#include "util.h"

#define LINE_MAX_BYTES 4096
#define LIST_FIELDS_V1 15  // Lists written before conversions.
#define LIST_FIELDS 18
#define SYNC_FIELDS 5
#define NS_PER_MS INT64_C(1000000)
#define DEFAULT_EXT "mkv"

// --- Names on the card ---

// Latin-1's letters (U+00C0 to U+00FF) without accents; "" for its signs.
static const char *const LATIN1[64] = {
    "A", "A", "A", "A", "A", "A", "AE", "C", "E", "E", "E", "E", "I", "I", "I", "I",
    "D", "N", "O", "O", "O", "O", "O", "",  "O", "U", "U", "U", "U", "Y", "Th", "ss",
    "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
    "d", "n", "o", "o", "o", "o", "o", "",  "o", "u", "u", "u", "u", "y", "th", "y",
};

// What a character becomes in a name on the card. " " is a space (folded
// with others), "" nothing.
static const char *plainOf(unsigned c, char ascii[2])
{
    if (c < 0x80) {
        if (c < 0x20 || strchr("\\/*?\"<>|", (int)c))
            return " ";
        if (c == ':')
            return " - ";
        ascii[0] = (char)c;
        ascii[1] = '\0';
        return ascii;
    }
    if (c >= 0xC0 && c <= 0xFF)
        return LATIN1[c - 0xC0];
    switch (c) {
    case 0x152: return "OE";
    case 0x153: return "oe";
    case 0x160: return "S";
    case 0x161: return "s";
    case 0x17D: return "Z";
    case 0x17E: return "z";
    case 0x2018: case 0x2019: return "'";  // Curly quotes.
    case 0x2013: case 0x2014: return "-";  // Dashes.
    case 0xA0: return " ";
    default: return "";
    }
}

void dlSafeName(const char *text, char *out, size_t size)
{
    if (size == 0)
        return;
    size_t n = 0;
    bool space = false, full = false;
    const char *p = text;
    while (*p && !full) {
        char ascii[2];
        for (const char *r = plainOf(utilNextCodePoint(&p), ascii); *r && !full; r++) {
            if (*r == ' ') {
                space = n > 0;
            } else if (n == 0 && *r == '.') {
                // A name can't start with a dot (it would be hidden).
            } else if (n + (space ? 2 : 1) >= size) {
                full = true;
            } else {
                if (space)
                    out[n++] = ' ';
                space = false;
                out[n++] = *r;
            }
        }
    }
    while (n > 0 && (out[n - 1] == '.' || out[n - 1] == ' '))
        n--;
    out[n] = '\0';
}

void dlExtensionOf(const char *file_name, char *out, size_t size)
{
    if (size == 0)
        return;
    out[0] = '\0';
    const char *dot = strrchr(file_name, '.');
    if (!dot || !dot[1] || strlen(dot + 1) >= DL_EXT_MAX || strlen(dot + 1) >= size)
        return;
    size_t n = 0;
    for (const char *c = dot + 1; *c; c++) {
        if (!isalnum((unsigned char)*c)) {
            out[0] = '\0';
            return;
        }
        out[n++] = (char)tolower((unsigned char)*c);
    }
    out[n] = '\0';
}

// True if title ends with "(year)" already (some servers name films so).
static bool endsWithYear(const char *title, int year)
{
    char tail[16];
    int n = snprintf(tail, sizeof(tail), "(%d)", year);
    size_t len = strlen(title);
    return n > 0 && len >= (size_t)n && !strcmp(title + len - (size_t)n, tail);
}

bool dlPathFor(const VideoName *name, const char *ext, const char *fallback, const char *root, char *out,
               size_t size)
{
    char plain_fallback[NAMING_MAX], title[NAMING_MAX], base[NAMING_MAX + 16];
    dlSafeName(fallback, plain_fallback, sizeof(plain_fallback));
    dlSafeName(name->title, title, sizeof(title));
    const char *e = ext && ext[0] ? ext : DEFAULT_EXT;
    int n;
    if (name->is_episode) {
        char show[NAMING_MAX], file[NAMING_MAX * 2 + 16];
        dlSafeName(name->show, show, sizeof(show));
        if (!show[0])
            snprintf(show, sizeof(show), "%s", plain_fallback);
        snprintf(file, sizeof(file), "%s S%02dE%02d%s%s", show, name->season, name->episode, title[0] ? " " : "",
                 title);
        n = snprintf(out, size, "%s/Series/%s/Season %02d/%s.%s", root, show, name->season, file, e);
    } else {
        if (!title[0])
            snprintf(base, sizeof(base), "%s", plain_fallback);
        else if (name->year > 0 && !endsWithYear(title, name->year))
            snprintf(base, sizeof(base), "%s (%d)", title, name->year);
        else
            snprintf(base, sizeof(base), "%s", title);
        n = snprintf(out, size, "%s/Films/%s/%s.%s", root, base, base, e);
    }
    return n > 0 && (size_t)n < size;
}

// --- Accounts ---

// Tabs and line breaks would break the files' lines.
static void flatten(char *s)
{
    for (; *s; s++)
        if (*s == '\t' || *s == '\r' || *s == '\n')
            *s = ' ';
}

static bool fitsLine(const char *s, size_t max)
{
    return s && s[0] && strlen(s) < max && !strpbrk(s, "\t\r\n");
}

void dlAccountMake(const char *kind, const char *server, const char *user, char *out, size_t size)
{
    snprintf(out, size, "%s|%s|%s", kind, server, user);
    flatten(out);
}

bool dlAccountIsKind(const char *account, const char *kind)
{
    size_t len = strlen(kind);
    return strncmp(account, kind, len) == 0 && account[len] == '|';
}

// --- The list ---

void dlListInit(DownloadList *list)
{
    memset(list, 0, sizeof(*list));
}

void dlListFree(DownloadList *list)
{
    free(list->items);
    dlListInit(list);
}

static Download *grow(DownloadList *list)
{
    if (list->count == list->capacity) {
        size_t grown = list->capacity ? list->capacity * 2 : 16;
        Download *items = realloc(list->items, grown * sizeof(Download));
        if (!items)
            return NULL;
        list->items = items;
        list->capacity = grown;
    }
    Download *d = &list->items[list->count];
    memset(d, 0, sizeof(*d));
    return d;
}

int dlListFindOrigin(const DownloadList *list, const char *origin, const char *account)
{
    for (size_t i = 0; i < list->count; i++)
        if (!strcmp(list->items[i].origin, origin) && (!account || !strcmp(list->items[i].account, account)))
            return (int)i;
    return -1;
}

int dlListFindPath(const DownloadList *list, const char *path)
{
    for (size_t i = 0; i < list->count; i++)
        if (!strcmp(list->items[i].path, path))
            return (int)i;
    return -1;
}

bool dlPathIsSafe(const char *path)
{
    if (strncmp(path, "sdmc:/", 6) != 0 || !browserIsVideo(path))
        return false;
    for (const char *p = path + 5; p; p = strchr(p + 1, '/'))
        if (!strncmp(p, "/../", 4) || !strncmp(p, "/./", 3) || !strcmp(p, "/..") || !strcmp(p, "/."))
            return false;
    return true;
}

int dlListAdd(DownloadList *list, const char *origin, const char *account, const VideoName *name,
              int64_t runtime_ns, const char *path, int64_t added)
{
    if (!fitsLine(origin, LIBRARY_PATH_MAX) || !fitsLine(account, DL_ACCOUNT_MAX) ||
        !fitsLine(path, LIBRARY_PATH_MAX - strlen(DL_PART_SUFFIX)) || !dlPathIsSafe(path))
        return -1;
    int there = dlListFindOrigin(list, origin, account);
    if (there >= 0)
        return there;
    if (dlListFindPath(list, path) >= 0)
        return -1;
    Download *d = grow(list);
    if (!d)
        return -1;
    d->state = DL_QUEUED;
    snprintf(d->origin, sizeof(d->origin), "%s", origin);
    snprintf(d->account, sizeof(d->account), "%s", account);
    snprintf(d->path, sizeof(d->path), "%s", path);
    d->name = *name;
    flatten(d->name.title);
    flatten(d->name.show);
    d->size = -1;
    d->runtime_ns = runtime_ns;
    d->added = added;
    return (int)list->count++;
}

void dlListRemove(DownloadList *list, int index)
{
    if (index < 0 || (size_t)index >= list->count)
        return;
    memmove(&list->items[index], &list->items[index + 1], (list->count - (size_t)index - 1) * sizeof(Download));
    list->count--;
}

int dlListNext(const DownloadList *list)
{
    int waiting = -1;
    for (size_t i = 0; i < list->count; i++) {
        if (list->items[i].state == DL_ACTIVE)
            return (int)i;
        if (list->items[i].state == DL_QUEUED && waiting < 0)
            waiting = (int)i;
    }
    return waiting;
}

double dlProgress(const Download *d)
{
    if (d->state == DL_DONE)
        return 1;
    if (d->max_bitrate > 0)
        return d->part_count > 0 ? (double)d->parts_done / d->part_count : 0;
    return d->size > 0 ? (double)d->done / (double)d->size : 0;
}

// Splits line at its tabs (empty fields kept). How many fields, at most max.
static int splitTabs(char *line, char **fields, int max)
{
    int n = 0;
    char *p = line;
    while (n < max) {
        fields[n++] = p;
        char *tab = strchr(p, '\t');
        if (!tab)
            break;
        *tab = '\0';
        p = tab + 1;
    }
    return n;
}

static bool readNumber(const char *text, int64_t *out)
{
    char *end;
    long long v = strtoll(text, &end, 10);
    if (end == text || *end)
        return false;
    *out = v;
    return true;
}

// Where a list is written before it replaces the file.
static void sidePath(const char *file, char *out, size_t size)
{
    snprintf(out, size, "%s.new", file);
}

// Opens the side file to write a list into.
static FILE *beginSave(const char *file)
{
    char side[LIBRARY_PATH_MAX + 64];
    sidePath(file, side, sizeof(side));
    return fopen(side, "w");
}

// Closes the side file and puts it in the list's place. False if it wasn't all written.
static bool endSave(FILE *f, const char *file)
{
    char side[LIBRARY_PATH_MAX + 64];
    sidePath(file, side, sizeof(side));
    bool written = !ferror(f);
    if (fclose(f) != 0 || !written) {
        remove(side);
        return false;
    }
    remove(file);  // The card's file system doesn't rename over a file.
    return rename(side, file) == 0;
}

// Reads a file's lines after its header. False if it isn't such a file.
static bool readLines(const char *file, const char *header, bool (*take)(char *line, void *user), void *user)
{
    char side[LIBRARY_PATH_MAX + 64];
    sidePath(file, side, sizeof(side));
    FILE *f = fopen(file, "r");
    if (!f && (f = fopen(side, "r")) != NULL) {  // Stopped between removing the old and renaming the new.
        fclose(f);
        rename(side, file);
        f = fopen(file, "r");
    }
    if (!f)
        return true;  // None yet.
    char *line = malloc(LINE_MAX_BYTES);
    bool ok = line && fgets(line, LINE_MAX_BYTES, f) && !strncmp(line, header, strlen(header));
    while (ok && fgets(line, LINE_MAX_BYTES, f)) {
        if (!strchr(line, '\n') && !feof(f)) {  // Too long: skip the whole line.
            int ch;
            while ((ch = fgetc(f)) != EOF && ch != '\n') {
            }
            continue;
        }
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] && !take(line, user))
            break;
    }
    free(line);
    fclose(f);
    return ok;
}

static bool takeDownload(char *line, void *user)
{
    DownloadList *list = user;
    char *f[LIST_FIELDS];
    int64_t v[9], extra[3] = {0, 0, 0};
    int fields = splitTabs(line, f, LIST_FIELDS);
    if (fields != LIST_FIELDS && fields != LIST_FIELDS_V1)
        return true;
    for (int i = 0; fields == LIST_FIELDS && i < 3; i++)
        if (!readNumber(f[LIST_FIELDS_V1 + i], &extra[i]))
            return true;
    for (int i = 0; i < 9; i++)
        if (!readNumber(f[i], &v[i]))
            return true;
    if (v[0] < 0 || v[0] >= DL_STATE_COUNT)
        return true;
    VideoName name = {.is_episode = v[5] != 0, .season = (int)v[6], .episode = (int)v[7], .year = (int)v[8]};
    snprintf(name.show, sizeof(name.show), "%s", f[12]);
    snprintf(name.title, sizeof(name.title), "%s", f[13]);
    int at = dlListAdd(list, f[9], f[10], &name, v[4] * NS_PER_MS, f[11], v[3]);
    if (at < 0 || (size_t)at != list->count - 1)
        return true;  // A bad line, or a repeat: skipped.
    Download *d = &list->items[at];
    d->state = v[0] == DL_ACTIVE ? DL_QUEUED : (DlState)v[0];
    d->size = v[1];
    d->done = v[2];
    snprintf(d->error, sizeof(d->error), "%s", f[14]);
    d->max_bitrate = extra[0] > 0 ? extra[0] : 0;
    d->parts_done = extra[1] > 0 ? (int)extra[1] : 0;
    d->part_count = extra[2] > 0 ? (int)extra[2] : 0;
    return true;
}

bool dlListLoad(DownloadList *list, const char *file)
{
    list->count = 0;
    bool ok = readLines(file, DL_LIST_HEADER, takeDownload, list);
    if (!ok)
        list->count = 0;
    return ok;
}

bool dlListSave(const DownloadList *list, const char *file)
{
    FILE *f = beginSave(file);
    if (!f)
        return false;
    fprintf(f, "%s\n", DL_LIST_HEADER);
    for (size_t i = 0; i < list->count; i++) {
        const Download *d = &list->items[i];
        char error[DL_ERROR_MAX];
        snprintf(error, sizeof(error), "%s", d->error);
        flatten(error);
        fprintf(f,
                "%d\t%" PRId64 "\t%" PRId64 "\t%" PRId64 "\t%" PRId64 "\t%d\t%d\t%d\t%d\t%s\t%s\t%s\t%s\t%s\t%s\t%lld\t%d\t%d\n",
                (int)d->state, d->size, d->done, d->added, d->runtime_ns / NS_PER_MS, d->name.is_episode ? 1 : 0,
                d->name.season, d->name.episode, d->name.year, d->origin, d->account, d->path, d->name.show,
                d->name.title, error, d->max_bitrate, d->parts_done, d->part_count);
    }
    return endSave(f, file);
}

void dlApplyNames(Library *lib, const DownloadList *list)
{
    for (size_t i = 0; i < list->count; i++) {
        const Download *d = &list->items[i];
        int item = d->state == DL_DONE ? libraryFind(lib, d->path) : -1;
        if (item < 0)
            continue;
        lib->items[item].named = true;
        lib->items[item].name = d->name;
        if (!lib->items[item].runtime_ns)
            lib->items[item].runtime_ns = d->runtime_ns;
    }
}

// --- What waits to be told ---

void dlSyncInit(DlSyncQueue *q)
{
    memset(q, 0, sizeof(*q));
}

void dlSyncFree(DlSyncQueue *q)
{
    free(q->items);
    dlSyncInit(q);
}

int dlSyncFind(const DlSyncQueue *q, const char *account, const char *origin)
{
    for (size_t i = 0; i < q->count; i++)
        if (!strcmp(q->items[i].origin, origin) && !strcmp(q->items[i].account, account))
            return (int)i;
    return -1;
}

void dlSyncRemove(DlSyncQueue *q, int index)
{
    if (index < 0 || (size_t)index >= q->count)
        return;
    memmove(&q->items[index], &q->items[index + 1], (q->count - (size_t)index - 1) * sizeof(DlSyncEvent));
    q->count--;
}

void dlSyncRecord(DlSyncQueue *q, const char *account, const char *origin, int64_t position_ns, bool played,
                  int64_t when)
{
    if (!fitsLine(account, DL_ACCOUNT_MAX) || !fitsLine(origin, LIBRARY_PATH_MAX))
        return;
    int at = dlSyncFind(q, account, origin);
    if (at < 0) {
        if (q->count == q->capacity) {
            size_t grown = q->capacity ? q->capacity * 2 : 16;
            DlSyncEvent *items = realloc(q->items, grown * sizeof(DlSyncEvent));
            if (!items)
                return;
            q->items = items;
            q->capacity = grown;
        }
        at = (int)q->count++;
        snprintf(q->items[at].account, sizeof(q->items[at].account), "%s", account);
        snprintf(q->items[at].origin, sizeof(q->items[at].origin), "%s", origin);
    }
    q->items[at].position_ns = position_ns > 0 ? position_ns : 0;
    q->items[at].played = played;
    q->items[at].when = when;
}

static bool takeEvent(char *line, void *user)
{
    char *f[SYNC_FIELDS];
    int64_t when, position_ms, played;
    if (splitTabs(line, f, SYNC_FIELDS) != SYNC_FIELDS || !readNumber(f[0], &when) ||
        !readNumber(f[1], &position_ms) || !readNumber(f[2], &played))
        return true;
    dlSyncRecord(user, f[3], f[4], position_ms * NS_PER_MS, played != 0, when);
    return true;
}

bool dlSyncLoad(DlSyncQueue *q, const char *file)
{
    q->count = 0;
    bool ok = readLines(file, DL_SYNC_HEADER, takeEvent, q);
    if (!ok)
        q->count = 0;
    return ok;
}

bool dlSyncSave(const DlSyncQueue *q, const char *file)
{
    FILE *f = beginSave(file);
    if (!f)
        return false;
    fprintf(f, "%s\n", DL_SYNC_HEADER);
    for (size_t i = 0; i < q->count; i++) {
        const DlSyncEvent *e = &q->items[i];
        fprintf(f, "%" PRId64 "\t%" PRId64 "\t%d\t%s\t%s\n", e->when, e->position_ns / NS_PER_MS, e->played ? 1 : 0,
                e->account, e->origin);
    }
    return endSave(f, file);
}

bool dlSyncIsNewer(int64_t when, int64_t server_when)
{
    return when > server_when;
}
