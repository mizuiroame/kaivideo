#include "hlslist.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOCAL_NAME_MAX 48
#define EXTINF "#EXTINF:"
#define NS_PER_S 1e9

bool hlsResolveUrl(const char *base, const char *ref, char *out, size_t size)
{
    int n;
    const char *scheme = strstr(base, "://");
    if (strstr(ref, "://")) {
        n = snprintf(out, size, "%s", ref);
    } else if (!scheme) {
        return false;
    } else if (ref[0] == '/') {
        const char *path = strchr(scheme + 3, '/');
        size_t origin = path ? (size_t)(path - base) : strlen(base);
        n = snprintf(out, size, "%.*s%s", (int)origin, base, ref);
    } else {
        size_t end = strcspn(base, "?#");
        while (end > (size_t)(scheme + 3 - base) && base[end - 1] != '/')
            end--;
        n = snprintf(out, size, "%.*s%s%s", (int)end, base, base[end ? end - 1 : 0] == '/' ? "" : "/", ref);
    }
    return n > 0 && (size_t)n < size;
}

// The line starting at p: where it ends, without its line break.
static size_t lineLength(const char *p)
{
    return strcspn(p, "\r\n");
}

static const char *nextLine(const char *p, size_t length)
{
    p += length;
    p += strspn(p, "\r\n");
    return p;
}

bool hlsFirstEntry(const char *playlist, char *out, size_t size)
{
    for (const char *p = playlist; *p;) {
        size_t n = lineLength(p);
        if (n && p[0] != '#' && n < size) {
            memcpy(out, p, n);
            out[n] = '\0';
            return true;
        }
        p = nextLine(p, n);
    }
    return false;
}

static int countSegments(const char *playlist)
{
    int count = 0;
    for (const char *p = playlist; *p;) {
        size_t n = lineLength(p);
        count += n && p[0] != '#';
        p = nextLine(p, n);
    }
    return count;
}

// Which segment holds start_ns, and where it starts.
static int findStart(const char *playlist, int64_t start_ns, int64_t *base_ns)
{
    int64_t at = 0, length = 0;
    int index = 0;
    *base_ns = 0;
    for (const char *p = playlist; *p;) {
        size_t n = lineLength(p);
        if (!strncmp(p, EXTINF, sizeof(EXTINF) - 1)) {
            length = (int64_t)(strtod(p + sizeof(EXTINF) - 1, NULL) * NS_PER_S);
        } else if (n && p[0] != '#') {
            if (at + length > start_ns) {
                *base_ns = at;
                return index;
            }
            at += length;
            index++;
        }
        p = nextLine(p, n);
    }
    return 0;  // Past the end: from the start.
}

static void addText(HlsPlaylist *out, size_t *used, const char *text, size_t n)
{
    memcpy(out->text + *used, text, n);
    *used += n;
    out->text[(*used)++] = '\n';
}

static bool addSegment(HlsPlaylist *out, const char *line, size_t n, const char *base)
{
    char ref[HLS_URL_MAX], url[HLS_URL_MAX];
    if (n >= sizeof(ref))
        return false;
    memcpy(ref, line, n);
    ref[n] = '\0';
    if (!hlsResolveUrl(base, ref, url, sizeof(url)) || !(out->urls[out->count] = strdup(url)))
        return false;
    out->count++;
    return true;
}

bool hlsPlaylistParse(const char *playlist, const char *base, int64_t start_ns, HlsPlaylist *out)
{
    memset(out, 0, sizeof(*out));
    int total = countSegments(playlist);
    if (total == 0)
        return false;
    out->urls = calloc((size_t)total, sizeof(char *));
    out->starts_ns = calloc((size_t)total, sizeof(int64_t));
    out->text = malloc(strlen(playlist) + (size_t)total * LOCAL_NAME_MAX + 2);
    if (!out->urls || !out->starts_ns || !out->text) {
        hlsPlaylistFree(out);
        return false;
    }
    int first = findStart(playlist, start_ns, &out->base_ns);
    size_t used = 0;
    const char *length_line = NULL;
    size_t length_size = 0;
    int64_t at = 0;
    for (const char *p = playlist; *p;) {
        size_t n = lineLength(p);
        if (!strncmp(p, EXTINF, sizeof(EXTINF) - 1)) {
            length_line = p;  // It goes with the segment after it.
            length_size = n;
        } else if (n && p[0] != '#') {
            int index = out->count;
            if (!addSegment(out, p, n, base)) {
                hlsPlaylistFree(out);
                return false;
            }
            out->starts_ns[index] = at;
            int64_t length = length_line ? (int64_t)(strtod(length_line + sizeof(EXTINF) - 1, NULL) * NS_PER_S) : 0;
            at += length;
            out->end_ns = at;
            if (length > out->longest_ns)
                out->longest_ns = length;
            if (index >= first) {
                if (length_line)
                    addText(out, &used, length_line, length_size);
                used += (size_t)sprintf(out->text + used, HLS_LOCAL_PREFIX "%d.ts\n", index);
            }
            length_line = NULL;
        } else if (n) {
            addText(out, &used, p, n);
        }
        p = nextLine(p, n);
    }
    out->text[used] = '\0';
    out->size = used;
    out->ended = strstr(playlist, "#EXT-X-ENDLIST") != NULL;
    return true;
}

void hlsPlaylistFree(HlsPlaylist *p)
{
    for (int i = 0; p->urls && i < p->count; i++)
        free(p->urls[i]);
    free(p->urls);
    free(p->starts_ns);
    free(p->text);
    memset(p, 0, sizeof(*p));
}

// The last segment starting at or before at_ns.
static int segmentAt(const HlsPlaylist *p, int64_t at_ns)
{
    int low = 0, high = p->count - 1;
    while (low < high) {
        int middle = low + (high - low + 1) / 2;
        if (p->starts_ns[middle] <= at_ns)
            low = middle;
        else
            high = middle - 1;
    }
    return low;
}

void hlsPlaylistSpan(const HlsPlaylist *p, int64_t from_ns, int64_t length_ns, int *first, int *last)
{
    *first = segmentAt(p, from_ns);
    *last = segmentAt(p, from_ns + length_ns - 1);
}

int hlsLocalSegment(const char *url)
{
    size_t prefix = sizeof(HLS_LOCAL_PREFIX) - 1;
    if (strncmp(url, HLS_LOCAL_PREFIX, prefix) != 0)
        return -1;
    char *end;
    long n = strtol(url + prefix, &end, 10);
    return end != url + prefix && n >= 0 && strcmp(end, ".ts") == 0 ? (int)n : -1;
}
