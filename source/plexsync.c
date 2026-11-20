#include "plexsync.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <jansson.h>

#define API_TIMEOUT_S 30L
#define PROBE_TIMEOUT_S 5L      // An address of the server that doesn't answer in this is passed over.
#define PROBE_STALL_S 3L
#define SMALL_MAX_BYTES (256u * 1024)
#define PAGE_MAX_BYTES (32u * 1024 * 1024)
#define RESOURCES_MAX_BYTES (2u * 1024 * 1024)
#define SUBTITLE_MAX_BYTES (16u * 1024 * 1024)
#define SUBTITLE_TIMEOUT_S 60L
#define SERVERS_MAX 8
#define MAX_ITEMS 100000        // Against a server that never says it's done.
#define PLEX_MOVIE_TYPE 1
#define PLEX_EPISODE_TYPE 4
#define MS_TO_NS 1000000ll
#define POSTER_HEIGHT_NUM 3     // Posters are 2:3, wide pictures 16:9.
#define POSTER_HEIGHT_DEN 2
#define WIDE_HEIGHT_NUM 9
#define WIDE_HEIGHT_DEN 16

static void tvUrl(const PlexSession *s, const char *version, const char *path, bool with_token, char *out,
                  size_t size)
{
    char query[PLEX_QUERY_MAX];
    plexClientQuery(s->client_id, version, with_token ? s->token : NULL, query, sizeof(query));
    snprintf(out, size, PLEX_TV "%s%s%s", path, strchr(path, '?') ? "&" : "?", query);
}

void plexServerUrl(const PlexSession *s, const char *version, const char *path, char *out, size_t size)
{
    char query[PLEX_QUERY_MAX];
    plexClientQuery(s->client_id, version, s->server_token, query, sizeof(query));
    snprintf(out, size, "%s%s%s%s", s->server_uri, path, strchr(path, '?') ? "&" : "?", query);
}

// A JSON answer, to free with netResponseFree. False if it failed.
static bool fetchJson(NetClient *net, const char *url, const char *post, size_t max, long timeout_s,
                      const volatile bool *cancel, NetResponse *out)
{
    NetRequest request = {.url = url, .post_json = post, .accept_json = true, .timeout_s = timeout_s,
                          .cancel = cancel};
    return netFetchText(net, &request, max, out);
}

// --- Signing in ---

bool plexNetStartPin(NetClient *net, const PlexSession *s, const char *version, long long *id, char *code,
                     size_t code_size, const volatile bool *cancel)
{
    char url[PLEX_URL_MAX];
    tvUrl(s, version, "/api/v2/pins?strong=false", false, url, sizeof(url));
    NetResponse r;
    if (!fetchJson(net, url, "{}", SMALL_MAX_BYTES, API_TIMEOUT_S, cancel, &r))
        return false;
    bool ok = plexPinParse(r.data, id, code, code_size);
    netResponseFree(&r);
    return ok;
}

static void readUserName(NetClient *net, PlexSession *s, const char *version, const volatile bool *cancel)
{
    char url[PLEX_URL_MAX];
    tvUrl(s, version, "/api/v2/user", true, url, sizeof(url));
    NetResponse r;
    if (!fetchJson(net, url, NULL, SMALL_MAX_BYTES, API_TIMEOUT_S, cancel, &r))
        return;
    json_t *root = json_loads(r.data, 0, NULL);
    const char *name = json_string_value(json_object_get(root, "username"));
    snprintf(s->user_name, sizeof(s->user_name), "%s", name ? name : "");
    json_decref(root);
    netResponseFree(&r);
}

bool plexNetPollPin(NetClient *net, PlexSession *s, const char *version, long long id, const volatile bool *cancel)
{
    char path[64], url[PLEX_URL_MAX];
    snprintf(path, sizeof(path), "/api/v2/pins/%lld", id);
    tvUrl(s, version, path, false, url, sizeof(url));
    NetResponse r;
    if (!fetchJson(net, url, NULL, SMALL_MAX_BYTES, API_TIMEOUT_S, cancel, &r))
        return false;
    bool ok = plexPinToken(r.data, s->token, sizeof(s->token));
    netResponseFree(&r);
    if (ok)
        readUserName(net, s, version, cancel);
    return ok;
}

// The first of the server's addresses that answers, into s.
static bool reachServer(NetClient *net, PlexSession *s, const char *version, const PlexServer *server,
                        const volatile bool *cancel)
{
    snprintf(s->server_token, sizeof(s->server_token), "%s", server->token[0] ? server->token : s->token);
    for (int i = 0; i < server->uri_count && !(cancel && *cancel); i++) {
        char url[PLEX_URL_MAX];
        snprintf(s->server_uri, sizeof(s->server_uri), "%s", server->uris[i]);
        plexServerUrl(s, version, "/identity", url, sizeof(url));
        NetRequest request = {.url = url, .accept_json = true, .timeout_s = PROBE_TIMEOUT_S,
                              .stall_s = PROBE_STALL_S, .cancel = cancel};
        NetResponse r;
        if (netFetchText(net, &request, SMALL_MAX_BYTES, &r)) {
            netResponseFree(&r);
            snprintf(s->server_name, sizeof(s->server_name), "%s", server->name);
            snprintf(s->server_id, sizeof(s->server_id), "%s", server->id);
            return true;
        }
    }
    s->server_uri[0] = '\0';
    return false;
}

bool plexNetPickServer(NetClient *net, PlexSession *s, const char *version, char *error, size_t error_size,
                       const volatile bool *cancel)
{
    char url[PLEX_URL_MAX];
    tvUrl(s, version, "/api/v2/resources?includeHttps=1&includeRelay=1", true, url, sizeof(url));
    NetResponse r;
    if (!fetchJson(net, url, NULL, RESOURCES_MAX_BYTES, API_TIMEOUT_S, cancel, &r)) {
        snprintf(error, error_size, "%s", r.status ? "plex.tv refused that." : "plex.tv can't be reached.");
        return false;
    }
    PlexServer *servers = calloc(SERVERS_MAX, sizeof(*servers));
    int count = servers ? plexResourcesParse(r.data, servers, SERVERS_MAX) : -1;
    netResponseFree(&r);
    bool ok = false;
    for (int i = 0; i < count && !ok; i++)
        ok = reachServer(net, s, version, &servers[i], cancel);
    free(servers);
    if (!ok)
        snprintf(error, error_size, "%s",
                 count <= 0 ? "This Plex account has no server." : "Your Plex server doesn't answer.");
    return ok;
}

// --- The library ---

static bool readSection(NetClient *net, const PlexSession *s, const char *version, JfCatalog *c, int index,
                        int *loaded, PlexProgress progress, const volatile bool *cancel)
{
    int type = c->library.collections[index].kind == COLLECTION_FILMS ? PLEX_MOVIE_TYPE : PLEX_EPISODE_TYPE;
    int start = 0, total = 0;
    c->adding_collection = index;
    for (;;) {
        char path[256], url[PLEX_URL_MAX];
        snprintf(path, sizeof(path), "/library/sections/%s/all?type=%d&X-Plex-Container-Start=%d"
                 "&X-Plex-Container-Size=%d", c->view_ids[index], type, start, PLEX_PAGE_ITEMS);
        plexServerUrl(s, version, path, url, sizeof(url));
        NetResponse r;
        if (!fetchJson(net, url, NULL, PAGE_MAX_BYTES, API_TIMEOUT_S, cancel, &r))
            return false;
        int count = plexCatalogAddPage(c, r.data, &total);
        netResponseFree(&r);
        if (count < 0)
            return false;
        start += count;
        *loaded += count;
        if (progress)
            progress(*loaded, 0);
        if (count < PLEX_PAGE_ITEMS || start >= total || *loaded >= MAX_ITEMS)
            return true;
    }
}

bool plexNetReadLibrary(NetClient *net, const PlexSession *s, const char *version, JfCatalog *c,
                        PlexProgress progress, const volatile bool *cancel)
{
    char url[PLEX_URL_MAX];
    plexServerUrl(s, version, "/library/sections", url, sizeof(url));
    NetResponse r;
    if (!fetchJson(net, url, NULL, SMALL_MAX_BYTES, API_TIMEOUT_S, cancel, &r))
        return false;
    int sections = plexCatalogAddSections(c, r.data);
    netResponseFree(&r);
    int loaded = 0;
    bool ok = sections >= 0;
    for (int i = 0; ok && i < c->library.collection_count; i++)
        ok = readSection(net, s, version, c, i, &loaded, progress, cancel);
    c->adding_collection = -1;
    return ok;
}

bool plexNetCall(NetClient *net, const PlexSession *s, const char *version, const char *path, bool put, long timeout_s,
                 long *status)
{
    char url[PLEX_URL_MAX];
    plexServerUrl(s, version, path, url, sizeof(url));
    NetRequest request = {.url = url, .custom_method = put ? "PUT" : NULL, .accept_json = true,
                          .timeout_s = timeout_s};
    NetResponse r;
    bool ok = netFetchText(net, &request, SMALL_MAX_BYTES, &r);
    if (status)
        *status = r.status;
    if (ok)
        netResponseFree(&r);
    return ok;
}

// --- Playing ---

static void newSession(char *out, size_t size, int salt)
{
    snprintf(out, size, "kv%08x%08x%04x", (unsigned)rand(), (unsigned)time(NULL), (unsigned)salt & 0xffffu);
}

// Which of the part's tracks the server converts (it keeps the choice for the user).
static void choose(NetClient *net, const PlexSession *s, const char *version, const char *part, const char *what,
                   int id)
{
    char path[160];
    snprintf(path, sizeof(path), "/library/parts/%s?%s=%d&allParts=1", part, what, id);
    plexNetCall(net, s, version, path, true, API_TIMEOUT_S, NULL);
}

static int64_t durationOf(const char *json)
{
    json_t *root = json_loads(json, 0, NULL);
    const json_t *item = json_array_get(json_object_get(json_object_get(root, "MediaContainer"), "Metadata"), 0);
    int64_t ms = json_integer_value(json_object_get(item, "duration"));
    json_decref(root);
    return ms * MS_TO_NS;
}

bool plexNetResolve(NetClient *net, const PlexSession *s, const char *version, const char *location,
                    long long max_bitrate, int audio_id, int burn_id, JfPlan *plan, int64_t *duration_ns,
                    char *error, size_t error_size, const volatile bool *cancel)
{
    char item[PLEX_ID_MAX], part[PLEX_ID_MAX], path[96], url[PLEX_URL_MAX];
    snprintf(error, error_size, "The server can't be reached.");
    if (!plexLocationIds(location, item, part))
        return false;
    snprintf(path, sizeof(path), "/library/metadata/%s", item);
    plexServerUrl(s, version, path, url, sizeof(url));
    NetResponse r;
    if (!fetchJson(net, url, NULL, PAGE_MAX_BYTES, API_TIMEOUT_S, cancel, &r))
        return false;
    bool ok = plexPlanParse(r.data, max_bitrate, burn_id, plan);
    *duration_ns = durationOf(r.data);
    netResponseFree(&r);
    if (!ok) {
        snprintf(error, error_size, "The server can't play this video here.");
        return false;
    }
    if (plan->direct)
        return true;
    for (int i = 0; audio_id >= 0 && i < plan->audio_count; i++)
        if (plan->audio[i].index == audio_id) {
            choose(net, s, version, part, "audioStreamID", audio_id);
            plan->audio_current = i;
        }
    bool burn = plan->picture_burned >= 0;
    if (burn)
        choose(net, s, version, part, "subtitleStreamID", burn_id);
    newSession(plan->play_session, sizeof(plan->play_session), audio_id);
    plexTranscodePath(plan->transcode_url, sizeof(plan->transcode_url), item, plan->play_session, max_bitrate, burn,
                      false, "");
    return true;
}

bool plexNetAudioConversion(NetClient *net, const PlexSession *s, const char *version, const char *location,
                            int audio_id, char *url, size_t url_size, char *session, size_t session_size)
{
    char item[PLEX_ID_MAX], part[PLEX_ID_MAX], path[JF_TRANSCODE_URL_MAX];
    if (!plexLocationIds(location, item, part))
        return false;
    choose(net, s, version, part, "audioStreamID", audio_id);
    newSession(session, session_size, audio_id);
    plexTranscodePath(path, sizeof(path), item, session, 0, false, true, "");
    plexServerUrl(s, version, path, url, url_size);
    return true;
}

// --- Files and pictures ---

bool plexStreamUrl(const PlexSession *s, const char *version, const char *location, char *url, size_t size)
{
    char item[PLEX_ID_MAX], part[PLEX_ID_MAX], path[96];
    if (!s->server_uri[0] || !plexLocationIds(location, item, part))
        return false;
    snprintf(path, sizeof(path), "/library/parts/%s/0/file", part);
    plexServerUrl(s, version, path, url, size);
    return true;
}

bool plexImageUrl(const PlexSession *s, const char *version, const char *location, const char *kind, int max_width,
                  char *url, size_t size)
{
    if (!s->server_uri[0] || !plexIsLocation(location))
        return false;
    const char *start = location + sizeof(PLEX_PREFIX) - 1;
    size_t digits = strspn(start, "0123456789");
    if (digits == 0 || digits >= PLEX_ID_MAX || start[digits] != '/')
        return false;
    bool poster = !strcmp(kind, "Primary");
    int height = poster ? max_width * POSTER_HEIGHT_NUM / POSTER_HEIGHT_DEN : max_width * WIDE_HEIGHT_NUM / WIDE_HEIGHT_DEN;
    char path[256];
    snprintf(path, sizeof(path), "/photo/:/transcode?width=%d&height=%d&minSize=1&upscale=1&format=jpeg"
             "&url=%%2Flibrary%%2Fmetadata%%2F%.*s%%2F%s", max_width, height, (int)digits, start, poster ? "thumb" : "art");
    plexServerUrl(s, version, path, url, size);
    return true;
}

#define MARKERS_TIMEOUT_S 5L  // Parts to skip are a nicety: opening doesn't wait long for them.

int plexNetFetchMarkers(NetClient *net, const PlexSession *s, const char *version, const char *location,
                        JfSegment *out, int max)
{
    char item[PLEX_ID_MAX], part[PLEX_ID_MAX], path[96], url[PLEX_URL_MAX];
    if (!plexLocationIds(location, item, part))
        return 0;
    snprintf(path, sizeof(path), "/library/metadata/%s?includeMarkers=1", item);
    plexServerUrl(s, version, path, url, sizeof(url));
    NetResponse r;
    if (!fetchJson(net, url, NULL, PAGE_MAX_BYTES, MARKERS_TIMEOUT_S, NULL, &r))
        return 0;
    int count = plexMarkersParse(r.data, out, max);
    netResponseFree(&r);
    return count;
}

bool plexNetFetchSubtitle(NetClient *net, const PlexSession *s, const char *version, int stream_id,
                          const volatile bool *cancel, NetResponse *out)
{
    char path[64], url[PLEX_URL_MAX];
    snprintf(path, sizeof(path), "/library/streams/%d", stream_id);
    plexServerUrl(s, version, path, url, sizeof(url));
    NetRequest request = {.url = url, .cancel = cancel, .timeout_s = SUBTITLE_TIMEOUT_S};
    return netFetchText(net, &request, SUBTITLE_MAX_BYTES, out);
}
