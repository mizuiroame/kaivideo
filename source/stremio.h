// Stremio addons (the protocol Stremio and Nuvio share): an addon is a web
// address with a manifest.json; it may give catalogs (lists of films and
// series), metadata (a series' episodes) and streams. KaiVideo brings no
// addons: the user adds them by address. Only http(s) streams are kept:
// torrents (infoHash), YouTube ids and outside links are left out. Pure logic;
// stremioload.c asks the addons, app_stremio.c shows them.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "genres.h"
#include "library.h"
#include "streamheaders.h"

#define STREMIO_PREFIX "stremio:"
#define STREMIO_ADDONS_MAX 6
#define STREMIO_CATALOGS_MAX 12      // An addon's catalogs kept (films' and series' only).
#define STREMIO_LISTS_MAX 24         // Catalogs shown, of all the addons.
#define STREMIO_URL_MAX 512
#define STREMIO_ID_MAX 128
#define STREMIO_NAME_MAX 96
#define STREMIO_PREFIXES_MAX 256     // idPrefixes, joined by spaces; "" for any.
#define STREMIO_METAS_MAX 500        // In one catalog's answer.
#define STREMIO_LIST_METAS_MAX 3000  // In one catalog, all its pages read.
#define STREMIO_VIDEOS_MAX 3000      // A series' episodes.
#define STREMIO_STREAMS_MAX 100
#define STREMIO_TEXT_MAX (8u << 20)  // An addon's answer bigger than this isn't read.

typedef enum { STREMIO_CATALOG, STREMIO_META, STREMIO_STREAM, STREMIO_RESOURCES } StremioResource;

typedef struct {
    char type[16];                 // "movie" or "series".
    char id[STREMIO_ID_MAX];
    char name[STREMIO_NAME_MAX];
    bool search;                   // It can be searched (extra "search").
    bool search_only;              // Only searched: it lists nothing by itself.
    bool skip;                     // It gives pages after the first (extra "skip").
} StremioCatalogDef;

typedef struct {
    char base[STREMIO_URL_MAX];    // Its address without "/manifest.json".
    char id[STREMIO_ID_MAX];
    char name[STREMIO_NAME_MAX];
    bool resources[STREMIO_RESOURCES];
    char types[STREMIO_RESOURCES][64];                 // Each resource's types ("movie series"), "" for the addon's.
    char prefixes[STREMIO_RESOURCES][STREMIO_PREFIXES_MAX];  // Each resource's idPrefixes, "" for any.
    StremioCatalogDef catalogs[STREMIO_CATALOGS_MAX];
    int catalog_count;
    bool needs_setup;              // behaviorHints.configurationRequired: set it up on its page first.
} StremioAddon;

typedef struct {
    char *id;
    char type[16];
    char *name;
    char *poster;                  // An address, or NULL.
    char *description;
    int year;
    char genres[GENRES_TEXT_MAX];  // "Drama, Comedy", or "".
} StremioMeta;

typedef struct {
    char *id;                      // What its streams are asked by ("tt123:1:2").
    int season, episode;
    char *title;
    char *overview;
} StremioVideo;

typedef struct {
    char *url;
    char *name;                    // The addon's name for it ("1080p"), or NULL.
    char *detail;                  // Its title or description (the file, size...), on one line; or NULL.
    char addon[STREMIO_NAME_MAX];  // The addon it came from.
    StreamHeaders headers;         // behaviorHints.proxyHeaders.request: what to send along.
} StremioStream;

// The addresses typed: one manifest address a line (the addons file's text).
typedef struct {
    char urls[STREMIO_ADDONS_MAX][STREMIO_URL_MAX];
    int count;
} StremioAddonList;

void stremioAddonListRead(StremioAddonList *l, const char *text);
bool stremioAddonListWrite(const StremioAddonList *l, char *out, size_t size);
// A typed address as a manifest's ("stremio://x/manifest.json" is https; "/manifest.json" added if missing).
// False if it can't be one.
bool stremioNormalizeUrl(const char *typed, char *out, size_t size);
// Adds one (normalized). False if it's there already, the list is full, or it can't be one.
bool stremioAddonListAdd(StremioAddonList *l, const char *typed);
void stremioAddonListRemove(StremioAddonList *l, int index);

// A manifest (its address: manifest_url). False if it isn't one.
bool stremioParseManifest(const char *json, const char *manifest_url, StremioAddon *out);
// Whether it gives resource for this type and id (its prefixes).
bool stremioHandles(const StremioAddon *a, StremioResource r, const char *type, const char *id);
// A catalog's address: its first page, or a search (search non-NULL).
bool stremioCatalogUrl(const StremioAddon *a, const StremioCatalogDef *c, const char *search, char *out, size_t size);
// The page after the first skip titles it gave: /catalog/<type>/<id>/skip=<skip>.json.
bool stremioCatalogPageUrl(const StremioAddon *a, const StremioCatalogDef *c, int skip, char *out, size_t size);
// /meta/<type>/<id>.json or /stream/<type>/<id>.json.
bool stremioResourceUrl(const StremioAddon *a, StremioResource r, const char *type, const char *id, char *out,
                        size_t size);

// A catalog's answer ({"metas": [...]}). False if it isn't one.
bool stremioParseMetas(const char *json, StremioMeta **out, int *count);
void stremioMetasFree(StremioMeta *metas, int count);
// A meta's answer ({"meta": {..., "videos": [...]}}): a series' episodes by season and number
// (specials, season 0, last). False if it isn't one.
bool stremioParseVideos(const char *json, StremioVideo **out, int *count);
void stremioVideosFree(StremioVideo *videos, int count);
// A stream answer ({"streams": [...]}): the playable ones, added to out (count so far), from addon.
bool stremioParseStreams(const char *json, const char *addon, StremioStream **out, int *count);
void stremioStreamsFree(StremioStream *streams, int count);

// Locations: "stremio:movie/<id>", "stremio:series/<video id>" (an episode), "stremio:show/<series id>"
// (a series whose episodes weren't read yet). Ids are kept with %XX for anything but letters, digits and ":._-".
bool stremioIsLocation(const char *location);
bool stremioLocation(char *out, size_t size, const char *kind, const char *id);
bool stremioParseLocation(const char *location, char *kind, size_t kind_size, char *id, size_t id_size);

// What the addons gave, as the Addons view's library shows it.
typedef enum { STREMIO_EPISODES_UNREAD, STREMIO_EPISODES_READ, STREMIO_EPISODES_FAILED } StremioEpisodesState;

typedef struct {
    char title[STREMIO_NAME_MAX * 2];  // "Popular · Cinemeta".
    char type[16];
    StremioMeta *metas;
    int count;
    // Where it comes from (an index in the store's addons, and in that addon's catalogs), how many
    // titles its pages gave so far (the next page's skip), and whether another page may follow.
    int addon, catalog;
    int received;
    bool more;
} StremioList;

typedef struct {
    char *id;                      // The series'.
    StremioEpisodesState state;
    StremioVideo *videos;
    int count;
} StremioEpisodes;

typedef struct {
    StremioAddon addons[STREMIO_ADDONS_MAX];
    int addon_count;
    StremioList lists[STREMIO_LISTS_MAX];
    int list_count;
    StremioList found;             // The last search's.
    StremioEpisodes *series;
    int series_count;
} StremioStore;

void stremioStoreFree(StremioStore *s);
void stremioListFree(StremioList *l);
// Adds a page of a catalog's (count of them, taken: those it has already are freed) to its list, up to
// STREMIO_LIST_METAS_MAX. Another page may follow if the list's catalog takes skip and this page brought
// something new. Returns how many were added.
int stremioListAppend(StremioList *l, const StremioStore *s, StremioMeta *metas, int count);
// A series' episodes (taken), or failed (NULL).
void stremioStoreSetEpisodes(StremioStore *s, const char *series_id, StremioVideo *videos, int count);
StremioEpisodesState stremioStoreEpisodesState(const StremioStore *s, const char *series_id);

// The films and series in it, each once (the first catalog with it is its tab), as library items
// (not built). Its catalogs become the library's collections (8 at most); searched ones are in none.
void stremioFillLibrary(Library *lib, const StremioStore *s, const char *unread, const char *failed);
