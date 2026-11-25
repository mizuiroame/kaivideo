#include "plexhome.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <jansson.h>

#include "lang.h"

#define HTTP_NOT_FOUND 404
#define HTTP_TOO_MANY 429
#define HTTP_UNAUTHORIZED 401
#define HTTP_FORBIDDEN 403
#define HTTP_UNPROCESSABLE 422

static const char *textOf(const json_t *object, const char *key)
{
    const char *value = json_string_value(json_object_get(object, key));
    return value ? value : "";
}

// Plex's uuids are letters and digits (a hex string); nothing that could leave the path.
static bool isUuid(const char *text)
{
    size_t n = strlen(text);
    if (n == 0 || n >= PLEX_ID_MAX)
        return false;
    for (size_t i = 0; i < n; i++)
        if (!isalnum((unsigned char)text[i]) && text[i] != '-')
            return false;
    return true;
}

static bool readUser(const json_t *u, PlexHomeUser *out)
{
    memset(out, 0, sizeof(*out));
    const char *uuid = textOf(u, "uuid"), *title = textOf(u, "title"), *thumb = textOf(u, "thumb");
    if (!isUuid(uuid))
        return false;
    snprintf(out->uuid, sizeof(out->uuid), "%s", uuid);
    out->id = json_integer_value(json_object_get(u, "id"));
    snprintf(out->title, sizeof(out->title), "%s", title[0] ? title : textOf(u, "username"));
    if (strlen(thumb) < sizeof(out->thumb) && !strncmp(thumb, "https://", 8))  // Only plex.tv's own pictures.
        snprintf(out->thumb, sizeof(out->thumb), "%s", thumb);
    out->admin = json_is_true(json_object_get(u, "admin"));
    out->protected_pin = json_is_true(json_object_get(u, "protected"));
    out->restricted = json_is_true(json_object_get(u, "restricted"));
    out->guest = json_is_true(json_object_get(u, "guest"));
    return true;
}

int plexHomeUsersParse(const char *json, PlexHomeUser *out, int max)
{
    json_t *root = json_loads(json, 0, NULL);
    // {"id":..., "users":[...]}; an array alone is taken too.
    const json_t *users = json_is_array(root) ? root : json_object_get(root, "users");
    if (!json_is_array(users)) {
        json_decref(root);
        return -1;
    }
    int count = 0;
    for (int admin = 1; admin >= 0; admin--) {  // The owner first, then the rest in Plex's order.
        size_t i;
        const json_t *u;
        json_array_foreach(users, i, u) {
            if (count >= max || json_is_true(json_object_get(u, "admin")) != (admin == 1))
                continue;
            if (readUser(u, &out[count]) && !out[count].guest)
                count++;
        }
    }
    json_decref(root);
    return count;
}

bool plexHomeSwitchToken(const char *json, char *token, size_t size)
{
    json_t *root = json_loads(json, 0, NULL);
    const char *text = textOf(root, "authToken");
    if (!text[0])
        text = textOf(root, "authenticationToken");
    bool ok = text[0] && strlen(text) < size;
    if (ok)
        snprintf(token, size, "%s", text);
    json_decref(root);
    return ok;
}

PlexSwitchResult plexHomeSwitchError(long status)
{
    if (status == 0)
        return PLEX_SWITCH_UNREACHABLE;
    if (status == HTTP_TOO_MANY)
        return PLEX_SWITCH_TOO_MANY;
    if (status == HTTP_NOT_FOUND)
        return PLEX_SWITCH_GONE;
    if (status == HTTP_UNAUTHORIZED || status == HTTP_FORBIDDEN || status == HTTP_UNPROCESSABLE)
        return PLEX_SWITCH_WRONG_PIN;
    return PLEX_SWITCH_REFUSED;
}

const char *plexHomeSwitchMessage(PlexSwitchResult result)
{
    switch (result) {
    case PLEX_SWITCH_OK: return "";
    case PLEX_SWITCH_WRONG_PIN: return T("Wrong PIN.");
    case PLEX_SWITCH_TOO_MANY: return T("Too many wrong PINs. Wait a few minutes and try again.");
    case PLEX_SWITCH_GONE: return T("That profile is no longer in this Plex Home.");
    case PLEX_SWITCH_UNREACHABLE: return T("plex.tv can't be reached.");
    case PLEX_SWITCH_NO_SERVER: return T("This profile has no access to this Plex server.");
    default: return T("plex.tv refused that.");
    }
}

bool plexHomePinValid(const char *pin)
{
    if (strlen(pin) != PLEX_HOME_PIN_DIGITS)
        return false;
    for (int i = 0; i < PLEX_HOME_PIN_DIGITS; i++)
        if (!isdigit((unsigned char)pin[i]))
            return false;
    return true;
}

bool plexHomeSwitchPath(const char *uuid, const char *pin, char *out, size_t size)
{
    if (!isUuid(uuid) || (pin[0] && !plexHomePinValid(pin)))
        return false;
    int n = snprintf(out, size, "/api/v2/home/users/%s/switch%s%s", uuid, pin[0] ? "?pin=" : "", pin);
    return n > 0 && (size_t)n < size;
}

int plexHomePickServer(const PlexServer *servers, int count, const char *server_id)
{
    if (!server_id[0])
        return count > 0 ? 0 : -1;
    for (int i = 0; i < count; i++)
        if (!strcmp(servers[i].id, server_id))
            return i;
    return -1;  // Not shared with this profile: the library would change under it.
}

int plexHomeFind(const PlexHomeUser *users, int count, const char *uuid)
{
    for (int i = 0; uuid[0] && i < count; i++)
        if (!strcmp(users[i].uuid, uuid))
            return i;
    return -1;
}

void plexHomeName(const char *username, const char *title, char *out, size_t size)
{
    snprintf(out, size, "%s", username[0] ? username : title);
}
