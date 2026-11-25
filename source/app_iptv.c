// The IPTV list (iptv.h): its Settings page, and its channels and guide
// loaded in the background (iptvload.h) for the Live tab.
#include "app.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "approot.h"
#include "iptvload.h"
#include "keyboard_system.h"
#include "net.h"
#include "lang.h"
#include "ui_settings.h"

static IptvAccount g_account;  // What Settings shows and the loads use; the file keeps it.
static bool g_network;         // The console's network, held while a list is set: its channels open on it.

// Holds the network while there's a list (each part that needs it holds it), or lets it go.
static void useNetwork(void)
{
    bool wanted = g_account.kind != IPTV_NONE && g_account.address[0];
    if (wanted && !g_network)
        g_network = netInit();
    else if (!wanted && g_network) {
        netExit();
        g_network = false;
    }
}

static void accountPath(char *out, size_t size)
{
    appConfigPath(out, size, IPTV_ACCOUNT_FILE);
}

static void startLoad(App *app)
{
    (void)app;
    XtreamAccount x;  // Its films and series are read with it.
    iptvXtreamOf(&g_account, &x);
    xtreamVodSetAccount(g_account.kind == IPTV_XTREAM && g_account.address[0] ? &x : NULL,
                        g_account.any_certificate);
    memset(&x, 0, sizeof(x));
    if (g_account.kind != IPTV_NONE && g_account.address[0])
        iptvLoadStart(&g_account, (int64_t)time(NULL));
}

// The connection test: the channels are loaded again, and what comes (or why not) is said
// when it ends (appIptvUpdate). A load still running is replaced, so it can't hide this one.
static void testNow(App *app, uint64_t now)
{
    startLoad(app);
    appShowMessage(app, T("Testing the connection: loading the channels\xE2\x80\xA6"), now);
}

void appIptvStart(App *app)
{
    char path[APPROOT_PATH_MAX + 32];
    accountPath(path, sizeof(path));
    FILE *f = fopen(path, "rb");
    char text[4096] = "";
    if (f) {
        size_t n = fread(text, 1, sizeof(text) - 1, f);
        text[n] = '\0';
        fclose(f);
    }
    if (!iptvAccountRead(&g_account, text))
        memset(&g_account, 0, sizeof(g_account));
    memset(text, 0, sizeof(text));
    appIptvHomeStart(app);
    useNetwork();
    startLoad(app);  // Its channels for Live, from the start.
}

static void save(App *app, uint64_t now)
{
    char path[APPROOT_PATH_MAX + 32], text[4096];
    accountPath(path, sizeof(path));
    useNetwork();
    if (g_account.kind == IPTV_NONE) {
        remove(path);
        return;
    }
    FILE *f = iptvAccountWrite(&g_account, text, sizeof(text)) ? fopen(path, "wb") : NULL;
    bool ok = f && fputs(text, f) >= 0;
    if (f)
        ok = fclose(f) == 0 && ok;
    memset(text, 0, sizeof(text));
    if (!ok)
        appNotify(app, NOTICE_ERROR, T("It couldn't be written to the card."), now);
}

void appIptvExit(App *app)
{
    iptvLoadExit();
    iptvListFree(&app->iptv);
    iptvGuideFree(&app->iptv_guide);
    appIptvHomeExit(app);
    appXtreamExit(app);
    xtreamVodSetAccount(NULL, false);
    memset(&g_account, 0, sizeof(g_account));
    useNetwork();  // Let go.
}

// --- Settings ---

static void typeAddress(App *app, uint64_t now)
{
    bool xtream = g_account.kind == IPTV_XTREAM;
    char text[IPTV_URL_MAX], address[IPTV_URL_MAX];
    SystemKeyboardResult r = systemKeyboardInput(
        xtream ? T("The Xtream server: http://host:port") : T("The M3U list's address: https://..."), g_account.address,
        text, sizeof(text), sizeof(text) - 1);
    if (r == SYSTEM_KEYBOARD_UNAVAILABLE) {
        appNotify(app, NOTICE_ERROR, T("The keyboard couldn't open."), now);
        return;
    }
    if (r != SYSTEM_KEYBOARD_OK || !text[0])
        return;
    bool ok = xtream ? xtreamNormalizeServer(text, address, sizeof(address))
                     : (!strncmp(text, "http://", 7) || !strncmp(text, "https://", 8)) &&
                           snprintf(address, sizeof(address), "%s", text) < (int)sizeof(address);
    if (!ok) {
        appNotify(app, NOTICE_ERROR,
                  xtream ? T("That isn't a server's address.") : T("That isn't an address: it starts with http:// or https://."),
                  now);
        return;
    }
    bool other_server = strcmp(address, g_account.address) != 0;
    if (other_server)  // Another server: the old one's password isn't sent there.
        memset(g_account.password, 0, sizeof(g_account.password));
    snprintf(g_account.address, sizeof(g_account.address), "%s", address);
    if (g_account.kind == IPTV_NONE)
        g_account.kind = IPTV_M3U;
    save(app, now);
    if (xtream && (!g_account.user[0] || !g_account.password[0])) {  // Another server has none yet.
        appNotify(app, NOTICE_DONE, T("Server set: now its user and password."), now);
        return;
    }
    // An M3U list, or the same server with its account: tested at once, as the account is.
    appNotify(app, NOTICE_DONE, T("List set."), now);
    testNow(app, now);
}

static void typeAccount(App *app, uint64_t now)
{
    if (g_account.kind != IPTV_XTREAM) {
        appShowMessage(app, T("An M3U list needs no user: its address is enough."), now);
        return;
    }
    char user[IPTV_NAME_MAX], password[IPTV_NAME_MAX] = "";
    SystemKeyboardResult r = systemKeyboardInput(T("User name"), g_account.user, user, sizeof(user), sizeof(user) - 1);
    if (r == SYSTEM_KEYBOARD_OK && user[0])
        r = systemKeyboardPassword(T("Password"), password, sizeof(password), sizeof(password) - 1);
    if (r == SYSTEM_KEYBOARD_UNAVAILABLE) {
        appNotify(app, NOTICE_ERROR, T("The keyboard couldn't open."), now);
    } else if (r == SYSTEM_KEYBOARD_OK && user[0]) {
        snprintf(g_account.user, sizeof(g_account.user), "%s", user);
        snprintf(g_account.password, sizeof(g_account.password), "%s", password);
        save(app, now);
        appNotify(app, NOTICE_DONE, T("Saved: the password is only encoded on the card."), now);
        if (g_account.address[0])
            testNow(app, now);  // Every time: a wrong password is said now, not in Live.
        else
            appNotify(app, NOTICE_INFO, T("Now the server's address."), now);
    }
    memset(password, 0, sizeof(password));
}

static void typeGuide(App *app, uint64_t now)
{
    char text[IPTV_URL_MAX];
    SystemKeyboardResult r = systemKeyboardInput(T("An XMLTV guide's address (empty: the list's own)"), g_account.guide,
                                                 text, sizeof(text), sizeof(text) - 1);
    if (r == SYSTEM_KEYBOARD_UNAVAILABLE) {
        appNotify(app, NOTICE_ERROR, T("The keyboard couldn't open."), now);
        return;
    }
    if (r != SYSTEM_KEYBOARD_OK)
        return;
    if (text[0] && strncmp(text, "http://", 7) && strncmp(text, "https://", 8)) {
        appNotify(app, NOTICE_ERROR, T("That isn't an address: it starts with http:// or https://."), now);
        return;
    }
    snprintf(g_account.guide, sizeof(g_account.guide), "%s", text);
    save(app, now);
}

static void load(App *app, uint64_t now)
{
    if (!g_account.address[0] || (g_account.kind == IPTV_XTREAM && !g_account.user[0])) {
        appShowMessage(app, g_account.kind == IPTV_XTREAM ? T("Enter the server, and its user and password, first.")
                                                          : T("Enter the list's address first."),
                       now);
        return;
    }
    startLoad(app);
    appShowMessage(app, T("Loading the channels\xE2\x80\xA6"), now);
}

static void forget(App *app, uint64_t now)
{
    iptvLoadExit();
    memset(&g_account, 0, sizeof(g_account));
    save(app, now);
    iptvListFree(&app->iptv);
    iptvGuideFree(&app->iptv_guide);
    appXtreamExit(app);
    xtreamVodSetAccount(NULL, false);
    if (app->config.use_iptv)
        appLibrarySetSource(app, false);  // Its view goes: the card's.
    appLiveIptvChanged(app);
    appNotify(app, NOTICE_DONE, T("The IPTV list is forgotten."), now);
}

bool appIptvActivate(App *app, SettingRow row, uint64_t now)
{
    switch (row) {
    case SETTING_IPTV_KIND:  // M3U or Xtream: the address and account start over.
        g_account.kind = g_account.kind == IPTV_XTREAM ? IPTV_M3U : IPTV_XTREAM;
        g_account.address[0] = '\0';
        g_account.user[0] = '\0';
        memset(g_account.password, 0, sizeof(g_account.password));
        return true;  // Saved once an address is typed.
    case SETTING_IPTV_ADDRESS: typeAddress(app, now); return true;
    case SETTING_IPTV_ACCOUNT: typeAccount(app, now); return true;
    case SETTING_IPTV_GUIDE: typeGuide(app, now); return true;
    case SETTING_IPTV_CERTIFICATE:
        g_account.any_certificate = !g_account.any_certificate;
        if (g_account.address[0])
            save(app, now);
        appShowMessage(app,
                       g_account.any_certificate ? T("The list's certificates aren't checked: load the channels again.")
                                                 : T("The list's certificates are checked again."),
                       now);
        return true;
    case SETTING_IPTV_LOAD: load(app, now); return true;
    case SETTING_IPTV_FORGET: forget(app, now); return true;
    default: return false;
    }
}

// --- The channels that came ---

void appIptvUpdate(App *app, uint64_t now)
{
    IptvLoadStatus s;
    iptvLoadStatus(&s);
    if (s.serial == app->iptv_seen || (s.state != IPTV_LOAD_DONE && s.state != IPTV_LOAD_FAILED))
        return;
    app->iptv_seen = s.serial;
    if (s.state == IPTV_LOAD_FAILED) {
        appNotify(app, NOTICE_ERROR, s.error, now);
        return;
    }
    IptvList list;
    IptvGuide guide;
    XtreamCatalog catalog;
    if (!iptvLoadTake(&list, &guide, &catalog))
        return;
    iptvListFree(&app->iptv);
    iptvGuideFree(&app->iptv_guide);
    app->iptv = list;
    app->iptv_guide = guide;
    appLiveIptvChanged(app);
    appXtreamTake(app, &catalog);
    char line[NOTICE_TEXT_MAX];
    if (s.guide_error[0])
        snprintf(line, sizeof(line), T("%d IPTV channels; the guide: %s"), list.count, s.guide_error);
    else if (s.has_guide)
        snprintf(line, sizeof(line), T("%d IPTV channels, with their guide."), list.count);
    else
        snprintf(line, sizeof(line), T("%d IPTV channels (the list has no guide)."), list.count);
    appNotify(app, s.guide_error[0] ? NOTICE_INFO : NOTICE_DONE, line, now);
}

bool appIptvConfigured(void)
{
    return g_account.kind != IPTV_NONE && g_account.address[0];
}

bool appIptvAnyCertificate(void)
{
    return g_account.any_certificate;
}

void appIptvFeedSettings(const App *app)
{
    static char address[IPTV_URL_MAX], state[96];
    snprintf(address, sizeof(address), "%s", g_account.address);
    IptvLoadStatus s;
    iptvLoadStatus(&s);
    if (s.state == IPTV_LOAD_LIST)
        snprintf(state, sizeof(state), "%s", T("Loading\xE2\x80\xA6"));
    else if (s.state == IPTV_LOAD_GUIDE)
        snprintf(state, sizeof(state), "%s", T("Reading the guide\xE2\x80\xA6"));
    else if (app->iptv.count > 0)
        snprintf(state, sizeof(state), T("%d channels"), app->iptv.count);
    else
        state[0] = '\0';
    uiSettingsSetIptv(g_account.kind == IPTV_XTREAM ? T("Xtream") : T("M3U list"),
                      address, g_account.kind == IPTV_XTREAM ? g_account.user : NULL, g_account.guide, state,
                      !g_account.any_certificate);
}
