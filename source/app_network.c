// The network folder (dav.h): its Settings page, its test, and its place at
// the top of Files.
#include "app.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "approot.h"
#include "davnet.h"
#include "net.h"
#include "smbnet.h"
#include "keyboard_system.h"
#include "lang.h"
#include "ui_settings.h"

#define TEST_TEXT_MAX 256

static pthread_mutex_t g_test_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_testing, g_tested;
static char g_test_text[TEST_TEXT_MAX];

static void accountPath(char *out, size_t size)
{
    appConfigPath(out, size, DAV_ACCOUNT_FILE);
}

// The console's network, started for the network folder (the servers' sync
// starts it for itself too): without it, no socket opens.
static bool g_network;

static void useNetwork(void)
{
    bool wanted = davConfigured();
    if (wanted && !g_network)
        g_network = netInit();
    else if (!wanted && g_network) {
        netExit();
        g_network = false;
    }
}

// Files' top shows the folder when there is one; Files leaves it when it's gone.
static void refreshFiles(App *app)
{
    Browser *b = &app->browser;
    bool set = davConfigured();
    b->network = set ? davList : NULL;
    DavAccount a;
    davGetAccount(&a);
    char shown[DAV_ADDRESS_MAX];
    davShownAddress(a.address, shown, sizeof(shown));
    memset(&a, 0, sizeof(a));
    snprintf(b->network_name, sizeof(b->network_name), T("Network folder · %s"), shown);
    if (!strcmp(b->path, BROWSER_ROOT) || davIsLocation(b->path)) {  // Inside the folder: its account changed.
        size_t cursor = b->cursor;
        browserOpen(b, BROWSER_ROOT);
        b->cursor = cursor < b->count ? cursor : 0;
    }
}

void appNetworkStart(App *app)
{
    char path[APPROOT_PATH_MAX + 32];
    accountPath(path, sizeof(path));
    davLoadAccount(path);
    refreshFiles(app);
    useNetwork();
}

static void save(App *app, const DavAccount *a, uint64_t now)
{
    davUseAccount(a);
    smbForget();  // The listing's connection was the old account's.
    char path[APPROOT_PATH_MAX + 32];
    accountPath(path, sizeof(path));
    if (!davSaveAccount(path))
        appShowMessage(app, T("It couldn't be written to the card."), now);
    useNetwork();
    refreshFiles(app);
    appLibraryNetworkChanged(app);
}

static void typeAddress(App *app, uint64_t now)
{
    DavAccount a;
    davGetAccount(&a);
    char shown[DAV_ADDRESS_MAX], text[DAV_ADDRESS_MAX], address[DAV_ADDRESS_MAX];
    davShownAddress(a.address, shown, sizeof(shown));
    // Room for "https://" and the last '/' that may be added.
    SystemKeyboardResult result = systemKeyboardInput(T("Address: \\\\PC\\Videos (a shared folder) or 192.168.0.10:8098 (WebDAV)"), shown, text,
                                                      sizeof(text), sizeof(text) - 10);
    if (result == SYSTEM_KEYBOARD_UNAVAILABLE) {
        appShowMessage(app, T("The keyboard couldn't open."), now);
    } else if (result == SYSTEM_KEYBOARD_OK && !davNormalizeAddress(text, address, sizeof(address))) {
        appShowMessage(app, T("That isn't a network folder's address: \\\\PC\\Videos, smb://, http:// or https://."), now);
    } else if (result == SYSTEM_KEYBOARD_OK) {
        if (!davSameServer(address, a.address)) {  // Another server: the old one's password isn't sent there.
            a.user[0] = '\0';
            memset(a.password, 0, sizeof(a.password));
        }
        memcpy(a.address, address, sizeof(a.address));
        save(app, &a, now);
        appShowMessage(app, T("Network folder set: test the connection, or open it in Files."), now);
    }
    memset(&a, 0, sizeof(a));
}

static void typeAccount(App *app, uint64_t now)
{
    DavAccount a;
    davGetAccount(&a);
    if (!a.address[0]) {
        appShowMessage(app, T("Enter the folder's address first."), now);
        memset(&a, 0, sizeof(a));
        return;
    }
    char user[DAV_TEXT_MAX], password[DAV_TEXT_MAX] = "";
    SystemKeyboardResult result =
        systemKeyboardInput(T("User name (empty: none; DOMAIN\\user for a domain)"), a.user, user, sizeof(user), sizeof(user) - 1);
    if (result == SYSTEM_KEYBOARD_OK && user[0])
        result = systemKeyboardPassword(T("Password"), password, sizeof(password), sizeof(password) - 1);
    if (result == SYSTEM_KEYBOARD_UNAVAILABLE) {
        appShowMessage(app, T("The keyboard couldn't open."), now);
    } else if (result == SYSTEM_KEYBOARD_OK) {
        snprintf(a.user, sizeof(a.user), "%s", user);
        snprintf(a.password, sizeof(a.password), "%s", password);
        save(app, &a, now);
    }
    memset(password, 0, sizeof(password));
    memset(&a, 0, sizeof(a));
}

static void *testThread(void *arg)
{
    (void)arg;
    char text[TEST_TEXT_MAX];
    davTest(text, sizeof(text));
    pthread_mutex_lock(&g_test_lock);
    snprintf(g_test_text, sizeof(g_test_text), "%s", text);
    g_testing = false;
    g_tested = true;
    pthread_mutex_unlock(&g_test_lock);
    return NULL;
}

static void test(App *app, uint64_t now)
{
    if (!davConfigured()) {
        appShowMessage(app, T("Enter the folder's address first."), now);
        return;
    }
    pthread_mutex_lock(&g_test_lock);
    bool busy = g_testing;
    pthread_t thread;
    if (!busy)
        g_testing = pthread_create(&thread, NULL, testThread, NULL) == 0;
    bool started = !busy && g_testing;
    pthread_mutex_unlock(&g_test_lock);
    if (started)
        pthread_detach(thread);
    appShowMessage(app, busy || started ? T("Testing the connection...") : T("The network can't be used."), now);
}

static void forget(App *app, uint64_t now)
{
    DavAccount none = {0};
    save(app, &none, now);
    appShowMessage(app, T("The network folder is forgotten."), now);
}

bool appNetworkActivate(App *app, SettingRow row, uint64_t now)
{
    switch (row) {
    case SETTING_NET_ADDRESS: typeAddress(app, now); return true;
    case SETTING_NET_ACCOUNT: typeAccount(app, now); return true;
    case SETTING_NET_TEST: test(app, now); return true;
    case SETTING_NET_FORGET: forget(app, now); return true;
    default: return false;
    }
}

void appNetworkUpdate(App *app, uint64_t now)
{
    char text[TEST_TEXT_MAX] = "";
    pthread_mutex_lock(&g_test_lock);
    if (g_tested) {
        snprintf(text, sizeof(text), "%s", g_test_text);
        g_tested = false;
    }
    pthread_mutex_unlock(&g_test_lock);
    if (text[0]) {
        char raw[256];
        smbLastError(raw, sizeof(raw));
        appLog("Network folder test: %s%s%s", text, raw[0] ? " | smb: " : "", raw);
        appShowMessage(app, text, now);
    }
}

void appNetworkExit(void)
{
    smbForget();
    if (g_network)
        netExit();
    g_network = false;
}

void appNetworkFeedSettings(void)
{
    static char shown[DAV_ADDRESS_MAX], user[DAV_TEXT_MAX];
    DavAccount a;
    davGetAccount(&a);
    davShownAddress(a.address, shown, sizeof(shown));
    snprintf(user, sizeof(user), "%s", a.user);
    memset(&a, 0, sizeof(a));
    uiSettingsSetNetwork(shown, user);
}
