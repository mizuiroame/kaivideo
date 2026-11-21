#include "dav.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define HTTP "http://"
#define HTTPS "https://"
#define LISTING_GROWTH 64
#define HREF_MAX 4096  // A 255-byte name in CJK is ~765 characters percent-encoded, plus its folders.

bool davIsLocation(const char *path)
{
    return path && !strncmp(path, DAV_PREFIX, sizeof(DAV_PREFIX) - 1);
}

static size_t schemeLength(const char *s)
{
    if (!strncasecmp(s, HTTP, sizeof(HTTP) - 1))
        return sizeof(HTTP) - 1;
    if (!strncasecmp(s, HTTPS, sizeof(HTTPS) - 1))
        return sizeof(HTTPS) - 1;
    return 0;
}

bool davNormalizeAddress(const char *typed, char *out, size_t size)
{
    while (*typed == ' ')
        typed++;
    size_t length = strlen(typed);
    while (length && typed[length - 1] == ' ')
        length--;
    if (!length || memchr(typed, ' ', length))
        return false;
    size_t scheme = schemeLength(typed);
    if (!scheme && strstr(typed, "://"))  // Only spaces follow length.
        return false;  // smb://, ftp://...: not this.
    if (scheme == length)
        return false;
    size_t authority = strcspn(typed + scheme, "/");
    if (memchr(typed + scheme, '@', authority < length - scheme ? authority : length - scheme))
        return false;  // "user:password@host": shown everywhere the address is.
    bool slash = typed[length - 1] == '/';
    int n = snprintf(out, size, "%s%.*s%s", scheme ? "" : HTTP, (int)length, typed, slash ? "" : "/");
    return n > 0 && (size_t)n < size;
}

// The scheme, host and port: "http://pc:8098".
static size_t serverLength(const char *address)
{
    size_t scheme = schemeLength(address);
    return scheme + strcspn(address + scheme, "/");
}

bool davSameServer(const char *a, const char *b)
{
    size_t n = serverLength(a);
    return n == serverLength(b) && !strncasecmp(a, b, n);
}

void davShownAddress(const char *address, char *out, size_t size)
{
    const char *s = !strncasecmp(address, HTTP, sizeof(HTTP) - 1) ? address + sizeof(HTTP) - 1 : address;
    size_t length = strlen(s);
    if (length && s[length - 1] == '/')
        length--;
    snprintf(out, size, "%.*s", (int)length, s);
}

// Letters, digits, "-._~" and '/' stay; every other byte is %XX.
static bool keeps(unsigned char c)
{
    return isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~' || c == '/';
}

bool davUrlOf(const char *address, const char *location, char *out, size_t size)
{
    if (!davIsLocation(location))
        return false;
    const char *path = location + sizeof(DAV_PREFIX) - 1;
    size_t n = (size_t)snprintf(out, size, "%s", address);
    if (n >= size)
        return false;
    for (const unsigned char *p = (const unsigned char *)path; *p; p++) {
        if (n + 4 > size)
            return false;
        if (keeps(*p))
            out[n++] = (char)*p;
        else
            n += (size_t)snprintf(out + n, 4, "%%%02X", *p);
    }
    out[n] = '\0';
    return true;
}

static const char BASE64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static bool base64(const unsigned char *in, size_t length, char *out, size_t size)
{
    if ((length + 2) / 3 * 4 + 1 > size)
        return false;
    size_t o = 0;
    for (size_t i = 0; i < length; i += 3) {
        unsigned v = (unsigned)in[i] << 16 | (i + 1 < length ? (unsigned)in[i + 1] << 8 : 0) |
                     (i + 2 < length ? in[i + 2] : 0);
        out[o++] = BASE64[v >> 18 & 63];
        out[o++] = BASE64[v >> 12 & 63];
        out[o++] = i + 1 < length ? BASE64[v >> 6 & 63] : '=';
        out[o++] = i + 2 < length ? BASE64[v & 63] : '=';
    }
    out[o] = '\0';
    return true;
}

bool davAuthorization(const DavAccount *a, char *out, size_t size)
{
    if (!size)
        return false;
    out[0] = '\0';
    if (!a->user[0])
        return true;
    char pair[DAV_TEXT_MAX * 2 + 2];
    int length = snprintf(pair, sizeof(pair), "%s:%s", a->user, a->password);
    static const char BASIC[] = "Basic ";
    bool ok = length > 0 && size > sizeof(BASIC) - 1 &&
              base64((const unsigned char *)pair, (size_t)length, out + sizeof(BASIC) - 1, size - (sizeof(BASIC) - 1));
    if (ok)
        memcpy(out, BASIC, sizeof(BASIC) - 1);
    else
        out[0] = '\0';
    memset(pair, 0, sizeof(pair));
    return ok;
}

// --- The account's file ---

static void readValue(char *out, size_t size, const char *value, size_t length)
{
    snprintf(out, size, "%.*s", (int)(length < size ? length : size - 1), value);
}

void davAccountRead(DavAccount *a, const char *text)
{
    memset(a, 0, sizeof(*a));
    for (const char *line = text; *line;) {
        size_t length = strcspn(line, "\r\n");
        const char *eq = memchr(line, '=', length);
        if (eq) {
            size_t key = (size_t)(eq - line), value = length - key - 1;
            if (key == 7 && !strncmp(line, "address", 7))
                readValue(a->address, sizeof(a->address), eq + 1, value);
            else if (key == 4 && !strncmp(line, "user", 4))
                readValue(a->user, sizeof(a->user), eq + 1, value);
            else if (key == 8 && !strncmp(line, "password", 8))
                readValue(a->password, sizeof(a->password), eq + 1, value);
        }
        line += length;
        line += strspn(line, "\r\n");
    }
    char address[DAV_ADDRESS_MAX];
    if (a->address[0] && davNormalizeAddress(a->address, address, sizeof(address)))
        memcpy(a->address, address, sizeof(address));
    else
        a->address[0] = '\0';
}

bool davAccountWrite(const DavAccount *a, char *out, size_t size)
{
    if (strpbrk(a->address, "\r\n") || strpbrk(a->user, "\r\n") || strpbrk(a->password, "\r\n"))
        return false;
    int n = snprintf(out, size, "address=%s\nuser=%s\npassword=%s\n", a->address, a->user, a->password);
    return n > 0 && (size_t)n < size;
}

// --- Dates ---

static int64_t daysFromCivil(int64_t y, int m, int d)  // Days since 1970-01-01 (Howard Hinnant's).
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

int64_t davParseDate(const char *text)
{
    static const char MONTHS[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *comma = strchr(text, ',');
    char month[4];
    int day, year, hour, minute, second;
    if (!comma || sscanf(comma + 1, " %d %3s %d %d:%d:%d", &day, month, &year, &hour, &minute, &second) != 6)
        return 0;
    const char *at = strstr(MONTHS, month);
    if (strlen(month) != 3 || !at || (at - MONTHS) % 3 || day < 1 || day > 31 || year < 1970 || year > 9999 ||
        hour > 23 || minute > 59 || second > 60 || hour < 0 || minute < 0 || second < 0)
        return 0;
    int m = (int)(at - MONTHS) / 3 + 1;
    return daysFromCivil(year, m, day) * 86400 + hour * 3600 + minute * 60 + second;
}

// --- The listing ---

typedef struct {
    const char *at;  // Where the next tag is looked for.
} Scanner;

typedef struct {
    const char *name;  // Its local name (no prefix).
    size_t name_length;
    bool closing, empty;  // "</x>", "<x/>".
    const char *after;    // Just after '>'.
} Tag;

static bool nextTag(Scanner *s, Tag *t)
{
    const char *open;
    while ((open = strchr(s->at, '<')) != NULL) {
        const char *close = strchr(open, '>');
        if (!close)
            return false;
        s->at = close + 1;
        if (open[1] == '?' || open[1] == '!')
            continue;  // <?xml ?>, comments.
        t->closing = open[1] == '/';
        const char *name = open + 1 + t->closing;
        size_t length = strcspn(name, " \t\r\n/>");
        const char *colon = memchr(name, ':', length);
        if (colon) {
            length -= (size_t)(colon + 1 - name);
            name = colon + 1;
        }
        t->name = name;
        t->name_length = length;
        t->empty = close[-1] == '/';
        t->after = close + 1;
        return true;
    }
    return false;
}

static bool tagIs(const Tag *t, const char *name)
{
    return t->name_length == strlen(name) && !strncasecmp(t->name, name, t->name_length);
}

// The text up to the next '<', with XML's entities undone. False if it didn't fit.
static bool tagText(const char *from, char *out, size_t size)
{
    size_t n = 0;
    while (*from && *from != '<' && n + 1 < size) {
        static const struct {
            const char *entity;
            char c;
        } ENTITIES[] = {{"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}};
        bool done = false;
        for (size_t i = 0; i < sizeof(ENTITIES) / sizeof(ENTITIES[0]) && !done; i++) {
            size_t length = strlen(ENTITIES[i].entity);
            if (!strncmp(from, ENTITIES[i].entity, length)) {
                out[n++] = ENTITIES[i].c;
                from += length;
                done = true;
            }
        }
        if (!done && from[0] == '&' && from[1] == '#') {  // &#38; &#x26;: one byte is all paths need here.
            char *end;
            long c = from[2] == 'x' ? strtol(from + 3, &end, 16) : strtol(from + 2, &end, 10);
            if (*end == ';' && c > 0 && c < 128) {
                out[n++] = (char)c;
                from = end + 1;
                done = true;
            }
        }
        if (!done)
            out[n++] = *from++;
    }
    out[n] = '\0';
    return *from == '<' || *from == '\0';
}

static int hexValue(char c)
{
    return isdigit((unsigned char)c) ? c - '0' : isxdigit((unsigned char)c) ? (tolower((unsigned char)c) - 'a' + 10) : -1;
}

// False if it decodes a NUL (the name would end there).
static bool percentDecode(char *s)
{
    char *o = s;
    for (const char *p = s; *p; p++) {
        int hi, lo;
        if (*p == '%' && (hi = hexValue(p[1])) >= 0 && (lo = hexValue(p[2])) >= 0) {
            if (!hi && !lo)
                return false;
            *o++ = (char)(hi << 4 | lo);
            p += 2;
        } else {
            *o++ = *p;
        }
    }
    *o = '\0';
    return true;
}

// The path part of an href or URL ("https://h/a/b" or "/a/b" -> "/a/b"),
// decoded, without a last '/'. False if it didn't fit or can't be decoded.
static bool hrefPath(const char *href, char *out, size_t size)
{
    size_t scheme = schemeLength(href);
    if (scheme) {
        const char *slash = strchr(href + scheme, '/');
        href = slash ? slash : "/";
    }
    if ((size_t)snprintf(out, size, "%s", href) >= size)
        return false;
    char *query = strchr(out, '?');
    if (query)
        *query = '\0';
    if (!percentDecode(out))
        return false;
    size_t length = strlen(out);
    while (length > 1 && out[length - 1] == '/')
        out[--length] = '\0';
    return true;
}

// A name a line-based file (resume.txt and the like) can keep.
static bool plainName(const char *name)
{
    for (const unsigned char *p = (const unsigned char *)name; *p; p++)
        if (*p < 0x20 || *p == 0x7f)
            return false;
    return true;
}

typedef struct {
    DavEntry *items;
    size_t count, capacity;
} Listing;

static bool add(Listing *l, const DavEntry *e)
{
    if (l->count >= DAV_LISTING_MAX)
        return true;  // The rest isn't shown.
    if (l->count == l->capacity) {
        size_t grown = l->capacity ? l->capacity * 2 : LISTING_GROWTH;
        DavEntry *items = realloc(l->items, grown * sizeof(DavEntry));
        if (!items)
            return false;
        l->items = items;
        l->capacity = grown;
    }
    l->items[l->count++] = *e;
    return true;
}

// The entry an href names, if it's in the folder (not the folder itself).
static bool entryOf(const char *href, const char *folder, DavEntry *e)
{
    char path[HREF_MAX];
    if (!hrefPath(href, path, sizeof(path)) || !strcmp(path, folder))
        return false;
    char *slash = strrchr(path, '/');
    if (!slash)
        return false;
    *slash = '\0';
    const char *name = slash + 1;
    // Its folder must be this one: a decoded "%2F" in a name would put it elsewhere.
    if (strcmp(path[0] ? path : "/", folder))
        return false;
    size_t length = strlen(name);
    if (!length || !strcmp(name, ".") || !strcmp(name, "..") || length >= sizeof(e->name) || !plainName(name))
        return false;
    memcpy(e->name, name, length + 1);
    return true;
}

bool davParseListing(const char *xml, const char *folder_url, DavEntry **out, size_t *count)
{
    *out = NULL;
    *count = 0;
    char folder[HREF_MAX];
    if (!hrefPath(folder_url, folder, sizeof(folder)))
        return false;
    Scanner s = {xml};
    Tag t;
    Listing l = {0};
    bool listing = false, in_response = false, named = false, href_seen = false, ok = true;
    DavEntry e;
    char text[HREF_MAX];
    while (ok && nextTag(&s, &t)) {
        if (tagIs(&t, "multistatus")) {
            listing = true;
        } else if (tagIs(&t, "response") && !t.closing) {
            in_response = true;
            named = href_seen = false;
            memset(&e, 0, sizeof(e));
            e.size = -1;
        } else if (tagIs(&t, "response") && t.closing) {
            if (in_response && named)
                ok = add(&l, &e);
            in_response = false;
        } else if (in_response && !href_seen && !t.closing && !t.empty && tagIs(&t, "href")) {
            // The response's own href comes first; later ones are in its
            // properties (a lock's root), and aren't its name.
            href_seen = true;
            named = tagText(t.after, text, sizeof(text)) && entryOf(text, folder, &e);
        } else if (in_response && !t.closing && tagIs(&t, "collection")) {
            e.is_dir = true;
        } else if (in_response && !t.closing && !t.empty && tagIs(&t, "getcontentlength")) {
            if (!tagText(t.after, text, sizeof(text)))
                text[0] = '\0';
            char *end;
            long long size = strtoll(text, &end, 10);
            e.size = end != text && size >= 0 ? size : -1;
        } else if (in_response && !t.closing && !t.empty && tagIs(&t, "getlastmodified")) {
            e.modified = tagText(t.after, text, sizeof(text)) ? davParseDate(text) : 0;
        }
    }
    if (!listing || !ok) {
        free(l.items);
        return false;
    }
    for (size_t i = 0; i < l.count; i++)
        if (l.items[i].is_dir)
            l.items[i].size = -1;
    *out = l.items;
    *count = l.count;
    return true;
}
