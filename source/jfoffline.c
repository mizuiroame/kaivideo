// What a video downloaded from a server was watched as, told to the server
// when it's back: who it belongs to (the server and its user), what the
// server says of it now, and setting it. From any thread; each call talks to
// the server itself and waits for the answer.
#include <stdio.h>
#include <string.h>

#include "downloads.h"
#include "jfsync.h"
#include "jfsync_internal.h"
#include "net.h"
#include "plexsync.h"

#define OFFLINE_TIMEOUT_S 15L
#define ITEM_MAX_BYTES (1024u * 1024)
#define BODY_MAX 256

// A refusal that won't change by asking again (the video is gone, or the
// request is wrong); others (signed out, the server busy) are tried later.
static DlSendResult afterRefusal(long status)
{
    return status == 400 || status == 404 || status == 410 ? DL_SEND_DROPPED : DL_SEND_LATER;
}

// Accounts name the kind of server with these, not its shown name (which may be translated).
static const char *const KIND_IDS[SERVER_KIND_COUNT] = {"jellyfin", "emby", "plex"};

bool jfSyncAccountOf(const char *location, char *out, size_t size)
{
    if (size)
        out[0] = '\0';
    if (!jfSyncIsUp() || !jfIsRemoteLocation(location))
        return false;
    ServerKind kind = serverKindOf(location);
    if (kind == SERVER_PLEX) {
        // Plex Home profiles share the server: the user tells them apart.
        PlexSession s = jfSyncPlexSession();
        if (!s.token[0] || !s.server_id[0])
            return false;
        dlAccountMake(KIND_IDS[kind], s.server_id, s.user_name, out, size);
        return true;
    }
    JfSession s = jfSyncSessionFor(kind);
    if (!s.token[0] || !s.user_id[0])
        return false;
    dlAccountMake(KIND_IDS[kind], s.server, s.user_id, out, size);
    return true;
}

// Plex: what the server says, then the position or the watched mark.
static DlSendResult sendPlex(NetClient *net, const char *location, int64_t position_ns, bool played, int64_t when,
                             const volatile bool *cancel)
{
    char item[PLEX_ID_MAX], part[PLEX_ID_MAX], path[320], url[PLEX_URL_MAX];
    if (!plexLocationIds(location, item, part))
        return DL_SEND_DROPPED;
    PlexSession s = jfSyncPlexSession();
    snprintf(path, sizeof(path), "/library/metadata/%s", item);
    plexServerUrl(&s, jfSyncVersionText(), path, url, sizeof(url));
    NetRequest request = {.url = url, .timeout_s = OFFLINE_TIMEOUT_S, .accept_json = true, .cancel = cancel};
    NetResponse r;
    if (!netFetchText(net, &request, ITEM_MAX_BYTES, &r))
        return afterRefusal(r.status);  // Gone from the server: nothing to tell.
    JfUserData now = {0};
    bool known = plexUserDataParse(r.data, &now);
    netResponseFree(&r);
    if (!known)
        return DL_SEND_DROPPED;
    if (!dlSyncIsNewer(when, now.last_played))
        return DL_SEND_SERVER_NEWER;
    long status = 0;
    if (played)
        plexPlayedPath(path, sizeof(path), item, true);
    else if (position_ns > 0)
        plexProgressPath(path, sizeof(path), item, position_ns);
    else if (now.played || now.position_ns > 0)
        plexPlayedPath(path, sizeof(path), item, false);  // Back at the start: Plex ignores a position of 0.
    else
        return DL_SEND_DONE;  // It says so already.
    if (cancel && *cancel)
        return DL_SEND_LATER;
    if (!plexNetCall(net, &s, jfSyncVersionText(), path, false, OFFLINE_TIMEOUT_S, &status))
        return afterRefusal(status);
    return DL_SEND_DONE;
}

// Jellyfin and Emby: the item's UserData, then the new one.
static DlSendResult sendJellyfin(NetClient *net, const char *location, int64_t position_ns, bool played,
                                 int64_t when, const volatile bool *cancel)
{
    char item[JF_ID_MAX], source[JF_ID_MAX], url[JF_URL_MAX], auth[JF_AUTH_MAX], body[BODY_MAX];
    if (!jfLocationIds(location, item, source))
        return DL_SEND_DROPPED;
    ServerKind kind = serverKindOf(location);
    JfSession s = jfSyncSessionFor(kind);
    jfAuthHeader(&s, jfSyncVersionText(), auth, sizeof(auth));
    if (kind == SERVER_EMBY)  // Emby keeps the older addresses.
        snprintf(url, sizeof(url), "%s/Users/%s/Items/%s", s.server, s.user_id, item);
    else
        snprintf(url, sizeof(url), "%s/Items/%s?userId=%s", s.server, item, s.user_id);
    NetRequest get = {.url = url, .authorization = auth, .timeout_s = OFFLINE_TIMEOUT_S, .cancel = cancel};
    NetResponse r;
    if (!netFetchText(net, &get, ITEM_MAX_BYTES, &r))
        return afterRefusal(r.status);
    JfUserData now = {0};
    bool known = jfUserDataParse(r.data, &now);
    netResponseFree(&r);
    if (!known)
        return DL_SEND_DROPPED;
    if (!dlSyncIsNewer(when, now.last_played))
        return DL_SEND_SERVER_NEWER;
    if (kind == SERVER_EMBY)
        snprintf(url, sizeof(url), "%s/Users/%s/Items/%s/UserData", s.server, s.user_id, item);
    else
        snprintf(url, sizeof(url), "%s/UserItems/%s/UserData?userId=%s", s.server, item, s.user_id);
    jfUserDataBody(body, sizeof(body), played ? 0 : position_ns, played, when);
    NetRequest set = {.url = url, .authorization = auth, .post_json = body, .timeout_s = OFFLINE_TIMEOUT_S,
                      .cancel = cancel};
    if (!netFetchText(net, &set, ITEM_MAX_BYTES, &r))
        return afterRefusal(r.status);
    netResponseFree(&r);
    return DL_SEND_DONE;
}

DlSendResult jfSyncSendOffline(const char *location, const char *account, int64_t position_ns, bool played,
                               int64_t when, const volatile bool *cancel)
{
    char current[DL_ACCOUNT_MAX];
    if (!jfSyncAccountOf(location, current, sizeof(current)) || strcmp(current, account) != 0)
        return DL_SEND_LATER;  // Its user isn't signed in here now.
    NetClient *net = netClientOpen();
    if (!net)
        return DL_SEND_LATER;
    DlSendResult result = plexIsLocation(location) ? sendPlex(net, location, position_ns, played, when, cancel)
                                                   : sendJellyfin(net, location, position_ns, played, when, cancel);
    netClientClose(net);
    return result;
}
