#include "jellyfin.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <jansson.h>

#define DEFAULT_PORT "8096"
#define DEFAULT_HTTPS_PORT "8920"
#define HTTP_BAD_REQUEST 400
#define MAX_HD_WIDTH 2000
#define MAX_BITS 10
#define TICKS_TO_NS 100
#define CACHE_LINE_MAX 65536
#define CACHE_MAGIC "kaivideo-jellyfin-3"
#define CACHE_FIELDS 18
#define CACHE_COLLECTION "#collection\t"  // A library: its kind, then its name.
#define FIRST_CAPACITY 512

ServerKind serverKindOf(const char *location)
{
    return !strncmp(location, PLEX_PREFIX, sizeof(PLEX_PREFIX) - 1) ? SERVER_PLEX
           : jfIsEmbyLocation(location)                             ? SERVER_EMBY
                                                                    : SERVER_JELLYFIN;
}

bool jfIsEmbyLocation(const char *path);

bool jfIsEmbyLocation(const char *path)
{
    return strncmp(path, EMBY_PREFIX, sizeof(EMBY_PREFIX) - 1) == 0;
}

bool jfIsLocation(const char *path)
{
    return strncmp(path, JF_PREFIX, sizeof(JF_PREFIX) - 1) == 0 || jfIsEmbyLocation(path);
}

const char *serverKindName(ServerKind kind)
{
    static const char *const NAMES[SERVER_KIND_COUNT] = {"Jellyfin", "Emby", "Plex"};
    return kind >= 0 && kind < SERVER_KIND_COUNT ? NAMES[kind] : NAMES[0];
}

bool jfIsRemoteLocation(const char *path)
{
    return jfIsLocation(path) || strncmp(path, PLEX_PREFIX, sizeof(PLEX_PREFIX) - 1) == 0;
}

static bool isId(const char *text, size_t length)
{
    if (length == 0 || length >= JF_ID_MAX)
        return false;
    for (size_t i = 0; i < length; i++)
        if (!isalnum((unsigned char)text[i]) && text[i] != '-' && text[i] != '_')  // Emby: "mediasource_17".
            return false;
    return true;
}

bool jfLocationIds(const char *location, char *item, char *source)
{
    if (!jfIsLocation(location))
        return false;
    const char *start = strchr(location, ':') + 1, *slash = strchr(start, '/');
    if (!slash || !isId(start, (size_t)(slash - start)) || !isId(slash + 1, strlen(slash + 1)))
        return false;
    snprintf(item, JF_ID_MAX, "%.*s", (int)(slash - start), start);
    snprintf(source, JF_ID_MAX, "%s", slash + 1);
    return true;
}

void jfLocationMake(char *out, size_t size, const char *item, const char *source)
{
    jfLocationMakeWith(out, size, JF_PREFIX, item, source);
}

void jfLocationMakeWith(char *out, size_t size, const char *prefix, const char *item, const char *source)
{
    snprintf(out, size, "%s%s/%s", prefix, item, source);
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
    case JF_TOO_LARGE: return "4K: your server converts it for the console.";
    case JF_HDR: return "HDR: your server converts it for the console.";
    case JF_TOO_HEAVY: return "High bitrate: your server converts it for the console.";
    case JF_FORMAT: return "Your server converts this format for the console.";
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

static const char *textOf(const json_t *object, const char *key);
static int intOf(const json_t *object, const char *key);

// --- How to play a video ---

// What the console takes as it is, and what the server should make otherwise.
#define JF_DEVICE_PROFILE                                                                                              \
    "{\"Name\":\"KaiVideo\",\"MaxStreamingBitrate\":%lld,"                                                           \
    "\"DirectPlayProfiles\":[{\"Type\":\"Video\",\"Container\":\"mkv,matroska,mp4,m4v,mov,webm\","                   \
    "\"VideoCodec\":\"h264,hevc,vp9,av1,vp8\",\"AudioCodec\":\"aac,mp3,opus,vorbis,flac,ac3,eac3,dts,truehd\"}],"     \
    "\"TranscodingProfiles\":[{\"Type\":\"Video\",\"Container\":\"ts\",\"Protocol\":\"hls\","                       \
    "\"Context\":\"Streaming\",\"VideoCodec\":\"h264\",\"AudioCodec\":\"aac\",\"MaxAudioChannels\":\"2\","          \
    "\"MinSegments\":1,\"BreakOnNonKeyFrames\":true}],"                                                               \
    "\"CodecProfiles\":[{\"Type\":\"Video\",\"Conditions\":["                                                       \
    "{\"Condition\":\"EqualsAny\",\"Property\":\"VideoRangeType\",\"Value\":\"SDR\",\"IsRequired\":true},"          \
    "{\"Condition\":\"LessThanEqual\",\"Property\":\"Width\",\"Value\":\"2000\",\"IsRequired\":true},"              \
    "{\"Condition\":\"LessThanEqual\",\"Property\":\"VideoBitDepth\",\"Value\":\"10\",\"IsRequired\":true},"         \
    "{\"Condition\":\"LessThanEqual\",\"Property\":\"VideoBitrate\",\"Value\":\"%lld\",\"IsRequired\":true}]},"     \
    "{\"Type\":\"Video\",\"Codec\":\"vp9,av1,vp8\",\"Conditions\":["                                               \
    "{\"Condition\":\"LessThanEqual\",\"Property\":\"VideoBitDepth\",\"Value\":\"8\",\"IsRequired\":true}]}],"     \
    "\"SubtitleProfiles\":[{\"Format\":\"srt\",\"Method\":\"External\"},{\"Format\":\"subrip\",\"Method\":\"External\"},"\
    "{\"Format\":\"ass\",\"Method\":\"External\"},{\"Format\":\"ssa\",\"Method\":\"External\"},"                   \
    "{\"Format\":\"vtt\",\"Method\":\"External\"}]}"

void jfPlaybackBody(char *out, size_t size, const char *user_id, long long max_bitrate, int audio_index,
                    int subtitle_index)
{
    int n = snprintf(out, size, "{\"UserId\":\"%s\",\"MaxStreamingBitrate\":%lld,\"EnableDirectPlay\":true,"
                                "\"EnableDirectStream\":false,\"EnableTranscoding\":true,\"AutoOpenLiveStream\":true,"
                                "\"SubtitleStreamIndex\":%d,",
                     user_id, max_bitrate, subtitle_index);
    if (audio_index >= 0)
        n += snprintf(out + n, size - (size_t)n, "\"AudioStreamIndex\":%d,", audio_index);
    n += snprintf(out + n, size - (size_t)n, "\"DeviceProfile\":" JF_DEVICE_PROFILE "}", max_bitrate,
                  max_bitrate < JF_MAX_BITRATE ? max_bitrate : JF_MAX_BITRATE);
    (void)n;
}

static void readTrack(const json_t *stream, JfTrack *t)
{
    t->index = intOf(stream, "Index");
    t->external = json_is_true(json_object_get(stream, "IsExternal"));
    const char *name = textOf(stream, "DisplayTitle");
    snprintf(t->name, sizeof(t->name), "%s", name[0] ? name : textOf(stream, "Language"));
}

static void readTracks(const json_t *source, JfPlan *p)
{
    int wanted_audio = intOf(source, "DefaultAudioStreamIndex");
    const json_t *default_sub = json_object_get(source, "DefaultSubtitleStreamIndex");
    p->audio_current = p->sub_default = -1;
    size_t i;
    const json_t *stream;
    json_array_foreach(json_object_get(source, "MediaStreams"), i, stream) {
        const char *type = textOf(stream, "Type");
        if (!strcmp(type, "Audio") && p->audio_count < JF_PLAN_AUDIO) {
            readTrack(stream, &p->audio[p->audio_count]);
            if (p->audio[p->audio_count].index == wanted_audio)
                p->audio_current = p->audio_count;
            p->audio_count++;
        } else if (!strcmp(type, "Subtitle") && p->sub_count < JF_PLAN_SUBS &&
                   json_is_true(json_object_get(stream, "IsTextSubtitleStream"))) {
            readTrack(stream, &p->subs[p->sub_count]);
            bool named = default_sub && json_integer_value(default_sub) >= 0;
            if (named ? p->subs[p->sub_count].index == json_integer_value(default_sub)
                      : p->sub_default < 0 && json_is_true(json_object_get(stream, "IsDefault")))
                p->sub_default = p->sub_count;
            p->sub_count++;
        } else if (!strcmp(type, "Subtitle") && p->picture_count < JF_PLAN_PICTURES) {
            readTrack(stream, &p->pictures[p->picture_count++]);
        }
    }
    if (p->audio_current < 0 && p->audio_count)
        p->audio_current = 0;
}

void jfPictureLabel(const JfTrack *t, char *out, size_t size)
{
    snprintf(out, size, "%s (burned in)", t->name);
}

// The picture subtitle the server draws on the conversion: its address says
// "SubtitleStreamIndex=N&SubtitleMethod=Encode". An index in pictures, -1 for none.
static int burnedPicture(const JfPlan *p)
{
    const char *at = strstr(p->transcode_url, "SubtitleStreamIndex=");
    if (!at || !strstr(p->transcode_url, "SubtitleMethod=Encode"))
        return -1;
    int index = atoi(at + sizeof("SubtitleStreamIndex=") - 1);
    for (int i = 0; i < p->picture_count; i++)
        if (p->pictures[i].index == index)
            return i;
    return -1;
}

bool jfPlanParse(const char *json, JfPlan *out)
{
    memset(out, 0, sizeof(*out));
    json_t *root = json_loads(json, 0, NULL);
    const json_t *source = json_array_get(json_object_get(root, "MediaSources"), 0);
    const char *url = textOf(source, "TranscodingUrl");
    out->direct = json_is_true(json_object_get(source, "SupportsDirectPlay"));
    bool ok = source && (out->direct || (url[0] && strlen(url) < sizeof(out->transcode_url)));
    if (ok) {
        snprintf(out->transcode_url, sizeof(out->transcode_url), "%s", url);
        snprintf(out->play_session, sizeof(out->play_session), "%s", textOf(root, "PlaySessionId"));
        snprintf(out->live_stream_id, sizeof(out->live_stream_id), "%s", textOf(source, "LiveStreamId"));
        readTracks(source, out);
        out->picture_burned = burnedPicture(out);
    }
    json_decref(root);
    return ok;
}

#define REPORT_NS_PER_TICK 100

void jfReportBody(char *out, size_t size, const JfReport *r)
{
    bool live = !strcmp(r->source, "live");
    int n = snprintf(out, size, "{\"ItemId\":\"%s\",", r->item);
    if (!live && n > 0 && (size_t)n < size)
        n += snprintf(out + n, size - (size_t)n, "\"MediaSourceId\":\"%s\",", r->source);
    if (live && r->live_stream[0] && n > 0 && (size_t)n < size)
        n += snprintf(out + n, size - (size_t)n, "\"LiveStreamId\":\"%s\",", r->live_stream);
    if (n > 0 && (size_t)n < size)
        snprintf(out + n, size - (size_t)n,
                 "\"PositionTicks\":%" PRId64 ",\"IsPaused\":%s,\"CanSeek\":%s,\"PlayMethod\":\"%s\","
                 "\"PlaySessionId\":\"%s\"}",
                 r->position_ns / REPORT_NS_PER_TICK, r->paused ? "true" : "false", live ? "false" : "true",
                 r->converted ? "Transcode" : "DirectPlay", r->play_session);
}

void jfLiveInfoBody(char *out, size_t size, const char *user_id)
{
    snprintf(out, size, "{\"UserId\":\"%s\",\"AutoOpenLiveStream\":false,\"EnableDirectPlay\":true}", user_id);
}

// An http(s) address whose path (not its query) ends in ".m3u8".
static bool isHlsAddress(const char *url)
{
    if (strncmp(url, "http://", 7) && strncmp(url, "https://", 8))
        return false;
    size_t path = strcspn(url, "?#"), ext = sizeof(".m3u8") - 1;
    return path > ext && !strncasecmp(url + path - ext, ".m3u8", ext);
}

bool jfLivePlanParse(const char *json, JfPlan *out)
{
    memset(out, 0, sizeof(*out));
    json_t *root = json_loads(json, 0, NULL);
    const json_t *source = json_array_get(json_object_get(root, "MediaSources"), 0);
    const char *path = textOf(source, "Path");
    bool ok = source && !strcmp(textOf(source, "Protocol"), "Http") &&
              json_is_true(json_object_get(source, "IsInfiniteStream")) && isHlsAddress(path) &&
              strlen(path) < sizeof(out->source_url);
    if (ok) {
        out->direct = true;
        out->audio_current = out->sub_default = out->picture_burned = -1;
        snprintf(out->source_url, sizeof(out->source_url), "%s", path);
        snprintf(out->play_session, sizeof(out->play_session), "%s", textOf(root, "PlaySessionId"));
    }
    json_decref(root);
    return ok;
}

// --- Catalogs ---

void jfCatalogInit(JfCatalog *c)
{
    memset(c, 0, sizeof(*c));
    libraryInit(&c->library, JF_ROOT);
    c->adding_collection = -1;
    snprintf(c->prefix, sizeof(c->prefix), "%s", JF_PREFIX);
}

bool jfCatalogAppend(JfCatalog *dst, const JfCatalog *src, const char *suffix)
{
    int map[LIBRARY_COLLECTIONS];  // src's library -> dst's, -1 if it didn't fit.
    for (int i = 0; i < src->library.collection_count; i++) {
        char name[LIBRARY_COLLECTION_NAME];
        snprintf(name, sizeof(name), "%s%s", src->library.collections[i].name, suffix ? suffix : "");
        map[i] = libraryAddCollection(&dst->library, name, src->library.collections[i].kind);
    }
    for (size_t i = 0; i < src->library.count; i++) {
        const LibraryItem *item = &src->library.items[i];
        int collection = item->collection >= 0 && item->collection < src->library.collection_count
                             ? map[item->collection] : -1;
        LibraryRemote remote = {.overview = item->overview, .series_art = item->series_art,
                                .runtime_ns = item->runtime_ns, .blocked = item->blocked,
                                .favorite = item->favorite, .in_collection = collection + 1};
        if (!jfCatalogAddVideo(dst, item->path, &item->name, item->size, item->added, &remote, &src->users[i]))
            return false;
    }
    return true;
}

void jfCatalogFree(JfCatalog *c)
{
    libraryFree(&c->library);
    free(c->users);
    c->users = NULL;
    c->capacity = 0;
    free(c->favorite_series);
    c->favorite_series = NULL;
    c->favorite_series_count = 0;
}

// What a library holds, from its CollectionType; false for what isn't videos.
static bool collectionKind(const char *type, CollectionKind *kind)
{
    if (!type[0] || !strcmp(type, "mixed") || !strcmp(type, "homevideos")) {
        *kind = COLLECTION_MIXED;
        return true;
    }
    if (!strcmp(type, "movies"))
        *kind = COLLECTION_FILMS;
    else if (!strcmp(type, "tvshows"))
        *kind = COLLECTION_SERIES;
    else
        return false;
    return true;
}

int jfCatalogAddViews(JfCatalog *c, const char *json)
{
    json_t *root = json_loads(json, 0, NULL);
    const json_t *items = json_object_get(root, "Items");
    if (!json_is_array(items)) {
        json_decref(root);
        return -1;
    }
    int added = 0;
    size_t i;
    const json_t *view;
    json_array_foreach(items, i, view) {
        CollectionKind kind;
        const char *id = textOf(view, "Id");
        if (!collectionKind(textOf(view, "CollectionType"), &kind) || !id[0] || strlen(id) >= JF_ID_MAX)
            continue;
        int index = libraryAddCollection(&c->library, textOf(view, "Name"), kind);
        if (index < 0)
            continue;
        snprintf(c->view_ids[index], JF_ID_MAX, "%s", id);
        added++;
    }
    json_decref(root);
    return added;
}

int jfCatalogSetFavoriteSeries(JfCatalog *c, const char *json)
{
    json_t *root = json_loads(json, 0, NULL);
    const json_t *items = json_object_get(root, "Items");
    if (!json_is_array(items)) {
        json_decref(root);
        return -1;
    }
    free(c->favorite_series);
    c->favorite_series_count = 0;
    c->favorite_series = calloc(JF_FAVORITE_SERIES_MAX, JF_ID_MAX);
    size_t i;
    const json_t *series;
    json_array_foreach(items, i, series) {
        const char *id = textOf(series, "Id");
        if (c->favorite_series && c->favorite_series_count < JF_FAVORITE_SERIES_MAX && isId(id, strlen(id)))
            snprintf(c->favorite_series[c->favorite_series_count++], JF_ID_MAX, "%s", id);
    }
    json_decref(root);
    return c->favorite_series_count;
}

static bool seriesIsFavorite(const JfCatalog *c, const char *series)
{
    for (int i = 0; series[0] && i < c->favorite_series_count; i++)
        if (!strcmp(c->favorite_series[i], series))
            return true;
    return false;
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
    if (!stream || !isId(id, strlen(id)) || !isId(source_id, strlen(source_id)))
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
    jfLocationMakeWith(location, sizeof(location), c->prefix, id, source_id);
    if (isId(series, strlen(series)))
        jfLocationMakeWith(art, sizeof(art), c->prefix, series, JF_SERIES_SOURCE);
    LibraryRemote remote = {
        .overview = textOf(item, "Overview"),
        .series_art = art,
        .runtime_ns = json_integer_value(json_object_get(item, "RunTimeTicks")) * TICKS_TO_NS,
        .blocked = (int)jfPlayability(&facts),
        .favorite = name.is_episode ? seriesIsFavorite(c, series)
                                    : json_is_true(json_object_get(json_object_get(item, "UserData"), "IsFavorite")),
        .in_collection = c->adding_collection + 1,
    };
    int64_t size = json_integer_value(json_object_get(source, "Size"));
    JfUserData user;
    readUserData(item, &user);
    return jfCatalogAddVideo(c, location, &name, size, jfParseDate(textOf(item, "DateCreated")), &remote, &user);
}

bool jfCatalogAddVideo(JfCatalog *c, const char *location, const VideoName *name, int64_t size, int64_t added,
                       const LibraryRemote *remote, const JfUserData *user)
{
    if (!growUsers(c) || !libraryAddRemote(&c->library, location, name, size, added, remote))
        return false;
    c->users[c->library.count - 1] = *user;
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
    for (int i = 0; i < c->library.collection_count; i++) {
        fprintf(f, CACHE_COLLECTION "%d\t", (int)c->library.collections[i].kind);
        writeField(f, c->library.collections[i].name);
        fputc('\n', f);
    }
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
        fprintf(f, "%d\t%d\n", it->favorite ? 1 : 0, it->collection + 1);
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

// "#collection<TAB>kind<TAB>name<TAB>": one of the server's libraries.
static void loadCollection(JfCatalog *c, char *line)
{
    char *kind = line + sizeof(CACHE_COLLECTION) - 1, *name = strchr(kind, '\t');
    if (!name)
        return;
    *name++ = '\0';
    name[strcspn(name, "\t")] = '\0';
    int k = atoi(kind);
    bool known = k >= COLLECTION_MIXED && k <= COLLECTION_SERIES;
    libraryAddCollection(&c->library, name, known ? (CollectionKind)k : COLLECTION_MIXED);
}

static void loadLine(JfCatalog *c, char *line)
{
    if (!strncmp(line, CACHE_COLLECTION, sizeof(CACHE_COLLECTION) - 1)) {
        loadCollection(c, line);
        return;
    }
    char *v[CACHE_FIELDS];
    if (splitFields(line, v) < CACHE_FIELDS || !jfIsRemoteLocation(v[11]) || !growUsers(c))
        return;
    VideoName name = {.is_episode = atoi(v[4]) != 0, .season = atoi(v[5]), .episode = atoi(v[6]), .year = atoi(v[7])};
    snprintf(name.title, sizeof(name.title), "%s", v[12]);
    snprintf(name.show, sizeof(name.show), "%s", v[13]);
    LibraryRemote remote = {.overview = v[14], .series_art = v[15], .runtime_ns = strtoll(v[2], NULL, 10),
                            .blocked = atoi(v[3]), .favorite = atoi(v[16]) != 0, .in_collection = atoi(v[17])};
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
        if (!jfIsRemoteLocation(resume->items[i].path))
            resume->items[kept++] = resume->items[i];
    resume->count = kept;
    kept = 0;
    for (size_t i = 0; i < watched->count; i++)
        if (!jfIsRemoteLocation(watched->paths[i])) {
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

// --- Testing the connection, and finding servers ---

// host: where the address's host starts; its length up to the port or path.
static size_t hostOf(const char *server, const char **host)
{
    const char *scheme = strstr(server, "://");
    *host = scheme ? scheme + 3 : server;
    return strcspn(*host, ":/");
}

bool jfAddressIsLocal(const char *server)
{
    const char *host;
    size_t n = hostOf(server, &host);
    char name[JF_SERVER_MAX];
    if (n == 0 || n >= sizeof(name))
        return false;
    memcpy(name, host, n);
    name[n] = '\0';
    unsigned a, b, c, d;
    char extra;
    if (sscanf(name, "%u.%u.%u.%u%c", &a, &b, &c, &d, &extra) == 4)
        return a == 10 || a == 127 || (a == 192 && b == 168) || (a == 172 && b >= 16 && b <= 31) ||
               (a == 169 && b == 254);
    size_t length = strlen(name);
    bool dot_local = length > 6 && !strcmp(name + length - 6, ".local");
    return !strchr(name, '.') || dot_local;
}

static const char *problemAdvice(NetProblem problem)
{
    switch (problem) {
    case NET_NO_NAME: return "The name in the address couldn't be found. Check it, or use the server's IP address.";
    case NET_NO_CONNECT:
        return "Nothing answered at that address and port. Is the server on, and is the port right (usually 8096)?";
    case NET_TIMEOUT: return "The server didn't answer in time. Is the console on the same network as the server?";
    case NET_SECURE: return "The secure connection failed. Does the server use https on that port?";
    case NET_CERTIFICATE:
        return "The server's certificate isn't trusted. If the server has its own certificate, turn Certificate check off.";
    default: return "The connection failed. Check the address and the network.";
    }
}

bool jfConnectionVerdict(NetProblem problem, long status, const char *body, const char *server, char *out, size_t size)
{
    return jfConnectionVerdictFor(SERVER_JELLYFIN, problem, status, body, server, out, size);
}

// Jellyfin names itself; Emby says nothing of its product, only its version and id.
static bool answersAs(ServerKind kind, const json_t *root)
{
    const char *product = textOf(root, "ProductName");
    if (kind == SERVER_EMBY)
        return !strstr(product, "Jellyfin") && textOf(root, "Version")[0] && textOf(root, "Id")[0];
    return strstr(product, "Jellyfin") != NULL;
}

bool jfConnectionVerdictFor(ServerKind kind, NetProblem problem, long status, const char *body, const char *server,
                            char *out, size_t size)
{
    const char *name = serverKindName(kind);
    if (problem == NET_STATUS && status == HTTP_BAD_REQUEST && !strncmp(server, "http://", 7)) {
        // What a server answers when plain http comes to its https port.
        snprintf(out, size, "That port may want a secure connection: write https:// before the address.");
        return false;
    }
    if (problem == NET_STATUS) {
        snprintf(out, size, "That address answers (%ld), but not as %s. Check the port and the path.", status, name);
        return false;
    }
    if (problem != NET_OK) {
        snprintf(out, size, "%s", problemAdvice(problem));
        return false;
    }
    json_t *root = body ? json_loads(body, 0, NULL) : NULL;
    bool jellyfin = answersAs(kind, root);
    if (!jellyfin) {
        snprintf(out, size, "That address answers, but it isn't %s. Check the port (usually 8096).", name);
    } else {
        bool plain = strncmp(server, "https://", 8) != 0 && !jfAddressIsLocal(server);
        snprintf(out, size, "%s %s on %s answers.%s", name, textOf(root, "Version"),
                 textOf(root, "ServerName")[0] ? textOf(root, "ServerName") : "the server",
                 plain ? " The connection isn't encrypted: use https outside your home." : "");
    }
    json_decref(root);
    return jellyfin;
}

bool jfDiscoveryParse(const char *json, const char *sender_ip, char *server, size_t server_size, char *name,
                      size_t name_size)
{
    json_t *root = json_loads(json, 0, NULL);
    const char *address = textOf(root, "Address");
    bool https = !strncmp(address, "https://", 8);
    bool ok = https || !strncmp(address, "http://", 7);
    if (ok) {
        const char *host;
        size_t n = hostOf(address, &host);
        const char *port = host[n] == ':' ? host + n + 1 : NULL;
        int port_length = port ? (int)strcspn(port, "/") : 0;
        const char *usual = https ? DEFAULT_HTTPS_PORT : DEFAULT_PORT;  // Jellyfin's own, without a port.
        int written = snprintf(server, server_size, "%s://%s:%.*s", https ? "https" : "http", sender_ip,
                               port ? port_length : (int)strlen(usual), port ? port : usual);
        ok = written > 0 && (size_t)written < server_size;
        const char *found = textOf(root, "Name");
        snprintf(name, name_size, "%s", found[0] ? found : "Jellyfin");
    }
    json_decref(root);
    return ok;
}

// --- Parts to skip ---

static bool segmentKind(const char *type, JfSegmentKind *kind)
{
    static const struct {
        const char *name;
        JfSegmentKind kind;
    } KINDS[] = {{"Intro", JF_SEGMENT_INTRO}, {"Recap", JF_SEGMENT_RECAP}, {"Preview", JF_SEGMENT_PREVIEW},
                 {"Outro", JF_SEGMENT_OUTRO}};
    for (size_t i = 0; i < sizeof(KINDS) / sizeof(KINDS[0]); i++)
        if (!strcmp(type, KINDS[i].name)) {
            *kind = KINDS[i].kind;
            return true;
        }
    return false;  // Commercials and unknown kinds aren't offered.
}

#define SEGMENTS_READ_MAX 64

int jfSegmentsParse(const char *json, JfSegment *out, int max)
{
    JfSegment all[SEGMENTS_READ_MAX];  // All of them by start, then the first max.
    json_t *root = json_loads(json, 0, NULL);
    int n = 0;
    size_t i;
    const json_t *item;
    json_array_foreach(json_object_get(root, "Items"), i, item) {
        JfSegment s;
        if (n >= SEGMENTS_READ_MAX || !segmentKind(textOf(item, "Type"), &s.kind))
            continue;
        s.start_ns = json_integer_value(json_object_get(item, "StartTicks")) * TICKS_TO_NS;
        s.end_ns = json_integer_value(json_object_get(item, "EndTicks")) * TICKS_TO_NS;
        if (s.end_ns <= s.start_ns)
            continue;
        int at = n++;
        while (at > 0 && all[at - 1].start_ns > s.start_ns) {  // Kept in order of start.
            all[at] = all[at - 1];
            at--;
        }
        all[at] = s;
    }
    json_decref(root);
    if (n > max)
        n = max;
    memcpy(out, all, (size_t)(n > 0 ? n : 0) * sizeof(JfSegment));
    return n;
}

int jfSegmentAt(const JfSegment *s, int count, int64_t pos_ns)
{
    for (int i = 0; i < count; i++)
        if (pos_ns >= s[i].start_ns && pos_ns < s[i].end_ns - JF_SEGMENT_TAIL_NS)
            return i;
    return -1;
}

const char *jfSegmentLabel(JfSegmentKind kind)
{
    switch (kind) {
    case JF_SEGMENT_INTRO: return "Skip intro";
    case JF_SEGMENT_RECAP: return "Skip recap";
    case JF_SEGMENT_PREVIEW: return "Skip preview";
    case JF_SEGMENT_OUTRO: return "Skip credits";
    }
    return "Skip";
}
