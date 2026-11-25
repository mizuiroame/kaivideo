#include "iptv.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <jansson.h>

void iptvListFree(IptvList *list)
{
    free(list->channels);
    free(list->groups);
    memset(list, 0, sizeof(*list));
}

static bool isWebUrl(const char *url)
{
    return !strncasecmp(url, "http://", 7) || !strncasecmp(url, "https://", 8);
}

// Copies text (length bytes) trimmed, cut between characters.
static void copyTrimmed(char *out, size_t size, const char *text, size_t length)
{
    while (length > 0 && isspace((unsigned char)*text)) {
        text++;
        length--;
    }
    while (length > 0 && isspace((unsigned char)text[length - 1]))
        length--;
    if (length >= size) {
        length = size - 1;
        while (length > 0 && ((unsigned char)text[length] & 0xC0) == 0x80)
            length--;
    }
    memcpy(out, text, length);
    out[length] = '\0';
}

// The group named so, made if new. -1 if there's no room.
static int groupOf(IptvList *list, const char *name, const char *id)
{
    if (!name[0])
        return -1;
    for (int i = 0; i < list->group_count; i++)
        if (id ? !strcmp(list->groups[i].id, id) : !strcmp(list->groups[i].name, name))
            return i;
    if (list->group_count >= IPTV_GROUPS_MAX)
        return -1;
    IptvGroup *grown = realloc(list->groups, (size_t)(list->group_count + 1) * sizeof(IptvGroup));
    if (!grown)
        return -1;
    list->groups = grown;
    IptvGroup *g = &list->groups[list->group_count];
    memset(g, 0, sizeof(*g));
    snprintf(g->name, sizeof(g->name), "%s", name);
    snprintf(g->id, sizeof(g->id), "%s", id ? id : "");
    return list->group_count++;
}

// A new channel at the list's end, or NULL (full: truncated; or memory).
static IptvChannel *addChannel(IptvList *list, size_t *capacity)
{
    if (list->count >= IPTV_CHANNELS_MAX) {
        list->truncated = true;
        return NULL;
    }
    if ((size_t)list->count == *capacity) {
        size_t grown_to = *capacity ? *capacity * 2 : 256;
        if (grown_to > IPTV_CHANNELS_MAX)
            grown_to = IPTV_CHANNELS_MAX;
        IptvChannel *grown = realloc(list->channels, grown_to * sizeof(IptvChannel));
        if (!grown)
            return NULL;
        list->channels = grown;
        *capacity = grown_to;
    }
    IptvChannel *c = &list->channels[list->count];
    memset(c, 0, sizeof(*c));
    c->group = -1;
    return c;
}

void iptvLocation(char *out, size_t size, int index)
{
    snprintf(out, size, IPTV_PREFIX "%d", index);
}

bool iptvIsLocation(const char *location)
{
    return iptvLocationIndex(location) >= 0;
}

int iptvLocationIndex(const char *location)
{
    size_t n = sizeof(IPTV_PREFIX) - 1;
    if (!location || strncmp(location, IPTV_PREFIX, n) || !isdigit((unsigned char)location[n]))
        return -1;
    char *end;
    long index = strtol(location + n, &end, 10);
    return *end || index < 0 || index >= IPTV_CHANNELS_MAX ? -1 : (int)index;
}

// --- M3U ---

// The value of key="value" in an #EXTINF or #EXTM3U line (up to stop). False if it has none.
static bool attribute(const char *line, const char *stop, const char *key, char *out, size_t size)
{
    size_t key_length = strlen(key);
    for (const char *p = line; p + key_length + 2 <= stop; p++) {
        if (strncasecmp(p, key, key_length) || p[key_length] != '=' || p[key_length + 1] != '"')
            continue;
        if (p > line && !isspace((unsigned char)p[-1]) && p[-1] != ':')
            continue;  // Part of a longer name ("x-tvg-url" isn't "tvg-url").
        const char *value = p + key_length + 2;
        const char *end = memchr(value, '"', (size_t)(stop - value));
        if (!end)
            return false;
        copyTrimmed(out, size, value, (size_t)(end - value));
        return true;
    }
    if (size)
        out[0] = '\0';
    return false;
}

// Where an #EXTINF line's name starts: after the first comma outside quotes.
static const char *extinfName(const char *line, const char *stop)
{
    bool quoted = false;
    for (const char *p = line; p < stop; p++) {
        if (*p == '"')
            quoted = !quoted;
        else if (*p == ',' && !quoted)
            return p + 1;
    }
    return stop;
}

bool iptvParseM3u(const char *text, size_t length, IptvList *out)
{
    memset(out, 0, sizeof(*out));
    if (!text || length > IPTV_TEXT_MAX)
        return false;
    const char *end = text + length;
    if (length >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
        text += 3;  // A BOM.
    size_t capacity = 0;
    bool header = false, pending = false;
    IptvChannel next;
    StreamHeaders headers;  // Kept apart: #EXTVLCOPT may come before #EXTINF too.
    char group[IPTV_GROUP_MAX] = "";
    memset(&next, 0, sizeof(next));
    memset(&headers, 0, sizeof(headers));
    for (const char *line = text; line < end;) {
        const char *stop = memchr(line, '\n', (size_t)(end - line));
        const char *after = stop ? stop + 1 : end;
        if (!stop)
            stop = end;
        const char *trimmed_end = stop;
        while (trimmed_end > line && (trimmed_end[-1] == '\r' || isspace((unsigned char)trimmed_end[-1])))
            trimmed_end--;
        while (line < trimmed_end && isspace((unsigned char)*line))
            line++;
        size_t n = (size_t)(trimmed_end - line);
        if (n >= 7 && !strncasecmp(line, "#EXTM3U", 7)) {
            header = true;
            if (!attribute(line, trimmed_end, "url-tvg", out->epg_url, sizeof(out->epg_url)))
                attribute(line, trimmed_end, "x-tvg-url", out->epg_url, sizeof(out->epg_url));
            char *comma = strchr(out->epg_url, ',');  // Several guides: the first.
            if (comma)
                *comma = '\0';
            if (!isWebUrl(out->epg_url))
                out->epg_url[0] = '\0';
        } else if (n >= 8 && !strncasecmp(line, "#EXTINF:", 8)) {
            memset(&next, 0, sizeof(next));
            pending = true;
            const char *name = extinfName(line, trimmed_end);
            copyTrimmed(next.name, sizeof(next.name), name, (size_t)(trimmed_end - name));
            if (!next.name[0])
                attribute(line, name, "tvg-name", next.name, sizeof(next.name));
            attribute(line, name, "tvg-id", next.epg_id, sizeof(next.epg_id));
            attribute(line, name, "tvg-logo", next.logo, sizeof(next.logo));
            attribute(line, name, "tvg-chno", next.number, sizeof(next.number));
            attribute(line, name, "group-title", group, sizeof(group));
            if (!isWebUrl(next.logo))
                next.logo[0] = '\0';
        } else if (n >= 8 && !strncasecmp(line, "#EXTGRP:", 8)) {
            copyTrimmed(group, sizeof(group), line + 8, n - 8);
        } else if (n >= 11 && !strncasecmp(line, "#EXTVLCOPT:", 11)) {
            streamHeadersFromVlcOpt(&headers, line, n);
        } else if (n > 0 && line[0] != '#') {
            char url[IPTV_URL_MAX + STREAM_HEADER_MAX * 3];
            bool fits = isWebUrl(line) && n < sizeof(url);
            if (fits) {
                copyTrimmed(url, sizeof(url), line, n);
                streamHeadersFromPipe(&headers, url);
                fits = strlen(url) < sizeof(next.url);
            }
            if (fits) {
                IptvChannel *c = addChannel(out, &capacity);
                if (!c && !out->truncated) {
                    iptvListFree(out);
                    return false;
                }
                if (c) {
                    *c = next;
                    c->headers = headers;
                    memcpy(c->url, url, strlen(url) + 1);  // It fits: checked above.
                    if (!pending || !c->name[0])
                        copyTrimmed(c->name, sizeof(c->name), url, strlen(url));
                    c->group = pending ? groupOf(out, group, NULL) : -1;
                    if (c->group >= 0)
                        out->groups[c->group].count++;
                    out->count++;
                }
            }
            pending = false;
            group[0] = '\0';
            memset(&next, 0, sizeof(next));
            memset(&headers, 0, sizeof(headers));
        }
        line = after;
    }
    if (!header && out->count == 0) {
        iptvListFree(out);
        return false;
    }
    return true;
}

// --- Xtream ---

bool xtreamNormalizeServer(const char *typed, char *out, size_t size)
{
    char text[IPTV_URL_MAX];
    copyTrimmed(text, sizeof(text), typed, strlen(typed));
    const char *rest = text;
    const char *scheme = "http://";
    if (!strncasecmp(rest, "http://", 7)) {
        rest += 7;
    } else if (!strncasecmp(rest, "https://", 8)) {
        rest += 8;
        scheme = "https://";
    } else if (strstr(rest, "://")) {
        return false;  // Another scheme.
    }
    size_t host = strcspn(rest, "/?#");
    if (host == 0)
        return false;
    for (size_t i = 0; i < host; i++) {
        unsigned char c = (unsigned char)rest[i];
        if (!isalnum(c) && c != '.' && c != '-' && c != ':' && c != '[' && c != ']')
            return false;  // A user part ("@"), spaces, or odd characters.
    }
    int n = snprintf(out, size, "%s%.*s", scheme, (int)host, rest);
    return n > 0 && (size_t)n < size;
}

// Appends text percent-encoded (all but letters, digits and -._~).
static bool appendEncoded(char *out, size_t size, size_t *n, const char *text)
{
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        bool keep = isalnum(*p) || *p == '-' || *p == '.' || *p == '_' || *p == '~';
        if (*n + (keep ? 1 : 3) >= size)
            return false;
        if (keep)
            out[(*n)++] = (char)*p;
        else
            *n += (size_t)snprintf(out + *n, size - *n, "%%%02X", *p);
    }
    out[*n] = '\0';
    return true;
}

static bool appendText(char *out, size_t size, size_t *n, const char *text)
{
    size_t length = strlen(text);
    if (*n + length >= size)
        return false;
    memcpy(out + *n, text, length + 1);
    *n += length;
    return true;
}

bool xtreamApiUrl(const XtreamAccount *a, const char *action, const char *extra, char *out, size_t size)
{
    size_t n = 0;
    out[0] = '\0';
    bool ok = a->server[0] && appendText(out, size, &n, a->server) &&
              appendText(out, size, &n, "/player_api.php?username=") && appendEncoded(out, size, &n, a->user) &&
              appendText(out, size, &n, "&password=") && appendEncoded(out, size, &n, a->password);
    if (ok && action && action[0])
        ok = appendText(out, size, &n, "&action=") && appendEncoded(out, size, &n, action);
    if (ok && extra && extra[0])
        ok = appendText(out, size, &n, "&") && appendText(out, size, &n, extra);
    return ok;
}

bool xtreamStreamUrl(const XtreamAccount *a, const char *kind, long long id, const char *extension, char *out,
                     size_t size)
{
    size_t n = 0;
    char tail[48];
    snprintf(tail, sizeof(tail), "/%lld.", id);
    bool ext_ok = extension[0] && strlen(extension) < 8;
    for (const char *p = extension; ext_ok && *p; p++)
        ext_ok = isalnum((unsigned char)*p);
    return a->server[0] && ext_ok && id > 0 && appendText(out, size, &n, a->server) &&
           appendText(out, size, &n, "/") && appendText(out, size, &n, kind) && appendText(out, size, &n, "/") &&
           appendEncoded(out, size, &n, a->user) && appendText(out, size, &n, "/") &&
           appendEncoded(out, size, &n, a->password) && appendText(out, size, &n, tail) &&
           appendText(out, size, &n, extension);
}

bool xtreamGuideUrl(const XtreamAccount *a, char *out, size_t size)
{
    size_t n = 0;
    return a->server[0] && appendText(out, size, &n, a->server) &&
           appendText(out, size, &n, "/xmltv.php?username=") && appendEncoded(out, size, &n, a->user) &&
           appendText(out, size, &n, "&password=") && appendEncoded(out, size, &n, a->password);
}

// --- The account's file ---

static const char BASE64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static bool encode64(const char *in, char *out, size_t size)
{
    size_t n = 0, length = strlen(in);
    const unsigned char *s = (const unsigned char *)in;
    for (size_t i = 0; i < length; i += 3) {
        unsigned v = (unsigned)s[i] << 16 | (i + 1 < length ? (unsigned)s[i + 1] << 8 : 0) |
                     (i + 2 < length ? s[i + 2] : 0);
        if (n + 4 >= size)
            return false;
        out[n++] = BASE64[v >> 18 & 63];
        out[n++] = BASE64[v >> 12 & 63];
        out[n++] = i + 1 < length ? BASE64[v >> 6 & 63] : '=';
        out[n++] = i + 2 < length ? BASE64[v & 63] : '=';
    }
    out[n] = '\0';
    return true;
}

static bool decode64(const char *in, char *out, size_t size)
{
    size_t n = 0;
    unsigned v = 0;
    int bits = 0;
    for (const char *p = in; *p && *p != '='; p++) {
        const char *at = strchr(BASE64, *p);
        if (!at || !*p)
            return false;
        v = v << 6 | (unsigned)(at - BASE64);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (n + 1 >= size)
                return false;
            out[n++] = (char)(v >> bits & 0xFF);
        }
    }
    out[n] = '\0';
    return true;
}

static bool hasLineBreak(const char *s)
{
    return strpbrk(s, "\r\n") != NULL;
}

bool iptvAccountRead(IptvAccount *a, const char *text)
{
    memset(a, 0, sizeof(*a));
    for (const char *line = text; line && *line;) {
        const char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) : strlen(line);
        const char *eq = memchr(line, '=', length);
        if (eq) {
            size_t key = (size_t)(eq - line);
            char value[IPTV_URL_MAX];
            copyTrimmed(value, sizeof(value), eq + 1, length - key - 1);
            if (key == 4 && !strncmp(line, "kind", 4))
                a->kind = !strcmp(value, "m3u") ? IPTV_M3U : !strcmp(value, "xtream") ? IPTV_XTREAM : IPTV_NONE;
            else if (key == 7 && !strncmp(line, "address", 7) && isWebUrl(value))
                snprintf(a->address, sizeof(a->address), "%s", value);
            else if (key == 4 && !strncmp(line, "user", 4))
                copyTrimmed(a->user, sizeof(a->user), value, strlen(value));
            else if (key == 8 && !strncmp(line, "password", 8) && !strncmp(value, "b64:", 4))
                decode64(value + 4, a->password, sizeof(a->password));
            else if (key == 5 && !strncmp(line, "guide", 5) && isWebUrl(value))
                snprintf(a->guide, sizeof(a->guide), "%s", value);
            else if (key == 12 && !strncmp(line, "certificates", 12))
                a->any_certificate = !strcmp(value, "off");
        }
        line = end ? end + 1 : NULL;
    }
    if (a->kind == IPTV_NONE || !a->address[0]) {
        memset(a, 0, sizeof(*a));
        return false;
    }
    return true;
}

bool iptvAccountWrite(const IptvAccount *a, char *out, size_t size)
{
    char password[IPTV_NAME_MAX * 2];
    if (hasLineBreak(a->address) || hasLineBreak(a->user) || hasLineBreak(a->guide) ||
        !encode64(a->password, password, sizeof(password)))
        return false;
    int n = snprintf(out, size, "kind=%s\naddress=%s\nuser=%s\npassword=b64:%s\nguide=%s\ncertificates=%s\n",
                     a->kind == IPTV_XTREAM ? "xtream" : a->kind == IPTV_M3U ? "m3u" : "", a->address, a->user,
                     password, a->guide, a->any_certificate ? "off" : "on");
    memset(password, 0, sizeof(password));
    return n > 0 && (size_t)n < size;
}

void iptvXtreamOf(const IptvAccount *a, XtreamAccount *out)
{
    memset(out, 0, sizeof(*out));
    if (a->kind != IPTV_XTREAM)
        return;
    snprintf(out->server, sizeof(out->server), "%s", a->address);
    snprintf(out->user, sizeof(out->user), "%s", a->user);
    snprintf(out->password, sizeof(out->password), "%s", a->password);
}

void iptvGuideUrl(const IptvAccount *a, const char *list_epg, char *out, size_t size)
{
    out[0] = '\0';
    if (a->guide[0]) {
        snprintf(out, size, "%s", a->guide);
    } else if (a->kind == IPTV_XTREAM) {
        XtreamAccount x;
        iptvXtreamOf(a, &x);
        if (!xtreamGuideUrl(&x, out, size))
            out[0] = '\0';
        memset(&x, 0, sizeof(x));
    } else if (list_epg && isWebUrl(list_epg)) {
        snprintf(out, size, "%s", list_epg);
    }
}

// A field Xtream sends as a string or a number.
static long long number(const json_t *object, const char *key)
{
    const json_t *v = json_object_get(object, key);
    if (json_is_integer(v))
        return (long long)json_integer_value(v);
    if (json_is_real(v))
        return (long long)json_real_value(v);
    if (json_is_string(v))
        return atoll(json_string_value(v));
    return 0;
}

static void text(const json_t *object, const char *key, char *out, size_t size)
{
    const json_t *v = json_object_get(object, key);
    out[0] = '\0';
    if (json_is_string(v))
        copyTrimmed(out, size, json_string_value(v), strlen(json_string_value(v)));
    else if (json_is_integer(v))
        snprintf(out, size, "%lld", (long long)json_integer_value(v));
}

bool xtreamParseAuth(const char *json, XtreamAuth *out)
{
    memset(out, 0, sizeof(*out));
    json_t *root = json ? json_loads(json, 0, NULL) : NULL;
    const json_t *user = json_object_get(root, "user_info");
    bool ok = json_is_object(user);
    if (ok) {
        text(user, "status", out->status, sizeof(out->status));
        out->expires = number(user, "exp_date");
        out->max_connections = (int)number(user, "max_connections");
        bool signed_in = number(user, "auth") == 1;
        out->active = signed_in && !strcasecmp(out->status, "Active");
        // An account's own status (Expired, Banned, Disabled...) is said as it is, even without auth;
        // without one, a refused sign-in is the user or password.
        out->refused = !signed_in && (!out->status[0] || !strcasecmp(out->status, "Active"));
    }
    json_decref(root);
    return ok;
}

bool xtreamParseCategories(const char *json, IptvList *list)
{
    json_t *root = json ? json_loads(json, 0, NULL) : NULL;
    if (!json_is_array(root)) {
        json_decref(root);
        return false;
    }
    free(list->groups);
    list->groups = NULL;
    list->group_count = 0;
    size_t i;
    json_t *item;
    json_array_foreach(root, i, item)
    {
        char id[IPTV_ID_MAX], name[IPTV_GROUP_MAX];
        text(item, "category_id", id, sizeof(id));
        text(item, "category_name", name, sizeof(name));
        if (id[0] && name[0])
            groupOf(list, name, id);
    }
    json_decref(root);
    return true;
}

bool xtreamParseLive(const char *json, const XtreamAccount *a, IptvList *list)
{
    json_t *root = json ? json_loads(json, 0, NULL) : NULL;
    if (!json_is_array(root)) {
        json_decref(root);
        return false;
    }
    free(list->channels);
    list->channels = NULL;
    list->count = 0;
    list->truncated = false;
    for (int g = 0; g < list->group_count; g++)
        list->groups[g].count = 0;
    size_t capacity = 0, i;
    json_t *item;
    bool ok = true;
    json_array_foreach(root, i, item)
    {
        long long id = number(item, "stream_id");
        char name[IPTV_NAME_MAX];
        text(item, "name", name, sizeof(name));
        if (id <= 0 || !name[0])
            continue;
        IptvChannel *c = addChannel(list, &capacity);
        if (!c) {
            ok = list->truncated;
            break;
        }
        snprintf(c->name, sizeof(c->name), "%s", name);
        if (!xtreamStreamUrl(a, "live", id, "m3u8", c->url, sizeof(c->url)))
            continue;
        text(item, "stream_icon", c->logo, sizeof(c->logo));
        if (!isWebUrl(c->logo))
            c->logo[0] = '\0';
        text(item, "epg_channel_id", c->epg_id, sizeof(c->epg_id));
        long long num = number(item, "num");
        if (num > 0 && num < 100000000)
            snprintf(c->number, sizeof(c->number), "%d", (int)num);
        char category[IPTV_ID_MAX];
        text(item, "category_id", category, sizeof(category));
        for (int g = 0; g < list->group_count && category[0]; g++)
            if (!strcmp(list->groups[g].id, category)) {
                c->group = g;
                list->groups[g].count++;
                break;
            }
        list->count++;
    }
    json_decref(root);
    if (!ok)
        iptvListFree(list);
    return ok;
}
