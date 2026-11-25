// IPTV lists: an M3U playlist's channels, or an Xtream server's (its API's
// answers), with their groups. Pure logic; the app asks the network.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "streamheaders.h"

#define IPTV_NAME_MAX 128
#define IPTV_URL_MAX 512
#define IPTV_ID_MAX 64         // A guide id (tvg-id, epg_channel_id).
#define IPTV_NUMBER_MAX 12
#define IPTV_GROUP_MAX 64
#define IPTV_CHANNELS_MAX 2000  // Kept from a list; the rest are left out.
#define IPTV_GROUPS_MAX 300
#define IPTV_TEXT_MAX (64u << 20)  // A list or an API answer bigger than this isn't read.

typedef struct {
    char name[IPTV_NAME_MAX];
    char url[IPTV_URL_MAX];      // The stream (http or https).
    char logo[IPTV_URL_MAX];     // May be empty.
    char epg_id[IPTV_ID_MAX];    // Its id in the guide; may be empty.
    char number[IPTV_NUMBER_MAX];
    int group;                   // An index in groups, -1 for none.
    StreamHeaders headers;       // What the list says to send along (#EXTVLCOPT, or "|" after the address).
} IptvChannel;

typedef struct {
    char name[IPTV_GROUP_MAX];
    char id[IPTV_ID_MAX];        // Xtream's category id; "" for an M3U's.
    int count;                   // Channels in it.
} IptvGroup;

typedef struct {
    IptvChannel *channels;
    int count;
    IptvGroup *groups;
    int group_count;
    bool truncated;              // There were more than IPTV_CHANNELS_MAX.
    char epg_url[IPTV_URL_MAX];  // The guide an M3U names (url-tvg), "" if none.
} IptvList;

void iptvListFree(IptvList *list);

// A channel's location for the player: "iptv:<index in the list>".
#define IPTV_PREFIX "iptv:"
void iptvLocation(char *out, size_t size, int index);
bool iptvIsLocation(const char *location);
// Its index, or -1 if it isn't one.
int iptvLocationIndex(const char *location);

// An M3U playlist (#EXTM3U, #EXTINF with tvg-id, tvg-name, tvg-logo,
// tvg-chno and group-title, #EXTGRP), with the headers a channel asks for
// (#EXTVLCOPT:http-referrer, http-user-agent, http-origin, or Kodi's
// "address|Referer=..."). Entries that aren't http(s) are left out. False if
// it isn't an M3U playlist, or memory runs out.
bool iptvParseM3u(const char *text, size_t length, IptvList *out);

// An Xtream account: its server ("http://host:8080"), user and password.
typedef struct {
    char server[IPTV_URL_MAX];
    char user[IPTV_NAME_MAX];
    char password[IPTV_NAME_MAX];
} XtreamAccount;

// The server's address as typed ("host:8080/", "http://host/player_api.php")
// made "http://host:8080". False if it can't be one.
bool xtreamNormalizeServer(const char *typed, char *out, size_t size);
// player_api.php for an action ("" for the account's own answer,
// "get_live_categories", "get_live_streams"...). extra: more of the query, or NULL.
bool xtreamApiUrl(const XtreamAccount *a, const char *action, const char *extra, char *out, size_t size);
// A stream's address: kind "live", "movie" or "series", and its extension ("m3u8", "mp4", "mkv").
bool xtreamStreamUrl(const XtreamAccount *a, const char *kind, long long id, const char *extension, char *out,
                     size_t size);
// The guide's address (xmltv.php).
bool xtreamGuideUrl(const XtreamAccount *a, char *out, size_t size);

typedef struct {
    bool active;          // Signed in, and the account is Active.
    bool refused;         // The user or password is wrong: auth isn't 1 and no status of the account came
                          // (real panels answer {"user_info":{"auth":0}}).
    char status[32];      // "Active", "Expired", "Banned", "Disabled"...
    int64_t expires;      // Seconds since 1970; 0 if never or unknown.
    int max_connections;  // 0 if unknown.
} XtreamAuth;

// --- The list set in Settings (iptv.txt in KaiVideo's folder) ---

#define IPTV_ACCOUNT_FILE "iptv.txt"

typedef enum { IPTV_NONE, IPTV_M3U, IPTV_XTREAM } IptvKind;

typedef struct {
    IptvKind kind;
    char address[IPTV_URL_MAX];  // The M3U's address, or the Xtream server.
    char user[IPTV_NAME_MAX];    // Xtream's.
    char password[IPTV_NAME_MAX];
    char guide[IPTV_URL_MAX];    // An XMLTV guide to use instead of the list's; "" for the list's own.
    bool any_certificate;        // Certificate check off: the list's, its guide's and its channels' https
                                 // certificates aren't checked (many IPTV servers' are broken).
} IptvAccount;

// Reads iptv.txt's text. False (and a cleared account) if it isn't one.
bool iptvAccountRead(IptvAccount *a, const char *text);
// Writes it: the password only encoded (base64), never as typed. False if it doesn't fit.
bool iptvAccountWrite(const IptvAccount *a, char *out, size_t size);
// The Xtream account in it.
void iptvXtreamOf(const IptvAccount *a, XtreamAccount *out);
// The guide to read: the one set, else an Xtream server's, else the M3U's own (list_epg). "" for none.
void iptvGuideUrl(const IptvAccount *a, const char *list_epg, char *out, size_t size);

// The account's answer. False if it isn't an Xtream server's.
bool xtreamParseAuth(const char *json, XtreamAuth *out);
// Categories (get_live_categories), as the list's groups (replacing them).
bool xtreamParseCategories(const char *json, IptvList *list);
// Live channels (get_live_streams), as HLS addresses, in the groups read before.
bool xtreamParseLive(const char *json, const XtreamAccount *a, IptvList *list);
