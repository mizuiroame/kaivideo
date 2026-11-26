// Plex Home's flow on a PC through plexsync, as the console does it: the
// Home's profiles, a switch with a wrong PIN, the right one, a profile without
// the server, one gone, too many wrong PINs, and back to the owner.
//
// Against fake_plextv.py (made-up answers), built with PLEX_TV pointing at it:
//   docker run --rm -v "$PWD:/w" -w /w kaivideo-host sh tests/host_plexhome/run.sh
//
// Against the real plex.tv (personal testing only; nothing secret is printed):
//   PLEX_TOKEN=<account's> PLEX_SERVER_ID=<server's clientIdentifier> PLEX_PIN=<a test profile's>
//   host_plexhome live
// It switches to the first managed profile with a PIN (one wrong PIN first
// with PLEX_TRY_WRONG=1),
// reads the server's libraries as it, and switches back to the account's own
// profile (with PLEX_OWNER_PIN if the owner has one; else it checks it's asked for).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plexsync.h"

#define VERSION "host"
#define WRONG_TRIES 3

static int g_failures;

static void expect(bool ok, const char *what)
{
    printf("%s %s\n", ok ? "  ok  " : "  FAIL", what);
    g_failures += !ok;
}

static const PlexHomeUser *byUuid(const PlexHomeUser *users, int count, const char *uuid)
{
    int i = plexHomeFind(users, count, uuid);
    return i >= 0 ? &users[i] : NULL;
}

static void fakeFlow(NetClient *net)
{
    PlexSession s = {0}, before;
    snprintf(s.client_id, sizeof(s.client_id), "host-test");
    snprintf(s.token, sizeof(s.token), "admin-token");
    snprintf(s.server_id, sizeof(s.server_id), "srv1");
    snprintf(s.server_token, sizeof(s.server_token), "srv-owner");
    snprintf(s.account, sizeof(s.account), "own1");
    snprintf(s.profile, sizeof(s.profile), "own1");
    snprintf(s.user_name, sizeof(s.user_name), "owner-made-up");
    PlexHomeUser users[PLEX_HOME_USERS_MAX];
    char message[160];
    int n = plexNetHomeUsers(net, &s, VERSION, users, PLEX_HOME_USERS_MAX, NULL);
    expect(n == 4 && users[0].admin && !strcmp(users[0].uuid, "own1"), "the Home's profiles, the owner first");
    if (n != 4)
        return;
    const PlexHomeUser *kid = byUuid(users, n, "kid1"), *teen = byUuid(users, n, "teen1"),
                       *nosrv = byUuid(users, n, "nosrv1"), *owner = byUuid(users, n, "own1");
    expect(kid && kid->protected_pin && teen && !teen->protected_pin, "who has a PIN");

    before = s;
    expect(plexNetSwitchHome(net, &s, VERSION, kid, "", message, sizeof(message), NULL) == PLEX_SWITCH_WRONG_PIN &&
               !memcmp(&s, &before, sizeof(s)),
           "a profile with a PIN, without one: refused, the session as it was");
    expect(plexNetSwitchHome(net, &s, VERSION, kid, "1111", message, sizeof(message), NULL) == PLEX_SWITCH_WRONG_PIN &&
               message[0] && !memcmp(&s, &before, sizeof(s)),
           "a wrong PIN: refused, with why");
    expect(plexNetSwitchHome(net, &s, VERSION, kid, "2468", message, sizeof(message), NULL) == PLEX_SWITCH_OK,
           "the right PIN");
    expect(!strcmp(s.token, "kid1-token") && !strcmp(s.server_token, "srv-kid") && !strcmp(s.profile, "kid1") &&
               !strcmp(s.user_name, "Kids") && !strcmp(s.server_id, "srv1") && !plexSessionIsAccount(&s) &&
               !strcmp(s.account, "own1"),
           "the profile's token, its own token for the same server, and its name");
    // From the managed profile: its Home again, and a profile without the server.
    expect(plexNetHomeUsers(net, &s, VERSION, users, PLEX_HOME_USERS_MAX, NULL) == 4,
           "the profiles, read with the profile's token");
    before = s;
    expect(plexNetSwitchHome(net, &s, VERSION, nosrv, "", message, sizeof(message), NULL) == PLEX_SWITCH_NO_SERVER &&
               message[0] && !memcmp(&s, &before, sizeof(s)),
           "a profile without this server: refused, the profile before stays");
    PlexHomeUser gone = *teen;
    snprintf(gone.uuid, sizeof(gone.uuid), "gone1");
    expect(plexNetSwitchHome(net, &s, VERSION, &gone, "", message, sizeof(message), NULL) == PLEX_SWITCH_GONE,
           "a profile no longer in the Home");
    PlexSwitchResult r = PLEX_SWITCH_OK;
    for (int i = 0; i <= WRONG_TRIES; i++)
        r = plexNetSwitchHome(net, &s, VERSION, kid, "9999", message, sizeof(message), NULL);
    expect(r == PLEX_SWITCH_TOO_MANY && message[0], "too many wrong PINs");
    expect(plexNetSwitchHome(net, &s, VERSION, owner, "", message, sizeof(message), NULL) == PLEX_SWITCH_OK &&
               !strcmp(s.token, "admin-token") && !strcmp(s.server_token, "srv-owner") &&
               !strcmp(s.user_name, "owner-made-up") && plexSessionIsAccount(&s),
           "back to the owner, under the account's own name");
}

// The HTTP status of a switch, as plex.tv answers it (0 without an answer).
static long rawSwitchStatus(NetClient *net, const PlexSession *s, const char *uuid, const char *pin)
{
    char path[PLEX_HOME_PATH_MAX], url[PLEX_URL_MAX];
    if (!plexHomeSwitchPath(uuid, pin, path, sizeof(path)))
        return -1;
    plexTvUrl(s, VERSION, path, true, url, sizeof(url));
    NetRequest request = {.url = url, .post_json = "{}", .accept_json = true, .timeout_s = 30};
    NetResponse r;
    if (netFetchText(net, &request, 64 * 1024, &r))
        netResponseFree(&r);
    memset(url, 0, sizeof(url));
    return r.status;
}

// Tells the server, as the session's profile, that the longest film was left
// at a point, as KaiVideo's player does when it stops, and reads the library
// again (as the app does) to see the point kept for that profile.
static void checkProgress(NetClient *net, const PlexSession *s, const char *who)
{
    JfCatalog c;
    jfCatalogInit(&c);
    bool read = plexNetReadLibrary(net, s, VERSION, &c, NULL, NULL);
    int best = -1;
    for (size_t i = 0; read && i < c.library.count; i++)
        if (!c.library.items[i].name.is_episode && (best < 0 || c.library.items[i].runtime_ns >
                                                                   c.library.items[best].runtime_ns))
            best = (int)i;
    char what[96];
    printf("        %s: library read=%d, %zu videos in %d libraries\n", who, read, c.library.count,
           c.library.collection_count);
    snprintf(what, sizeof(what), "%s: the library has a film", who);
    expect(best >= 0, what);
    if (best < 0) {
        for (int i = 0; i < c.library.collection_count; i++) {  // What the server answers, as is.
            static const char *const QUERIES[] = {"", "?type=1", "?type=4"};
            for (int q = 0; q < 3; q++) {
                char p[128], url[PLEX_URL_MAX];
                snprintf(p, sizeof(p), "/library/sections/%s/all%s", c.view_ids[i], QUERIES[q]);
                plexServerUrl(s, VERSION, p, url, sizeof(url));
                NetRequest request = {.url = url, .accept_json = true, .timeout_s = 30};
                NetResponse r;
                bool ok = netFetchText(net, &request, 1 << 20, &r);
                const char *size = ok ? strstr(r.data, "\"size\"") : NULL;
                printf("        library %d%s: status %ld %.12s\n", i, QUERIES[q], r.status, size ? size : "");
                if (ok)
                    netResponseFree(&r);
            }
        }
        jfCatalogFree(&c);
        return;
    }
    char item[PLEX_ID_MAX], part[PLEX_ID_MAX], path[320];
    plexLocationIds(c.library.items[best].path, item, part);
    int64_t duration = c.library.items[best].runtime_ns, at = duration / 3;
    long status = 0;
    plexTimelinePath(path, sizeof(path), item, "playing", at, duration);
    bool told = plexNetCall(net, s, VERSION, path, false, 30, &status);
    plexTimelinePath(path, sizeof(path), item, "stopped", at, duration);
    told = told && plexNetCall(net, s, VERSION, path, false, 30, &status);
    snprintf(what, sizeof(what), "%s: the server is told where it stopped (status %ld)", who, status);
    expect(told, what);
    jfCatalogFree(&c);
    jfCatalogInit(&c);
    read = plexNetReadLibrary(net, s, VERSION, &c, NULL, NULL);
    int64_t kept = -1;
    for (size_t i = 0; read && i < c.library.count; i++)
        if (strstr(c.library.items[i].path, item) == c.library.items[i].path + strlen(PLEX_PREFIX))
            kept = c.users[i].position_ns;
    printf("        %s: left at %llds of %llds, the server keeps %llds\n", who, (long long)(at / 1000000000),
           (long long)(duration / 1000000000), (long long)(kept / 1000000000));
    snprintf(what, sizeof(what), "%s: the library read again has where it was left", who);
    expect(kept > at - 2000000000ll && kept < at + 2000000000ll, what);
    jfCatalogFree(&c);
}

static void liveFlow(NetClient *net)
{
    const char *token = getenv("PLEX_TOKEN"), *server = getenv("PLEX_SERVER_ID"), *pin = getenv("PLEX_PIN");
    const char *owner_pin = getenv("PLEX_OWNER_PIN");  // Optional: the owner's own PIN, to switch back.
    if (!token || !server || !pin) {
        printf("PLEX_TOKEN, PLEX_SERVER_ID and PLEX_PIN are needed\n");
        g_failures++;
        return;
    }
    PlexSession s = {0};
    char message[160];
    snprintf(s.client_id, sizeof(s.client_id), "kaivideo-host-test");
    snprintf(s.token, sizeof(s.token), "%s", token);
    expect(plexNetPickServerOf(net, &s, VERSION, server, message, sizeof(message), NULL), "the account reaches its server");
    PlexHomeUser users[PLEX_HOME_USERS_MAX];
    int n = plexNetHomeUsers(net, &s, VERSION, users, PLEX_HOME_USERS_MAX, NULL);
    printf("        %d profiles\n", n);
    expect(n >= 2, "a Home with more than one profile");
    const PlexHomeUser *target = NULL, *owner = NULL;
    for (int i = 0; i < n; i++) {
        printf("        profile %d: admin=%d pin=%d managed=%d picture=%d\n", i, users[i].admin, users[i].protected_pin,
               users[i].restricted, users[i].thumb[0] != '\0');
        if (!target && !users[i].admin && users[i].protected_pin)
            target = &users[i];
        if (users[i].admin)
            owner = &users[i];
    }
    expect(target && owner, "a profile with a PIN, and the owner");
    if (!target || !owner)
        return;
    snprintf(s.account, sizeof(s.account), "%s", owner->uuid);  // As signing in keeps it.
    snprintf(s.profile, sizeof(s.profile), "%s", owner->uuid);
    checkProgress(net, &s, "the owner");
    PlexSession before = s;
    char wrong[8];
    snprintf(wrong, sizeof(wrong), "%s", strcmp(pin, "0000") ? "0000" : "1111");
    long status = 0;
    PlexSwitchResult r = PLEX_SWITCH_OK;
    if (getenv("PLEX_TRY_WRONG")) {  // plex.tv counts wrong PINs: only when asked (a few runs lock it a while).
        status = rawSwitchStatus(net, &s, target->uuid, wrong);
        printf("        plex.tv's status for a wrong PIN: %ld\n", status);
        r = plexNetSwitchHome(net, &s, VERSION, target, wrong, message, sizeof(message), NULL);
        printf("        a wrong PIN: result %d\n", r);
        expect(r == PLEX_SWITCH_WRONG_PIN && !memcmp(&s, &before, sizeof(s)),
               "a wrong PIN: refused, the session as it was");
    }
    r = plexNetSwitchHome(net, &s, VERSION, target, pin, message, sizeof(message), NULL);
    printf("        the right PIN: result %d%s%s\n", r, message[0] ? ": " : "", message);
    expect(r == PLEX_SWITCH_OK, "the right PIN is taken");
    if (r != PLEX_SWITCH_OK)
        return;
    expect(strcmp(s.token, before.token) && strcmp(s.server_token, before.server_token) && !plexSessionIsAccount(&s) &&
               !strcmp(s.server_id, before.server_id),
           "the profile's own tokens, for the same server");
    status = 0;
    expect(plexNetCall(net, &s, VERSION, "/library/sections", false, 30, &status), "its libraries, as it sees them");
    checkProgress(net, &s, "the profile");
    expect(plexNetHomeUsers(net, &s, VERSION, users, PLEX_HOME_USERS_MAX, NULL) == n,
           "the profiles, read with the profile's token");
    PlexSession managed = s;
    status = rawSwitchStatus(net, &s, owner->uuid, "");
    printf("        back to the owner without a PIN: status %ld (the owner %s a PIN)\n", status,
           owner->protected_pin ? "has" : "has no");
    r = plexNetSwitchHome(net, &s, VERSION, owner, owner_pin ? owner_pin : "", message, sizeof(message), NULL);
    printf("        back to the owner%s: result %d\n", owner_pin ? " with its PIN" : "", r);
    if (owner->protected_pin && !owner_pin)
        expect(r == PLEX_SWITCH_WRONG_PIN && !memcmp(&s, &managed, sizeof(s)), "the owner's PIN is asked for");
    else
        expect(r == PLEX_SWITCH_OK && plexSessionIsAccount(&s), "back to the account's own profile");
    memset(&managed, 0, sizeof(managed));
    memset(&s, 0, sizeof(s));
    memset(&before, 0, sizeof(before));
}

int main(int argc, char **argv)
{
    if (!netInit())
        return 1;
    NetClient *net = netClientOpen();
    if (argc > 1 && !strcmp(argv[1], "live"))
        liveFlow(net);
    else
        fakeFlow(net);
    netClientClose(net);
    netExit();
    printf("%s\n", g_failures ? "FAILED" : "all good");
    return g_failures ? 1 : 0;
}
