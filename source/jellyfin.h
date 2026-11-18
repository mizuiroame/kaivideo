// What KaiVideo knows of a Jellyfin server, without talking to it: the session
// kept on the card, addresses and request headers, the server's answers turned
// into a Library, which videos the console can play, and merging where they
// were left with this console's lists. Pure logic over strings and files;
// jfsync.c does the talking.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "library.h"
#include "resume.h"
#include "watched.h"

#define JF_PREFIX "jellyfin:"
#define JF_ROOT "jellyfin:"     // The root of a library that comes from the server.
#define JF_SERVER_MAX 256
#define JF_ID_MAX 40
#define JF_NAME_MAX 64
#define JF_TOKEN_MAX 128
#define JF_URL_MAX 768
#define JF_AUTH_MAX 512
#define JF_PAGE_ITEMS 200
#define JF_SERIES_SOURCE "series"  // A series' picture: "jellyfin:<series id>/series".
#define JF_MAX_BITRATE 25000000ll  // Picture and sound each read the file: twice this goes over Wi-Fi.

typedef struct {
    char server[JF_SERVER_MAX];  // "http://host:8096", no slash at the end.
    char user_id[JF_ID_MAX], user_name[JF_NAME_MAX], token[JF_TOKEN_MAX];
    char device_id[JF_ID_MAX];
} JfSession;

// Where the signed-in user left a video, parallel to the library's items.
typedef struct {
    int64_t position_ns;
    int64_t last_played;  // Seconds since 1970, 0 if never.
    bool played;
} JfUserData;

typedef enum { JF_PLAYABLE, JF_TOO_LARGE, JF_HDR, JF_TOO_HEAVY, JF_FORMAT } JfPlayability;

typedef struct {
    const char *codec, *range, *container;  // As the server names them; may be "".
    int width, bits;
    int64_t bitrate;
} JfVideoFacts;

bool jfIsLocation(const char *path);
// The item and media source ids in a location. False if it isn't a good one.
bool jfLocationIds(const char *location, char *item, char *source);
void jfLocationMake(char *out, size_t size, const char *item, const char *source);

// "192.168.0.2" -> "http://192.168.0.2:8096". False if it can't be an address.
bool jfServerNormalize(const char *text, char *out, size_t size);
// The Authorization header; the token is left out when the session has none.
void jfAuthHeader(const JfSession *s, const char *version, char *out, size_t size);

bool jfSessionLoad(JfSession *s, const char *file);
bool jfSessionSave(const JfSession *s, const char *file);

JfPlayability jfPlayability(const JfVideoFacts *facts);
const char *jfPlayabilityMessage(JfPlayability p);
// Seconds since 1970 from "2024-03-09T21:15:00.123Z", 0 if it isn't a date.
int64_t jfParseDate(const char *text);

// Holds a library being read with its user data, which grows with it.
typedef struct {
    Library library;
    JfUserData *users;  // One per library item.
    size_t capacity;
} JfCatalog;

void jfCatalogInit(JfCatalog *c);
void jfCatalogFree(JfCatalog *c);
// Adds the films and episodes of one /Items answer. Returns how many it held
// (those that can't be used included), -1 if it can't be read. total, if not
// NULL, gets TotalRecordCount.
int jfCatalogAddPage(JfCatalog *c, const char *json, int *total);

// Keeps a catalog between runs, and reads it back (built). False if there's none.
bool jfCatalogSave(const JfCatalog *c, const char *file);
bool jfCatalogLoad(JfCatalog *c, const char *file);

// Makes resume and watched say what the server says about its videos; what
// they hold about files on the card stays.
void jfMergeUserData(const JfCatalog *c, ResumeList *resume, Watched *watched);
