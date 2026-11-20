// What KaiVideo knows of Plex, without talking to it: signing in with a code
// approved at plex.tv/link, the account's servers and how to reach them, the
// server's libraries turned into a JfCatalog (so the screens, resume points
// and watched marks work as with Jellyfin), and the addresses that tell the
// server what's being watched. Pure logic over strings; the talking is elsewhere.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "jellyfin.h"

#define PLEX_TV "https://plex.tv"
#define PLEX_LINK_PAGE "plex.tv/link"
#define PLEX_PRODUCT "KaiVideo"
#define PLEX_ID_MAX 48
#define PLEX_TOKEN_MAX 64
#define PLEX_NAME_MAX 64
#define PLEX_URI_MAX 256
#define PLEX_URIS 6
#define PLEX_QUERY_MAX 1024
#define PLEX_PAGE_ITEMS 200

typedef struct {
    char client_id[PLEX_ID_MAX];  // This console's, made once and kept.
    char token[PLEX_TOKEN_MAX];   // The account's, "" when signed out.
    char user_name[PLEX_NAME_MAX];
    char server_name[PLEX_NAME_MAX];
    char server_id[PLEX_ID_MAX];
    char server_token[PLEX_TOKEN_MAX];  // For the server; the account's own on a server it owns.
    char server_uri[PLEX_URI_MAX];      // The address that answered last, no slash at the end.
} PlexSession;

bool plexIsLocation(const char *path);
// The item's ratingKey and its file's part id in a location. False if it isn't a good one.
bool plexLocationIds(const char *location, char *item, char *part);
void plexLocationMake(char *out, size_t size, const char *item, const char *part);

// The query that names KaiVideo to Plex ("X-Plex-Product=...&X-Plex-Client-Identifier=..."),
// with "&X-Plex-Token=..." when token isn't NULL or "".
void plexClientQuery(const char *client_id, const char *version, const char *token, char *out, size_t size);

// The answer to POST /api/v2/pins: the PIN's id and the code to type at plex.tv/link.
bool plexPinParse(const char *json, long long *id, char *code, size_t code_size);
// The answer to GET /api/v2/pins/<id>: true once approved, with the account's token.
bool plexPinToken(const char *json, char *token, size_t size);

typedef struct {
    char name[PLEX_NAME_MAX], id[PLEX_ID_MAX], token[PLEX_TOKEN_MAX];
    char uris[PLEX_URIS][PLEX_URI_MAX];  // Home network first, relays last.
    int uri_count;
    bool owned;
} PlexServer;

// The servers in a GET /api/v2/resources answer, the ones the account owns
// first. Returns how many (at most max), -1 if it can't be read.
int plexResourcesParse(const char *json, PlexServer *out, int max);

// The server's video libraries (/library/sections), as collections; their
// keys go in view_ids. Returns how many, -1 if it can't be read.
int plexCatalogAddSections(JfCatalog *c, const char *json);
// The films or episodes of one page of /library/sections/<key>/all. Returns
// how many it held, -1 if it can't be read. total, if not NULL, gets totalSize.
int plexCatalogAddPage(JfCatalog *c, const char *json, int *total);

// What tells the server how far a video went (a path with its query, after the server's address).
void plexTimelinePath(char *out, size_t size, const char *item, const char *state, int64_t position_ns,
                      int64_t duration_ns);
// Marks an item watched or not.
void plexPlayedPath(char *out, size_t size, const char *item, bool played);
// Where the user left an item, from its /library/metadata/<item> answer. False if it has none.
bool plexUserDataParse(const char *json, JfUserData *out);
// Sets where an item was left, without a player's session (what was watched offline).
void plexProgressPath(char *out, size_t size, const char *item, int64_t position_ns);

// How to play an item, from its /library/metadata/<item> answer: its sound
// tracks (the server's stream ids), its subtitles, and whether the console can
// read the file itself at max_bitrate (direct). Played directly, only external
// subtitles are fetched (the file holds the rest); converted, subtitles inside
// the file can only be drawn on by the server, so they join the picture ones.
// The chosen picture subtitle (burn_id, -1 for none) is marked burned and
// makes it converted. False if it has no file.
bool plexPlanParse(const char *json, long long max_bitrate, int burn_id, JfPlan *out);

// The conversion's address after the server's (no token: the caller adds it
// with plexClientQuery). burn: subtitles drawn onto the video. A small one
// (picture 320x180) is for a sound track alone.
void plexTranscodePath(char *out, size_t size, const char *item, const char *session, long long max_bitrate,
                       bool burn, bool small, const char *client_query);

// The parts Plex marked in an item (its /library/metadata/<item>?includeMarkers=1
// answer): intros and credits, by start. Returns how many (at most max).
int plexMarkersParse(const char *json, JfSegment *out, int max);

// Plex has no favorites of its own: this console keeps them in a file, one
// location a line (a film's, or "plex:<series>/series").
// Marks the catalog's videos the file names (before libraryBuild).
void plexFavoritesApply(JfCatalog *c, const char *file);
// Adds a location to the file, or takes it out. False if it can't be written.
bool plexFavoritesSet(const char *file, const char *location, bool favorite);

bool plexSessionLoad(PlexSession *s, const char *file);
bool plexSessionSave(const PlexSession *s, const char *file);
