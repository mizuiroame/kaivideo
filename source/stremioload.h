// Asks the Stremio addons (stremio.h), each kind of request on a thread of
// its own: loading the addons (their manifests, then each catalog's first
// page), a series' episodes, a video's streams, and a search. A request
// replaces the one of its kind under way. The network must be held by the
// caller while they run (netInit).
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "stremio.h"

typedef enum { STREMIO_JOB_LOAD, STREMIO_JOB_EPISODES, STREMIO_JOB_STREAMS, STREMIO_JOB_SEARCH, STREMIO_JOBS } StremioJob;

#define STREMIO_NOTE_MAX 512

void stremioLoadStart(const StremioAddonList *l);
// The addons and their catalogs (into out, emptied first), and a note of what failed ("" if nothing).
// False until it's done. ok: at least one addon answered.
bool stremioLoadTake(StremioStore *out, bool *ok, char *note, size_t note_size);

// The episodes of a series, from the first addon that gives its metadata.
void stremioAskEpisodes(const StremioStore *s, const char *series_id);
// False until they came. series_id: the one asked. videos NULL if they couldn't be read.
bool stremioTakeEpisodes(char *series_id, size_t id_size, StremioVideo **videos, int *count);

// A video's streams ("movie" or "series"; an episode's id), from every addon that gives them.
void stremioAskStreams(const StremioStore *s, const char *type, const char *video_id);
// False until they came. video_id: the one asked. note: what failed, "" if nothing.
bool stremioTakeStreams(char *video_id, size_t id_size, StremioStream **streams, int *count, char *note,
                        size_t note_size);

// Searches every catalog that can be searched.
void stremioAskSearch(const StremioStore *s, const char *query);
// False until it's done: the films and series found (each once).
bool stremioTakeSearch(StremioList *found);

// Whether a request of that kind is under way.
bool stremioBusy(StremioJob job);
// Stops them all, and waits.
void stremioLoadExit(void);

// The stream chosen for a location (its address and the headers it needs, which may be NULL), kept for
// the reader while it plays; the last few.
void stremioChoose(const char *location, const char *url, const StreamHeaders *headers);
// The headers the chosen stream needs (cleared if none, or none was chosen).
bool stremioChosenHeaders(const char *location, StreamHeaders *out);
// The chosen address for a location. False if none was chosen.
bool stremioChosenUrl(const char *location, char *url, size_t size);
// mediafile.c's request for a location: the chosen address, no header.
bool stremioStreamRequest(const char *location, char *url, size_t url_size, char *authorization, size_t auth_size);
// Whether the chosen address is an HLS list (it opens as an IPTV channel's does).
bool stremioChosenIsHls(const char *location);
