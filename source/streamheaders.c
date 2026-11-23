#include "streamheaders.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define VLC_PREFIX "#EXTVLCOPT:"

bool streamHeadersAny(const StreamHeaders *h)
{
    return h && (h->referer[0] || h->user_agent[0] || h->origin[0]);
}

// Printable ASCII only: no line break can start another header.
static bool safeValue(const char *value)
{
    size_t n = strlen(value);
    if (n == 0 || n >= STREAM_HEADER_MAX)
        return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)value[i];
        if (c < 0x20 || c > 0x7E)
            return false;
    }
    return true;
}

static char *slotOf(StreamHeaders *h, const char *name)
{
    if (!strcasecmp(name, "Referer") || !strcasecmp(name, "Referrer"))
        return h->referer;
    if (!strcasecmp(name, "User-Agent"))
        return h->user_agent;
    if (!strcasecmp(name, "Origin"))
        return h->origin;
    return NULL;
}

bool streamHeadersSet(StreamHeaders *h, const char *name, const char *value)
{
    char *slot = name && value ? slotOf(h, name) : NULL;
    if (!slot || !safeValue(value))
        return false;
    snprintf(slot, STREAM_HEADER_MAX, "%s", value);
    return true;
}

// Copies length bytes without the spaces around them. False if it doesn't fit.
static bool copyTrimmed(char *out, size_t size, const char *text, size_t length)
{
    while (length > 0 && isspace((unsigned char)*text)) {
        text++;
        length--;
    }
    while (length > 0 && isspace((unsigned char)text[length - 1]))
        length--;
    if (length >= size)
        return false;
    memcpy(out, text, length);
    out[length] = '\0';
    return true;
}

bool streamHeadersFromVlcOpt(StreamHeaders *h, const char *line, size_t length)
{
    static const struct {
        const char *option, *header;
    } OPTIONS[] = {
        {"http-referrer", "Referer"},
        {"http-referer", "Referer"},
        {"http-user-agent", "User-Agent"},
        {"http-origin", "Origin"},
    };
    size_t prefix = sizeof(VLC_PREFIX) - 1;
    if (length <= prefix || strncasecmp(line, VLC_PREFIX, prefix))
        return false;
    const char *option = line + prefix;
    const char *equals = memchr(option, '=', length - prefix);
    if (!equals)
        return false;
    char name[32], value[STREAM_HEADER_MAX];
    if (!copyTrimmed(name, sizeof(name), option, (size_t)(equals - option)) ||
        !copyTrimmed(value, sizeof(value), equals + 1, (size_t)(line + length - equals - 1)))
        return false;
    for (size_t i = 0; i < sizeof(OPTIONS) / sizeof(OPTIONS[0]); i++)
        if (!strcasecmp(name, OPTIONS[i].option))
            return streamHeadersSet(h, OPTIONS[i].header, value);
    return false;
}

static int hexValue(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    c = (char)tolower((unsigned char)c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

// Decodes %XX in place. A "+" stays one: user agents have them ("Version/17.0+").
static void percentDecode(char *text)
{
    char *out = text;
    for (const char *p = text; *p; p++) {
        int high = p[0] == '%' ? hexValue(p[1]) : -1;
        int low = high >= 0 ? hexValue(p[2]) : -1;
        if (low >= 0) {
            *out++ = (char)(high * 16 + low);
            p += 2;
        } else {
            *out++ = *p;
        }
    }
    *out = '\0';
}

bool streamHeadersFromPipe(StreamHeaders *h, char *url)
{
    char *pipe = strchr(url, '|');
    if (!pipe)
        return false;
    *pipe = '\0';
    for (char *pair = pipe + 1; *pair;) {
        size_t n = strcspn(pair, "&");
        char *next = pair[n] ? pair + n + 1 : pair + n;
        pair[n] = '\0';
        char *equals = strchr(pair, '=');
        if (equals) {
            *equals = '\0';
            char *value = equals + 1;
            percentDecode(value);
            streamHeadersSet(h, pair, value);
        }
        pair = next;
    }
    size_t n = strlen(url);
    while (n > 0 && isspace((unsigned char)url[n - 1]))
        url[--n] = '\0';
    return true;
}

void streamHeadersNames(const StreamHeaders *h, char *out, size_t size)
{
    snprintf(out, size, "%s%s%s%s%s", h->referer[0] ? "Referer" : "",
             h->referer[0] && (h->user_agent[0] || h->origin[0]) ? ", " : "", h->user_agent[0] ? "User-Agent" : "",
             h->user_agent[0] && h->origin[0] ? ", " : "", h->origin[0] ? "Origin" : "");
}
