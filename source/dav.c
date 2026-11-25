#include "dav.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "md4.h"

#define HTTP "http://"
#define HTTPS "https://"
#define SMB "smb://"
#define FTP "ftp://"
#define FTPS "ftps://"
#define FTPES "ftpes://"
#define SFTP "sftp://"
#define SFTP_PORT 22
#define FTP_PORT 21
#define FTPS_PORT 990
#define LISTING_GROWTH 64
#define HREF_MAX 4096  // A 255-byte name in CJK is ~765 characters percent-encoded, plus its folders.

bool davIsLocation(const char *path)
{
    return path && !strncmp(path, DAV_PREFIX, sizeof(DAV_PREFIX) - 1);
}

static size_t schemeLength(const char *s)
{
    static const char *const SCHEMES[] = {HTTP, HTTPS, FTP, FTPS, FTPES, SFTP};
    for (size_t i = 0; i < sizeof(SCHEMES) / sizeof(SCHEMES[0]); i++)
        if (!strncasecmp(s, SCHEMES[i], strlen(SCHEMES[i])))
            return strlen(SCHEMES[i]);
    return 0;
}

static bool startsWith(const char *s, const char *prefix)
{
    return !strncasecmp(s, prefix, strlen(prefix));
}

DavProtocol davProtocolOf(const char *address)
{
    if (!address || !address[0])
        return DAV_NONE;
    if (davIsSmbAddress(address))
        return DAV_SMB;
    if (startsWith(address, SFTP))
        return DAV_SFTP;
    if (startsWith(address, FTP) || startsWith(address, FTPS) || startsWith(address, FTPES))
        return DAV_FTP;
    return startsWith(address, HTTP) || startsWith(address, HTTPS) ? DAV_WEBDAV : DAV_NONE;
}

bool davIsSmbAddress(const char *address)
{
    return !strncasecmp(address, SMB, sizeof(SMB) - 1);
}

// "smb://PC/Videos", "\\PC\Videos\sub" or "//PC/Videos": "smb://PC/Videos/sub/".
static bool normalizeSmb(const char *typed, size_t length, char *out, size_t size)
{
    char path[DAV_ADDRESS_MAX];
    size_t skip = davIsSmbAddress(typed) ? sizeof(SMB) - 1 : 2;
    if (length <= skip || length - skip >= sizeof(path))
        return false;
    size_t n = 0;
    for (size_t i = skip; i < length; i++) {
        char c = typed[i] == '\\' ? '/' : typed[i];
        if (c == '/' && n && path[n - 1] == '/')
            continue;  // "\\PC\\Videos": one slash.
        path[n++] = c;
    }
    while (n && path[n - 1] == '/')
        n--;
    path[n] = '\0';
    const char *slash = strchr(path, '/');
    if (!slash || slash == path || memchr(path, '@', (size_t)(slash - path)))
        return false;  // No share, no server, or "user:password@".
    int written = snprintf(out, size, "%s%s/", SMB, path);
    return written > 0 && (size_t)written < size;
}

bool davNormalizeAddress(const char *typed, char *out, size_t size)
{
    while (*typed == ' ')
        typed++;
    size_t typed_length = strlen(typed);
    while (typed_length && typed[typed_length - 1] == ' ')
        typed_length--;
    if (davIsSmbAddress(typed) || !strncmp(typed, "\\\\", 2) || !strncmp(typed, "//", 2))
        return normalizeSmb(typed, typed_length, out, size);  // Spaces are fine in a share's folders.
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

bool davSetAddress(DavAccount *a, const char *address)
{
    bool other = !davSameServer(address, a->address);
    if (other) {
        a->user[0] = '\0';
        memset(a->password, 0, sizeof(a->password));
        a->host_key[0] = '\0';
    }
    snprintf(a->address, sizeof(a->address), "%s", address);
    return other;
}

bool davSmbParts(const char *address, char *server, size_t server_size, char *share, size_t share_size, char *folder,
                 size_t folder_size)
{
    if (!davIsSmbAddress(address))
        return false;
    const char *s = address + sizeof(SMB) - 1;
    size_t server_length = strcspn(s, "/");
    const char *share_at = s + server_length + (s[server_length] == '/');
    size_t share_length = strcspn(share_at, "/");
    const char *folder_at = share_at + share_length + (share_at[share_length] == '/');
    size_t folder_length = strlen(folder_at);
    while (folder_length && folder_at[folder_length - 1] == '/')
        folder_length--;
    if (!server_length || !share_length || server_length >= server_size || share_length >= share_size ||
        folder_length >= folder_size)
        return false;
    snprintf(server, server_size, "%.*s", (int)server_length, s);
    snprintf(share, share_size, "%.*s", (int)share_length, share_at);
    snprintf(folder, folder_size, "%.*s", (int)folder_length, folder_at);
    return true;
}

bool davSmbPath(const char *address, const char *location, char *out, size_t size)
{
    char server[DAV_ADDRESS_MAX], share[DAV_ADDRESS_MAX], folder[DAV_ADDRESS_MAX];
    if (!davIsLocation(location) || !davSmbParts(address, server, sizeof(server), share, sizeof(share), folder,
                                                 sizeof(folder)))
        return false;
    const char *rest = location + sizeof(DAV_PREFIX) - 1;
    size_t rest_length = strlen(rest);
    while (rest_length && rest[rest_length - 1] == '/')
        rest_length--;
    int n = snprintf(out, size, "%s%s%.*s", folder, folder[0] && rest_length ? "/" : "", (int)rest_length, rest);
    return n >= 0 && (size_t)n < size;
}

// An FTP or SFTP address's authority ("user@host:port" has no user here) and its length.
static const char *authorityOf(const char *address, size_t *length)
{
    DavProtocol p = davProtocolOf(address);
    if (p != DAV_FTP && p != DAV_SFTP)
        return NULL;
    const char *a = address + schemeLength(address);
    *length = strcspn(a, "/");
    return *length ? a : NULL;
}

bool davServerPath(const char *address, const char *location, char *out, size_t size)
{
    size_t authority;
    const char *a = authorityOf(address, &authority);
    if (!a || !davIsLocation(location))
        return false;
    const char *base = a + authority;  // "/media/" or "".
    const char *rest = location + sizeof(DAV_PREFIX) - 1;
    size_t base_length = strlen(base);
    bool slash = base_length && base[base_length - 1] == '/';
    int n = snprintf(out, size, "%s%s%s", base_length ? base : "/", slash || !base_length ? "" : "/", rest);
    return n >= 0 && (size_t)n < size;
}

bool davHostPort(const char *address, char *host, size_t host_size, int *port)
{
    size_t authority;
    const char *a = authorityOf(address, &authority);
    if (!a)
        return false;
    const char *colon = NULL;
    if (a[0] == '[') {  // "[fe80::1]:22"
        const char *close = memchr(a, ']', authority);
        if (!close)
            return false;
        colon = close + 1 < a + authority && close[1] == ':' ? close + 1 : NULL;
    } else {
        colon = memchr(a, ':', authority);
    }
    size_t host_length = colon ? (size_t)(colon - a) : authority;
    if (!host_length || host_length >= host_size)
        return false;
    snprintf(host, host_size, "%.*s", (int)host_length, a);
    *port = startsWith(address, SFTP) ? SFTP_PORT : startsWith(address, FTPS) ? FTPS_PORT : FTP_PORT;
    if (colon) {
        char *end;
        long value = strtol(colon + 1, &end, 10);
        if (end != a + authority || value < 1 || value > 65535)
            return false;
        *port = (int)value;
    }
    return true;
}

void davSplitUser(const char *user, char *domain, size_t domain_size, char *name, size_t name_size)
{
    const char *slash = strchr(user, '\\');
    if (!slash) {
        snprintf(domain, domain_size, "%s", "");
        snprintf(name, name_size, "%s", user);
        return;
    }
    snprintf(domain, domain_size, "%.*s", (int)(slash - user), user);
    snprintf(name, name_size, "%s", slash + 1);
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

bool davBase64(const unsigned char *in, size_t length, char *out, size_t size)
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
              davBase64((const unsigned char *)pair, (size_t)length, out + sizeof(BASIC) - 1, size - (sizeof(BASIC) - 1));
    if (ok)
        memcpy(out, BASIC, sizeof(BASIC) - 1);
    else
        out[0] = '\0';
    memset(pair, 0, sizeof(pair));
    return ok;
}

static int base64Value(char c)
{
    const char *at = c ? strchr(BASE64, c) : NULL;
    return at ? (int)(at - BASE64) : -1;
}

// Decodes base64 into out (NUL-terminated). False if it isn't base64 or doesn't fit.
static bool unbase64(const char *in, char *out, size_t size)
{
    size_t length = strlen(in), o = 0;
    if (length % 4)
        return false;
    for (size_t i = 0; i < length; i += 4) {
        int v[4];
        for (int k = 0; k < 4; k++) {
            bool pad = in[i + k] == '=' && i + 4 == length && k >= 2;
            v[k] = pad ? 0 : base64Value(in[i + k]);
            if (v[k] < 0 || (pad && k == 2 && in[i + 3] != '='))
                return false;
        }
        unsigned bits = (unsigned)v[0] << 18 | (unsigned)v[1] << 12 | (unsigned)v[2] << 6 | (unsigned)v[3];
        int bytes = in[i + 2] == '=' ? 1 : in[i + 3] == '=' ? 2 : 3;
        if (o + (size_t)bytes >= size)
            return false;
        for (int k = 0; k < bytes; k++)
            out[o++] = (char)(bits >> (16 - 8 * k) & 0xFF);
    }
    out[o] = '\0';
    return strlen(out) == o;  // No NUL inside.
}

// --- The password's NT hash ---

// The next code point of UTF-8 text at *p, moving past it; -1 if it isn't UTF-8
// (cut short, overlong, a surrogate, past U+10FFFF).
static long nextCodePoint(const unsigned char **p)
{
    const unsigned char *s = *p;
    long c;
    int more;
    if (s[0] < 0x80)
        c = s[0], more = 0;
    else if (s[0] >= 0xC2 && s[0] <= 0xDF)
        c = s[0] & 0x1F, more = 1;
    else if (s[0] >= 0xE0 && s[0] <= 0xEF)
        c = s[0] & 0x0F, more = 2;
    else if (s[0] >= 0xF0 && s[0] <= 0xF4)
        c = s[0] & 0x07, more = 3;
    else
        return -1;
    for (int i = 1; i <= more; i++) {
        if ((s[i] & 0xC0) != 0x80)
            return -1;
        c = c << 6 | (s[i] & 0x3F);
    }
    static const long LEAST[4] = {0, 0x80, 0x800, 0x10000};
    if (c < LEAST[more] || (c >= 0xD800 && c <= 0xDFFF) || c > 0x10FFFF)
        return -1;
    *p = s + 1 + more;
    return c;
}

bool davNtHash(const char *password, char out[DAV_NT_HASH_SIZE])
{
    uint8_t utf16[DAV_TEXT_MAX * 4];  // Each byte of UTF-8 is at most two of UTF-16.
    size_t n = 0;
    const unsigned char *p = (const unsigned char *)password;
    bool ok = strlen(password) < DAV_TEXT_MAX;
    while (ok && *p) {
        long c = nextCodePoint(&p);
        ok = c >= 0;
        if (ok && c >= 0x10000) {  // A surrogate pair.
            c -= 0x10000;
            long high = 0xD800 + (c >> 10), low = 0xDC00 + (c & 0x3FF);
            utf16[n++] = (uint8_t)high, utf16[n++] = (uint8_t)(high >> 8);
            utf16[n++] = (uint8_t)low, utf16[n++] = (uint8_t)(low >> 8);
        } else if (ok) {
            utf16[n++] = (uint8_t)c, utf16[n++] = (uint8_t)(c >> 8);
        }
    }
    if (ok) {
        uint8_t digest[MD4_DIGEST_SIZE];
        md4(utf16, n, digest);
        int length = snprintf(out, DAV_NT_HASH_SIZE, "%s", DAV_NT_HASH_PREFIX);
        for (int i = 0; i < MD4_DIGEST_SIZE; i++)
            length += snprintf(out + length, DAV_NT_HASH_SIZE - (size_t)length, "%02X", digest[i]);
        memset(digest, 0, sizeof(digest));
    }
    memset(utf16, 0, sizeof(utf16));
    return ok;
}

static bool isNtHash(const char *password)
{
    size_t prefix = sizeof(DAV_NT_HASH_PREFIX) - 1;
    if (strlen(password) != DAV_NT_HASH_SIZE - 1 || strncmp(password, DAV_NT_HASH_PREFIX, prefix))
        return false;
    for (const char *p = password + prefix; *p; p++)
        if (!isxdigit((unsigned char)*p))
            return false;
    return true;
}

bool davSetPassword(DavAccount *a, const char *typed)
{
    if (strlen(typed) >= sizeof(a->password) || strpbrk(typed, "\r\n"))
        return false;
    if (!davIsSmbAddress(a->address) || !typed[0]) {
        snprintf(a->password, sizeof(a->password), "%s", typed);
        return true;
    }
    char hash[DAV_NT_HASH_SIZE];
    bool ok = davNtHash(typed, hash);
    if (ok)
        snprintf(a->password, sizeof(a->password), "%s", hash);
    memset(hash, 0, sizeof(hash));
    return ok;
}

// --- The account's file ---

#define ENCODED_PREFIX "b64:"

static void readValue(char *out, size_t size, const char *value, size_t length)
{
    snprintf(out, size, "%.*s", (int)(length < size ? length : size - 1), value);
}

static bool isKey(const char *line, size_t key, const char *name)
{
    return key == strlen(name) && !strncmp(line, name, key);
}

// The password as kept (a hash, encoded, or an older file's as typed) into a's
// form. True if it was an older form.
static bool readPassword(DavAccount *a, const char *kept)
{
    char typed[DAV_TEXT_MAX] = "";
    bool older = false;
    if (!strncmp(kept, ENCODED_PREFIX, sizeof(ENCODED_PREFIX) - 1)) {
        if (!unbase64(kept + sizeof(ENCODED_PREFIX) - 1, typed, sizeof(typed)))
            older = true;  // Damaged: none, rather than a wrong one.
    } else if (isNtHash(kept)) {
        if (davIsSmbAddress(a->address))
            snprintf(a->password, sizeof(a->password), "%.*s", (int)(DAV_NT_HASH_SIZE - 1), kept);  // isNtHash: that long.
        return !davIsSmbAddress(a->address);  // A WebDAV folder can't use it: dropped.
    } else {
        size_t length = strlen(kept);
        if (length < sizeof(typed))  // Longer can't have been typed: none, rather than a cut one.
            memcpy(typed, kept, length + 1);
        older = true;
    }
    if (!davSetPassword(a, typed)) {
        a->password[0] = '\0';
        older = true;
    }
    older = older || (davIsSmbAddress(a->address) && typed[0]);  // Encoded, but SMB keeps the hash.
    memset(typed, 0, sizeof(typed));
    return older;
}

bool davAccountRead(DavAccount *a, const char *text)
{
    memset(a, 0, sizeof(*a));
    char kept[DAV_TEXT_MAX * 2] = "";
    for (const char *line = text; *line;) {
        size_t length = strcspn(line, "\r\n");
        const char *eq = memchr(line, '=', length);
        if (eq) {
            size_t key = (size_t)(eq - line), value = length - key - 1;
            if (isKey(line, key, "address"))
                readValue(a->address, sizeof(a->address), eq + 1, value);
            else if (isKey(line, key, "user"))
                readValue(a->user, sizeof(a->user), eq + 1, value);
            else if (isKey(line, key, "password"))
                readValue(kept, sizeof(kept), eq + 1, value);
            else if (isKey(line, key, "forget_password"))
                a->forget_password = value == 1 && eq[1] == '1';
            else if (isKey(line, key, "allow_unsigned"))
                a->allow_unsigned = value == 1 && eq[1] == '1';
            else if (isKey(line, key, "host_key"))
                readValue(a->host_key, sizeof(a->host_key), eq + 1, value);
        }
        line += length;
        line += strspn(line, "\r\n");
    }
    char address[DAV_ADDRESS_MAX];
    if (a->address[0] && davNormalizeAddress(a->address, address, sizeof(address)))
        memcpy(a->address, address, sizeof(address));
    else
        a->address[0] = '\0';
    bool older = false;
    if (a->forget_password)
        older = kept[0] != '\0';  // Kept by mistake: dropped.
    else if (kept[0])
        older = readPassword(a, kept);
    memset(kept, 0, sizeof(kept));
    return older;
}

bool davAccountWrite(const DavAccount *a, char *out, size_t size)
{
    if (strpbrk(a->address, "\r\n") || strpbrk(a->user, "\r\n") || strpbrk(a->password, "\r\n") ||
        strpbrk(a->host_key, "\r\n"))
        return false;
    char kept[DAV_TEXT_MAX * 2] = "";
    if (a->forget_password || !a->password[0])
        kept[0] = '\0';
    else if (isNtHash(a->password))
        snprintf(kept, sizeof(kept), "%s", a->password);
    else if (!davBase64((const unsigned char *)a->password, strlen(a->password), kept + sizeof(ENCODED_PREFIX) - 1,
                     sizeof(kept) - (sizeof(ENCODED_PREFIX) - 1)))
        return false;
    else
        memcpy(kept, ENCODED_PREFIX, sizeof(ENCODED_PREFIX) - 1);
    int n = snprintf(out, size, "address=%s\nuser=%s\npassword=%s\n%s%s%s%s%s", a->address, a->user, kept,
                     a->forget_password ? "forget_password=1\n" : "", a->allow_unsigned ? "allow_unsigned=1\n" : "",
                     a->host_key[0] ? "host_key=" : "", a->host_key, a->host_key[0] ? "\n" : "");
    memset(kept, 0, sizeof(kept));
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

bool davPercentDecode(char *s)
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
bool davHrefPath(const char *href, char *out, size_t size)
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
    if (!davPercentDecode(out))
        return false;
    size_t length = strlen(out);
    while (length > 1 && out[length - 1] == '/')
        out[--length] = '\0';
    return true;
}

bool davNameIsPlain(const char *name)
{
    for (const unsigned char *p = (const unsigned char *)name; *p; p++)
        if (*p < 0x20 || *p == 0x7f)
            return false;
    return true;
}

bool davListingAdd(DavListing *l, const DavEntry *e)
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

bool davEntryOfHref(const char *href, const char *folder, DavEntry *e)
{
    char path[HREF_MAX];
    if (!davHrefPath(href, path, sizeof(path)) || !strcmp(path, folder))
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
    if (!length || !strcmp(name, ".") || !strcmp(name, "..") || length >= sizeof(e->name) || !davNameIsPlain(name))
        return false;
    memcpy(e->name, name, length + 1);
    return true;
}

bool davParseListing(const char *xml, const char *folder_url, DavEntry **out, size_t *count)
{
    *out = NULL;
    *count = 0;
    char folder[HREF_MAX];
    if (!davHrefPath(folder_url, folder, sizeof(folder)))
        return false;
    Scanner s = {xml};
    Tag t;
    DavListing l = {0};
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
                ok = davListingAdd(&l, &e);
            in_response = false;
        } else if (in_response && !href_seen && !t.closing && !t.empty && tagIs(&t, "href")) {
            // The response's own href comes first; later ones are in its
            // properties (a lock's root), and aren't its name.
            href_seen = true;
            named = tagText(t.after, text, sizeof(text)) && davEntryOfHref(text, folder, &e);
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
