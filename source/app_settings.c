// Settings and About, over the library: up and down choose a row, left and
// right change it, A opens what a row opens (or changes it), B goes back a
// page or closes. Changes apply at once and are kept in settings.txt. The
// subtitles' size, color and style are changed in the player's Subtitles list.
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <switch.h>

#include "app.h"
#include "approot.h"
#include "background.h"
#include "buttons.h"
#include "font.h"
#include "jfprobe.h"
#include "keyboard_system.h"
#include "lang.h"
#include "languages.h"
#include "net.h"
#include "theme.h"
#include "ui.h"
#include "ui_library.h"
#include "ui_settings.h"
#include "util.h"

#ifndef KAIVIDEO_VERSION
#define KAIVIDEO_VERSION "dev"
#endif

#define HTTP_PREFIX "http://"
#define CREDENTIAL_TEXT_MAX 128

void appApplySubtitleStyle(const App *app)
{
    uiSetSubtitleStyle(settingSubtitlePx(&app->config), settingSubtitleColor(&app->config),
                       app->config.subtitle_style == SUBTITLE_BOX);
}

#define FONT_FILES_MAX 64

static int compareFontNames(const void *a, const void *b)
{
    return utilCompareNames((const char *)a, (const char *)b);
}

// The .ttf and .otf files in the config folder's fonts folder, by name.
static int listSubtitleFonts(char names[][SETTINGS_NAME_MAX], int max)
{
    char folder[APPROOT_PATH_MAX + 32];
    appConfigPath(folder, sizeof(folder), SUBTITLE_FONTS_FOLDER);
    DIR *dir = opendir(folder);
    if (!dir)
        return 0;
    int count = 0;
    struct dirent *entry;
    while (count < max && (entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        if (name[0] != '.' && (utilHasExtension(name, "ttf") || utilHasExtension(name, "otf")) &&
            strlen(name) < SETTINGS_NAME_MAX)
            snprintf(names[count++], SETTINGS_NAME_MAX, "%s", name);
    }
    closedir(dir);
    qsort(names, (size_t)count, SETTINGS_NAME_MAX, compareFontNames);
    return count;
}

// One of the fonts KaiVideo brings, by name, or -1.
static int bundledFont(const char *name)
{
    for (int i = 0; i < fontBundledCount(); i++)
        if (!strcmp(fontBundledName(i), name))
            return i;
    return -1;
}

// Text subtitles in the font chosen (config.subtitle_font): KaiVideo's own,
// one it brings (by name), or a file in config/fonts. False if that file can't be read.
static bool loadSubtitleFont(const App *app)
{
    if (!app->config.subtitle_font[0])
        return fontSetSubtitleFile(NULL);
    int bundled = bundledFont(app->config.subtitle_font);
    if (bundled >= 0) {
        fontSetSubtitleBundled(bundled);
        return true;
    }
    char folder[APPROOT_PATH_MAX + 32], path[APPROOT_PATH_MAX + SETTINGS_NAME_MAX + 40];
    appConfigPath(folder, sizeof(folder), SUBTITLE_FONTS_FOLDER);
    snprintf(path, sizeof(path), "%s/%s", folder, app->config.subtitle_font);
    return fontSetSubtitleFile(path);
}

bool appStepSubtitleFont(App *app, int delta, char *note, size_t note_size)
{
    // KaiVideo's own font, the ones it brings, then the files in config/fonts.
    static char names[FONT_FILES_MAX][SETTINGS_NAME_MAX];
    int bundled = fontBundledCount() < FONT_FILES_MAX ? fontBundledCount() : FONT_FILES_MAX;
    for (int i = 0; i < bundled; i++)
        snprintf(names[i], SETTINGS_NAME_MAX, "%s", fontBundledName(i));
    int count = bundled + listSubtitleFonts(names + bundled, FONT_FILES_MAX - bundled);
    int index = 0;  // 0 is the built-in font, then the others.
    for (int i = 0; i < count; i++)
        if (!strcmp(names[i], app->config.subtitle_font))
            index = i + 1;
    index = ((index + delta) % (count + 1) + count + 1) % (count + 1);
    snprintf(app->config.subtitle_font, sizeof(app->config.subtitle_font), "%s", index ? names[index - 1] : "");
    if (!loadSubtitleFont(app)) {
        snprintf(note, note_size, T("%.80s can't be read as a font."), app->config.subtitle_font);
        app->config.subtitle_font[0] = '\0';
        fontSetSubtitleFile(NULL);
        return false;
    }
    return true;
}

static Color g_theme_accent;  // The accent chosen in Display.

static void applyLooks(const App *app)
{
    themeApply(app->config.theme, app->config.accent);
    g_theme_accent = g_colors.accent;
    for (int k = 0; k < SERVER_KIND_COUNT; k++) {  // Each server's own color, or the one chosen for it.
        int accent = settingServerColor(&app->config, (ServerKind)k);
        uiSetServerColor((ServerKind)k, accent < 0 ? uiServerOwnColor((ServerKind)k) : themeAccentColor(accent));
    }
    uiLibrarySetThumbnails(app->config.thumbnails);
    appApplySubtitleStyle(app);
}

void appApplyViewAccent(const App *app)
{
    bool server = app->config.use_jellyfin && !app->settings_open;  // Settings shows the accent chosen.
    g_colors.accent = server ? uiServerColor((ServerKind)app->config.server_view) : g_theme_accent;
}

// Puts the chosen language in use (English if it's gone), and says its name in Settings.
static void useLanguage(App *app)
{
    if (!languageUse(app->config.language[0] ? app->config.language : "en"))
        snprintf(app->config.language, sizeof(app->config.language), "en");
    int at = languageIndex(app->config.language);
    snprintf(app->config.language_name, sizeof(app->config.language_name), "%s", languageName(at));
}

void appSettingsStart(App *app)
{
    char lang[APPROOT_PATH_MAX + 32];
    appConfigPath(lang, sizeof(lang), "lang");
    mkdir(lang, 0777);  // Where to put more languages' files, there to be seen.
    languagesFind(lang);
    useLanguage(app);
    applyLooks(app);
    char fonts[APPROOT_PATH_MAX + 32];
    appConfigPath(fonts, sizeof(fonts), SUBTITLE_FONTS_FOLDER);
    mkdir(fonts, 0777);  // Where to put fonts for subtitles, there to be seen.
    if (!loadSubtitleFont(app))
        app->config.subtitle_font[0] = '\0';  // Gone or broken: the built-in one.
    netSetCheckCertificates(app->config.jf_check_certificate);
    if (app->config.background[0] &&
        !backgroundLoad(app->library_root, app->config.background, app->config.blur))
        app->config.background[0] = '\0';  // Gone or unreadable: not tried again every start.
}

static bool signedIn(const App *app)
{
    return app->jf.state == JF_SIGNED_IN;
}

static void openPage(App *app, SettingsPage page)
{
    SettingRow rows[SETTINGS_PAGE_MAX_ROWS];
    settingsPageRows(page, signedIn(app), (ServerKind)app->config.server_kind, rows);
    app->settings_page = page;
    app->settings_cursor = rows[0];
}

static void openServer(App *app, ServerKind kind, uint64_t now);

void appOpenSettings(App *app)
{
    appOpenSettingsAt(app, SETTINGS_PAGE_MAIN);
}

void appOpenSettingsAt(App *app, SettingsPage page)
{
    app->settings_open = true;
    app->settings_from_welcome = false;
    app->about_open = false;
    openPage(app, page);
}

// Back from a page to the main one (on the row that opened it), or closed;
// from the welcome's page, closed too (back to the welcome).
static void back(App *app)
{
    if (app->settings_page == SETTINGS_PAGE_MAIN || app->settings_from_welcome) {
        app->settings_open = false;
    } else {
        app->settings_cursor = settingPageOpener((SettingsPage)app->settings_page, (ServerKind)app->config.server_kind);
        app->settings_page = SETTINGS_PAGE_MAIN;
    }
}

// Steps through "None" and the images in <library>/Backgrounds.
static void stepBackground(App *app, int delta, uint64_t now)
{
    static char names[BACKGROUND_MAX_IMAGES][BACKGROUND_NAME_MAX];
    int count = (int)backgroundList(app->library_root, names, BACKGROUND_MAX_IMAGES);
    if (count == 0) {
        app->config.background[0] = '\0';
        appShowMessage(app,
                       app->library_root[0]
                           ? T("Put JPEG or PNG images in a Backgrounds folder in your library folder.")
                           : T("Choose the library folder first."),
                       now);
        return;
    }
    int index = 0;  // 0 is none, then the images.
    for (int i = 0; i < count; i++)
        if (strcmp(names[i], app->config.background) == 0)
            index = i + 1;
    index = ((index + delta) % (count + 1) + count + 1) % (count + 1);
    snprintf(app->config.background, sizeof(app->config.background), "%s", index ? names[index - 1] : "");
    if (!backgroundLoad(app->library_root, app->config.background, app->config.blur)) {
        char message[BACKGROUND_NAME_MAX + 80];
        snprintf(message, sizeof(message), T("%s can't be read (JPEG or PNG, up to 50 megapixels)."),
                 app->config.background);
        appNotify(app, NOTICE_ERROR, message, now);
        app->config.background[0] = '\0';
    }
}

static void openJellyfin(App *app, uint64_t now)
{
    if (!appJellyfinEnsure(app)) {
        appNotify(app, NOTICE_ERROR, T("The network can't be used."), now);
        return;
    }
    jfSyncGetStatus(&app->jf);
    openPage(app, SETTINGS_PAGE_JELLYFIN);
}

void appOpenJellyfinSettings(App *app, uint64_t now)
{
    app->settings_open = true;
    app->settings_from_welcome = false;
    app->about_open = false;
    openPage(app, SETTINGS_PAGE_MAIN);
    openServer(app, (ServerKind)app->config.server_kind, now);
}

static void typeServer(App *app, uint64_t now)
{
    if (signedIn(app)) {
        appShowMessage(app, T("Sign out first to change the server."), now);
        return;
    }
    char text[JF_SERVER_MAX];
    const char *initial = app->jf.server;
    if (!strncmp(initial, HTTP_PREFIX, sizeof(HTTP_PREFIX) - 1))
        initial += sizeof(HTTP_PREFIX) - 1;
    char prompt[160];
    snprintf(prompt, sizeof(prompt), T("%s server, like 192.168.0.10:8096"),
             serverKindName((ServerKind)app->config.server_kind));
    SystemKeyboardResult result = systemKeyboardInput(prompt, initial, text, sizeof(text), JF_SERVER_MAX - 1);
    if (result == SYSTEM_KEYBOARD_UNAVAILABLE)
        appNotify(app, NOTICE_ERROR, T("The keyboard couldn't open."), now);
    else if (result == SYSTEM_KEYBOARD_OK && !jfSyncSetServer(text))
        appNotify(app, NOTICE_ERROR, T("That isn't a server's address."), now);
}

// The library keeps the other servers signed in; with none left, it's the card's.
static void signOut(App *app)
{
    bool others = false;
    for (int k = 0; k < SERVER_KIND_COUNT; k++)
        others = others || (k != app->config.server_kind && jfSyncSignedIn((ServerKind)k));
    if (app->config.use_jellyfin && !others)
        appLibrarySetSource(app, false);
    jfSyncSignOut();
}

static void codeSignIn(App *app, uint64_t now)
{
    if (!app->jf.server[0]) {
        appShowMessage(app, T("Enter the server's address first."), now);
    } else {
        app->jf_signing = true;
        jfSyncSignIn();
    }
}

static void passwordSignIn(App *app, uint64_t now)
{
    if (!app->jf.server[0]) {
        appShowMessage(app, T("Enter the server's address first."), now);
        return;
    }
    char user[CREDENTIAL_TEXT_MAX], password[CREDENTIAL_TEXT_MAX];
    char prompt[160];
    snprintf(prompt, sizeof(prompt), T("%s user name"), serverKindName((ServerKind)app->config.server_kind));
    SystemKeyboardResult result = systemKeyboardInput(prompt, "", user, sizeof(user), sizeof(user) - 1);
    if (result == SYSTEM_KEYBOARD_OK && user[0])
        result = systemKeyboardPassword(T("Password"), password, sizeof(password), sizeof(password) - 1);
    else if (result == SYSTEM_KEYBOARD_OK)
        return;
    if (result == SYSTEM_KEYBOARD_UNAVAILABLE) {
        appNotify(app, NOTICE_ERROR, T("The keyboard couldn't open."), now);
    } else if (result == SYSTEM_KEYBOARD_OK) {
        app->jf_signing = true;
        jfSyncSignInWithPassword(user, password);
    }
    memset(password, 0, sizeof(password));
}

// A server's page: signing in and out, testing and finding are for it.
static void openServer(App *app, ServerKind kind, uint64_t now)
{
    app->config.server_kind = kind;
    appSaveSettings(app);
    if (appJellyfinEnsure(app)) {
        jfSyncUseKind(kind);
        jfProbeSetKind(kind);
    }
    openJellyfin(app, now);  // On its first row; Back lands on the server's row.
}

static void change(App *app, SettingRow row, int delta, uint64_t now)
{
    if (row == SETTING_IPTV_KIND || row == SETTING_IPTV_CERTIFICATE) {  // The IPTV list's: its file.
        appIptvActivate(app, row, now);
        return;
    }
    if (row == SETTING_NET_REMEMBER || row == SETTING_NET_UNSIGNED) {  // The network folder's account: its file.
        appNetworkActivate(app, row, now);
        return;
    } else if (row == SETTING_BACKGROUND) {
        stepBackground(app, delta, now);
    } else if (row == SETTING_LANGUAGE) {  // Through the languages found (English, Spanish, files).
        int count = languagesCount();
        int at = (languageIndex(app->config.language) + delta % count + count) % count;
        snprintf(app->config.language, sizeof(app->config.language), "%s", languageId(at));
        useLanguage(app);
        appSaveSettings(app);
        appLibraryRefreshHome(app);
    } else {
        settingStep(&app->config, row, delta);
        applyLooks(app);
        netSetCheckCertificates(app->config.jf_check_certificate);
        if (row == SETTING_THEME || row == SETTING_BLUR)
            backgroundSetBlur(app->config.blur);  // Made again over the theme's color, or with the new blur.
    }
    appSaveSettings(app);
}

// Testing the server, or finding one: the answer comes as a message (app_jellyfin.c).
static void askNetwork(App *app, SettingRow row, uint64_t now)
{
    if (jfProbeRunning()) {
        appShowMessage(app, T("Still asking the network..."), now);
    } else if (row == SETTING_JF_FIND) {
        char looking[80];
        snprintf(looking, sizeof(looking), T("Looking for %s servers on this network..."),
                 serverKindName((ServerKind)app->config.server_kind));
        if (jfProbeStartFind(JF_PROBE_BROADCAST))
            appShowMessage(app, looking, now);
    } else if (!app->jf.server[0]) {
        appShowMessage(app, T("Enter the server's address first, or find it on this network."), now);
    } else if (jfProbeStartTest(app->jf.server)) {
        appShowMessage(app, T("Testing the connection..."), now);
    }
}

static void activate(App *app, SettingRow row, uint64_t now)
{
    switch (row) {
    case SETTING_LIBRARY:
        app->settings_open = false;
        appOpenPicker(app);
        break;
    case SETTING_RESCAN:
        app->settings_open = false;
        appLibraryRescan(app, now);
        break;
    case SETTING_OPEN_DISPLAY: openPage(app, SETTINGS_PAGE_DISPLAY); break;
    case SETTING_OPEN_LIBRARY: openPage(app, SETTINGS_PAGE_LIBRARY); break;
    case SETTING_OPEN_PLAYBACK: openPage(app, SETTINGS_PAGE_PLAYBACK); break;
    case SETTING_OPEN_JELLYFIN: openServer(app, SERVER_JELLYFIN, now); break;
    case SETTING_OPEN_EMBY: openServer(app, SERVER_EMBY, now); break;
    case SETTING_OPEN_PLEX: openServer(app, SERVER_PLEX, now); break;
    case SETTING_JF_SERVER: typeServer(app, now); break;
    case SETTING_JF_CODE: codeSignIn(app, now); break;
    case SETTING_PLEX_CODE:
        app->jf_signing = true;
        jfSyncSignIn();
        break;
    case SETTING_PLEX_SERVER: appShowMessage(app, T("The server comes from your Plex account."), now); break;
    case SETTING_JF_PASSWORD: passwordSignIn(app, now); break;
    case SETTING_JF_SIGN_OUT: signOut(app); break;
    case SETTING_JF_FIND:
    case SETTING_JF_TEST:
        if (row == SETTING_JF_FIND && signedIn(app))
            appShowMessage(app, T("Sign out first to change the server."), now);
        else
            askNetwork(app, row, now);
        break;
    case SETTING_ABOUT: openPage(app, SETTINGS_PAGE_ABOUT); break;
    case SETTING_CREDITS: app->about_open = true; break;
    case SETTING_UPDATE_NOW: appUpdatesActivate(app, now); break;
    case SETTING_OPEN_NETWORK: openPage(app, SETTINGS_PAGE_NETWORK); break;
    case SETTING_OPEN_IPTV: openPage(app, SETTINGS_PAGE_IPTV); break;
    case SETTING_OPEN_ADDONS: openPage(app, SETTINGS_PAGE_ADDONS); break;
    default:
        if (!appIptvActivate(app, row, now) && !appNetworkActivate(app, row, now) &&
            !appStremioActivate(app, row, now))
            change(app, row, 1, now);
        break;
    }
}

// Up or down a row on the current page, wrapping.
static void moveCursor(App *app, int delta)
{
    SettingRow rows[SETTINGS_PAGE_MAX_ROWS];
    int count = settingsPageRows((SettingsPage)app->settings_page, signedIn(app), (ServerKind)app->config.server_kind, rows), at = 0;
    for (int i = 0; i < count; i++)
        if ((int)rows[i] == app->settings_cursor)
            at = i;
    app->settings_cursor = rows[((at + delta) % count + count) % count];
}

static void aboutInput(App *app, uint64_t pressed, TouchEvent touch)
{
    LibHit hit = touch.type == TOUCH_TAP ? uiLibraryTap(touch.x, touch.y) : (LibHit){LIBHIT_NONE, 0, 0};
    if ((pressed & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_Minus)) || hit.kind == LIBHIT_CLOSE)
        app->about_open = false;
}

static void tapInput(App *app, LibHit hit, uint64_t now)
{
    switch (hit.kind) {
    case LIBHIT_SETTING:
        app->settings_cursor = hit.a;
        activate(app, (SettingRow)hit.a, now);
        break;
    case LIBHIT_SETTING_PREV:
    case LIBHIT_SETTING_NEXT:
        app->settings_cursor = hit.a;
        change(app, (SettingRow)hit.a, hit.kind == LIBHIT_SETTING_PREV ? -1 : 1, now);
        break;
    case LIBHIT_PAGE_BACK: back(app); break;
    case LIBHIT_CLOSE: app->settings_open = false; break;
    default: break;
    }
}

// Signing in or out changes the page's rows: the cursor stays on one of them.
static void keepCursorOnPage(App *app)
{
    SettingRow rows[SETTINGS_PAGE_MAX_ROWS];
    int count = settingsPageRows((SettingsPage)app->settings_page, signedIn(app), (ServerKind)app->config.server_kind, rows);
    for (int i = 0; i < count; i++)
        if ((int)rows[i] == app->settings_cursor)
            return;
    app->settings_cursor = rows[0];
}

void appSettingsInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now)
{
    pressed = buttonsRightStickAsDpad(pressed);
    keepCursorOnPage(app);
    if (pressed & HidNpadButton_Plus) {
        app->quit = true;
        return;
    }
    if (app->about_open) {
        aboutInput(app, pressed, touch);
        return;
    }
    if (app->jf_signing) {  // Only cancelling, while the code shows.
        LibHit hit = touch.type == TOUCH_TAP ? uiLibraryTap(touch.x, touch.y) : (LibHit){LIBHIT_NONE, 0, 0};
        if ((pressed & HidNpadButton_B) || hit.kind == LIBHIT_CLOSE) {
            app->jf_signing = false;
            jfSyncCancelSignIn();
        }
        return;
    }
    SettingRow row = (SettingRow)app->settings_cursor;
    if (pressed & BUTTONS_UP)
        moveCursor(app, -1);
    if (pressed & BUTTONS_DOWN)
        moveCursor(app, 1);
    if ((pressed & BUTTONS_LEFT) && settingIsChoice(row))
        change(app, row, -1, now);
    if ((pressed & BUTTONS_RIGHT) && settingIsChoice(row))
        change(app, row, 1, now);
    SettingsPage opens;
    ServerKind opens_kind;
    if ((pressed & BUTTONS_RIGHT) && settingOpensPage(row, &opens, &opens_kind))
        activate(app, row, now);
    if (pressed & HidNpadButton_A)
        activate(app, row, now);
    if (pressed & HidNpadButton_B)
        back(app);
    if (pressed & HidNpadButton_Minus)
        app->settings_open = false;
    if (touch.type == TOUCH_TAP)
        tapInput(app, uiLibraryTap(touch.x, touch.y), now);
}

void appSettingsDraw(const App *app)
{
    if (!app->settings_open)
        return;
    const char *server = app->jf.server;
    if (!strncmp(server, HTTP_PREFIX, sizeof(HTTP_PREFIX) - 1))
        server += sizeof(HTTP_PREFIX) - 1;
    uiSettingsSetJellyfin(server, signedIn(app) ? app->jf.user : "");
    static char users[SERVER_KIND_COUNT][JF_NAME_MAX];  // Who's signed in where, for the main page.
    const char *names[SERVER_KIND_COUNT];
    for (int k = 0; k < SERVER_KIND_COUNT; k++) {
        jfSyncUserName((ServerKind)k, users[k], sizeof(users[k]));
        names[k] = users[k];
    }
    uiSettingsSetServers(names);
    appNetworkFeedSettings();
    appIptvFeedSettings(app);
    appStremioFeedSettings(app);
    static char update_now[96];
    appUpdatesValue(app, SETTING_UPDATE_NOW, update_now, sizeof(update_now));
    uiSettingsSetUpdate(update_now);
    if (app->jf_signing)
        uiDrawSignInCode(app->config.server_kind == SERVER_PLEX, app->jf.code, server, app->jf.linked);
    else if (app->about_open)
        uiDrawAbout(KAIVIDEO_VERSION);
    else
        uiDrawSettings(&app->config, (SettingsPage)app->settings_page, app->settings_cursor, app->library_root,
                       KAIVIDEO_VERSION);
}
