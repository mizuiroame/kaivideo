#include "jellyfin.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>

#define DEFAULT_PORT "8096"
#define MAX_HD_WIDTH 2000
#define MAX_BITS 10
#define TICKS_TO_NS 100
#define CACHE_LINE_MAX 65536
#define CACHE_MAGIC "kaivideo-jellyfin-2"
#define CACHE_FIELDS 16
#define FIRST_CAPACITY 512

bool jfIsLocation(const char *path)
{
    return strncmp(path, JF_PREFIX, sizeof(JF_PREFIX) - 1) == 0;
}

static bool isId(const char *text, size_t length)
{
    if (length == 0 || length >= JF_ID_MAX)
        return false;
    for (size_t i = 0; i < length; i++)
        if (!isalnum((unsigned char)text[i]) && text[i] != '-')
            return false;
    return true;
}

bool jfLocationIds(const char *location, char *item, char *source)
{
    if (!jfIsLocation(location))
        return false;
    const char *start = location + sizeof(JF_PREFIX) - 1, *slash = strchr(start, '/');
    if (!slash || !isId(start, (size_t)(slash - start)) || !isId(slash + 1, strlen(slash + 1)))
        return false;
    snprintf(item, JF_ID_MAX, "%.*s", (int)(slash - start), start);
    snprintf(source, JF_ID_MAX, "%s", slash + 1);
    return true;
}

void jfLocationMake(char *out, size_t size, const char *item, const char *source)
{
    snprintf(out, size, JF_PREFIX "%s/%s", item, source);
}

bool jfServerNormalize(const char *text, char *out, size_t size)
{
    while (isspace((unsigned char)*text))
        text++;
    size_t length = strlen(text);
    while (length && (isspace((unsigned char)text[length - 1]) || text[length - 1] == '/'))
        length--;
    for (size_t i = 0; i < length; i++)
        if (isspace((unsigned char)text[i]) || strchr("\"\\<>", text[i]))
            return false;
    const char *scheme = strstr(text, "://");
    bool has_scheme = scheme && (size_t)(scheme - text) < length;
    const char *host = has_scheme ? scheme + 3 : text;
    size_t host_length = length - (size_t)(host - text);
    if (host_length == 0)
        return false;
    bool web = has_scheme && (!strncmp(text, "http://", 7) || !strncmp(text, "https://", 8));
    if (has_scheme && !web)
        return false;
    const char *rest = memchr(host, '/', host_length);
    size_t authority = rest ? (size_t)(rest - host) : host_length;
    bool has_port = memchr(host, ':', authority) != NULL;
    bool add_port = !has_port && !rest && !(has_scheme && !strncmp(text, "https://", 8));
    int n = snprintf(out, size, "%s%.*s%s", has_scheme ? "" : "http://", (int)length, text,
                     add_port ? ":" DEFAULT_PORT : "");
    return n > 0 && (size_t)n < size;
}

void jfAuthHeader(const JfSession *s, const char *version, char *out, size_t size)
{
    snprintf(out, size, "MediaBrowser Client=\"KaiVideo\", Device=\"Nintendo Switch 2\", DeviceId=\"%s\", Version=\"%s\"%s%s%s",
             s->device_id, version, s->token[0] ? ", Token=\"" : "", s->token, s->token[0] ? "\"" : "");
}

// --- The session on the card ---

static void readSessionLine(JfSession *s, const char *line)
{
    static const struct {
        const char *key;
        size_t offset, size;
    } FIELDS[] = {
        {"server=", offsetof(JfSession, server), JF_SERVER_MAX},
        {"user_id=", offsetof(JfSession, user_id), JF_ID_MAX},
        {"user=", offsetof(JfSession, user_name), JF_NAME_MAX},
        {"token=", offsetof(JfSession, token), JF_TOKEN_MAX},
        {"device=", offsetof(JfSession, device_id), JF_ID_MAX},
    };
    for (size_t i = 0; i < sizeof(FIELDS) / sizeof(FIELDS[0]); i++) {
        size_t n = strlen(FIELDS[i].key);
        if (!strncmp(line, FIELDS[i].key, n))
            snprintf((char *)s + FIELDS[i].offset, FIELDS[i].size, "%s", line + n);
    }
}

bool jfSessionLoad(JfSession *s, const char *file)
{
    memset(s, 0, sizeof(*s));
    FILE *f = fopen(file, "r");
    if (!f)
        return false;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        readSessionLine(s, line);
    }
    fclose(f);
    return true;
}

bool jfSessionSave(const JfSession *s, const char *file)
{
    FILE *f = fopen(file, "w");
    if (!f)
        return false;
    fprintf(f, "server=%s\nuser_id=%s\nuser=%s\ntoken=%s\ndevice=%s\n", s->server, s->user_id, s->user_name, s->token,
            s->device_id);
    return fclose(f) == 0;
}

// --- What the console can play ---

static bool containerIsKnown(const char *container)
{
    static const char *const KNOWN[] = {"mkv", "matroska", "mp4", "m4v", "mov", "webm"};
    for (size_t i = 0; i < sizeof(KNOWN) / sizeof(KNOWN[0]); i++)
        if (strstr(container, KNOWN[i]))
            return true;
    return !container[0];
}

JfPlayability jfPlayability(const JfVideoFacts *v)
{
    bool hevc = !strcmp(v->codec, "hevc"), h264 = !strcmp(v->codec, "h264");
    bool hardware_only = !strcmp(v->codec, "vp9") || !strcmp(v->codec, "av1") || !strcmp(v->codec, "vp8");
    if (v->width > MAX_HD_WIDTH)
        return JF_TOO_LARGE;
    if (v->range[0] && strcmp(v->range, "SDR"))
        return JF_HDR;
    if (v->bitrate > JF_MAX_BITRATE)
        return JF_TOO_HEAVY;
    if (!containerIsKnown(v->container) || !(hevc || h264 || hardware_only) || v->bits > MAX_BITS ||
        (hardware_only && v->bits > 8))
        return JF_FORMAT;
    return JF_PLAYABLE;
}

const char *jfPlayabilityMessage(JfPlayability p)
{
    switch (p) {
    case JF_TOO_LARGE: return "4K videos are too heavy for the console.";
    case JF_HDR: return "HDR videos can't be played yet.";
    case JF_TOO_HEAVY: return "This video's bitrate is too high for the console.";
    case JF_FORMAT: return "KaiVideo can't play this video's format.";
    default: return "";
    }
}

// Days since 1970 of a date of the Gregorian calendar.
static int64_t daysFromCivil(int year, int month, int day)
{
    year -= month <= 2;
    int64_t era = (year >= 0 ? year : year - 399) / 400;
    int64_t yoe = year - era * 400;
    int64_t doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

int64_t jfParseDate(const char *text)
{
    int year, month, day, hour, minute, second;
    if (!text || sscanf(text, "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6)
        return 0;
    if (year < 1971 || month < 1 || month > 12 || day < 1 || day > 31)
        return 0;
    return daysFromCivil(year, month, day) * 86400 + hour * 3600 + minute * 60 + second;
}

// --- Catalogs ---

void jfCatalogInit(JfCatalog *c)
{
    memset(c, 0, sizeof(*c));
    libraryInit(&c->library, JF_ROOT);
}

void jfCatalogFree(JfCatalog *c)
{
    libraryFree(&c->library);
    free(c->users);
    c->users = NULL;
    c->capacity = 0;
}

// Room for one more user entry.
static bool growUsers(JfCatalog *c)
{
    if (c->library.count < c->capacity)
        return true;
    size_t grown = c->capacity ? c->capacity * 2 : FIRST_CAPACITY;
    JfUserData *users = realloc(c->users, grown * sizeof(*users));
    if (!users)
        return false;
    c->users = users;
    c->capacity = grown;
    return true;
}

static const char *textOf(const json_t *object, const char *key)
{
    const char *value = json_string_value(json_object_get(object, key));
    return value ? value : "";
}

static int intOf(const json_t *object, const char *key)
{
    return (int)json_integer_value(json_object_get(object, key));
}

static const json_t *videoStream(const json_t *source)
{
    size_t i;
    const json_t *stream;
    json_array_foreach(json_object_get(source, "MediaStreams"), i, stream)
        if (!strcmp(textOf(stream, "Type"), "Video"))
            return stream;
    return NULL;
}

static void readName(const json_t *item, VideoName *name)
{
    memset(name, 0, sizeof(*name));
    snprintf(name->title, sizeof(name->title), "%s", textOf(item, "Name"));
    name->year = intOf(item, "ProductionYear");
    if (strcmp(textOf(item, "Type"), "Episode"))
        return;
    name->is_episode = true;
    snprintf(name->show, sizeof(name->show), "%s", textOf(item, "SeriesName"));
    name->season = intOf(item, "ParentIndexNumber");
    name->episode = intOf(item, "IndexNumber");
}

static void readUserData(const json_t *item, JfUserData *out)
{
    const json_t *data = json_object_get(item, "UserData");
    out->position_ns = json_integer_value(json_object_get(data, "PlaybackPositionTicks")) * TICKS_TO_NS;
    out->last_played = jfParseDate(textOf(data, "LastPlayedDate"));
    out->played = json_is_true(json_object_get(data, "Played"));
}

// One film or episode with a file behind it. False if it has none.
static bool addItem(JfCatalog *c, const json_t *item)
{
    const json_t *source = json_array_get(json_object_get(item, "MediaSources"), 0);
    const json_t *stream = source ? videoStream(source) : NULL;
    const char *id = textOf(item, "Id"), *source_id = textOf(source, "Id");
    if (!stream || !isId(id, strlen(id)) || !isId(source_id, strlen(source_id)) || !growUsers(c))
        return false;
    VideoName name;
    readName(item, &name);
    if (name.is_episode && !name.show[0])
        return false;
    JfVideoFacts facts = {textOf(stream, "Codec"), textOf(stream, "VideoRange"), textOf(source, "Container"),
                          intOf(stream, "Width"), intOf(stream, "BitDepth"),
                          json_integer_value(json_object_get(source, "Bitrate"))};
    char location[LIBRARY_PATH_MAX], art[LIBRARY_PATH_MAX] = "";
    const char *series = textOf(item, "SeriesId");
    jfLocationMake(location, sizeof(location), id, source_id);
    if (isId(series, strlen(series)))
        jfLocationMake(art, sizeof(art), series, JF_SERIES_SOURCE);
    LibraryRemote remote = {
        .overview = textOf(item, "Overview"),
        .series_art = art,
        .runtime_ns = json_integer_value(json_object_get(item, "RunTimeTicks")) * TICKS_TO_NS,
        .blocked = (int)jfPlayability(&facts),
    };
    int64_t size = json_integer_value(json_object_get(source, "Size"));
    if (!libraryAddRemote(&c->library, location, &name, size, jfParseDate(textOf(item, "DateCreated")), &remote))
        return false;
    readUserData(item, &c->users[c->library.count - 1]);
    return true;
}

int jfCatalogAddPage(JfCatalog *c, const char *json, int *total)
{
    json_t *root = json_loads(json, 0, NULL);
    const json_t *items = json_object_get(root, "Items");
    if (!json_is_array(items)) {
        json_decref(root);
        return -1;
    }
    if (total)
        *total = intOf(root, "TotalRecordCount");
    size_t i;
    const json_t *item;
    json_array_foreach(items, i, item)
        addItem(c, item);
    int count = (int)json_array_size(items);
    json_decref(root);
    return count;
}

// --- Keeping a catalog ---

// Tabs and line breaks would break the line it's written on.
static void writeField(FILE *f, const char *text)
{
    for (; *text; text++)
        fputc(*text == '\t' || *text == '\n' || *text == '\r' ? ' ' : *text, f);
    fputc('\t', f);
}

bool jfCatalogSave(const JfCatalog *c, const char *file)
{
    FILE *f = fopen(file, "w");
    if (!f)
        return false;
    fprintf(f, CACHE_MAGIC "\n");
    for (size_t i = 0; i < c->library.count; i++) {
        const LibraryItem *it = &c->library.items[i];
        const JfUserData *u = &c->users[i];
        fprintf(f, "%" PRId64 "\t%" PRId64 "\t%" PRId64 "\t%d\t%d\t%d\t%d\t%d\t%" PRId64 "\t%" PRId64 "\t%d\t",
                it->size, it->added, it->runtime_ns, it->blocked, it->name.is_episode, it->name.season,
                it->name.episode, it->name.year, u->position_ns, u->last_played, u->played);
        writeField(f, it->path);
        writeField(f, it->name.title);
        writeField(f, it->name.show);
        writeField(f, it->overview ? it->overview : "");
        writeField(f, it->series_art ? it->series_art : "");
        fputc('\n', f);
    }
    return fclose(f) == 0;
}

// Splits a line at its tabs. How many fields there are.
static int splitFields(char *line, char *fields[CACHE_FIELDS])
{
    int n = 0;
    while (n < CACHE_FIELDS) {
        fields[n++] = line;
        char *tab = strchr(line, '\t');
        if (!tab)
            break;
        *tab = '\0';
        line = tab + 1;
    }
    return n;
}

static void loadLine(JfCatalog *c, char *line)
{
    char *v[CACHE_FIELDS];
    if (splitFields(line, v) < CACHE_FIELDS || !jfIsLocation(v[11]) || !growUsers(c))
        return;
    VideoName name = {.is_episode = atoi(v[4]) != 0, .season = atoi(v[5]), .episode = atoi(v[6]), .year = atoi(v[7])};
    snprintf(name.title, sizeof(name.title), "%s", v[12]);
    snprintf(name.show, sizeof(name.show), "%s", v[13]);
    LibraryRemote remote = {.overview = v[14], .series_art = v[15], .runtime_ns = strtoll(v[2], NULL, 10),
                            .blocked = atoi(v[3])};
    if (!libraryAddRemote(&c->library, v[11], &name, strtoll(v[0], NULL, 10), strtoll(v[1], NULL, 10), &remote))
        return;
    c->users[c->library.count - 1] = (JfUserData){strtoll(v[8], NULL, 10), strtoll(v[9], NULL, 10), atoi(v[10]) != 0};
}

bool jfCatalogLoad(JfCatalog *c, const char *file)
{
    FILE *f = fopen(file, "r");
    if (!f)
        return false;
    char *line = malloc(CACHE_LINE_MAX);
    bool ok = line && fgets(line, CACHE_LINE_MAX, f) && !strncmp(line, CACHE_MAGIC, sizeof(CACHE_MAGIC) - 1);
    while (ok && fgets(line, CACHE_LINE_MAX, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        loadLine(c, line);
    }
    free(line);
    fclose(f);
    if (ok)
        libraryBuild(&c->library);
    return ok;
}

// --- Where videos were left ---

static const JfCatalog *g_sorting;  // qsort has no context.

static int compareLastPlayed(const void *pa, const void *pb)
{
    int a = *(const int *)pa, b = *(const int *)pb;
    int64_t ta = g_sorting->users[a].last_played, tb = g_sorting->users[b].last_played;
    return ta < tb ? -1 : ta > tb ? 1 : a - b;
}

static void dropRemoteEntries(ResumeList *resume, Watched *watched)
{
    size_t kept = 0;
    for (size_t i = 0; i < resume->count; i++)
        if (!jfIsLocation(resume->items[i].path))
            resume->items[kept++] = resume->items[i];
    resume->count = kept;
    kept = 0;
    for (size_t i = 0; i < watched->count; i++)
        if (!jfIsLocation(watched->paths[i])) {
            if (kept != i)
                memcpy(watched->paths[kept], watched->paths[i], WATCHED_PATH_MAX);
            kept++;
        }
    watched->count = kept;
}

void jfMergeUserData(const JfCatalog *c, ResumeList *resume, Watched *watched)
{
    size_t count = c->library.count;
    int *order = malloc((count ? count : 1) * sizeof(int));
    if (!order)
        return;
    dropRemoteEntries(resume, watched);
    for (size_t i = 0; i < count; i++)
        order[i] = (int)i;
    g_sorting = c;
    qsort(order, count, sizeof(int), compareLastPlayed);  // The oldest first: the latest ends on top.
    for (size_t n = 0; n < count; n++) {
        const LibraryItem *item = &c->library.items[order[n]];
        const JfUserData *user = &c->users[order[n]];
        if (user->played)
            watchedSet(watched, item->path, true);
        if (user->position_ns > 0)
            resumeRemember(resume, item->path, user->position_ns, item->runtime_ns);
    }
    free(order);
}
