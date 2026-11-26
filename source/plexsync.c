#include "plexsync.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <jansson.h>

#include "lang.h"
#include "plexhome.h"

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
#define PLEX_SHOW_TYPE 2
#define MS_TO_NS 1000000ll
#define POSTER_HEIGHT_NUM 3     // Posters are 2:3, wide pictures 16:9.
#define POSTER_HEIGHT_DEN 2
#define WIDE_HEIGHT_NUM 9
#define WIDE_HEIGHT_DEN 16

void plexTvUrl(const PlexSession *s, const char *version, const char *path, bool with_token, char *out,
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
    plexTvUrl(s, version, "/api/v2/pins?strong=false", false, url, sizeof(url));
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
    plexTvUrl(s, version, "/api/v2/user", true, url, sizeof(url));
    NetResponse r;
    if (!fetchJson(net, url, NULL, SMALL_MAX_BYTES, API_TIMEOUT_S, cancel, &r))
        return;
    json_t *root = json_loads(r.data, 0, NULL);
    const char *name = json_string_value(json_object_get(root, "username"));
    const char *uuid = json_string_value(json_object_get(root, "uuid"));
    snprintf(s->user_name, sizeof(s->user_name), "%s", name ? name : "");
    // Signing in: the account is the profile in use (Plex Home's profiles switch from it).
    snprintf(s->account, sizeof(s->account), "%s", uuid && strlen(uuid) < sizeof(s->account) ? uuid : "");
    snprintf(s->profile, sizeof(s->profile), "%s", s->account);
    json_decref(root);
    netResponseFree(&r);
}

bool plexNetPollPin(NetClient *net, PlexSession *s, const char *version, long long id, const volatile bool *cancel)
{
    char path[64], url[PLEX_URL_MAX];
    snprintf(path, sizeof(path), "/api/v2/pins/%lld", id);
    plexTvUrl(s, version, path, false, url, sizeof(url));
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
    return plexNetPickServerOf(net, s, version, "", error, error_size, cancel);
}

bool plexNetPickServerOf(NetClient *net, PlexSession *s, const char *version, const char *server_id, char *error,
                         size_t error_size, const volatile bool *cancel)
{
    char url[PLEX_URL_MAX];
    plexTvUrl(s, version, "/api/v2/resources?includeHttps=1&includeRelay=1", true, url, sizeof(url));
    NetResponse r;
    if (!fetchJson(net, url, NULL, RESOURCES_MAX_BYTES, API_TIMEOUT_S, cancel, &r)) {
        snprintf(error, error_size, "%s", r.status ? T("plex.tv refused that.") : T("plex.tv can't be reached."));
        return false;
    }
    PlexServer *servers = calloc(SERVERS_MAX, sizeof(*servers));
    int count = servers ? plexResourcesParse(r.data, servers, SERVERS_MAX) : -1;
    netResponseFree(&r);
    bool ok = false;
    if (server_id[0]) {  // That one only: another would be another library.
        int at = count > 0 ? plexHomePickServer(servers, count, server_id) : -1;
        ok = at >= 0 && reachServer(net, s, version, &servers[at], cancel);
        if (!ok)
            snprintf(error, error_size, "%s",
                     at < 0 ? T("This profile has no access to this Plex server.") : T("Your Plex server doesn't answer."));
        free(servers);
        return ok;
    }
    for (int i = 0; i < count && !ok; i++)
        ok = reachServer(net, s, version, &servers[i], cancel);
    free(servers);
    if (!ok)
        snprintf(error, error_size, "%s",
                 count <= 0 ? T("This Plex account has no server.") : T("Your Plex server doesn't answer."));
    return ok;
}

// --- Plex Home ---

int plexNetHomeUsers(NetClient *net, const PlexSession *s, const char *version, PlexHomeUser *users, int max,
                     const volatile bool *cancel)
{
    char url[PLEX_URL_MAX];
    plexTvUrl(s, version, "/api/v2/home/users", true, url, sizeof(url));
    NetResponse r;
    if (!fetchJson(net, url, NULL, SMALL_MAX_BYTES, API_TIMEOUT_S, cancel, &r))
        return -1;
    int count = plexHomeUsersParse(r.data, users, max);
    netResponseFree(&r);
    return count;
}

// The user name of the account a token is for ("" for a managed profile, or without an answer).
static void readProfileName(NetClient *net, const PlexSession *s, const char *version, char *out, size_t size,
                            const volatile bool *cancel)
{
    char url[PLEX_URL_MAX];
    out[0] = '\0';
    plexTvUrl(s, version, "/api/v2/user", true, url, sizeof(url));
    NetResponse r;
    if (!fetchJson(net, url, NULL, SMALL_MAX_BYTES, API_TIMEOUT_S, cancel, &r))
        return;
    json_t *root = json_loads(r.data, 0, NULL);
    const char *name = json_string_value(json_object_get(root, "username"));
    snprintf(out, size, "%s", name ? name : "");
    json_decref(root);
    netResponseFree(&r);
}

// The profile's token, from plex.tv. Its PIN goes in the address's query, never in a log.
static PlexSwitchResult askSwitch(NetClient *net, const PlexSession *s, const char *version, const PlexHomeUser *user,
                                  const char *pin, char *token, size_t token_size, const volatile bool *cancel)
{
    char path[PLEX_HOME_PATH_MAX], url[PLEX_URL_MAX];
    if (!plexHomeSwitchPath(user->uuid, pin, path, sizeof(path)))
        return pin[0] ? PLEX_SWITCH_WRONG_PIN : PLEX_SWITCH_REFUSED;
    plexTvUrl(s, version, path, true, url, sizeof(url));
    NetResponse r;
    bool ok = fetchJson(net, url, "{}", SMALL_MAX_BYTES, API_TIMEOUT_S, cancel, &r);
    memset(url, 0, sizeof(url));
    memset(path, 0, sizeof(path));
    if (!ok)
        return plexHomeSwitchError(r.status);
    ok = plexHomeSwitchToken(r.data, token, token_size);
    memset(r.data, 0, r.size);
    netResponseFree(&r);
    return ok ? PLEX_SWITCH_OK : PLEX_SWITCH_REFUSED;
}

PlexSwitchResult plexNetSwitchHome(NetClient *net, PlexSession *s, const char *version, const PlexHomeUser *user,
                                   const char *pin, char *message, size_t message_size, const volatile bool *cancel)
{
    PlexSession next = *s;
    PlexSwitchResult result =
        askSwitch(net, s, version, user, pin, next.token, sizeof(next.token), cancel);
    if (result != PLEX_SWITCH_OK) {
        snprintf(message, message_size, "%s", plexHomeSwitchMessage(result));
        return result;
    }
    // The profile's token is for plex.tv: the server wants the one plex.tv gives it for that server.
    if (!plexNetPickServerOf(net, &next, version, s->server_id, message, message_size, cancel)) {
        memset(&next, 0, sizeof(next));
        return PLEX_SWITCH_NO_SERVER;
    }
    char username[PLEX_NAME_MAX];
    readProfileName(net, &next, version, username, sizeof(username), cancel);
    plexHomeName(username, user->title, next.user_name, sizeof(next.user_name));
    snprintf(next.profile, sizeof(next.profile), "%s", user->uuid);
    *s = next;
    memset(&next, 0, sizeof(next));
    message[0] = '\0';
    return PLEX_SWITCH_OK;
}

// --- The library ---

// Reads one kind of a library's items (type), a page at a time. Series (type 2) only give their genres.
static bool readSectionType(NetClient *net, const PlexSession *s, const char *version, JfCatalog *c, int index,
                            int type, int *loaded, PlexProgress progress, const volatile bool *cancel)
{
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
        if (type != PLEX_SHOW_TYPE) {
            *loaded += count;
            if (progress)
                progress(*loaded, 0);
        }
        if (count < PLEX_PAGE_ITEMS || start >= total || *loaded >= MAX_ITEMS || start >= MAX_ITEMS)
            return true;
    }
}

static bool readSection(NetClient *net, const PlexSession *s, const char *version, JfCatalog *c, int index,
                        int *loaded, PlexProgress progress, const volatile bool *cancel)
{
    if (c->library.collections[index].kind == COLLECTION_FILMS)
        return readSectionType(net, s, version, c, index, PLEX_MOVIE_TYPE, loaded, progress, cancel);
    // The series' genres first (their episodes come without them); without them, episodes have none.
    readSectionType(net, s, version, c, index, PLEX_SHOW_TYPE, loaded, progress, cancel);
    return readSectionType(net, s, version, c, index, PLEX_EPISODE_TYPE, loaded, progress, cancel);
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
    snprintf(error, error_size, "%s", T("The server can't be reached."));
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
        snprintf(error, error_size, "%s", T("The server can't play this video here."));
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
    // The server decides first: once a conversion of this video has been stopped,
    // it refuses a new start without its decision ("session lacking decision").
    char decision[JF_TRANSCODE_URL_MAX];
    const char *start = strstr(plan->transcode_url, "start.m3u8");
    if (start) {
        snprintf(decision, sizeof(decision), "%.*sdecision%s", (int)(start - plan->transcode_url), plan->transcode_url,
                 start + strlen("start.m3u8"));
        plexNetCall(net, s, version, decision, false, API_TIMEOUT_S, NULL);
    }
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
