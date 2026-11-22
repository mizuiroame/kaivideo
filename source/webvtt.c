#include "webvtt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NS_PER_MS 1000000ll
#define MS_PER_S 1000ll
#define MPEGTS_HZ 90000ll
#define NS_PER_S 1000000000ll
#define TEXT_MAX 1024
#define ARROW "-->"
#define TIMESTAMP_MAP "X-TIMESTAMP-MAP="

// "hh:mm:ss.mmm" or "mm:ss.mmm" at p, in ns; where it ends in *end. False if it isn't one.
static bool readTime(const char *p, const char **end, int64_t *out)
{
    long parts[3];
    int count = 0;
    char *after;
    while (count < 3) {
        parts[count] = strtol(p, &after, 10);
        if (after == p || parts[count] < 0 || parts[count] > 99999)  // No overflow below.
            return false;
        count++;
        p = after;
        if (*p != ':')
            break;
        p++;
    }
    if (*p != '.' || count < 2)
        return false;
    long ms = strtol(p + 1, &after, 10);
    if (after - (p + 1) != 3 || ms < 0)
        return false;
    int64_t hours = count == 3 ? parts[0] : 0, minutes = parts[count - 2], seconds = parts[count - 1];
    *out = ((hours * 60 + minutes) * 60 + seconds) * NS_PER_S + ms * NS_PER_MS;
    *end = after;
    return true;
}

// From X-TIMESTAMP-MAP=LOCAL:00:00:00.000,MPEGTS:900000 (in either order): how
// far the segment's times are from the MPEG-TS clock.
static int64_t mapOffset(const char *line)
{
    const char *local = strstr(line, "LOCAL:"), *mpegts = strstr(line, "MPEGTS:"), *end;
    int64_t local_ns = 0;
    if (local && !readTime(local + sizeof("LOCAL:") - 1, &end, &local_ns))
        local_ns = 0;
    long long ticks = mpegts ? strtoll(mpegts + sizeof("MPEGTS:") - 1, NULL, 10) : 0;
    return ticks * NS_PER_S / MPEGTS_HZ - local_ns;
}

// The line at p (without its break) into out; the next line's start.
static const char *takeLine(const char *p, const char *end, char *out, size_t size)
{
    const char *stop = p;
    while (stop < end && *stop != '\n' && *stop != '\r')
        stop++;
    size_t n = (size_t)(stop - p) < size - 1 ? (size_t)(stop - p) : size - 1;
    memcpy(out, p, n);
    out[n] = '\0';
    if (stop < end && *stop == '\r')
        stop++;
    if (stop < end && *stop == '\n')
        stop++;
    return stop;
}

int webvttAdd(SubTrack *t, const char *data, size_t size, int64_t shift_ns)
{
    const char *p = data, *end = data + size;
    if (size >= 3 && !memcmp(p, "\xEF\xBB\xBF", 3))
        p += 3;  // A BOM.
    if (end - p < 6 || strncmp(p, "WEBVTT", 6))
        return -1;
    char line[TEXT_MAX], text[TEXT_MAX];
    int64_t offset = 0;
    int added = 0;
    p = takeLine(p, end, line, sizeof(line));
    while (p < end) {
        p = takeLine(p, end, line, sizeof(line));
        if (!strncmp(line, TIMESTAMP_MAP, sizeof(TIMESTAMP_MAP) - 1)) {
            offset = mapOffset(line);
            continue;
        }
        const char *arrow = strstr(line, ARROW), *after;
        int64_t start, stop;
        if (!arrow || !readTime(line + strspn(line, " \t"), &after, &start) ||
            !readTime(arrow + sizeof(ARROW) - 1 + strspn(arrow + sizeof(ARROW) - 1, " \t"), &after, &stop))
            continue;  // A cue's name, a note, a style block: not a timing line.
        // Its text: the lines up to a blank one.
        size_t used = 0;
        text[0] = '\0';
        while (p < end) {
            p = takeLine(p, end, line, sizeof(line));
            if (!line[0])
                break;
            used += (size_t)snprintf(text + used, sizeof(text) - used, "%s%s", used ? "\n" : "", line);
            if (used >= sizeof(text) - 1)
                used = sizeof(text) - 1;
        }
        if (text[0] && stop > start && subsAdd(t, start + offset - shift_ns, stop + offset - shift_ns, text))
            added++;
    }
    return added;
}
