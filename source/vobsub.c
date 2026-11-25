#include "vobsub.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MS_NS 1000000ll
#define SCAN_MAX 4096        // Bytes looked through for a packet's start, past where the index says.
#define PACKETS_MAX 512      // Packets looked at for one picture.
#define SUB_STREAM_BASE 0x20 // A subtitle stream's id in a private stream 1 packet: 0x20 + its number.

// --- The index ---

void vobsubIndexFree(VobsubIndex *index)
{
    free(index->header);
    for (int i = 0; i < index->count; i++)
        free(index->languages[i].entries);
    memset(index, 0, sizeof(*index));
}

// "hh:mm:ss:mmm" (a "-" first for a delay back). False if it isn't one.
static bool readTime(const char *p, int64_t *out)
{
    bool negative = *p == '-';
    if (*p == '-' || *p == '+')
        p++;
    unsigned h, m, s, ms;
    int used = 0;
    if (sscanf(p, "%u:%u:%u:%u%n", &h, &m, &s, &ms, &used) != 4 || used == 0 || m > 59 || s > 59 || ms > 999 ||
        h > 99999)
        return false;
    int64_t ns = (((int64_t)h * 60 + m) * 60 + s) * 1000 * MS_NS + (int64_t)ms * MS_NS;
    *out = negative ? -ns : ns;
    return true;
}

static bool startsWith(const char *line, const char *word)
{
    return !strncmp(line, word, strlen(word));
}

// "id: en, index: 0": a new language. False if there's no room or it isn't one.
static bool addLanguage(VobsubIndex *x, const char *line)
{
    if (x->count >= VOBSUB_LANGUAGES_MAX)
        return false;
    VobsubLanguage *l = &x->languages[x->count];
    memset(l, 0, sizeof(*l));
    const char *p = line + 3;
    while (*p == ' ')
        p++;
    size_t n = 0;
    while (n < sizeof(l->code) - 1 && isalpha((unsigned char)p[n]))
        n++;
    memcpy(l->code, p, n);
    l->code[n] = '\0';
    const char *index = strstr(line, "index:");
    l->stream = index ? atoi(index + 6) : x->count;
    if (l->stream < 0 || l->stream > 31)
        return false;
    x->count++;
    return true;
}

// "timestamp: 00:00:01:234, filepos: 000000000" for the last language.
static void addEntry(VobsubIndex *x, const char *line, int64_t delay_ns, int *capacity)
{
    VobsubLanguage *l = &x->languages[x->count - 1];
    const char *pos = strstr(line, "filepos:");
    int64_t at;
    if (!pos || !readTime(line + 10 + strspn(line + 10, " "), &at) || l->count >= VOBSUB_PICTURES_MAX)
        return;
    char *end;
    unsigned long long filepos = strtoull(pos + 8, &end, 16);
    if (end == pos + 8)
        return;
    if (l->count == *capacity) {
        int grown_size = *capacity ? *capacity * 2 : 256;
        VobsubEntry *grown = realloc(l->entries, (size_t)grown_size * sizeof(VobsubEntry));
        if (!grown)
            return;
        l->entries = grown;
        *capacity = grown_size;
    }
    at += delay_ns;
    l->entries[l->count++] = (VobsubEntry){at > 0 ? at : 0, filepos};
}

static int byTime(const void *a, const void *b)
{
    const VobsubEntry *x = a, *y = b;
    return x->start_ns < y->start_ns ? -1 : x->start_ns > y->start_ns;
}

bool vobsubParseIndex(const char *text, size_t size, VobsubIndex *out)
{
    memset(out, 0, sizeof(*out));
    if (!text || size == 0 || size > VOBSUB_IDX_MAX)
        return false;
    size_t header_end = size;
    int capacity = 0;
    int64_t delay_ns = 0;
    bool skipping = false;  // A language past VOBSUB_LANGUAGES_MAX: its lines are left out.
    char line[512];
    for (size_t at = 0; at < size;) {
        const char *nl = memchr(text + at, '\n', size - at);
        size_t end = nl ? (size_t)(nl - text) : size, n = end - at;
        if (n >= sizeof(line))
            n = sizeof(line) - 1;
        memcpy(line, text + at, n);
        line[n] = '\0';
        line[strcspn(line, "\r")] = '\0';
        if (startsWith(line, "id:")) {
            if (header_end == size)
                header_end = at;
            skipping = !addLanguage(out, line);
            capacity = 0;
            delay_ns = 0;
        } else if (!skipping && out->count > 0 && startsWith(line, "delay:")) {
            int64_t d;
            if (readTime(line + 6 + strspn(line + 6, " "), &d))
                delay_ns += d;
        } else if (!skipping && out->count > 0 && startsWith(line, "timestamp:")) {
            addEntry(out, line, delay_ns, &capacity);
        }
        at = end + 1;
    }
    int kept = 0;
    for (int i = 0; i < out->count; i++) {  // Languages without a picture are left out.
        VobsubLanguage *l = &out->languages[i];
        if (l->count == 0) {
            free(l->entries);
            continue;
        }
        qsort(l->entries, (size_t)l->count, sizeof(VobsubEntry), byTime);
        out->languages[kept++] = *l;
    }
    out->count = kept;
    out->header = malloc(header_end + 1);
    if (!out->header || kept == 0) {
        vobsubIndexFree(out);
        return false;
    }
    memcpy(out->header, text, header_end);
    out->header[header_end] = '\0';
    out->header_size = header_end;
    return true;
}

// --- The .sub's packets ---

static bool startCode(const uint8_t *p, size_t left)
{
    return left >= 4 && p[0] == 0 && p[1] == 0 && p[2] == 1;
}

// Where a private stream 1 packet's data begins (its stream byte), from its header. 0 if it makes no sense.
static size_t payloadStart(const uint8_t *b, size_t packet_end)
{
    size_t at = 6;
    if (at + 3 <= packet_end && (b[at] & 0xC0) == 0x80)  // MPEG-2: flags, then the header's length.
        return at + 3 + b[at + 2];
    while (at < packet_end && b[at] == 0xFF)  // MPEG-1: stuffing, then the times.
        at++;
    if (at < packet_end && (b[at] & 0xC0) == 0x40)
        at += 2;
    if (at >= packet_end)
        return 0;
    if ((b[at] & 0xF0) == 0x20)
        return at + 5;
    if ((b[at] & 0xF0) == 0x30)
        return at + 10;
    return at + 1;
}

size_t vobsubPicture(const uint8_t *sub, size_t size, uint64_t filepos, int stream, uint8_t *out, size_t out_max)
{
    if (!sub || filepos >= size || stream < 0 || stream > 31)
        return 0;
    size_t at = (size_t)filepos, got = 0, total = 0, scanned = 0;
    for (int packets = 0; packets < PACKETS_MAX && at < size; packets++) {
        if (!startCode(sub + at, size - at)) {  // Not on a packet: look a little further.
            if (++scanned > SCAN_MAX)
                return 0;
            at++;
            packets--;
            continue;
        }
        const uint8_t *b = sub + at;
        uint8_t id = b[3];
        if (id == 0xB9)  // The program's end.
            break;
        if (id == 0xBA) {  // A pack header: MPEG-2's or MPEG-1's.
            size_t length = size - at >= 14 && (b[4] & 0xC0) == 0x40 ? 14 + (b[13] & 7) : 12;
            at += length;
            continue;
        }
        if (size - at < 6)
            break;
        size_t packet_end = 6 + ((size_t)b[4] << 8 | b[5]);
        if (packet_end > size - at)
            break;
        size_t data = id == 0xBD ? payloadStart(b, packet_end) : 0;
        if (data && data < packet_end && b[data] == SUB_STREAM_BASE + stream) {
            size_t n = packet_end - data - 1;
            if (got + n > out_max)
                n = out_max - got;
            memcpy(out + got, b + data + 1, n);
            got += n;
            if (total == 0 && got >= 2)
                total = (size_t)out[0] << 8 | out[1];  // The picture's size, its first two bytes.
            if (total && got >= total)
                return total <= out_max ? total : 0;
            if (got == out_max)
                return 0;
        }
        at += packet_end;
    }
    return 0;  // It ended before the picture did.
}

// --- Names ---

bool vobsubIdxFor(const char *video_path, char *out, size_t size)
{
    const char *slash = strrchr(video_path, '/'), *dot = strrchr(video_path, '.');
    size_t stem = dot && (!slash || dot > slash) ? (size_t)(dot - video_path) : strlen(video_path);
    int n = snprintf(out, size, "%.*s.idx", (int)stem, video_path);
    return n > 0 && (size_t)n < size;
}

bool vobsubSubFor(const char *idx_path, char *out, size_t size)
{
    size_t n = strlen(idx_path);
    if (n < 4 || n >= size || strcasecmp(idx_path + n - 4, ".idx"))
        return false;
    memcpy(out, idx_path, n - 3);
    snprintf(out + n - 3, size - (n - 3), "%s", idx_path[n - 3] == 'I' ? "SUB" : "sub");
    return true;
}
