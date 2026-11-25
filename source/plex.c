#include "plex.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>

#define MS_TO_NS 1000000ll
#define KBPS_TO_BPS 1000ll
#define TEN_BIT_BITS 10
#define EIGHT_BIT_BITS 8
#define SESSION_LINE_MAX 512

// --- Locations ---

bool plexIsLocation(const char *path)
{
    return strncmp(path, PLEX_PREFIX, sizeof(PLEX_PREFIX) - 1) == 0;
}

// Plex's ratingKeys and part ids are numbers.
static bool isKey(const char *text, size_t length)
{
    if (length == 0 || length >= PLEX_ID_MAX)
        return false;
    for (size_t i = 0; i < length; i++)
        if (!isdigit((unsigned char)text[i]))
            return false;
    return true;
}

bool plexLocationIds(const char *location, char *item, char *part)
{
    if (!plexIsLocation(location))
        return false;
    const char *start = location + sizeof(PLEX_PREFIX) - 1, *slash = strchr(start, '/');
    if (!slash || !isKey(start, (size_t)(slash - start)) || !isKey(slash + 1, strlen(slash + 1)))
        return false;
    snprintf(item, PLEX_ID_MAX, "%.*s", (int)(slash - start), start);
    snprintf(part, PLEX_ID_MAX, "%s", slash + 1);
    return true;
}

void plexLocationMake(char *out, size_t size, const char *item, const char *part)
{
    snprintf(out, size, PLEX_PREFIX "%s/%s", item, part);
}

void plexClientQuery(const char *client_id, const char *version, const char *token, char *out, size_t size)
{
    int n = snprintf(out, size,
                     "X-Plex-Product=" PLEX_PRODUCT "&X-Plex-Version=%s&X-Plex-Client-Identifier=%s"
                     "&X-Plex-Platform=Generic&X-Plex-Device=Nintendo%%20Switch%%202"
                     "&X-Plex-Device-Name=" PLEX_PRODUCT,
                     version, client_id);
    if (token && token[0] && n > 0 && (size_t)n < size)
        snprintf(out + n, size - (size_t)n, "&X-Plex-Token=%s", token);
}

// --- JSON helpers ---

static const char *textOf(const json_t *object, const char *key)
{
    const char *value = json_string_value(json_object_get(object, key));
    return value ? value : "";
}

static long long numberOf(const json_t *object, const char *key)
{
    const json_t *value = json_object_get(object, key);
    if (json_is_string(value))
        return strtoll(json_string_value(value), NULL, 10);
    return json_integer_value(value);
}

// "id": a number in some answers, a string in others.
static void keyOf(const json_t *object, const char *key, char *out)
{
    const json_t *value = json_object_get(object, key);
    if (json_is_integer(value))
        snprintf(out, PLEX_ID_MAX, "%lld", (long long)json_integer_value(value));
    else
        snprintf(out, PLEX_ID_MAX, "%s", json_is_string(value) ? json_string_value(value) : "");
}

// --- Signing in ---

bool plexPinParse(const char *json, long long *id, char *code, size_t code_size)
{
    json_t *root = json_loads(json, 0, NULL);
    const char *text = textOf(root, "code");
    *id = json_integer_value(json_object_get(root, "id"));
    bool ok = *id > 0 && text[0] && strlen(text) < code_size;
    if (ok)
        snprintf(code, code_size, "%s", text);
    json_decref(root);
    return ok;
}

bool plexPinToken(const char *json, char *token, size_t size)
{
    json_t *root = json_loads(json, 0, NULL);
    const char *text = textOf(root, "authToken");
    bool ok = text[0] && strlen(text) < size;
    if (ok)
        snprintf(token, size, "%s", text);
    json_decref(root);
    return ok;
}

// --- Servers ---

static void addUri(PlexServer *s, const char *uri)
{
    if (!uri[0] || strlen(uri) >= PLEX_URI_MAX || s->uri_count >= PLEX_URIS)
        return;
    for (int i = 0; i < s->uri_count; i++)
        if (!strcmp(s->uris[i], uri))
            return;
    snprintf(s->uris[s->uri_count++], PLEX_URI_MAX, "%s", uri);
}

enum { PASS_LOCAL, PASS_LOCAL_PLAIN, PASS_REMOTE, PASS_RELAY, PASSES };

// The connections of one pass: on the home network (the server's own https
// name), the same addresses over plain http (when that name can't be looked
// up), from outside, then through Plex's relay.
static void addConnections(PlexServer *s, const json_t *connections, int pass)
{
    size_t i;
    const json_t *c;
    json_array_foreach(connections, i, c) {
        bool local = json_is_true(json_object_get(c, "local")), relay = json_is_true(json_object_get(c, "relay"));
        if (json_is_true(json_object_get(c, "IPv6")))
            continue;
        if (pass == PASS_LOCAL && local && !relay)
            addUri(s, textOf(c, "uri"));
        else if (pass == PASS_LOCAL_PLAIN && local && !relay && textOf(c, "address")[0]) {
            char plain[PLEX_URI_MAX];
            snprintf(plain, sizeof(plain), "http://%s:%lld", textOf(c, "address"), numberOf(c, "port"));
            addUri(s, plain);
        } else if (pass == PASS_REMOTE && !local && !relay)
            addUri(s, textOf(c, "uri"));
        else if (pass == PASS_RELAY && relay)
            addUri(s, textOf(c, "uri"));
    }
}

static bool readServer(const json_t *r, PlexServer *out)
{
    memset(out, 0, sizeof(*out));
    if (!strstr(textOf(r, "provides"), "server"))
        return false;
    const char *name = textOf(r, "name"), *id = textOf(r, "clientIdentifier"), *token = textOf(r, "accessToken");
    if (!id[0] || strlen(id) >= PLEX_ID_MAX || strlen(token) >= PLEX_TOKEN_MAX)
        return false;
    snprintf(out->name, sizeof(out->name), "%s", name[0] ? name : "Plex");
    snprintf(out->id, sizeof(out->id), "%s", id);
    snprintf(out->token, sizeof(out->token), "%s", token);
    out->owned = json_is_true(json_object_get(r, "owned"));
    for (int pass = 0; pass < PASSES; pass++)
        addConnections(out, json_object_get(r, "connections"), pass);
    return out->uri_count > 0;
}

int plexResourcesParse(const char *json, PlexServer *out, int max)
{
    json_t *root = json_loads(json, 0, NULL);
    if (!json_is_array(root)) {
        json_decref(root);
        return -1;
    }
    int count = 0;
    for (int owned = 1; owned >= 0; owned--) {
        size_t i;
        const json_t *r;
        json_array_foreach(root, i, r) {
            if (count < max && json_is_true(json_object_get(r, "owned")) == (owned == 1) && readServer(r, &out[count]))
                count++;
        }
    }
    json_decref(root);
    return count;
}

// --- Libraries ---

static const json_t *container(const json_t *root, const char *list)
{
    return json_object_get(json_object_get(root, "MediaContainer"), list);
}

int plexCatalogAddSections(JfCatalog *c, const char *json)
{
    json_t *root = json_loads(json, 0, NULL);
    if (!json_is_object(json_object_get(root, "MediaContainer"))) {
        json_decref(root);
        return -1;
    }
    int added = 0;
    size_t i;
    const json_t *d;
    json_array_foreach(container(root, "Directory"), i, d) {
        const char *type = textOf(d, "type");
        char key[PLEX_ID_MAX];
        keyOf(d, "key", key);
        CollectionKind kind = !strcmp(type, "movie") ? COLLECTION_FILMS : COLLECTION_SERIES;
        if ((strcmp(type, "movie") && strcmp(type, "show")) || !isKey(key, strlen(key)) || strlen(key) >= JF_ID_MAX)
            continue;
        int index = libraryAddCollection(&c->library, textOf(d, "title"), kind);
        if (index < 0)
            continue;
        snprintf(c->view_ids[index], JF_ID_MAX, "%.*s", JF_ID_MAX - 1, key);
        added++;
    }
    json_decref(root);
    return added;
}

// What decides whether the console plays the file itself. A profile such as
// "main 10" or "high 10" means 10-bit; HDR isn't in a library's list, so the
// server decides about it when the video is opened.
static JfPlayability playability(const json_t *media)
{
    const char *profile = textOf(media, "videoProfile");
    JfVideoFacts facts = {textOf(media, "videoCodec"), "", textOf(media, "container"), (int)numberOf(media, "width"),
                          strstr(profile, "10") ? TEN_BIT_BITS : EIGHT_BIT_BITS,
                          numberOf(media, "bitrate") * KBPS_TO_BPS};
    return jfPlayability(&facts);
}

static void readName(const json_t *item, VideoName *name)
{
    memset(name, 0, sizeof(*name));
    snprintf(name->title, sizeof(name->title), "%s", textOf(item, "title"));
    name->year = (int)numberOf(item, "year");
    if (strcmp(textOf(item, "type"), "episode"))
        return;
    name->is_episode = true;
    snprintf(name->show, sizeof(name->show), "%s", textOf(item, "grandparentTitle"));
    name->season = (int)numberOf(item, "parentIndex");
    name->episode = (int)numberOf(item, "index");
}

// One film or episode with a file behind it. False if it has none.
static bool addItem(JfCatalog *c, const json_t *item)
{
    const json_t *media = json_array_get(json_object_get(item, "Media"), 0);
    const json_t *part = json_array_get(json_object_get(media, "Part"), 0);
    char key[PLEX_ID_MAX], part_id[PLEX_ID_MAX], series[PLEX_ID_MAX];
    keyOf(item, "ratingKey", key);
    keyOf(part, "id", part_id);
    keyOf(item, "grandparentRatingKey", series);
    if (!part || !isKey(key, strlen(key)) || !isKey(part_id, strlen(part_id)))
        return false;
    VideoName name;
    readName(item, &name);
    if (name.is_episode && !name.show[0])
        return false;
    char location[LIBRARY_PATH_MAX], art[LIBRARY_PATH_MAX] = "";
    plexLocationMake(location, sizeof(location), key, part_id);
    if (isKey(series, strlen(series)))
        snprintf(art, sizeof(art), PLEX_PREFIX "%s/" JF_SERIES_SOURCE, series);
    char genres[GENRES_TEXT_MAX];
    genresJoin(json_object_get(item, "Genre"), "tag", genres, sizeof(genres));
    int at = genreTableFind(&c->series_genres, series);
    const char *of_series = genres[0] ? NULL : genreTableGenres(&c->series_genres, at);
    LibraryRemote remote = {
        .genres = of_series ? of_series : genres,
        .series_overview = genreTableTakeOverview(&c->series_genres, at),
        .overview = textOf(item, "summary"),
        .series_art = art,
        .runtime_ns = numberOf(item, "duration") * MS_TO_NS,
        .blocked = (int)playability(media),
        .in_collection = c->adding_collection + 1,
    };
    JfUserData user = {
        .position_ns = numberOf(item, "viewOffset") * MS_TO_NS,
        .last_played = numberOf(item, "lastViewedAt"),
        .played = numberOf(item, "viewCount") > 0,
    };
    return jfCatalogAddVideo(c, location, &name, numberOf(part, "size"), numberOf(item, "addedAt"), &remote, &user);
}

int plexCatalogAddPage(JfCatalog *c, const char *json, int *total)
{
    json_t *root = json_loads(json, 0, NULL);
    const json_t *mc = json_object_get(root, "MediaContainer");
    if (!json_is_object(mc)) {
        json_decref(root);
        return -1;
    }
    if (total)
        *total = (int)(json_object_get(mc, "totalSize") ? numberOf(mc, "totalSize") : numberOf(mc, "size"));
    int count = 0;
    size_t i;
    const json_t *item;
    json_array_foreach(json_object_get(mc, "Metadata"), i, item) {
        if (!strcmp(textOf(item, "type"), "show")) {  // A series: its genres, for its episodes.
            char key[PLEX_ID_MAX], genres[GENRES_TEXT_MAX];
            keyOf(item, "ratingKey", key);
            genresJoin(json_object_get(item, "Genre"), "tag", genres, sizeof(genres));
            genreTableAdd(&c->series_genres, key, genres, textOf(item, "summary"));
        } else {
            addItem(c, item);
        }
        count++;
    }
    json_decref(root);
    return count;
}

// --- Playing ---

#define HDR_TRANSFERS "smpte2084 arib-std-b67"
#define KBPS 1000

static bool isPictureCodec(const char *codec)
{
    return !strcmp(codec, "pgs") || !strcmp(codec, "vobsub") || !strcmp(codec, "dvd_subtitle") ||
           !strcmp(codec, "hdmv_pgs_subtitle") || !strcmp(codec, "dvb_subtitle");
}

static void readTrack(const json_t *stream, JfTrack *t)
{
    const char *name = textOf(stream, "extendedDisplayTitle");
    t->index = (int)numberOf(stream, "id");
    t->external = json_object_get(stream, "key") != NULL;
    snprintf(t->name, sizeof(t->name), "%s", name[0] ? name : textOf(stream, "displayTitle"));
}

static bool videoIsSdr(const json_t *part)
{
    size_t i;
    const json_t *s;
    json_array_foreach(json_object_get(part, "Stream"), i, s) {
        const char *transfer = textOf(s, "colorTrc");
        if (numberOf(s, "streamType") == 1 && transfer[0] && strstr(HDR_TRANSFERS, transfer))
            return false;
    }
    return true;
}

static void addSubtitle(JfPlan *p, const json_t *s, int burn_id)
{
    bool picture = isPictureCodec(textOf(s, "codec")), external = json_object_get(s, "key") != NULL;
    if (picture || (!p->direct && !external)) {  // Only the server can show it: it draws it.
        if (p->picture_count >= JF_PLAN_PICTURES)
            return;
        readTrack(s, &p->pictures[p->picture_count]);
        if (p->pictures[p->picture_count].index == burn_id)
            p->picture_burned = p->picture_count;
        p->picture_count++;
    } else if (external && p->sub_count < JF_PLAN_SUBS) {
        readTrack(s, &p->subs[p->sub_count]);
        if (json_is_true(json_object_get(s, "selected")))
            p->sub_default = p->sub_count;
        p->sub_count++;
    }
}

static void readStreams(const json_t *part, int burn_id, JfPlan *p)
{
    size_t i;
    const json_t *s;
    json_array_foreach(json_object_get(part, "Stream"), i, s) {
        long long type = numberOf(s, "streamType");
        if (type == 2 && p->audio_count < JF_PLAN_AUDIO) {
            readTrack(s, &p->audio[p->audio_count]);
            if (json_is_true(json_object_get(s, "selected")))
                p->audio_current = p->audio_count;
            p->audio_count++;
        } else if (type == 3) {
            addSubtitle(p, s, burn_id);
        }
    }
    if (p->audio_current < 0 && p->audio_count)
        p->audio_current = 0;
}

bool plexPlanParse(const char *json, long long max_bitrate, int burn_id, JfPlan *out)
{
    memset(out, 0, sizeof(*out));
    out->audio_current = out->sub_default = out->picture_burned = -1;
    json_t *root = json_loads(json, 0, NULL);
    const json_t *item = json_array_get(container(root, "Metadata"), 0);
    const json_t *media = json_array_get(json_object_get(item, "Media"), 0);
    const json_t *part = json_array_get(json_object_get(media, "Part"), 0);
    bool ok = part != NULL;
    if (ok) {
        long long bitrate = numberOf(media, "bitrate") * KBPS;
        out->direct = burn_id < 0 && playability(media) == JF_PLAYABLE && videoIsSdr(part) &&
                      (max_bitrate <= 0 || bitrate <= max_bitrate);
        readStreams(part, burn_id, out);
    }
    json_decref(root);
    return ok;
}

void plexTranscodePath(char *out, size_t size, const char *item, const char *session, long long max_bitrate,
                       bool burn, bool small, const char *client_query)
{
    // Without a profile of its own, Plex finds no way to convert for an unknown
    // app: this one says H.264 and AAC in MPEG-TS over HLS.
    snprintf(out, size,
             "/video/:/transcode/universal/start.m3u8?path=%%2Flibrary%%2Fmetadata%%2F%s&mediaIndex=0&partIndex=0"
             "&protocol=hls&directPlay=0&directStream=1&directStreamAudio=0&fastSeek=1&copyts=1"
             "&videoResolution=%s&maxVideoBitrate=%lld&audioChannelCount=2&subtitles=%s&session=%s"
             "&X-Plex-Session-Identifier=%s&X-Plex-Client-Profile-Extra=add-transcode-target%%28type%%3DvideoProfile"
             "%%26context%%3Dstreaming%%26protocol%%3Dhls%%26container%%3Dmpegts%%26videoCodec%%3Dh264"
             "%%26audioCodec%%3Daac%%29%s%s",
             item, small ? "320x180" : "1920x1080", small ? 200ll : max_bitrate / KBPS, burn ? "burn" : "none",
             session, session, client_query[0] ? "&" : "", client_query);
}

// --- Telling the server ---

void plexTimelinePath(char *out, size_t size, const char *item, const char *state, int64_t position_ns,
                      int64_t duration_ns)
{
    snprintf(out, size,
             "/:/timeline?ratingKey=%s&key=%%2Flibrary%%2Fmetadata%%2F%s&state=%s&time=%lld&duration=%lld", item,
             item, state, (long long)(position_ns / MS_TO_NS), (long long)(duration_ns / MS_TO_NS));
}

void plexPlayedPath(char *out, size_t size, const char *item, bool played)
{
    snprintf(out, size, "/:/%s?identifier=com.plexapp.plugins.library&key=%s", played ? "scrobble" : "unscrobble",
             item);
}

// --- Parts to skip ---

int plexMarkersParse(const char *json, JfSegment *out, int max)
{
    json_t *root = json_loads(json, 0, NULL);
    const json_t *item = json_array_get(container(root, "Metadata"), 0);
    int n = 0;
    size_t i;
    const json_t *m;
    json_array_foreach(json_object_get(item, "Marker"), i, m) {
        const char *type = textOf(m, "type");
        JfSegment s = {.start_ns = numberOf(m, "startTimeOffset") * MS_TO_NS,
                       .end_ns = numberOf(m, "endTimeOffset") * MS_TO_NS};
        if (!strcmp(type, "intro"))
            s.kind = JF_SEGMENT_INTRO;
        else if (!strcmp(type, "credits"))
            s.kind = JF_SEGMENT_OUTRO;
        else
            continue;
        if (n >= max || s.end_ns <= s.start_ns)
            continue;
        int at = n++;
        while (at > 0 && out[at - 1].start_ns > s.start_ns) {  // Kept in order of start.
            out[at] = out[at - 1];
            at--;
        }
        out[at] = s;
    }
    json_decref(root);
    return n;
}

// --- Favorites, kept on the console ---

#define FAVORITES_MAX 4096

// The file's lines (at most FAVORITES_MAX), to free. How many in *count.
static char (*readFavorites(const char *file, int *count))[LIBRARY_PATH_MAX]
{
    *count = 0;
    char(*lines)[LIBRARY_PATH_MAX] = calloc(FAVORITES_MAX, LIBRARY_PATH_MAX);
    FILE *f = lines ? fopen(file, "r") : NULL;
    while (f && *count < FAVORITES_MAX && fgets(lines[*count], LIBRARY_PATH_MAX, f)) {
        lines[*count][strcspn(lines[*count], "\r\n")] = '\0';
        if (plexIsLocation(lines[*count]))
            (*count)++;
    }
    if (f)
        fclose(f);
    return lines;
}

static bool listed(char (*lines)[LIBRARY_PATH_MAX], int count, const char *location)
{
    for (int i = 0; location && i < count; i++)
        if (!strcmp(lines[i], location))
            return true;
    return false;
}

void plexFavoritesApply(JfCatalog *c, const char *file)
{
    int count;
    char(*lines)[LIBRARY_PATH_MAX] = readFavorites(file, &count);
    for (size_t i = 0; lines && i < c->library.count; i++) {
        LibraryItem *item = &c->library.items[i];
        item->favorite = listed(lines, count, item->name.is_episode ? item->series_art : item->path);
    }
    free(lines);
}

bool plexFavoritesSet(const char *file, const char *location, bool favorite)
{
    int count;
    char(*lines)[LIBRARY_PATH_MAX] = readFavorites(file, &count);
    FILE *f = lines && plexIsLocation(location) ? fopen(file, "w") : NULL;
    if (!f) {
        free(lines);
        return false;
    }
    for (int i = 0; i < count; i++)
        if (strcmp(lines[i], location))
            fprintf(f, "%s\n", lines[i]);
    if (favorite)
        fprintf(f, "%s\n", location);
    free(lines);
    return fclose(f) == 0;
}

// --- The session on the card ---

static void readSessionLine(PlexSession *s, const char *line)
{
    static const struct {
        const char *key;
        size_t offset, size;
    } FIELDS[] = {
        {"client=", offsetof(PlexSession, client_id), PLEX_ID_MAX},
        {"token=", offsetof(PlexSession, token), PLEX_TOKEN_MAX},
        {"user=", offsetof(PlexSession, user_name), PLEX_NAME_MAX},
        {"server_name=", offsetof(PlexSession, server_name), PLEX_NAME_MAX},
        {"server_id=", offsetof(PlexSession, server_id), PLEX_ID_MAX},
        {"server_token=", offsetof(PlexSession, server_token), PLEX_TOKEN_MAX},
        {"server_uri=", offsetof(PlexSession, server_uri), PLEX_URI_MAX},
    };
    for (size_t i = 0; i < sizeof(FIELDS) / sizeof(FIELDS[0]); i++) {
        size_t n = strlen(FIELDS[i].key);
        if (!strncmp(line, FIELDS[i].key, n)) {
            snprintf((char *)s + FIELDS[i].offset, FIELDS[i].size, "%s", line + n);
            return;
        }
    }
}

bool plexSessionLoad(PlexSession *s, const char *file)
{
    memset(s, 0, sizeof(*s));
    FILE *f = fopen(file, "r");
    if (!f)
        return false;
    char line[SESSION_LINE_MAX];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        readSessionLine(s, line);
    }
    fclose(f);
    return true;
}

bool plexSessionSave(const PlexSession *s, const char *file)
{
    FILE *f = fopen(file, "w");
    if (!f)
        return false;
    fprintf(f, "client=%s\ntoken=%s\nuser=%s\nserver_name=%s\nserver_id=%s\nserver_token=%s\nserver_uri=%s\n",
            s->client_id, s->token, s->user_name, s->server_name, s->server_id, s->server_token, s->server_uri);
    return fclose(f) == 0;
}

// --- Where the user left a video, told later ---

bool plexUserDataParse(const char *json, JfUserData *out)
{
    json_t *root = json_loads(json, 0, NULL);
    const json_t *item = json_array_get(container(root, "Metadata"), 0);
    bool ok = json_is_object(item);
    if (ok)
        *out = (JfUserData){.position_ns = numberOf(item, "viewOffset") * MS_TO_NS,
                            .last_played = numberOf(item, "lastViewedAt"),
                            .played = numberOf(item, "viewCount") > 0};
    json_decref(root);
    return ok;
}

void plexProgressPath(char *out, size_t size, const char *item, int64_t position_ns)
{
    snprintf(out, size, "/:/progress?key=%s&identifier=com.plexapp.plugins.library&time=%lld&state=stopped", item,
             (long long)(position_ns > 0 ? position_ns / MS_TO_NS : 0));
}
