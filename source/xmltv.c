#define _GNU_SOURCE  // memmem.
#include "xmltv.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

#include "xmlread.h"

#define ELEMENT_MAX (256u << 10)  // A <programme> longer than this is broken: skipped.
#define INFLATE_CHUNK 65536

typedef struct {
    const char *id;               // Points into ids given (the caller keeps them).
    LiveProgram programs[XMLTV_PER_CHANNEL];
    int count;
} Slot;

struct XmltvReader {
    Slot *slots;                  // One per different id, sorted by id.
    int slot_count;
    int *slot_of;                 // Each channel's slot, -1 for none.
    int channel_count;
    int64_t from, until;
    int matched;
    bool started, gzip, inflating, broken;
    z_stream z;
    char *pending;                // Text not read yet (a <programme> cut by a chunk's end).
    size_t pending_length, pending_capacity;
};

static int byId(const void *a, const void *b)
{
    return strcmp(((const Slot *)a)->id, ((const Slot *)b)->id);
}

XmltvReader *xmltvBegin(const char *const *ids, int count, int64_t from_s, int64_t until_s)
{
    XmltvReader *r = calloc(1, sizeof(XmltvReader));
    if (!r)
        return NULL;
    r->from = from_s;
    r->until = until_s;
    r->channel_count = count;
    r->slots = count > 0 ? calloc((size_t)count, sizeof(Slot)) : NULL;
    r->slot_of = count > 0 ? malloc((size_t)count * sizeof(int)) : NULL;
    if (count > 0 && (!r->slots || !r->slot_of)) {
        xmltvFree(r);
        return NULL;
    }
    for (int i = 0; i < count; i++)
        if (ids[i] && ids[i][0])
            r->slots[r->slot_count++].id = ids[i];
    qsort(r->slots, (size_t)r->slot_count, sizeof(Slot), byId);
    int unique = 0;
    for (int i = 0; i < r->slot_count; i++)
        if (unique == 0 || strcmp(r->slots[unique - 1].id, r->slots[i].id))
            r->slots[unique++] = r->slots[i];
    r->slot_count = unique;
    for (int i = 0; i < count; i++) {
        Slot key = {.id = ids[i] ? ids[i] : ""};
        Slot *found = key.id[0] ? bsearch(&key, r->slots, (size_t)r->slot_count, sizeof(Slot), byId) : NULL;
        r->slot_of[i] = found ? (int)(found - r->slots) : -1;
    }
    return r;
}

void xmltvFree(XmltvReader *r)
{
    if (!r)
        return;
    if (r->inflating)
        inflateEnd(&r->z);
    free(r->slots);
    free(r->slot_of);
    free(r->pending);
    free(r);
}

// --- Times ---

static int64_t daysFromCivil(int64_t y, int m, int d)  // Days since 1970-01-01 (Howard Hinnant's).
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static bool digits(const char *p, int n, int *out)
{
    int v = 0;
    for (int i = 0; i < n; i++) {
        if (!isdigit((unsigned char)p[i]))
            return false;
        v = v * 10 + (p[i] - '0');
    }
    *out = v;
    return true;
}

int64_t xmltvParseTime(const char *text)
{
    int y, mo, d, h = 0, mi = 0, s = 0;
    if (!text || !digits(text, 4, &y) || !digits(text + 4, 2, &mo) || !digits(text + 6, 2, &d))
        return 0;
    const char *p = text + 8;
    if (digits(p, 2, &h) && digits(p + 2, 2, &mi)) {
        p += 4;
        if (digits(p, 2, &s))
            p += 2;
    }
    if (y < 1970 || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 60)
        return 0;
    int64_t t = daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s;
    while (*p == ' ')
        p++;
    int zh, zm;
    if ((*p == '+' || *p == '-') && digits(p + 1, 2, &zh) && digits(p + 3, 2, &zm)) {
        int64_t zone = zh * 3600 + zm * 60;
        t -= *p == '+' ? zone : -zone;
    }
    return t;
}

// --- Programs ---

// Keeps p for slot, by start: the earliest ones when it's full.
static void keep(Slot *slot, const LiveProgram *p)
{
    for (int i = 0; i < slot->count; i++)
        if (slot->programs[i].start == p->start)
            return;  // Said twice (guides merged from several sources).
    int at = slot->count;
    while (at > 0 && slot->programs[at - 1].start > p->start)
        at--;
    if (at >= XMLTV_PER_CHANNEL)
        return;
    int moving = (slot->count < XMLTV_PER_CHANNEL ? slot->count : XMLTV_PER_CHANNEL - 1) - at;
    memmove(&slot->programs[at + 1], &slot->programs[at], (size_t)moving * sizeof(LiveProgram));
    slot->programs[at] = *p;
    if (slot->count < XMLTV_PER_CHANNEL)
        slot->count++;
}

// One <programme ...>...</programme> element.
static void readProgramme(XmltvReader *r, const char *text, size_t length)
{
    XmlReader x;
    xmlBegin(&x, text, length);
    if (xmlNext(&x) != XML_OPEN || strcmp(x.name, "programme"))
        return;
    char channel[128], start[48], stop[48];
    xmlAttr(&x, "channel", channel, sizeof(channel));
    xmlAttr(&x, "start", start, sizeof(start));
    xmlAttr(&x, "stop", stop, sizeof(stop));
    Slot key = {.id = channel};
    Slot *slot = channel[0] ? bsearch(&key, r->slots, (size_t)r->slot_count, sizeof(Slot), byId) : NULL;
    if (!slot)
        return;
    LiveProgram p;
    memset(&p, 0, sizeof(p));
    p.start = xmltvParseTime(start);
    p.end = xmltvParseTime(stop);
    if (!p.start || p.end <= p.start || p.end <= r->from || p.start >= r->until)
        return;
    bool in_title = false;
    size_t title_length = 0;
    for (XmlToken t; (t = xmlNext(&x)) != XML_END;) {
        if (t == XML_OPEN && !strcmp(x.name, "title") && !p.title[0])
            in_title = true;
        else if (t == XML_CLOSE && in_title)
            break;
        else if (t == XML_TEXT && in_title)
            title_length = xmlTextAppend(&x, p.title, sizeof(p.title), title_length);
    }
    r->matched++;
    keep(slot, &p);
}

// Reads the whole <programme> elements in pending, and keeps what may start the next.
static void readPending(XmltvReader *r)
{
    static const char OPEN[] = "<programme", CLOSE[] = "</programme>";
    char *text = r->pending, *end = r->pending + r->pending_length;
    char *at = text;
    for (;;) {
        char *start = memmem(at, (size_t)(end - at), OPEN, sizeof(OPEN) - 1);
        if (!start) {
            // Nothing begins here: keep only a tail that could be the start of "<programme".
            size_t keep_tail = (size_t)(end - at) < sizeof(OPEN) ? (size_t)(end - at) : sizeof(OPEN) - 1;
            at = end - keep_tail;
            break;
        }
        char *stop = memmem(start, (size_t)(end - start), CLOSE, sizeof(CLOSE) - 1);
        if (!stop) {
            at = start;
            if ((size_t)(end - start) > ELEMENT_MAX)
                at = start + sizeof(OPEN) - 1;  // Broken: past it.
            break;
        }
        stop += sizeof(CLOSE) - 1;
        readProgramme(r, start, (size_t)(stop - start));
        at = stop;
    }
    r->pending_length = (size_t)(end - at);
    memmove(r->pending, at, r->pending_length);
}

static bool addText(XmltvReader *r, const char *data, size_t size)
{
    if (r->pending_length + size > r->pending_capacity) {
        size_t capacity = (r->pending_length + size) * 2;
        char *grown = realloc(r->pending, capacity);
        if (!grown)
            return false;
        r->pending = grown;
        r->pending_capacity = capacity;
    }
    memcpy(r->pending + r->pending_length, data, size);
    r->pending_length += size;
    readPending(r);
    return true;
}

bool xmltvFeed(XmltvReader *r, const void *data, size_t size)
{
    if (r->broken)
        return false;
    const unsigned char *bytes = data;
    if (!r->started && size > 0) {
        r->started = true;
        r->gzip = size >= 2 && bytes[0] == 0x1F && bytes[1] == 0x8B;
        if (r->gzip) {
            memset(&r->z, 0, sizeof(r->z));
            if (inflateInit2(&r->z, 16 + MAX_WBITS) != Z_OK) {
                r->broken = true;
                return false;
            }
            r->inflating = true;
        }
    }
    if (!r->gzip) {
        if (!addText(r, data, size))
            r->broken = true;
        return !r->broken;
    }
    r->z.next_in = (Bytef *)bytes;
    r->z.avail_in = (uInt)size;
    char out[INFLATE_CHUNK];
    while (r->z.avail_in > 0) {
        r->z.next_out = (Bytef *)out;
        r->z.avail_out = sizeof(out);
        int rc = inflate(&r->z, Z_NO_FLUSH);
        if (rc != Z_OK && rc != Z_STREAM_END && rc != Z_BUF_ERROR) {
            r->broken = true;
            return false;
        }
        size_t made = sizeof(out) - r->z.avail_out;
        if (made && !addText(r, out, made)) {
            r->broken = true;
            return false;
        }
        if (rc == Z_STREAM_END) {
            // Another gzip member may follow (some guides are several glued together).
            if (r->z.avail_in > 0 && inflateReset(&r->z) != Z_OK) {
                r->broken = true;
                return false;
            }
            if (r->z.avail_in == 0)
                break;
        } else if (rc == Z_BUF_ERROR && made == 0) {
            break;
        }
    }
    return true;
}

const LiveProgram *xmltvPrograms(const XmltvReader *r, int channel, int *count)
{
    int slot = channel >= 0 && channel < r->channel_count ? r->slot_of[channel] : -1;
    *count = slot >= 0 ? r->slots[slot].count : 0;
    return slot >= 0 ? r->slots[slot].programs : NULL;
}

int xmltvMatched(const XmltvReader *r)
{
    return r->matched;
}
