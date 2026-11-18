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
#include "theme.h"
#include "ui.h"
#include "ui_library.h"
#include "ui_settings.h"

#ifndef KAIVIDEO_VERSION
#define KAIVIDEO_VERSION "dev"
#endif

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

static void openPage(App *app, SettingsPage page)
{
    SettingRow rows[SETTINGS_PAGE_MAX_ROWS];
    settingsPageRows(page, rows);
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
        app->settings_page = SETTINGS_PAGE_MAIN;
        app->settings_cursor = SETTING_OPEN_DISPLAY;
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
    case SETTING_ABOUT: app->about_open = true; break;
    default: change(app, row, 1, now); break;
    }
}

// Up or down a row on the current page, wrapping.
static void moveCursor(App *app, int delta)
{
    SettingRow rows[SETTINGS_PAGE_MAX_ROWS];
    int count = settingsPageRows((SettingsPage)app->settings_page, rows), at = 0;
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

void appSettingsInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now)
{
    if (pressed & HidNpadButton_Plus) {
        app->quit = true;
        return;
    }
    if (app->about_open) {
        aboutInput(app, pressed, touch);
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
    if ((pressed & BUTTONS_RIGHT) && row == SETTING_OPEN_DISPLAY)
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
    if (app->about_open)
        uiDrawAbout(KAIVIDEO_VERSION);
    else
        uiDrawSettings(&app->config, (SettingsPage)app->settings_page, app->settings_cursor, app->library_root,
                       KAIVIDEO_VERSION);
}
