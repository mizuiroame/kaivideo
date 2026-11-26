// Plex Home's profiles over the library: after signing in to an account
// whose Home has several profiles, and later from Settings or the + menu.
// The arrows (or a finger) choose a profile and A takes it; one with a PIN
// opens the PIN pad, where the arrows and A (or fingers) type it and B
// deletes. The switch itself is plexhomesync.c's.
#include <stdio.h>
#include <string.h>

#include <switch.h>

#include "app.h"
#include "buttons.h"
#include "lang.h"
#include "ui_library.h"
#include "ui_plexhome.h"

static void closeProfiles(App *app)
{
    app->profiles = PROFILES_NONE;
    memset(app->profiles_pin, 0, sizeof(app->profiles_pin));
    app->profiles_message[0] = '\0';
}

void appPlexHomeOpen(App *app, bool automatic, uint64_t now)
{
    if (!jfSyncSignedIn(SERVER_PLEX)) {
        if (!automatic)
            appShowMessage(app, T("Sign in to Plex first."), now);
        return;
    }
    if (app->profiles != PROFILES_NONE || !plexHomeStartLoad()) {
        if (!automatic)
            appShowMessage(app, T("Still asking plex.tv..."), now);
        return;
    }
    app->profiles = PROFILES_LOADING;
    app->profiles_auto = automatic;
}

static const PlexHomeUser *target(const App *app)
{
    int i = app->profiles_target;
    return i >= 0 && i < app->profiles_home.count ? &app->profiles_home.users[i] : NULL;
}

// The profiles came: chosen from, unless there's nothing to choose.
static void loaded(App *app, uint64_t now)
{
    if (app->profiles_home.count <= 1) {
        closeProfiles(app);
        if (!app->profiles_auto)
            appShowMessage(app, T("This Plex account has no other profiles."), now);
        return;
    }
    char current[PLEX_ID_MAX];
    jfSyncPlexProfile(current, sizeof(current));
    int at = plexHomeFind(app->profiles_home.users, app->profiles_home.count, current);
    app->profiles_cursor = at >= 0 ? at : 0;
    app->profiles = PROFILES_PICKER;
}

// A switch that didn't work: a wrong PIN is typed again; anything else goes back to the profiles.
static void switchFailed(App *app, const PlexHomeStatus *s, uint64_t now)
{
    const PlexHomeUser *u = target(app);
    bool pin = s->result == PLEX_SWITCH_WRONG_PIN || s->result == PLEX_SWITCH_TOO_MANY;
    memset(app->profiles_pin, 0, sizeof(app->profiles_pin));
    if (pin && u) {  // Plex asked for a PIN: the pad, with why.
        app->profiles = PROFILES_PIN;
        bool asked = u->protected_pin;
        snprintf(app->profiles_message, sizeof(app->profiles_message), "%s",
                 asked || s->result == PLEX_SWITCH_TOO_MANY ? s->message : T("This profile needs its PIN."));
        return;
    }
    app->profiles = PROFILES_PICKER;
    appNotify(app, NOTICE_ERROR, s->message[0] ? s->message : T("plex.tv refused that."), now);
}

void appPlexHomeUpdate(App *app, uint64_t now)
{
    if (app->profiles != PROFILES_LOADING && app->profiles != PROFILES_SWITCHING)
        return;
    PlexHomeStatus s;
    plexHomeGetStatus(&s);
    if (app->profiles == PROFILES_LOADING && s.state == PLEX_HOME_LOADED) {
        app->profiles_home = s;
        loaded(app, now);
    } else if (app->profiles == PROFILES_LOADING && s.state == PLEX_HOME_LOAD_FAILED) {
        closeProfiles(app);
        if (!app->profiles_auto)
            appNotify(app, NOTICE_ERROR, s.message, now);
    } else if (app->profiles == PROFILES_SWITCHING && s.state == PLEX_HOME_SWITCHED) {
        closeProfiles(app);
        app->settings_open = false;
        char message[PLEX_NAME_MAX + 64];
        snprintf(message, sizeof(message), T("Watching as %s. Loading their library..."), s.switched_to);
        appNotify(app, NOTICE_DONE, message, now);
        if (app->config.use_jellyfin)
            appLibraryShowServer(app, SERVER_PLEX);  // Plex's library, as this profile sees it.
    } else if (app->profiles == PROFILES_SWITCHING && s.state == PLEX_HOME_SWITCH_FAILED) {
        switchFailed(app, &s, now);
    }
}

static void startSwitch(App *app, uint64_t now)
{
    const PlexHomeUser *u = target(app);
    if (!u)
        return;
    if (plexHomeStartSwitch(u, app->profiles_pin))
        app->profiles = PROFILES_SWITCHING;
    else
        appShowMessage(app, T("Still asking plex.tv..."), now);
    memset(app->profiles_pin, 0, sizeof(app->profiles_pin));
}

static void choose(App *app, int index, uint64_t now)
{
    if (index < 0 || index >= app->profiles_home.count)
        return;
    const PlexHomeUser *u = &app->profiles_home.users[index];
    char current[PLEX_ID_MAX];
    jfSyncPlexProfile(current, sizeof(current));
    app->profiles_cursor = app->profiles_target = index;
    if (!strcmp(u->uuid, current)) {  // Already this one: nothing to switch.
        closeProfiles(app);
        return;
    }
    memset(app->profiles_pin, 0, sizeof(app->profiles_pin));
    app->profiles_message[0] = '\0';
    if (u->protected_pin) {
        app->profiles = PROFILES_PIN;
        app->profiles_key = 0;
    } else {
        startSwitch(app, now);
    }
}

// Moves a cursor on a grid of count cells, columns across, wrapping each way.
static int moveOnGrid(int cursor, int count, int columns, uint64_t pressed)
{
    if (count <= 0)
        return 0;
    if (pressed & BUTTONS_LEFT)
        cursor = (cursor + count - 1) % count;
    if (pressed & BUTTONS_RIGHT)
        cursor = (cursor + 1) % count;
    if ((pressed & BUTTONS_UP) && cursor - columns >= 0)
        cursor -= columns;
    if ((pressed & BUTTONS_DOWN) && cursor + columns < count)
        cursor += columns;
    return cursor;
}

static void pickerInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now)
{
    int count = app->profiles_home.count;
    app->profiles_cursor = moveOnGrid(app->profiles_cursor, count, UI_PROFILE_COLUMNS, pressed);
    if (pressed & HidNpadButton_A) {
        choose(app, app->profiles_cursor, now);
        return;
    }
    if (pressed & HidNpadButton_B) {
        closeProfiles(app);
        return;
    }
    LibHit hit = touch.type == TOUCH_TAP ? uiLibraryTap(touch.x, touch.y) : (LibHit){LIBHIT_NONE, 0, 0};
    if (hit.kind == LIBHIT_PROFILE)
        choose(app, hit.a, now);
    else if (hit.kind == LIBHIT_CLOSE)
        closeProfiles(app);
}

static void backToPicker(App *app)
{
    memset(app->profiles_pin, 0, sizeof(app->profiles_pin));
    app->profiles_message[0] = '\0';
    app->profiles = PROFILES_PICKER;
}

static void pressKey(App *app, int key, uint64_t now)
{
    size_t typed = strlen(app->profiles_pin);
    char digit = uiPinKeyDigit(key);
    app->profiles_key = key;
    if (key == UI_PIN_KEY_CANCEL) {
        backToPicker(app);
    } else if (key == UI_PIN_KEY_DELETE) {
        if (typed > 0)
            app->profiles_pin[typed - 1] = '\0';
    } else if (digit && typed < PLEX_HOME_PIN_DIGITS) {
        app->profiles_pin[typed] = digit;
        app->profiles_message[0] = '\0';
        if (typed + 1 == PLEX_HOME_PIN_DIGITS)  // The last digit: checked at once, as Plex's apps do.
            startSwitch(app, now);
    }
}

static void pinInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now)
{
    app->profiles_key = moveOnGrid(app->profiles_key, UI_PIN_KEYS, UI_PIN_KEY_COLUMNS, pressed);
    if (pressed & HidNpadButton_A) {
        pressKey(app, app->profiles_key, now);
        return;
    }
    if (pressed & HidNpadButton_B) {  // Deletes a digit; with none, back to the profiles.
        size_t typed = strlen(app->profiles_pin);
        if (typed > 0)
            app->profiles_pin[typed - 1] = '\0';
        else
            backToPicker(app);
        return;
    }
    LibHit hit = touch.type == TOUCH_TAP ? uiLibraryTap(touch.x, touch.y) : (LibHit){LIBHIT_NONE, 0, 0};
    if (hit.kind == LIBHIT_PIN_KEY)
        pressKey(app, hit.a, now);
    else if (hit.kind == LIBHIT_CLOSE)
        backToPicker(app);
}

void appPlexHomeInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now)
{
    pressed = buttonsRightStickAsDpad(pressed);
    if (pressed & HidNpadButton_Plus) {  // As in Settings: + exits.
        app->quit = true;
        return;
    }
    switch (app->profiles) {
    case PROFILES_PICKER: pickerInput(app, pressed, touch, now); break;
    case PROFILES_PIN: pinInput(app, pressed, touch, now); break;
    case PROFILES_LOADING: {  // B, or a tap beside it, leaves it: what comes is let go.
        LibHit hit = touch.type == TOUCH_TAP ? uiLibraryTap(touch.x, touch.y) : (LibHit){LIBHIT_NONE, 0, 0};
        if ((pressed & HidNpadButton_B) || hit.kind == LIBHIT_CLOSE)
            closeProfiles(app);
        break;
    }
    default: break;  // Switching: it ends by itself.
    }
}

void appPlexHomeDraw(const App *app)
{
    const PlexHomeStatus *h = &app->profiles_home;
    const PlexHomeUser *u = target(app);
    char current[PLEX_ID_MAX], busy[PLEX_NAME_MAX + 64];
    switch (app->profiles) {
    case PROFILES_LOADING: uiDrawProfiles(NULL, 0, 0, "", T("Reading your Plex Home...")); break;
    case PROFILES_PICKER:
        jfSyncPlexProfile(current, sizeof(current));
        uiDrawProfiles(h->users, h->count, app->profiles_cursor, current, NULL);
        break;
    case PROFILES_PIN:
        if (u)
            uiDrawPinPad(u, (int)strlen(app->profiles_pin), app->profiles_key, app->profiles_message, false);
        break;
    case PROFILES_SWITCHING:
        if (u && u->protected_pin) {
            uiDrawPinPad(u, PLEX_HOME_PIN_DIGITS, app->profiles_key, "", true);
        } else {
            snprintf(busy, sizeof(busy), T("Switching to %s..."), u ? u->title : "");
            uiDrawProfiles(NULL, 0, 0, "", busy);
        }
        break;
    default: break;
    }
}

const char *appPlexHomeHelp(const App *app)
{
    switch (app->profiles) {
    case PROFILES_PICKER: return T("[A] choose  [B] close");
    case PROFILES_PIN: return T("[A] press  [B] delete");
    case PROFILES_LOADING: return T("[B] cancel");
    default: return "";
    }
}

void appPlexHomeExit(void)
{
    plexHomeStop();
}
