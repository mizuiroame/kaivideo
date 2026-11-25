#include "hlslist.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define LOCAL_NAME_MAX 48
#define EXTINF "#EXTINF:"
#define MEDIA_SEQUENCE "#EXT-X-MEDIA-SEQUENCE:"
#define STREAM_INF "#EXT-X-STREAM-INF:"
#define NS_PER_S 1e9
#define HEADER_ROOM 256  // With an fMP4 list's #EXT-X-MAP line.
#define EXTINF_ROOM 32
#define CODECS_MAX 128
#define MAX_HEIGHT 1088  // 1080p, coded as 1088.

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

// The length of url's "scheme://host:port", or 0 if it has no "://" or has a user part.
static size_t originLength(const char *url)
{
    const char *scheme = strstr(url, "://");
    if (!scheme)
        return 0;
    const char *host = scheme + 3;
    size_t n = strcspn(host, "/?#");
    if (n == 0 || memchr(host, '@', n))
        return 0;
    return (size_t)(host - url) + n;
}

bool hlsSameOrigin(const char *a, const char *b)
{
    size_t n = originLength(a);
    return n > 0 && n == originLength(b) && strncasecmp(a, b, n) == 0;
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

// A segment's local name, with its line break: "file:kaivideo/12.ts", or
// "file:kaivideo/2/12.vtt" in a master list's list 2. How long it is.
static int localName(const HlsPlaylist *p, int index, char *out, size_t size)
{
    if (p->list < 0)
        return snprintf(out, size, HLS_LOCAL_PREFIX "%d.ts\n", index);
    return snprintf(out, size, HLS_LOCAL_PREFIX "%d/%d.%s\n", p->list, index, p->subtitles ? "vtt" : "ts");
}

// The #EXT-X-MAP line naming the list's initialization section locally, with its line break.
static int mapLine(const HlsPlaylist *p, char *out, size_t size)
{
    if (p->list < 0)
        return snprintf(out, size, "#EXT-X-MAP:URI=\"" HLS_LOCAL_PREFIX "init.mp4\"\n");
    return snprintf(out, size, "#EXT-X-MAP:URI=\"" HLS_LOCAL_PREFIX "%d/init.mp4\"\n", p->list);
}

// The URI of an #EXT-X-MAP line (n long), resolved against base; NULL if it has none.
static char *mapUrl(const char *line, size_t n, const char *base)
{
    const char *uri = NULL;
    for (const char *q = line; q + 5 <= line + n; q++)
        if (!strncmp(q, "URI=\"", 5)) {
            uri = q + 5;
            break;
        }
    const char *end = uri ? memchr(uri, '"', (size_t)(line + n - uri)) : NULL;
    char ref[HLS_URL_MAX], url[HLS_URL_MAX];
    if (!end || (size_t)(end - uri) >= sizeof(ref))
        return NULL;
    memcpy(ref, uri, (size_t)(end - uri));
    ref[end - uri] = '\0';
    return hlsResolveUrl(base, ref, url, sizeof(url)) ? strdup(url) : NULL;
}

bool hlsPlaylistComplete(const char *playlist)
{
    return strstr(playlist, "#EXT-X-ENDLIST") || strstr(playlist, "#EXT-X-PLAYLIST-TYPE:VOD");
}

bool hlsPlaylistParse(const char *playlist, const char *base, int64_t start_ns, HlsPlaylist *out)
{
    return hlsPlaylistParseList(playlist, base, start_ns, -1, false, out);
}

bool hlsPlaylistParseList(const char *playlist, const char *base, int64_t start_ns, int list, bool subtitles,
                          HlsPlaylist *out)
{
    memset(out, 0, sizeof(*out));
    out->list = list;
    out->subtitles = subtitles;
    int total = countSegments(playlist);
    if (total == 0)
        return false;
    out->urls = calloc((size_t)total, sizeof(char *));
    out->starts_ns = calloc((size_t)total, sizeof(int64_t));
    out->text = malloc(strlen(playlist) + (size_t)total * LOCAL_NAME_MAX + HEADER_ROOM);
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
                used += (size_t)localName(out, index, out->text + used, LOCAL_NAME_MAX);
            }
            length_line = NULL;
        } else if (n > 11 && !strncmp(p, "#EXT-X-MAP:", 11)) {  // fMP4: KaiVideo serves it too.
            if (!out->map_url)
                out->map_url = mapUrl(p, n, base);
            if (out->map_url)
                used += (size_t)mapLine(out, out->text + used, HEADER_ROOM);
        } else if (n) {
            addText(out, &used, p, n);
        }
        p = nextLine(p, n);
    }
    out->text[used] = '\0';
    out->size = used;
    out->ended = hlsPlaylistComplete(playlist);
    const char *sequence = strstr(playlist, MEDIA_SEQUENCE);
    out->sequence = sequence ? strtoll(sequence + sizeof(MEDIA_SEQUENCE) - 1, NULL, 10) : 0;
    out->first = first;
    return true;
}

static int64_t segmentLength(const HlsPlaylist *p, int i)
{
    return (i + 1 < p->count ? p->starts_ns[i + 1] : p->end_ns) - p->starts_ns[i];
}

// The renamed list again, from p->first, after segments were added.
static bool rebuildText(HlsPlaylist *p)
{
    size_t room = HEADER_ROOM + (size_t)(p->count - p->first) * (LOCAL_NAME_MAX + EXTINF_ROOM);
    char *text = malloc(room);
    if (!text)
        return false;
    int target = (int)((p->longest_ns + (int64_t)NS_PER_S - 1) / (int64_t)NS_PER_S);
    size_t used = (size_t)snprintf(text, room,
                                   "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:%d\n" MEDIA_SEQUENCE "%lld\n",
                                   target > 0 ? target : 1, (long long)(p->sequence + p->first));
    if (p->map_url)
        used += (size_t)mapLine(p, text + used, room - used);
    for (int i = p->first; i < p->count; i++) {
        used += (size_t)snprintf(text + used, room - used, EXTINF "%.3f,\n", segmentLength(p, i) / NS_PER_S);
        used += (size_t)localName(p, i, text + used, room - used);
    }
    if (p->ended)
        used += (size_t)snprintf(text + used, room - used, "#EXT-X-ENDLIST\n");
    free(p->text);
    p->text = text;
    p->size = used;
    return true;
}

// Makes room for more segments in p. False if there's none.
static bool grow(HlsPlaylist *p, int more)
{
    char **urls = realloc(p->urls, (size_t)(p->count + more) * sizeof(char *));
    if (!urls)
        return false;
    p->urls = urls;
    int64_t *starts = realloc(p->starts_ns, (size_t)(p->count + more) * sizeof(int64_t));
    if (!starts)
        return false;
    p->starts_ns = starts;
    return true;
}

// Only the segments p hasn't got are read: a channel's list can hold hours of them.
int hlsPlaylistAppend(HlsPlaylist *p, const char *playlist, const char *base)
{
    int total = countSegments(playlist);
    if (total == 0)
        return -1;
    const char *tag = strstr(playlist, MEDIA_SEQUENCE);
    int64_t sequence = tag ? strtoll(tag + sizeof(MEDIA_SEQUENCE) - 1, NULL, 10) : 0;
    int64_t have = p->sequence + p->count;  // The sequence number the next new segment has.
    int start = sequence + total > have ? (int)(have > sequence ? have - sequence : 0) : total;
    int added = total - start;
    bool was_ended = p->ended;
    p->ended = hlsPlaylistComplete(playlist);
    if (added && !grow(p, added))
        return -1;
    // A gap (the list slid past segments never read): numbering goes on from the new ones.
    if (added && sequence + start > have)
        p->sequence = sequence + start - p->count;
    int64_t length = 0;
    int index = 0;
    for (const char *line = playlist; *line;) {
        size_t n = lineLength(line);
        if (!strncmp(line, EXTINF, sizeof(EXTINF) - 1)) {
            length = (int64_t)(strtod(line + sizeof(EXTINF) - 1, NULL) * NS_PER_S);
        } else if (n && line[0] != '#') {
            if (index++ >= start) {
                int at = p->count;
                if (!addSegment(p, line, n, base))
                    return -1;
                p->starts_ns[at] = p->end_ns;
                p->end_ns += length;
                if (length > p->longest_ns)
                    p->longest_ns = length;
            }
            length = 0;
        }
        line = nextLine(line, n);
    }
    if ((added || p->ended != was_ended) && !rebuildText(p))
        return -1;
    return added;
}

bool hlsPlaylistTrim(HlsPlaylist *p, int first)
{
    if (first > p->count)
        first = p->count;
    if (first <= p->first)
        return true;
    p->first = first;
    return rebuildText(p);
}

// --- Master lists ---

typedef struct {
    long long bandwidth;
    int height;          // 0 if not said.
    bool hevc, sound_only;
    const char *uri;
    size_t uri_length;
} Variant;

static long long attributeNumber(const char *line, size_t n, const char *name)
{
    const char *at = strstr(line, name);
    return at && at < line + n ? strtoll(at + strlen(name), NULL, 10) : 0;
}

// Fills v from an #EXT-X-STREAM-INF line.
static void readVariant(const char *line, size_t n, Variant *v)
{
    v->bandwidth = attributeNumber(line, n, "BANDWIDTH=");
    const char *resolution = strstr(line, "RESOLUTION=");
    if (resolution && resolution < line + n) {
        const char *x = strchr(resolution, 'x');
        v->height = x && x < line + n ? atoi(x + 1) : 0;
    }
    char codecs[CODECS_MAX] = "";
    const char *c = strstr(line, "CODECS=\"");
    if (c && c < line + n) {
        c += sizeof("CODECS=\"") - 1;
        size_t length = strcspn(c, "\"\r\n");
        snprintf(codecs, sizeof(codecs), "%.*s", (int)(length < sizeof(codecs) ? length : sizeof(codecs) - 1), c);
    }
    v->hevc = strstr(codecs, "hvc1") || strstr(codecs, "hev1") || strstr(codecs, "dvh");
    bool picture = v->hevc || strstr(codecs, "avc") || strstr(codecs, "vp09") || strstr(codecs, "av01");
    v->sound_only = codecs[0] && !picture;
}

// Whether a is the better choice of the two, as hlsBestVariant says.
static bool better(const Variant *a, const Variant *b, long long max_bitrate)
{
    bool fits_a = a->height <= MAX_HEIGHT, fits_b = b->height <= MAX_HEIGHT;
    if (fits_a != fits_b)
        return fits_a;
    if (a->sound_only != b->sound_only)
        return !a->sound_only;
    if (a->hevc != b->hevc)
        return !a->hevc;
    bool within_a = max_bitrate <= 0 || a->bandwidth <= max_bitrate;
    bool within_b = max_bitrate <= 0 || b->bandwidth <= max_bitrate;
    if (within_a != within_b)
        return within_a;
    return within_a ? a->bandwidth > b->bandwidth : a->bandwidth < b->bandwidth;
}

bool hlsBestVariant(const char *playlist, long long max_bitrate, char *out, size_t size)
{
    Variant best = {0}, here = {0};
    bool found = false, pending = false;
    for (const char *p = playlist; *p;) {
        size_t n = lineLength(p);
        if (!strncmp(p, STREAM_INF, sizeof(STREAM_INF) - 1)) {
            here = (Variant){0};
            readVariant(p, n, &here);
            pending = true;
        } else if (pending && n && p[0] != '#') {
            here.uri = p;
            here.uri_length = n;
            if (!found || better(&here, &best, max_bitrate))
                best = here;
            found = true;
            pending = false;
        }
        p = nextLine(p, n);
    }
    if (!found || best.uri_length >= size)
        return false;
    memcpy(out, best.uri, best.uri_length);
    out[best.uri_length] = '\0';
    return true;
}

bool hlsMayFetch(size_t held, size_t largest, size_t max, bool own)
{
    return own || (held <= max && largest <= max - held);
}

void hlsPlaylistFree(HlsPlaylist *p)
{
    for (int i = 0; p->urls && i < p->count; i++)
        free(p->urls[i]);
    free(p->urls);
    free(p->starts_ns);
    free(p->text);
    free(p->map_url);
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

bool hlsLocalName(const char *url, int *list, int *index)
{
    size_t prefix = sizeof(HLS_LOCAL_PREFIX) - 1;
    if (strncmp(url, HLS_LOCAL_PREFIX, prefix) != 0)
        return false;
    if (!strcmp(url + prefix, "init.mp4")) {  // A list alone's initialization section.
        *list = 0;
        *index = HLS_INIT_INDEX;
        return true;
    }
    char *end;
    long first = strtol(url + prefix, &end, 10);
    if (end == url + prefix || first < 0)
        return false;
    if (!strcmp(end, ".ts")) {  // A list alone: "12.ts".
        *list = 0;
        *index = (int)first;
        return true;
    }
    if (*end != '/')
        return false;
    const char *rest = end + 1;
    if (!strcmp(rest, "index.m3u8")) {
        *list = (int)first;
        *index = -1;
        return true;
    }
    if (!strcmp(rest, "init.mp4")) {
        *list = (int)first;
        *index = HLS_INIT_INDEX;
        return true;
    }
    long second = strtol(rest, &end, 10);
    if (end == rest || second < 0 || (strcmp(end, ".ts") && strcmp(end, ".vtt")))
        return false;
    *list = (int)first;
    *index = (int)second;
    return true;
}
