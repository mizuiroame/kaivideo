#include "stremio.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <jansson.h>

#define MANIFEST_TAIL "/manifest.json"
#define TEXT_MAX 2000

// --- Small helpers ---

static bool isWebUrl(const char *url)
{
    return url && (!strncasecmp(url, "http://", 7) || !strncasecmp(url, "https://", 8));
}

static char *dupTrimmed(const char *s, size_t max)
{
    if (!s)
        return NULL;
    size_t length = strlen(s);
    while (length > 0 && isspace((unsigned char)*s)) {
        s++;
        length--;
    }
    while (length > 0 && isspace((unsigned char)s[length - 1]))
        length--;
    if (length == 0)
        return NULL;
    length = length < max ? length : max;
    char *out = malloc(length + 1);
    if (!out)
        return NULL;
    memcpy(out, s, length);
    out[length] = '\0';
    for (char *p = out; *p; p++)  // One line: the lists show it so.
        if (*p == '\n' || *p == '\r' || *p == '\t')
            *p = ' ';
    return out;
}

static char *jsonDup(const json_t *object, const char *key, size_t max)
{
    const json_t *v = json_object_get(object, key);
    return json_is_string(v) ? dupTrimmed(json_string_value(v), max) : NULL;
}

static void jsonCopy(const json_t *object, const char *key, char *out, size_t size)
{
    const json_t *v = json_object_get(object, key);
    snprintf(out, size, "%s", json_is_string(v) ? json_string_value(v) : "");
}

static long long jsonInt(const json_t *object, const char *key)
{
    const json_t *v = json_object_get(object, key);
    if (json_is_integer(v))
        return (long long)json_integer_value(v);
    if (json_is_real(v))
        return (long long)json_real_value(v);
    if (json_is_string(v))
        return atoll(json_string_value(v));
    return -1;
}

static bool append(char *out, size_t size, size_t *n, const char *text)
{
    size_t length = strlen(text);
    if (*n + length >= size)
        return false;
    memcpy(out + *n, text, length + 1);
    *n += length;
    return true;
}

// As encodeURIComponent does.
static bool appendEncoded(char *out, size_t size, size_t *n, const char *text)
{
    static const char HEX[] = "0123456789ABCDEF";
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        char piece[4] = {(char)*p, '\0'};
        if (!isalnum(*p) && !strchr("-_.!~*'()", *p)) {
            piece[0] = '%';
            piece[1] = HEX[*p >> 4];
            piece[2] = HEX[*p & 15];
            piece[3] = '\0';
        }
        if (!append(out, size, n, piece))
            return false;
    }
    return true;
}

static bool isMovieOrSeries(const char *type)
{
    return !strcmp(type, "movie") || !strcmp(type, "series");
}

// --- The addresses typed ---

// Whether a typed address has a scheme of its own ("ftp:", "magnet:"), not just "host:port".
static bool hasScheme(const char *typed)
{
    const char *colon = strchr(typed, ':'), *slash = strchr(typed, '/');
    return colon && (!slash || colon < slash) && !isdigit((unsigned char)colon[1]);
}

// A host on the local network (an IPv4 address, localhost, name.local): its addons are plain http.
static bool isLocalHost(const char *host)
{
    size_t n = strcspn(host, ":/");
    if (n == 9 && !strncasecmp(host, "localhost", 9))
        return true;
    if (n > 6 && !strncasecmp(host + n - 6, ".local", 6))
        return true;
    int dots = 0;
    for (size_t i = 0; i < n; i++) {
        if (host[i] == '.')
            dots++;
        else if (!isdigit((unsigned char)host[i]))
            return false;
    }
    return dots == 3;
}

bool stremioNormalizeUrl(const char *typed, char *out, size_t size)
{
    while (*typed && isspace((unsigned char)*typed))
        typed++;
    char url[STREMIO_URL_MAX];
    if (!strncasecmp(typed, "stremio://", 10))
        snprintf(url, sizeof(url), "https://%s", typed + 10);
    else if (!strstr(typed, "://") && !hasScheme(typed) && *typed)  // Typed without one: http on the LAN, else https.
        snprintf(url, sizeof(url), "%s://%s", isLocalHost(typed) ? "http" : "https", typed);
    else
        snprintf(url, sizeof(url), "%s", typed);
    size_t length = strlen(url);
    while (length > 0 && isspace((unsigned char)url[length - 1]))
        url[--length] = '\0';
    if (!isWebUrl(url) || strpbrk(url, " \t\r\n\"<>"))
        return false;
    size_t tail = strlen(MANIFEST_TAIL);
    bool has_tail = length >= tail && !strcasecmp(url + length - tail, MANIFEST_TAIL);
    while (!has_tail && length > 0 && url[length - 1] == '/')
        url[--length] = '\0';
    int n = snprintf(out, size, "%s%s", url, has_tail ? "" : MANIFEST_TAIL);
    return n > 0 && (size_t)n < size && strlen(url) > 8;
}

void stremioAddonListRead(StremioAddonList *l, const char *text)
{
    l->count = 0;
    const char *p = text;
    while (p && *p && l->count < STREMIO_ADDONS_MAX) {
        const char *end = strchr(p, '\n');
        size_t length = end ? (size_t)(end - p) : strlen(p);
        char line[STREMIO_URL_MAX];
        snprintf(line, sizeof(line), "%.*s", (int)length, p);
        stremioAddonListAdd(l, line);
        p = end ? end + 1 : NULL;
    }
}

bool stremioAddonListWrite(const StremioAddonList *l, char *out, size_t size)
{
    size_t n = 0;
    if (size == 0)
        return false;
    out[0] = '\0';
    for (int i = 0; i < l->count; i++)
        if (!append(out, size, &n, l->urls[i]) || !append(out, size, &n, "\n"))
            return false;
    return true;
}

bool stremioAddonListAdd(StremioAddonList *l, const char *typed)
{
    char url[STREMIO_URL_MAX];
    if (l->count >= STREMIO_ADDONS_MAX || !stremioNormalizeUrl(typed, url, sizeof(url)))
        return false;
    for (int i = 0; i < l->count; i++)
        if (!strcasecmp(l->urls[i], url))
            return false;
    snprintf(l->urls[l->count++], STREMIO_URL_MAX, "%s", url);
    return true;
}

void stremioAddonListRemove(StremioAddonList *l, int index)
{
    if (index < 0 || index >= l->count)
        return;
    memmove(l->urls[index], l->urls[index + 1], (size_t)(l->count - index - 1) * STREMIO_URL_MAX);
    l->count--;
}

// --- Manifests ---

static void joinStrings(const json_t *array, char *out, size_t size)
{
    size_t n = 0;
    out[0] = '\0';
    for (size_t i = 0; json_is_array(array) && i < json_array_size(array); i++) {
        const json_t *v = json_array_get(array, i);
        if (!json_is_string(v) || !json_string_value(v)[0] || strchr(json_string_value(v), ' '))
            continue;
        if ((n && !append(out, size, &n, " ")) || !append(out, size, &n, json_string_value(v)))
            break;
    }
}

static int resourceOf(const char *name)
{
    if (!strcmp(name, "catalog"))
        return STREMIO_CATALOG;
    if (!strcmp(name, "meta"))
        return STREMIO_META;
    if (!strcmp(name, "stream"))
        return STREMIO_STREAM;
    return -1;
}

static void readResources(const json_t *root, StremioAddon *a)
{
    const json_t *resources = json_object_get(root, "resources");
    for (size_t i = 0; json_is_array(resources) && i < json_array_size(resources); i++) {
        const json_t *r = json_array_get(resources, i);
        const char *name = json_is_string(r) ? json_string_value(r) : json_string_value(json_object_get(r, "name"));
        int which = name ? resourceOf(name) : -1;
        if (which < 0)
            continue;
        a->resources[which] = true;
        if (json_is_object(r)) {  // Its own types and prefixes.
            joinStrings(json_object_get(r, "types"), a->types[which], sizeof(a->types[which]));
            joinStrings(json_object_get(r, "idPrefixes"), a->prefixes[which], sizeof(a->prefixes[which]));
        }
    }
    char types[64], prefixes[STREMIO_PREFIXES_MAX];
    joinStrings(json_object_get(root, "types"), types, sizeof(types));
    joinStrings(json_object_get(root, "idPrefixes"), prefixes, sizeof(prefixes));
    for (int r = 0; r < STREMIO_RESOURCES; r++) {
        if (!a->types[r][0])
            snprintf(a->types[r], sizeof(a->types[r]), "%s", types);
        if (!a->prefixes[r][0])
            snprintf(a->prefixes[r], sizeof(a->prefixes[r]), "%s", prefixes);
    }
}

// Its extras: whether search is one, and whether one is required (search only, or one KaiVideo can't give);
// and whether it takes skip (more pages).
static bool readExtras(const json_t *c, bool *search, bool *search_only, bool *skip)
{
    *search = *search_only = *skip = false;
    bool usable = true;
    const json_t *extra = json_object_get(c, "extra");
    for (size_t i = 0; json_is_array(extra) && i < json_array_size(extra); i++) {
        const json_t *e = json_array_get(extra, i);
        const char *name = json_string_value(json_object_get(e, "name"));
        bool required = json_is_true(json_object_get(e, "isRequired"));
        if (name && !strcmp(name, "search")) {
            *search = true;
            *search_only = *search_only || required;
        } else if (name && !strcmp(name, "skip")) {
            *skip = true;
        } else if (required) {
            usable = false;  // A genre or such it must be given.
        }
    }
    const json_t *supported = json_object_get(c, "extraSupported"), *required = json_object_get(c, "extraRequired");
    for (size_t i = 0; json_is_array(supported) && i < json_array_size(supported); i++) {
        const char *name = json_string_value(json_array_get(supported, i));
        *search = *search || (name && !strcmp(name, "search"));
        *skip = *skip || (name && !strcmp(name, "skip"));
    }
    for (size_t i = 0; json_is_array(required) && i < json_array_size(required); i++) {
        const char *name = json_string_value(json_array_get(required, i));
        if (name && !strcmp(name, "search"))
            *search_only = true;
        else
            usable = false;
    }
    return usable;
}

static void readCatalogs(const json_t *root, StremioAddon *a)
{
    const json_t *catalogs = json_object_get(root, "catalogs");
    for (size_t i = 0; json_is_array(catalogs) && i < json_array_size(catalogs); i++) {
        if (a->catalog_count >= STREMIO_CATALOGS_MAX)
            break;
        const json_t *c = json_array_get(catalogs, i);
        StremioCatalogDef d;
        memset(&d, 0, sizeof(d));
        jsonCopy(c, "type", d.type, sizeof(d.type));
        jsonCopy(c, "id", d.id, sizeof(d.id));
        jsonCopy(c, "name", d.name, sizeof(d.name));
        if (!isMovieOrSeries(d.type) || !d.id[0] || !readExtras(c, &d.search, &d.search_only, &d.skip))
            continue;
        if (!d.name[0])
            snprintf(d.name, sizeof(d.name), "%.*s", (int)sizeof(d.name) - 1, d.id);
        a->catalogs[a->catalog_count++] = d;
    }
}

bool stremioParseManifest(const char *json, const char *manifest_url, StremioAddon *out)
{
    memset(out, 0, sizeof(*out));
    json_t *root = json ? json_loads(json, 0, NULL) : NULL;
    if (!json_is_object(root) || !json_is_string(json_object_get(root, "id"))) {
        json_decref(root);
        return false;
    }
    char url[STREMIO_URL_MAX];
    if (!stremioNormalizeUrl(manifest_url, url, sizeof(url))) {
        json_decref(root);
        return false;
    }
    url[strlen(url) - strlen(MANIFEST_TAIL)] = '\0';
    snprintf(out->base, sizeof(out->base), "%s", url);
    jsonCopy(root, "id", out->id, sizeof(out->id));
    jsonCopy(root, "name", out->name, sizeof(out->name));
    if (!out->name[0])
        snprintf(out->name, sizeof(out->name), "%.*s", (int)sizeof(out->name) - 1, out->id);
    readResources(root, out);
    readCatalogs(root, out);
    out->resources[STREMIO_CATALOG] = out->catalog_count > 0;  // Catalogs are known by the list itself.
    const json_t *hints = json_object_get(root, "behaviorHints");
    out->needs_setup = json_is_true(json_object_get(hints, "configurationRequired"));
    json_decref(root);
    return true;
}

static bool inList(const char *list, const char *word)
{
    size_t length = strlen(word);
    for (const char *p = list; *p;) {
        const char *end = strchr(p, ' ');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (n == length && !strncmp(p, word, n))
            return true;
        p = end ? end + 1 : p + n;
    }
    return false;
}

static bool hasPrefix(const char *prefixes, const char *id)
{
    if (!prefixes[0])
        return true;
    for (const char *p = prefixes; *p;) {
        const char *end = strchr(p, ' ');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (n && !strncmp(id, p, n))
            return true;
        p = end ? end + 1 : p + n;
    }
    return false;
}

bool stremioHandles(const StremioAddon *a, StremioResource r, const char *type, const char *id)
{
    return r >= 0 && r < STREMIO_RESOURCES && a->resources[r] && !a->needs_setup &&
           (!a->types[r][0] || inList(a->types[r], type)) && hasPrefix(a->prefixes[r], id);
}

bool stremioCatalogUrl(const StremioAddon *a, const StremioCatalogDef *c, const char *search, char *out, size_t size)
{
    size_t n = 0;
    out[0] = '\0';
    bool ok = append(out, size, &n, a->base) && append(out, size, &n, "/catalog/") &&
              append(out, size, &n, c->type) && append(out, size, &n, "/") && appendEncoded(out, size, &n, c->id);
    if (ok && search)
        ok = append(out, size, &n, "/search=") && appendEncoded(out, size, &n, search);
    return ok && append(out, size, &n, ".json");
}

bool stremioCatalogPageUrl(const StremioAddon *a, const StremioCatalogDef *c, int skip, char *out, size_t size)
{
    char page[32];
    snprintf(page, sizeof(page), "/skip=%d.json", skip > 0 ? skip : 0);
    if (!stremioCatalogUrl(a, c, NULL, out, size))
        return false;
    size_t n = strlen(out) - strlen(".json");  // In place of the first page's ending.
    out[n] = '\0';
    return append(out, size, &n, page);
}

bool stremioResourceUrl(const StremioAddon *a, StremioResource r, const char *type, const char *id, char *out,
                        size_t size)
{
    size_t n = 0;
    out[0] = '\0';
    const char *name = r == STREMIO_META ? "/meta/" : r == STREMIO_STREAM ? "/stream/" : NULL;
    return name && append(out, size, &n, a->base) && append(out, size, &n, name) && append(out, size, &n, type) &&
           append(out, size, &n, "/") && appendEncoded(out, size, &n, id) && append(out, size, &n, ".json");
}

// --- Answers ---

static int yearOf(const json_t *meta)
{
    const char *info = json_string_value(json_object_get(meta, "releaseInfo"));
    if (!info)
        info = json_string_value(json_object_get(meta, "year"));
    long long y = info ? atoll(info) : jsonInt(meta, "year");
    return y >= 1880 && y <= 2100 ? (int)y : 0;
}

void stremioMetasFree(StremioMeta *metas, int count)
{
    for (int i = 0; metas && i < count; i++) {
        free(metas[i].id);
        free(metas[i].name);
        free(metas[i].poster);
        free(metas[i].description);
    }
    free(metas);
}

bool stremioParseMetas(const char *json, StremioMeta **out, int *count)
{
    *out = NULL;
    *count = 0;
    json_t *root = json ? json_loads(json, 0, NULL) : NULL;
    const json_t *metas = json_object_get(root, "metas");
    if (!json_is_array(metas)) {
        json_decref(root);
        return false;
    }
    size_t total = json_array_size(metas) < STREMIO_METAS_MAX ? json_array_size(metas) : STREMIO_METAS_MAX;
    StremioMeta *list = total ? calloc(total, sizeof(StremioMeta)) : NULL;
    for (size_t i = 0; list && i < json_array_size(metas) && (size_t)*count < total; i++) {
        const json_t *m = json_array_get(metas, i);
        StremioMeta *e = &list[*count];
        jsonCopy(m, "type", e->type, sizeof(e->type));
        e->id = jsonDup(m, "id", STREMIO_ID_MAX - 1);
        e->name = jsonDup(m, "name", NAMING_MAX - 1);
        if (!e->id || !e->name || !isMovieOrSeries(e->type)) {
            free(e->id);
            free(e->name);
            memset(e, 0, sizeof(*e));
            continue;
        }
        e->poster = jsonDup(m, "poster", STREMIO_URL_MAX - 1);
        if (e->poster && !isWebUrl(e->poster)) {
            free(e->poster);
            e->poster = NULL;
        }
        e->description = jsonDup(m, "description", TEXT_MAX);
        e->year = yearOf(m);
        (*count)++;
    }
    json_decref(root);
    *out = list;
    return true;
}

void stremioVideosFree(StremioVideo *videos, int count)
{
    for (int i = 0; videos && i < count; i++) {
        free(videos[i].id);
        free(videos[i].title);
        free(videos[i].overview);
    }
    free(videos);
}

static int byEpisode(const void *a, const void *b)
{
    const StremioVideo *x = a, *y = b;
    int sx = x->season == 0 ? 100000 : x->season, sy = y->season == 0 ? 100000 : y->season;  // Specials last.
    if (sx != sy)
        return sx < sy ? -1 : 1;
    return x->episode < y->episode ? -1 : x->episode > y->episode;
}

bool stremioParseVideos(const char *json, StremioVideo **out, int *count)
{
    *out = NULL;
    *count = 0;
    json_t *root = json ? json_loads(json, 0, NULL) : NULL;
    const json_t *meta = json_object_get(root, "meta");
    if (!json_is_object(meta)) {
        json_decref(root);
        return false;
    }
    const json_t *videos = json_object_get(meta, "videos");
    size_t total = json_array_size(videos) < STREMIO_VIDEOS_MAX ? json_array_size(videos) : STREMIO_VIDEOS_MAX;
    StremioVideo *list = total ? calloc(total, sizeof(StremioVideo)) : NULL;
    for (size_t i = 0; list && i < json_array_size(videos) && (size_t)*count < total; i++) {
        const json_t *v = json_array_get(videos, i);
        StremioVideo *e = &list[*count];
        e->id = jsonDup(v, "id", STREMIO_ID_MAX - 1);
        if (!e->id)
            continue;
        long long season = jsonInt(v, "season"), episode = jsonInt(v, "episode");
        if (episode < 0)
            episode = jsonInt(v, "number");
        e->season = season >= 0 && season < 1000 ? (int)season : 1;
        e->episode = episode > 0 && episode < 100000 ? (int)episode : (int)i + 1;
        e->title = jsonDup(v, "title", NAMING_MAX - 1);
        if (!e->title)
            e->title = jsonDup(v, "name", NAMING_MAX - 1);
        e->overview = jsonDup(v, "overview", TEXT_MAX);
        if (!e->overview)
            e->overview = jsonDup(v, "description", TEXT_MAX);
        (*count)++;
    }
    json_decref(root);
    if (*count > 1)
        qsort(list, (size_t)*count, sizeof(*list), byEpisode);
    *out = list;
    return true;
}

void stremioStreamsFree(StremioStream *streams, int count)
{
    for (int i = 0; streams && i < count; i++) {
        free(streams[i].url);
        free(streams[i].name);
        free(streams[i].detail);
    }
    free(streams);
}

// A stream's proxyHeaders: the request headers it needs. False if it asks for one that isn't sent
// (only Referer, User-Agent and Origin are), or for one whose value isn't safe: it wouldn't play.
static bool proxyHeaders(const json_t *proxy, StreamHeaders *out)
{
    memset(out, 0, sizeof(*out));
    const json_t *request = json_object_get(proxy, "request");
    if (!proxy || !request)
        return true;  // No headers asked for (a "response" part alone is the proxy's business).
    if (!json_is_object(request))
        return false;
    const char *name;
    const json_t *value;
    json_object_foreach((json_t *)request, name, value) {
        if (!streamHeadersSet(out, name, json_string_value(value)))
            return false;
    }
    return true;
}

bool stremioParseStreams(const char *json, const char *addon, StremioStream **out, int *count)
{
    json_t *root = json ? json_loads(json, 0, NULL) : NULL;
    const json_t *streams = json_object_get(root, "streams");
    if (!json_is_array(streams)) {
        json_decref(root);
        return false;
    }
    for (size_t i = 0; i < json_array_size(streams) && *count < STREMIO_STREAMS_MAX; i++) {
        const json_t *s = json_array_get(streams, i);
        const char *url = json_string_value(json_object_get(s, "url"));
        const json_t *hints = json_object_get(s, "behaviorHints");
        // Only plain http(s): no torrents, YouTube or outside links.
        StreamHeaders headers;
        if (!isWebUrl(url) || strlen(url) >= STREMIO_URL_MAX * 2 ||
            !proxyHeaders(json_object_get(hints, "proxyHeaders"), &headers))
            continue;
        StremioStream *grown = realloc(*out, (size_t)(*count + 1) * sizeof(StremioStream));
        if (!grown)
            break;
        *out = grown;
        StremioStream *e = &grown[(*count)++];
        memset(e, 0, sizeof(*e));
        e->url = strdup(url);
        e->name = jsonDup(s, "name", 120);
        e->detail = jsonDup(s, "title", 300);
        if (!e->detail)
            e->detail = jsonDup(s, "description", 300);
        snprintf(e->addon, sizeof(e->addon), "%s", addon ? addon : "");
        e->headers = headers;
        if (!e->url)
            (*count)--;
    }
    json_decref(root);
    return true;
}

// --- Locations ---

bool stremioIsLocation(const char *location)
{
    return !strncmp(location, STREMIO_PREFIX, sizeof(STREMIO_PREFIX) - 1);
}

bool stremioLocation(char *out, size_t size, const char *kind, const char *id)
{
    static const char HEX[] = "0123456789ABCDEF";
    size_t n = 0;
    out[0] = '\0';
    if (!id[0] || !append(out, size, &n, STREMIO_PREFIX) || !append(out, size, &n, kind) || !append(out, size, &n, "/"))
        return false;
    for (const unsigned char *p = (const unsigned char *)id; *p; p++) {
        char piece[4] = {(char)*p, '\0'};
        if (!isalnum(*p) && !strchr(":._-", *p)) {
            piece[0] = '%';
            piece[1] = HEX[*p >> 4];
            piece[2] = HEX[*p & 15];
            piece[3] = '\0';
        }
        if (!append(out, size, &n, piece))
            return false;
    }
    return true;
}

static int hexValue(char c)
{
    return isdigit((unsigned char)c) ? c - '0' : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
}

bool stremioParseLocation(const char *location, char *kind, size_t kind_size, char *id, size_t id_size)
{
    if (!stremioIsLocation(location))
        return false;
    const char *p = location + sizeof(STREMIO_PREFIX) - 1, *slash = strchr(p, '/');
    if (!slash || slash == p || (size_t)(slash - p) >= kind_size || !slash[1])
        return false;
    snprintf(kind, kind_size, "%.*s", (int)(slash - p), p);
    size_t n = 0;
    for (const char *q = slash + 1; *q; q++) {
        char c = *q;
        if (c == '%') {
            int hi = hexValue(q[1]), lo = hi >= 0 ? hexValue(q[2]) : -1;
            if (lo < 0)
                return false;
            c = (char)(hi * 16 + lo);
            q += 2;
        }
        if (n + 1 >= id_size || c == '\0')
            return false;
        id[n++] = c;
    }
    id[n] = '\0';
    return true;
}

// --- What the addons gave ---

void stremioListFree(StremioList *l)
{
    stremioMetasFree(l->metas, l->count);
    l->metas = NULL;
    l->count = 0;
}

// A catalog's list's definition, or NULL.
static const StremioCatalogDef *listCatalog(const StremioList *l, const StremioStore *s)
{
    if (l->addon < 0 || l->addon >= s->addon_count || l->catalog < 0 ||
        l->catalog >= s->addons[l->addon].catalog_count)
        return NULL;
    return &s->addons[l->addon].catalogs[l->catalog];
}

int stremioListAppend(StremioList *l, const StremioStore *s, StremioMeta *metas, int count)
{
    int room = STREMIO_LIST_METAS_MAX - l->count, added = 0;
    size_t want = (size_t)l->count + (size_t)(count < room ? count : room);
    StremioMeta *grown = count > 0 && room > 0 ? realloc(l->metas, want * sizeof(StremioMeta)) : NULL;
    if (grown)
        l->metas = grown;
    for (int i = 0; grown && i < count && added < room; i++) {
        bool known = false;
        for (int k = 0; !known && k < l->count; k++)  // Pages may overlap when the catalog changed meanwhile.
            known = !strcmp(l->metas[k].id, metas[i].id);
        if (known)
            continue;
        l->metas[l->count++] = metas[i];
        memset(&metas[i], 0, sizeof(metas[i]));  // Taken.
        added++;
    }
    stremioMetasFree(metas, count);
    l->received += count;
    const StremioCatalogDef *d = listCatalog(l, s);
    l->more = d && d->skip && added > 0 && l->count < STREMIO_LIST_METAS_MAX;
    return added;
}

void stremioStoreFree(StremioStore *s)
{
    for (int i = 0; i < s->list_count; i++)
        stremioListFree(&s->lists[i]);
    s->list_count = 0;
    stremioListFree(&s->found);
    for (int i = 0; i < s->series_count; i++) {
        free(s->series[i].id);
        stremioVideosFree(s->series[i].videos, s->series[i].count);
    }
    free(s->series);
    s->series = NULL;
    s->series_count = 0;
    s->addon_count = 0;
}

static StremioEpisodes *findSeries(const StremioStore *s, const char *id)
{
    for (int i = 0; i < s->series_count; i++)
        if (!strcmp(s->series[i].id, id))
            return &s->series[i];
    return NULL;
}

void stremioStoreSetEpisodes(StremioStore *s, const char *series_id, StremioVideo *videos, int count)
{
    StremioEpisodes *e = findSeries(s, series_id);
    if (!e) {
        StremioEpisodes *grown = realloc(s->series, (size_t)(s->series_count + 1) * sizeof(*grown));
        char *id = grown ? strdup(series_id) : NULL;
        if (!id) {
            if (grown)
                s->series = grown;
            stremioVideosFree(videos, count);
            return;
        }
        s->series = grown;
        e = &s->series[s->series_count++];
        memset(e, 0, sizeof(*e));
        e->id = id;
    }
    stremioVideosFree(e->videos, e->count);
    e->videos = videos;
    e->count = videos ? count : 0;
    e->state = videos && count > 0 ? STREMIO_EPISODES_READ : STREMIO_EPISODES_FAILED;
}

StremioEpisodesState stremioStoreEpisodesState(const StremioStore *s, const char *series_id)
{
    const StremioEpisodes *e = findSeries(s, series_id);
    return e ? e->state : STREMIO_EPISODES_UNREAD;
}

// The locations added so far, so each film and series is added once.
typedef struct {
    char **slots;
    size_t size;
} Seen;

static unsigned long hashOf(const char *s)
{
    unsigned long h = 5381;
    while (*s)
        h = h * 33 + (unsigned char)*s++;
    return h;
}

// True if it's new (and now seen).
static bool see(Seen *seen, const char *key)
{
    if (!seen->slots)
        return true;
    size_t i = hashOf(key) % seen->size;
    while (seen->slots[i]) {
        if (!strcmp(seen->slots[i], key))
            return false;
        i = (i + 1) % seen->size;
    }
    seen->slots[i] = strdup(key);
    return true;
}

static void posterKey(char *out, size_t size, const char *url)
{
    snprintf(out, size, "%s%s", url ? LIBRARY_POSTER_PREFIX : "", url ? url : "");
}

static void addMeta(Library *lib, const StremioStore *s, const StremioMeta *m, int collection, Seen *seen,
                    const char *unread, const char *failed)
{
    char path[LIBRARY_PATH_MAX], art[STREMIO_URL_MAX + 8];
    bool series = !strcmp(m->type, "series");
    if (!stremioLocation(path, sizeof(path), series ? "show" : "movie", m->id) || !see(seen, path))
        return;
    posterKey(art, sizeof(art), m->poster);
    VideoName name;
    memset(&name, 0, sizeof(name));
    if (!series) {
        snprintf(name.title, sizeof(name.title), "%s", m->name);
        name.year = m->year;
        LibraryRemote remote = {.art = art, .overview = m->description, .in_collection = collection + 1};
        libraryAddRemote(lib, path, &name, 0, 0, &remote);
        return;
    }
    name.is_episode = true;
    snprintf(name.show, sizeof(name.show), "%s", m->name);
    const StremioEpisodes *e = findSeries(s, m->id);
    if (!e || e->state != STREMIO_EPISODES_READ) {  // A stand-in until they're read.
        name.season = name.episode = 1;
        snprintf(name.title, sizeof(name.title), "%s", e ? failed : unread);
        LibraryRemote remote = {.series_art = art, .overview = m->description, .in_collection = collection + 1};
        libraryAddRemote(lib, path, &name, 0, 0, &remote);
        return;
    }
    for (int i = 0; i < e->count; i++) {
        const StremioVideo *v = &e->videos[i];
        if (!stremioLocation(path, sizeof(path), "series", v->id))
            continue;
        name.season = v->season;
        name.episode = v->episode;
        snprintf(name.title, sizeof(name.title), "%s", v->title ? v->title : "");
        LibraryRemote remote = {.series_art = art, .overview = v->overview ? v->overview : m->description,
                                .in_collection = collection + 1};
        libraryAddRemote(lib, path, &name, 0, 0, &remote);
    }
}

void stremioFillLibrary(Library *lib, const StremioStore *s, const char *unread, const char *failed)
{
    size_t total = (size_t)s->found.count + 1;
    for (int i = 0; i < s->list_count; i++)
        total += (size_t)s->lists[i].count;
    Seen seen = {calloc(total * 2, sizeof(char *)), total * 2};
    for (int i = 0; i < s->list_count; i++) {
        const StremioList *l = &s->lists[i];
        int collection = libraryAddCollection(lib, l->title, !strcmp(l->type, "series") ? COLLECTION_SERIES
                                                                                         : COLLECTION_FILMS);
        for (int j = 0; j < l->count; j++)
            addMeta(lib, s, &l->metas[j], collection, &seen, unread, failed);
    }
    for (int j = 0; j < s->found.count; j++)
        addMeta(lib, s, &s->found.metas[j], -1, &seen, unread, failed);
    for (size_t i = 0; seen.slots && i < seen.size; i++)
        free(seen.slots[i]);
    free(seen.slots);
}
