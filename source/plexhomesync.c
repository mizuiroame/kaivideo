#include "plexhomesync.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "jfsync.h"
#include "jfsync_internal.h"
#include "lang.h"
#include "plexsync.h"

// --- The thread ---

static struct {
    pthread_mutex_t lock;
    pthread_t thread;
    bool started;              // A thread to join.
    PlexHomeStatus status;     // Under lock.
    PlexHomeUser target;       // The profile being switched to,
    char pin[PLEX_HOME_PIN_DIGITS + 1];  // and its PIN, until it's sent.
    volatile bool cancel;      // KaiVideo is closing: the request under way stops.
} g = {.lock = PTHREAD_MUTEX_INITIALIZER};

static void finish(PlexHomeState state)
{
    pthread_mutex_lock(&g.lock);
    g.status.state = state;
    pthread_mutex_unlock(&g.lock);
}

static void *loadThread(void *arg)
{
    (void)arg;
    PlexHomeUser users[PLEX_HOME_USERS_MAX];
    PlexSession s = jfSyncPlexSession();
    NetClient *net = netClientOpen();
    int count = net ? plexNetHomeUsers(net, &s, jfSyncVersionText(), users, PLEX_HOME_USERS_MAX, &g.cancel) : -1;
    netClientClose(net);
    memset(&s, 0, sizeof(s));
    pthread_mutex_lock(&g.lock);
    if (count >= 0) {
        memcpy(g.status.users, users, sizeof(users));
        g.status.count = count;
        g.status.state = PLEX_HOME_LOADED;
    } else {
        g.status.count = 0;
        snprintf(g.status.message, sizeof(g.status.message), "%s", T("plex.tv can't be reached."));
        g.status.state = PLEX_HOME_LOAD_FAILED;
    }
    pthread_mutex_unlock(&g.lock);
    return NULL;
}

static void *switchThread(void *arg)
{
    (void)arg;
    char pin[sizeof(g.pin)], message[sizeof(g.status.message)];
    pthread_mutex_lock(&g.lock);
    PlexHomeUser user = g.target;
    memcpy(pin, g.pin, sizeof(pin));
    memset(g.pin, 0, sizeof(g.pin));
    pthread_mutex_unlock(&g.lock);
    PlexSession s = jfSyncPlexSession();
    NetClient *net = netClientOpen();
    PlexSwitchResult result =
        net ? plexNetSwitchHome(net, &s, jfSyncVersionText(), &user, pin, message, sizeof(message), &g.cancel)
            : PLEX_SWITCH_UNREACHABLE;
    if (!net)
        snprintf(message, sizeof(message), "%s", plexHomeSwitchMessage(result));
    memset(pin, 0, sizeof(pin));
    netClientClose(net);
    if (result == PLEX_SWITCH_OK && !g.cancel)
        jfSyncPlexUseProfile(&s);
    pthread_mutex_lock(&g.lock);
    g.status.result = result;
    snprintf(g.status.message, sizeof(g.status.message), "%s", message);
    snprintf(g.status.switched_to, sizeof(g.status.switched_to), "%s", result == PLEX_SWITCH_OK ? s.user_name : "");
    pthread_mutex_unlock(&g.lock);
    memset(&s, 0, sizeof(s));
    finish(result == PLEX_SWITCH_OK ? PLEX_HOME_SWITCHED : PLEX_HOME_SWITCH_FAILED);
    return NULL;
}

// Joins a thread that has finished. False if one is still under way.
static bool settle(void)
{
    if (!g.started)
        return true;
    pthread_mutex_lock(&g.lock);
    bool busy = g.status.state == PLEX_HOME_LOADING || g.status.state == PLEX_HOME_SWITCHING;
    pthread_mutex_unlock(&g.lock);
    if (busy)
        return false;
    pthread_join(g.thread, NULL);
    g.started = false;
    return true;
}

static bool start(PlexHomeState state, void *(*run)(void *))
{
    if (!settle() || !jfSyncIsUp() || !jfSyncSignedIn(SERVER_PLEX))
        return false;
    pthread_mutex_lock(&g.lock);
    g.status.state = state;
    g.status.result = PLEX_SWITCH_OK;
    g.status.message[0] = g.status.switched_to[0] = '\0';
    pthread_mutex_unlock(&g.lock);
    g.started = pthread_create(&g.thread, NULL, run, NULL) == 0;
    if (!g.started)
        finish(state == PLEX_HOME_LOADING ? PLEX_HOME_LOAD_FAILED : PLEX_HOME_SWITCH_FAILED);
    return g.started;
}

bool plexHomeStartLoad(void)
{
    return start(PLEX_HOME_LOADING, loadThread);
}

bool plexHomeStartSwitch(const PlexHomeUser *user, const char *pin)
{
    if (!settle())
        return false;
    pthread_mutex_lock(&g.lock);
    g.target = *user;
    snprintf(g.pin, sizeof(g.pin), "%s", plexHomePinValid(pin) ? pin : "");
    pthread_mutex_unlock(&g.lock);
    bool ok = start(PLEX_HOME_SWITCHING, switchThread);
    if (!ok) {
        pthread_mutex_lock(&g.lock);
        memset(g.pin, 0, sizeof(g.pin));
        pthread_mutex_unlock(&g.lock);
    }
    return ok;
}

bool plexHomeBusy(void)
{
    return !settle();
}

void plexHomeGetStatus(PlexHomeStatus *out)
{
    settle();
    pthread_mutex_lock(&g.lock);
    *out = g.status;
    pthread_mutex_unlock(&g.lock);
}

void plexHomeStop(void)
{
    g.cancel = true;
    if (g.started)
        pthread_join(g.thread, NULL);
    g.started = false;
    g.cancel = false;
}
