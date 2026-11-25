// Plex Home without plex.tv: the Home's profiles, the token a switch gives,
// what a refused switch means, the PIN, the switch's path, which server the
// profile keeps, and the profile kept with the session. Made-up answers only.
#include <stdio.h>
#include <string.h>

#include "check.h"
#include "plexhome.h"

// As plex.tv answers GET /api/v2/home/users (fields trimmed; names and ids made up).
static const char HOME[] =
    "{\"id\":101,\"name\":\"Lantern House\",\"guestUserID\":9,\"guestUserUUID\":\"guest0\",\"guestEnabled\":false,"
    "\"subscription\":false,\"users\":["
    "{\"id\":202,\"uuid\":\"kid0aa11\",\"title\":\"Little Ones\",\"username\":\"\",\"thumb\":"
    "\"https://plex.tv/users/kid0aa11/avatar?c=1\",\"hasPassword\":false,\"restricted\":true,\"admin\":false,"
    "\"guest\":false,\"protected\":true},"
    "{\"id\":201,\"uuid\":\"own0bb22\",\"title\":\"Mizu\",\"username\":\"mizu-made-up\",\"thumb\":"
    "\"https://plex.tv/users/own0bb22/avatar?c=2\",\"hasPassword\":true,\"restricted\":false,\"admin\":true,"
    "\"guest\":false,\"protected\":false},"
    "{\"id\":9,\"uuid\":\"guest0\",\"title\":\"Guest\",\"admin\":false,\"guest\":true,\"protected\":false},"
    "{\"id\":203,\"uuid\":\"../../x\",\"title\":\"Bad id\",\"admin\":false},"
    "{\"id\":204,\"title\":\"No id\",\"admin\":false},"
    "{\"id\":205,\"uuid\":\"rel0cc33\",\"title\":\"Kai\",\"thumb\":\"http://elsewhere.example/a.png\","
    "\"admin\":false,\"guest\":false,\"protected\":false}]}";

static void testUsers(void)
{
    PlexHomeUser u[PLEX_HOME_USERS_MAX];
    int n = plexHomeUsersParse(HOME, u, PLEX_HOME_USERS_MAX);
    // The owner first, then the others in Plex's order; the guest and bad ids left out.
    CHECK(n == 3);
    CHECK(!strcmp(u[0].uuid, "own0bb22") && u[0].admin && !u[0].protected_pin && u[0].id == 201 &&
          !strcmp(u[0].title, "Mizu") && !strncmp(u[0].thumb, "https://plex.tv/users/own0bb22", 30));
    CHECK(!strcmp(u[1].uuid, "kid0aa11") && u[1].protected_pin && u[1].restricted && !u[1].admin &&
          !strcmp(u[1].title, "Little Ones"));
    CHECK(!strcmp(u[2].uuid, "rel0cc33") && !u[2].thumb[0]);  // Only https pictures are fetched.
    CHECK(plexHomeFind(u, n, "kid0aa11") == 1 && plexHomeFind(u, n, "nobody") == -1 && plexHomeFind(u, n, "") == -1);
    // At most max.
    CHECK(plexHomeUsersParse(HOME, u, 1) == 1 && u[0].admin);
    // An array alone is a list of profiles too; an account without a Home has one.
    CHECK(plexHomeUsersParse("[{\"uuid\":\"a1\",\"title\":\"Solo\",\"admin\":true}]", u, 4) == 1 &&
          !strcmp(u[0].title, "Solo"));
    // A title missing: the user name.
    CHECK(plexHomeUsersParse("{\"users\":[{\"uuid\":\"a1\",\"username\":\"solo\"}]}", u, 4) == 1 &&
          !strcmp(u[0].title, "solo"));
    CHECK(plexHomeUsersParse("{\"users\":[]}", u, 4) == 0);
    CHECK(plexHomeUsersParse("{\"id\":1}", u, 4) == -1);
    CHECK(plexHomeUsersParse("<html>", u, 4) == -1);
    CHECK(plexHomeUsersParse("", u, 4) == -1);
}

static void testSwitchAnswer(void)
{
    char token[PLEX_TOKEN_MAX];
    CHECK(plexHomeSwitchToken("{\"id\":202,\"uuid\":\"kid0aa11\",\"authToken\":\"profile-token-1\"}", token,
                              sizeof(token)) &&
          !strcmp(token, "profile-token-1"));
    CHECK(plexHomeSwitchToken("{\"authenticationToken\":\"older-token\"}", token, sizeof(token)) &&
          !strcmp(token, "older-token"));
    CHECK(!plexHomeSwitchToken("{\"authToken\":\"\"}", token, sizeof(token)));
    CHECK(!plexHomeSwitchToken("{\"errors\":[{\"code\":1041,\"message\":\"Invalid PIN\"}]}", token, sizeof(token)));
    CHECK(!plexHomeSwitchToken("not json", token, sizeof(token)));
    char small[4];
    CHECK(!plexHomeSwitchToken("{\"authToken\":\"too-long\"}", small, sizeof(small)));
}

static void testErrors(void)
{
    CHECK(plexHomeSwitchError(0) == PLEX_SWITCH_UNREACHABLE);
    CHECK(plexHomeSwitchError(401) == PLEX_SWITCH_WRONG_PIN && plexHomeSwitchError(403) == PLEX_SWITCH_WRONG_PIN &&
          plexHomeSwitchError(422) == PLEX_SWITCH_WRONG_PIN);
    CHECK(plexHomeSwitchError(429) == PLEX_SWITCH_TOO_MANY);
    CHECK(plexHomeSwitchError(404) == PLEX_SWITCH_GONE);
    CHECK(plexHomeSwitchError(500) == PLEX_SWITCH_REFUSED && plexHomeSwitchError(400) == PLEX_SWITCH_REFUSED);
    CHECK(!plexHomeSwitchMessage(PLEX_SWITCH_OK)[0]);
    for (int r = PLEX_SWITCH_WRONG_PIN; r <= PLEX_SWITCH_NO_SERVER; r++)
        CHECK(plexHomeSwitchMessage((PlexSwitchResult)r)[0] != '\0');
    CHECK(strcmp(plexHomeSwitchMessage(PLEX_SWITCH_WRONG_PIN), plexHomeSwitchMessage(PLEX_SWITCH_TOO_MANY)));
}

static void testPin(void)
{
    CHECK(plexHomePinValid("1234") && plexHomePinValid("0000"));
    CHECK(!plexHomePinValid("") && !plexHomePinValid("123") && !plexHomePinValid("12345") &&
          !plexHomePinValid("12a4") && !plexHomePinValid("12 4"));
    char path[PLEX_HOME_PATH_MAX];
    CHECK(plexHomeSwitchPath("kid0aa11", "4321", path, sizeof(path)) &&
          !strcmp(path, "/api/v2/home/users/kid0aa11/switch?pin=4321"));
    CHECK(plexHomeSwitchPath("own0bb22", "", path, sizeof(path)) && !strcmp(path, "/api/v2/home/users/own0bb22/switch"));
    // Nothing that could leave the path or add to the query.
    CHECK(!plexHomeSwitchPath("../admin", "", path, sizeof(path)));
    CHECK(!plexHomeSwitchPath("a?b=1", "", path, sizeof(path)));
    CHECK(!plexHomeSwitchPath("", "", path, sizeof(path)));
    CHECK(!plexHomeSwitchPath("kid0aa11", "12&x=1", path, sizeof(path)));
    char tiny[16];
    CHECK(!plexHomeSwitchPath("kid0aa11", "4321", tiny, sizeof(tiny)));
}

static void testServerChoice(void)
{
    PlexServer servers[3];
    memset(servers, 0, sizeof(servers));
    snprintf(servers[0].id, sizeof(servers[0].id), "srv-owned");
    snprintf(servers[1].id, sizeof(servers[1].id), "srv-shared");
    // The server in use is kept when the profile has it.
    CHECK(plexHomePickServer(servers, 2, "srv-shared") == 1);
    // Not shared with this profile: none (its library would be another).
    CHECK(plexHomePickServer(servers, 2, "srv-other") == -1);
    CHECK(plexHomePickServer(servers, 0, "srv-owned") == -1);
    // No server before: the first.
    CHECK(plexHomePickServer(servers, 2, "") == 0 && plexHomePickServer(servers, 0, "") == -1);
}

static void testNames(void)
{
    char name[PLEX_NAME_MAX];
    plexHomeName("mizu-made-up", "Mizu", name, sizeof(name));
    CHECK(!strcmp(name, "mizu-made-up"));  // A full account keeps the name its downloads are kept under.
    plexHomeName("", "Little Ones", name, sizeof(name));
    CHECK(!strcmp(name, "Little Ones"));
}

static void testSessionProfile(void)
{
    PlexSession s = {0}, back;
    snprintf(s.token, sizeof(s.token), "tok");
    snprintf(s.account, sizeof(s.account), "own0bb22");
    snprintf(s.profile, sizeof(s.profile), "kid0aa11");
    CHECK(!plexSessionIsAccount(&s));
    CHECK(plexSessionSave(&s, TMP_ROOT "_plexhome_session.txt"));
    CHECK(plexSessionLoad(&back, TMP_ROOT "_plexhome_session.txt"));
    CHECK(!strcmp(back.account, "own0bb22") && !strcmp(back.profile, "kid0aa11") && !plexSessionIsAccount(&back));
    remove(TMP_ROOT "_plexhome_session.txt");
    snprintf(s.profile, sizeof(s.profile), "own0bb22");
    CHECK(plexSessionIsAccount(&s));
    // A session kept before 1.18 names no profile: it's the account's.
    FILE *f = fopen(TMP_ROOT "_plexhome_old.txt", "w");
    CHECK(f != NULL);
    if (f) {
        fputs("client=console1\ntoken=tok\nuser=someone\n", f);
        fclose(f);
    }
    CHECK(plexSessionLoad(&back, TMP_ROOT "_plexhome_old.txt") && !back.profile[0] && plexSessionIsAccount(&back));
    remove(TMP_ROOT "_plexhome_old.txt");
}

void testPlexHome(void)
{
    testUsers();
    testSwitchAnswer();
    testErrors();
    testPin();
    testServerChoice();
    testNames();
    testSessionProfile();
}
