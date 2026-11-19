// Settings and About, over the library: up and down choose a row, left and
// right change it, A opens what a row opens (or changes it), B goes back a
// page or closes. Changes apply at once and are kept in settings.txt. The
// subtitles' size, color and style are changed in the player's Subtitles list.
#include <stdio.h>
#include <string.h>

#include <switch.h>

#include "app.h"
#include "background.h"
#include "buttons.h"
#include "keyboard_system.h"
#include "theme.h"
#include "ui.h"
#include "ui_library.h"
#include "ui_settings.h"

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

static void applyLooks(const App *app)
{
    themeApply(app->config.theme, app->config.accent);
    uiLibrarySetThumbnails(app->config.thumbnails);
    appApplySubtitleStyle(app);
}

void appSettingsStart(App *app)
{
    applyLooks(app);
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
    settingsPageRows(page, signedIn(app), rows);
    app->settings_page = page;
    app->settings_cursor = rows[0];
}

void appOpenSettings(App *app)
{
    app->settings_open = true;
    app->about_open = false;
    openPage(app, SETTINGS_PAGE_MAIN);
}

// Back from a page to the main one (on the row that opened it), or closed.
static void back(App *app)
{
    if (app->settings_page == SETTINGS_PAGE_MAIN) {
        app->settings_open = false;
    } else {
        app->settings_cursor = app->settings_page == SETTINGS_PAGE_JELLYFIN ? SETTING_OPEN_JELLYFIN : SETTING_OPEN_DISPLAY;
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
        appShowMessage(app, app->library_root[0] ? "Put JPEG or PNG images in a Backgrounds folder in your library folder."
                                                 : "Choose the library folder first.",
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
        snprintf(message, sizeof(message), "%s can't be read (JPEG or PNG, up to 50 megapixels).",
                 app->config.background);
        appShowMessage(app, message, now);
        app->config.background[0] = '\0';
    }
}

static void openJellyfin(App *app, uint64_t now)
{
    if (!appJellyfinEnsure(app)) {
        appShowMessage(app, "The network can't be used.", now);
        return;
    }
    jfSyncGetStatus(&app->jf);
    openPage(app, SETTINGS_PAGE_JELLYFIN);
}

void appOpenJellyfinSettings(App *app, uint64_t now)
{
    app->settings_open = true;
    app->about_open = false;
    openPage(app, SETTINGS_PAGE_MAIN);
    app->settings_cursor = SETTING_OPEN_JELLYFIN;
    openJellyfin(app, now);
}

static void typeServer(App *app, uint64_t now)
{
    if (signedIn(app)) {
        appShowMessage(app, "Sign out first to change the server.", now);
        return;
    }
    char text[JF_SERVER_MAX];
    const char *initial = app->jf.server;
    if (!strncmp(initial, HTTP_PREFIX, sizeof(HTTP_PREFIX) - 1))
        initial += sizeof(HTTP_PREFIX) - 1;
    SystemKeyboardResult result = systemKeyboardInput("Jellyfin server, like 192.168.0.10:8096", initial, text,
                                                      sizeof(text), JF_SERVER_MAX - 1);
    if (result == SYSTEM_KEYBOARD_UNAVAILABLE)
        appShowMessage(app, "The keyboard couldn't open.", now);
    else if (result == SYSTEM_KEYBOARD_OK && !jfSyncSetServer(text))
        appShowMessage(app, "That isn't a server's address.", now);
}

static void signOut(App *app)
{
    if (app->config.use_jellyfin)
        appLibrarySetSource(app, false);
    jfSyncSignOut();
}

static void codeSignIn(App *app, uint64_t now)
{
    if (!app->jf.server[0]) {
        appShowMessage(app, "Enter the server's address first.", now);
    } else {
        app->jf_signing = true;
        jfSyncSignIn();
    }
}

static void passwordSignIn(App *app, uint64_t now)
{
    if (!app->jf.server[0]) {
        appShowMessage(app, "Enter the server's address first.", now);
        return;
    }
    char user[CREDENTIAL_TEXT_MAX], password[CREDENTIAL_TEXT_MAX];
    SystemKeyboardResult result = systemKeyboardInput("Jellyfin user name", "", user, sizeof(user), sizeof(user) - 1);
    if (result == SYSTEM_KEYBOARD_OK && user[0])
        result = systemKeyboardPassword("Password", password, sizeof(password), sizeof(password) - 1);
    else if (result == SYSTEM_KEYBOARD_OK)
        return;
    if (result == SYSTEM_KEYBOARD_UNAVAILABLE) {
        appShowMessage(app, "The keyboard couldn't open.", now);
    } else if (result == SYSTEM_KEYBOARD_OK) {
        app->jf_signing = true;
        jfSyncSignInWithPassword(user, password);
    }
    memset(password, 0, sizeof(password));
}

static void change(App *app, SettingRow row, int delta, uint64_t now)
{
    if (row == SETTING_BACKGROUND) {
        stepBackground(app, delta, now);
    } else {
        settingStep(&app->config, row, delta);
        applyLooks(app);
        if (row == SETTING_THEME || row == SETTING_BLUR)
            backgroundSetBlur(app->config.blur);  // Made again over the theme's color, or with the new blur.
    }
    appSaveSettings(app);
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
    case SETTING_OPEN_JELLYFIN: openJellyfin(app, now); break;
    case SETTING_JF_SERVER: typeServer(app, now); break;
    case SETTING_JF_CODE: codeSignIn(app, now); break;
    case SETTING_JF_PASSWORD: passwordSignIn(app, now); break;
    case SETTING_JF_SIGN_OUT: signOut(app); break;
    case SETTING_ABOUT: app->about_open = true; break;
    default: change(app, row, 1, now); break;
    }
}

// Up or down a row on the current page, wrapping.
static void moveCursor(App *app, int delta)
{
    SettingRow rows[SETTINGS_PAGE_MAX_ROWS];
    int count = settingsPageRows((SettingsPage)app->settings_page, signedIn(app), rows), at = 0;
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
    int count = settingsPageRows((SettingsPage)app->settings_page, signedIn(app), rows);
    for (int i = 0; i < count; i++)
        if ((int)rows[i] == app->settings_cursor)
            return;
    app->settings_cursor = rows[0];
}

void appSettingsInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now)
{
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
    if ((pressed & BUTTONS_RIGHT) && (row == SETTING_OPEN_DISPLAY || row == SETTING_OPEN_JELLYFIN))
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
    if (app->jf_signing)
        uiDrawJellyfinCode(app->jf.code, server);
    else if (app->about_open)
        uiDrawAbout(KAIVIDEO_VERSION);
    else
        uiDrawSettings(&app->config, (SettingsPage)app->settings_page, app->settings_cursor, app->library_root,
                       KAIVIDEO_VERSION);
}
