#include "xmlread.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void xmlBegin(XmlReader *r, const char *text, size_t length)
{
    memset(r, 0, sizeof(*r));
    r->at = text;
    r->end = text ? text + length : text;
}

static bool startsWith(const XmlReader *r, const char *s)
{
    size_t n = strlen(s);
    return (size_t)(r->end - r->at) >= n && memcmp(r->at, s, n) == 0;
}

// Moves past the next occurrence of s, or to the end.
static void skipPast(XmlReader *r, const char *s)
{
    size_t n = strlen(s);
    while (r->at < r->end) {
        if ((size_t)(r->end - r->at) >= n && memcmp(r->at, s, n) == 0) {
            r->at += n;
            return;
        }
        r->at++;
    }
}

static bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static bool isNameChar(char c)
{
    return c && !isSpace(c) && c != '>' && c != '/' && c != '=' && c != '<' && c != '"' && c != '\'';
}

static void readName(XmlReader *r)
{
    size_t n = 0;
    while (r->at < r->end && isNameChar(*r->at)) {
        if (n + 1 < sizeof(r->name))
            r->name[n++] = *r->at;
        r->at++;
    }
    r->name[n] = '\0';
}

// The rest of a tag after its name: attributes up to '>' (quotes respected).
static XmlToken readOpen(XmlReader *r)
{
    readName(r);
    r->attrs = r->at;
    char quote = 0;
    while (r->at < r->end && (quote || *r->at != '>')) {
        if (quote && *r->at == quote)
            quote = 0;
        else if (!quote && (*r->at == '"' || *r->at == '\''))
            quote = *r->at;
        r->at++;
    }
    const char *last = r->at;
    if (r->at < r->end)
        r->at++;  // The '>'.
    r->close_next = last > r->attrs && last[-1] == '/';
    r->attrs_length = (size_t)(last - r->attrs) - (r->close_next ? 1 : 0);
    return XML_OPEN;
}

XmlToken xmlNext(XmlReader *r)
{
    r->attrs = NULL;
    r->attrs_length = 0;
    if (r->close_next) {
        r->close_next = false;
        return XML_CLOSE;  // The same name as the open tag just given.
    }
    while (r->at < r->end) {
        if (*r->at != '<') {
            const char *start = r->at;
            while (r->at < r->end && *r->at != '<')
                r->at++;
            r->text = start;
            r->text_length = (size_t)(r->at - start);
            r->text_raw = false;
            return XML_TEXT;
        }
        if (startsWith(r, "<![CDATA[")) {
            r->at += 9;
            const char *start = r->at;
            skipPast(r, "]]>");
            const char *stop = r->at >= start + 3 && memcmp(r->at - 3, "]]>", 3) == 0 ? r->at - 3 : r->at;
            r->text = start;
            r->text_length = (size_t)(stop - start);
            r->text_raw = true;
            return XML_TEXT;
        }
        if (startsWith(r, "<!--")) {
            skipPast(r, "-->");
        } else if (startsWith(r, "<?")) {
            skipPast(r, "?>");
        } else if (startsWith(r, "<!")) {
            skipPast(r, ">");  // A DOCTYPE (internal subsets aren't expected in a feed).
        } else if (startsWith(r, "</")) {
            r->at += 2;
            readName(r);
            skipPast(r, ">");
            return XML_CLOSE;
        } else {
            r->at++;
            return readOpen(r);
        }
    }
    return XML_END;
}

// Appends one Unicode code point as UTF-8.
static size_t putCode(uint32_t code, char *out, size_t size, size_t n)
{
    char bytes[4];
    size_t count;
    if (code == 0 || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))
        code = 0xFFFD;
    if (code < 0x80) {
        bytes[0] = (char)code;
        count = 1;
    } else if (code < 0x800) {
        bytes[0] = (char)(0xC0 | (code >> 6));
        bytes[1] = (char)(0x80 | (code & 0x3F));
        count = 2;
    } else if (code < 0x10000) {
        bytes[0] = (char)(0xE0 | (code >> 12));
        bytes[1] = (char)(0x80 | ((code >> 6) & 0x3F));
        bytes[2] = (char)(0x80 | (code & 0x3F));
        count = 3;
    } else {
        bytes[0] = (char)(0xF0 | (code >> 18));
        bytes[1] = (char)(0x80 | ((code >> 12) & 0x3F));
        bytes[2] = (char)(0x80 | ((code >> 6) & 0x3F));
        bytes[3] = (char)(0x80 | (code & 0x3F));
        count = 4;
    }
    if (n + count >= size)
        return n;  // Doesn't fit: the character is left out, never cut in half.
    memcpy(out + n, bytes, count);
    return n + count;
}

// Decodes an entity at s ("&amp;"...), up to stop. Returns the bytes it took,
// or 0 if it isn't one (the '&' is then kept as it is).
static size_t entity(const char *s, const char *stop, char *out, size_t size, size_t *n)
{
    static const struct { const char *name; char c; } NAMED[] = {
        {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}};
    for (size_t i = 0; i < sizeof(NAMED) / sizeof(NAMED[0]); i++) {
        size_t len = strlen(NAMED[i].name);
        if ((size_t)(stop - s) >= len && memcmp(s, NAMED[i].name, len) == 0) {
            *n = putCode((unsigned char)NAMED[i].c, out, size, *n);
            return len;
        }
    }
    if (stop - s < 4 || s[1] != '#')
        return 0;
    bool hex = s[2] == 'x' || s[2] == 'X';
    const char *p = s + (hex ? 3 : 2);
    uint32_t code = 0;
    int digits = 0;
    while (p < stop && digits < 8) {
        char c = *p;
        int v = c >= '0' && c <= '9' ? c - '0' : hex && c >= 'a' && c <= 'f' ? c - 'a' + 10
                                             : hex && c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (v < 0)
            break;
        code = code * (hex ? 16 : 10) + (uint32_t)v;
        digits++;
        p++;
    }
    if (!digits || p >= stop || *p != ';')
        return 0;
    *n = putCode(code, out, size, *n);
    return (size_t)(p + 1 - s);
}

static size_t decode(const char *s, size_t length, bool raw, char *out, size_t size, size_t n)
{
    if (!size)
        return 0;
    if (n >= size)
        n = size - 1;
    const char *stop = s + length;
    while (s < stop && n + 1 < size) {
        size_t took = !raw && *s == '&' ? entity(s, stop, out, size, &n) : 0;
        if (took) {
            s += took;
            continue;
        }
        out[n++] = *s++;
    }
    // A UTF-8 sequence cut by the size limit is dropped whole.
    size_t keep = n;
    while (keep > 0 && ((unsigned char)out[keep - 1] & 0xC0) == 0x80)
        keep--;
    if (keep > 0 && (unsigned char)out[keep - 1] >= 0xC0) {
        unsigned char lead = (unsigned char)out[keep - 1];
        size_t want = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2;
        if (n - (keep - 1) < want)
            n = keep - 1;
    }
    out[n] = '\0';
    return n;
}

bool xmlAttr(const XmlReader *r, const char *name, char *out, size_t size)
{
    if (size)
        out[0] = '\0';
    const char *p = r->attrs, *stop = r->attrs ? r->attrs + r->attrs_length : NULL;
    size_t want = strlen(name);
    while (p && p < stop) {
        while (p < stop && isSpace(*p))
            p++;
        const char *key = p;
        while (p < stop && isNameChar(*p))
            p++;
        size_t key_length = (size_t)(p - key);
        while (p < stop && isSpace(*p))
            p++;
        if (p >= stop || *p != '=') {
            if (p < stop && key_length == 0)
                p++;  // Something stray: past it.
            continue;
        }
        p++;
        while (p < stop && isSpace(*p))
            p++;
        if (p >= stop || (*p != '"' && *p != '\''))
            continue;
        char quote = *p++;
        const char *value = p;
        while (p < stop && *p != quote)
            p++;
        size_t value_length = (size_t)(p - value);
        if (p < stop)
            p++;
        if (key_length == want && memcmp(key, name, want) == 0) {
            decode(value, value_length, false, out, size, 0);
            return true;
        }
    }
    return false;
}

size_t xmlTextAppend(const XmlReader *r, char *out, size_t size, size_t length)
{
    return decode(r->text, r->text_length, r->text_raw, out, size, length);
}
