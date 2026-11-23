// An Xtream account's films (get_vod_streams) and series (get_series, and
// get_series_info for one series' episodes), as the IPTV view's library.
// Each video has a location of its own, "xtream:movie/<id>.<ext>" or
// "xtream:series/<episode id>.<ext>", made into its address only when it's
// read (the address holds the password). A series whose episodes weren't
// read yet has one stand-in, "xtream:show/<series id>", until they are.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "iptv.h"
#include "library.h"

#define XTREAM_PREFIX "xtream:"
#define XTREAM_FILMS_MAX 30000
#define XTREAM_SHOWS_MAX 10000
#define XTREAM_EPISODES_MAX 2000  // In one series.

typedef struct {
    long long id;          // Its episode id.
    int season, episode;
    char *title;           // May be NULL.
    char ext[8];           // container_extension.
    char *plot;            // May be NULL.
    int64_t runtime_s;     // 0 if unknown.
} XtreamEpisode;

typedef struct {
    long long id;
    char *name;
    char ext[8];
    char *poster;          // An address, or NULL.
    int64_t added;         // Seconds since 1970, 0 if unknown.
    int year;              // 0 if unknown.
} XtreamFilm;

typedef enum { XTREAM_EPISODES_UNREAD, XTREAM_EPISODES_READ, XTREAM_EPISODES_FAILED } XtreamEpisodesState;

typedef struct {
    long long id;
    char *name;
    char *cover;           // An address, or NULL.
    char *plot;            // May be NULL.
    int64_t added;
    XtreamEpisodesState state;
    XtreamEpisode *episodes;
    int episode_count;
} XtreamShow;

typedef struct {
    XtreamFilm *films;
    int film_count;
    XtreamShow *shows;
    int show_count;
} XtreamCatalog;

void xtreamCatalogFree(XtreamCatalog *c);
// get_vod_streams' answer, as c's films (replacing them). False if it isn't one.
bool xtreamParseFilms(const char *json, XtreamCatalog *c);
// get_series' answer, as c's series (replacing them, none of their episodes read). False if it isn't one.
bool xtreamParseShows(const char *json, XtreamCatalog *c);
// get_series_info's answer: the series' episodes, by season and number. False if it isn't one.
bool xtreamParseEpisodes(const char *json, XtreamEpisode **out, int *count);
void xtreamEpisodesFree(XtreamEpisode *episodes, int count);
// Gives a series its episodes (taking them), or marks them failed (NULL).
void xtreamShowSetEpisodes(XtreamCatalog *c, long long show_id, XtreamEpisode *episodes, int count);
XtreamShow *xtreamFindShow(XtreamCatalog *c, long long show_id);

// Locations. kind: "movie", "series" (an episode) or "show" (a series' stand-in, no extension).
bool xtreamIsLocation(const char *location);
bool xtreamLocation(char *out, size_t size, const char *kind, long long id, const char *ext);
// Its kind, id and extension. False if it isn't one.
bool xtreamParseLocation(const char *location, char *kind, size_t kind_size, long long *id, char *ext,
                         size_t ext_size);
// The series a stand-in is for, or -1 if it isn't one.
long long xtreamStandInShow(const char *location);

// A picture's thumbnail key: "poster:<address>" (cropped to fill a card, not shown whole as a logo).
#define XTREAM_POSTER_PREFIX LIBRARY_POSTER_PREFIX

// Adds the catalog's films and series to lib (not built). unread and failed: the stand-in's title for
// a series whose episodes weren't read yet, or couldn't be.
void xtreamFillLibrary(Library *lib, const XtreamCatalog *c, const char *unread, const char *failed);

// The account the locations are read with (set when the list is; cleared for an M3U one). Thread-safe.
void xtreamVodSetAccount(const XtreamAccount *a, bool any_certificate);
// A location's address (mediafile.c's remote reading). False if it isn't one, or there's no account.
bool xtreamVodStreamRequest(const char *location, char *url, size_t url_size, char *authorization,
                            size_t auth_size);
bool xtreamVodAnyCertificate(void);
// The account's player_api.php address for an action (xtreamApiUrl). False if there's no account.
bool xtreamVodApiUrl(const char *action, const char *extra, char *out, size_t size);
