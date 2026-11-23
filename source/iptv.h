// IPTV lists: an M3U playlist's channels, or an Xtream server's (its API's
// answers), with their groups. Pure logic; the app asks the network.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

// An M3U playlist (#EXTM3U, #EXTINF with tvg-id, tvg-name, tvg-logo,
// tvg-chno and group-title, #EXTGRP). Entries that aren't http(s) are left
// out. False if it isn't an M3U playlist, or memory runs out.
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
    char status[32];      // "Active", "Expired", "Banned", "Disabled"...
    int64_t expires;      // Seconds since 1970; 0 if never or unknown.
    int max_connections;  // 0 if unknown.
} XtreamAuth;

// The account's answer. False if it isn't an Xtream server's.
bool xtreamParseAuth(const char *json, XtreamAuth *out);
// Categories (get_live_categories), as the list's groups (replacing them).
bool xtreamParseCategories(const char *json, IptvList *list);
// Live channels (get_live_streams), as HLS addresses, in the groups read before.
bool xtreamParseLive(const char *json, const XtreamAccount *a, IptvList *list);
