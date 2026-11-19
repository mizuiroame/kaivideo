#include "hlsmaster.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEDIA_TAG "#EXT-X-MEDIA:"
#define STREAM_INF "#EXT-X-STREAM-INF:"
#define GROUP_MAX 64
#define LOCAL_AUDIO_GROUP "a"
#define LOCAL_SUBTITLES_GROUP "s"

static size_t lineLength(const char *p)
{
    return strcspn(p, "\r\n");
}

static const char *nextLine(const char *p, size_t length)
{
    p += length;
    return p + strspn(p, "\r\n");
}

// The value of an attribute of a tag line (quoted or not), without quotes
// and control characters. False if the line hasn't it.
static bool attribute(const char *line, size_t n, const char *key, char *out, size_t size)
{
    const char *end = line + n, *p = memchr(line, ':', n);
    size_t key_length = strlen(key);
    for (p = p ? p + 1 : end; p < end;) {
        const char *equals = memchr(p, '=', (size_t)(end - p));
        if (!equals)
            return false;
        bool match = (size_t)(equals - p) == key_length && !strncmp(p, key, key_length);
        const char *value = equals + 1, *value_end;
        if (value < end && *value == '"') {
            value++;
            value_end = memchr(value, '"', (size_t)(end - value));
            if (!value_end)
                value_end = end;
            p = value_end < end ? value_end + 1 : end;
            const char *comma = memchr(p, ',', (size_t)(end - p));  // Anything stray before the next one is skipped.
            p = comma ? comma + 1 : end;
        } else {
            value_end = memchr(value, ',', (size_t)(end - value));
            if (!value_end)
                value_end = end;
            p = value_end < end ? value_end + 1 : end;
        }
        if (match) {
            size_t used = 0;
            for (const char *c = value; c < value_end && used + 1 < size; c++)
                if ((unsigned char)*c >= ' ' && *c != '"' && *c != '\\')
                    out[used++] = *c;
            out[used] = '\0';
            return true;
        }
    }
    return false;
}

// The STREAM-INF line of the variant hlsBestVariant chose: its groups and codecs.
static void readVariantLine(const char *playlist, const char *uri, HlsMaster *out, char *audio, char *subtitles)
{
    const char *inf = NULL;
    size_t inf_length = 0;
    for (const char *p = playlist; *p;) {
        size_t n = lineLength(p);
        if (!strncmp(p, STREAM_INF, sizeof(STREAM_INF) - 1)) {
            inf = p;
            inf_length = n;
        } else if (inf && n && p[0] != '#') {
            if (n == strlen(uri) && !strncmp(p, uri, n))
                break;
            inf = NULL;
        }
        p = nextLine(p, n);
    }
    if (!inf)
        return;
    char number[32];
    if (attribute(inf, inf_length, "BANDWIDTH", number, sizeof(number)))
        out->bandwidth = strtoll(number, NULL, 10);
    attribute(inf, inf_length, "CODECS", out->codecs, sizeof(out->codecs));
    attribute(inf, inf_length, "AUDIO", audio, GROUP_MAX);
    attribute(inf, inf_length, "SUBTITLES", subtitles, GROUP_MAX);
}

// Adds an #EXT-X-MEDIA line's rendition if it's of the variant's groups.
static void addRendition(const char *line, size_t n, const char *base, const char *audio, const char *subtitles,
                         HlsMaster *out)
{
    char type[16], group[GROUP_MAX], uri[HLS_URL_MAX] = "", flag[8] = "";
    if (out->count >= HLS_RENDITIONS_MAX || !attribute(line, n, "TYPE", type, sizeof(type)) ||
        !attribute(line, n, "GROUP-ID", group, sizeof(group)))
        return;
    bool is_audio = !strcmp(type, "AUDIO") && audio[0] && !strcmp(group, audio);
    bool is_subtitles = !strcmp(type, "SUBTITLES") && subtitles[0] && !strcmp(group, subtitles);
    if (!is_audio && !is_subtitles)
        return;
    HlsRendition *r = &out->renditions[out->count];
    memset(r, 0, sizeof(*r));
    r->type = is_audio ? HLS_RENDITION_AUDIO : HLS_RENDITION_SUBTITLES;
    attribute(line, n, "URI", uri, sizeof(uri));
    if (uri[0]) {
        if (out->list_count >= HLS_LISTS_MAX || !hlsResolveUrl(base, uri, r->url, sizeof(r->url)))
            return;
        r->list = out->list_count++;
    } else if (is_subtitles) {
        return;  // Subtitles are always a list of their own.
    }
    attribute(line, n, "NAME", r->name, sizeof(r->name));
    attribute(line, n, "LANGUAGE", r->language, sizeof(r->language));
    r->is_default = attribute(line, n, "DEFAULT", flag, sizeof(flag)) && !strcmp(flag, "YES");
    out->count++;
}

bool hlsMasterParse(const char *playlist, const char *base, long long max_bitrate, HlsMaster *out)
{
    memset(out, 0, sizeof(*out));
    char uri[HLS_URL_MAX], audio[GROUP_MAX] = "", subtitles[GROUP_MAX] = "";
    if (!hlsBestVariant(playlist, max_bitrate, uri, sizeof(uri)) ||
        !hlsResolveUrl(base, uri, out->variant_url, sizeof(out->variant_url)))
        return false;
    readVariantLine(playlist, uri, out, audio, subtitles);
    out->list_count = 1;
    for (const char *p = playlist; *p;) {
        size_t n = lineLength(p);
        if (!strncmp(p, MEDIA_TAG, sizeof(MEDIA_TAG) - 1))
            addRendition(p, n, base, audio, subtitles, out);
        p = nextLine(p, n);
    }
    return true;
}

const char *hlsMasterListUrl(const HlsMaster *m, int list)
{
    if (list == 0)
        return m->variant_url;
    for (int i = 0; i < m->count; i++)
        if (m->renditions[i].list == list)
            return m->renditions[i].url;
    return "";
}

bool hlsMasterListIsSubtitles(const HlsMaster *m, int list)
{
    for (int i = 0; list > 0 && i < m->count; i++)
        if (m->renditions[i].list == list)
            return m->renditions[i].type == HLS_RENDITION_SUBTITLES;
    return false;
}

// Appends to out; false once it no longer fits.
static bool put(char *out, size_t size, size_t *used, const char *format, ...)
    __attribute__((format(printf, 4, 5)));

static bool put(char *out, size_t size, size_t *used, const char *format, ...)
{
    if (*used >= size)
        return false;
    va_list args;
    va_start(args, format);
    int n = vsnprintf(out + *used, size - *used, format, args);
    va_end(args);
    if (n < 0 || (size_t)n >= size - *used)
        return false;
    *used += (size_t)n;
    return true;
}

bool hlsLocalMaster(const HlsMaster *m, char *out, size_t size)
{
    size_t used = 0;
    bool ok = put(out, size, &used, "#EXTM3U\n");
    bool has_audio = false, has_subtitles = false;
    for (int i = 0; ok && i < m->count; i++) {
        const HlsRendition *r = &m->renditions[i];
        bool audio = r->type == HLS_RENDITION_AUDIO;
        has_audio = has_audio || audio;
        has_subtitles = has_subtitles || !audio;
        ok = put(out, size, &used, "#EXT-X-MEDIA:TYPE=%s,GROUP-ID=\"%s\",NAME=\"%s\"", audio ? "AUDIO" : "SUBTITLES",
                 audio ? LOCAL_AUDIO_GROUP : LOCAL_SUBTITLES_GROUP, r->name);
        if (ok && r->language[0])
            ok = put(out, size, &used, ",LANGUAGE=\"%s\"", r->language);
        // Sound: the default plays; subtitles: none shows until chosen.
        if (ok)
            ok = put(out, size, &used, ",DEFAULT=%s,AUTOSELECT=%s", audio && r->is_default ? "YES" : "NO",
                     audio ? "YES" : "NO");
        if (ok && r->list)
            ok = put(out, size, &used, ",URI=\"" HLS_LOCAL_PREFIX "%d/index.m3u8\"", r->list);
        if (ok)
            ok = put(out, size, &used, "\n");
    }
    if (ok)
        ok = put(out, size, &used, "#EXT-X-STREAM-INF:BANDWIDTH=%lld", m->bandwidth > 0 ? m->bandwidth : 1);
    if (ok && m->codecs[0])
        ok = put(out, size, &used, ",CODECS=\"%s\"", m->codecs);
    if (ok && has_audio)
        ok = put(out, size, &used, ",AUDIO=\"" LOCAL_AUDIO_GROUP "\"");
    if (ok && has_subtitles)
        ok = put(out, size, &used, ",SUBTITLES=\"" LOCAL_SUBTITLES_GROUP "\"");
    if (ok)
        ok = put(out, size, &used, "\n" HLS_LOCAL_PREFIX "0/index.m3u8\n");
    return ok;
}
