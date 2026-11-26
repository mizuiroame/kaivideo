// Talking to Plex: plex.tv for signing in and finding the account's servers,
// then the server for its libraries, how to play a video, and what's being
// watched. No state of its own: each call takes the session and a client, so
// jfsync.c's thread (and the players' threads) can use it.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "jellyfin.h"
#include "net.h"
#include "plex.h"
#include "plexhome.h"

#define PLEX_URL_MAX 2048

// The address of a server path (which may hold a query), with KaiVideo's
// names and the server's token in it.
void plexServerUrl(const PlexSession *s, const char *version, const char *path, char *out, size_t size);

// The address of a plex.tv path (which may hold a query), with KaiVideo's
// names, and the account's (or profile's) token when with_token.
void plexTvUrl(const PlexSession *s, const char *version, const char *path, bool with_token, char *out,
               size_t size);

// Asks plex.tv for a code to type at plex.tv/link.
bool plexNetStartPin(NetClient *net, const PlexSession *s, const char *version, long long *id, char *code,
                     size_t code_size, const volatile bool *cancel);
// Once it's approved: true, with the account's token in s.
bool plexNetPollPin(NetClient *net, PlexSession *s, const char *version, long long id, const volatile bool *cancel);
// The account's server to use (the one it owns, else the first shared) and an
// address of it that answers, into s. False, with the reason, if none answers.
bool plexNetPickServer(NetClient *net, PlexSession *s, const char *version, char *error, size_t error_size,
                       const volatile bool *cancel);
// The same for one server (its clientIdentifier), as the token in s sees it:
// a Plex Home profile's switch keeps the server in use. "": any, as above.
bool plexNetPickServerOf(NetClient *net, PlexSession *s, const char *version, const char *server_id, char *error,
                         size_t error_size, const volatile bool *cancel);

// Called as pages come: videos read so far, and how many the library has (0 if unknown).
typedef void (*PlexProgress)(int loaded, int total);
// Every video of the server's film and series libraries into c. False if a page can't be had.
bool plexNetReadLibrary(NetClient *net, const PlexSession *s, const char *version, JfCatalog *c,
                        PlexProgress progress, const volatile bool *cancel);

// A GET (or PUT) to the server that only needs to get through. Its status
// goes in status (0 without an answer). True for a 2xx answer.
bool plexNetCall(NetClient *net, const PlexSession *s, const char *version, const char *path, bool put, long timeout_s,
                 long *status);

// How to play a location: its plan, its length, and, converted, the
// conversion's path (in plan->transcode_url) and session (plan->play_session).
// audio_id: the sound track's stream id, -1 for the server's choice; burn_id:
// a subtitle for the server to draw, -1 for none.
bool plexNetResolve(NetClient *net, const PlexSession *s, const char *version, const char *location,
                    long long max_bitrate, int audio_id, int burn_id, JfPlan *plan, int64_t *duration_ns,
                    char *error, size_t error_size, const volatile bool *cancel);
// A conversion of one sound track alone (a tiny picture with it), with its own session.
bool plexNetAudioConversion(NetClient *net, const PlexSession *s, const char *version, const char *location,
                            int audio_id, char *url, size_t url_size, char *session, size_t session_size);

// The address for reading a location's file.
bool plexStreamUrl(const PlexSession *s, const char *version, const char *location, char *url, size_t size);
// A picture of a location (or of "plex:<series>/series"): kind "Primary" is
// the poster, anything else the wide one.
bool plexImageUrl(const PlexSession *s, const char *version, const char *location, const char *kind, int max_width,
                  char *url, size_t size);
// The intros and credits Plex marked in a location. How many, 0 if none or no answer.
int plexNetFetchMarkers(NetClient *net, const PlexSession *s, const char *version, const char *location,
                        JfSegment *out, int max);
// An external subtitle file (a stream id) of a location.
bool plexNetFetchSubtitle(NetClient *net, const PlexSession *s, const char *version, int stream_id,
                          const volatile bool *cancel, NetResponse *out);

// Plex Home (plexhomesync.c runs these on its thread). The Home's profiles into users; returns
// how many, -1 if plex.tv didn't answer or the answer can't be read.
int plexNetHomeUsers(NetClient *net, const PlexSession *s, const char *version, PlexHomeUser *users, int max,
                     const volatile bool *cancel);
// Switches s to user (its token, its server token, its name) keeping the
// server in use. s is left as it was unless it worked; then why, in message.
PlexSwitchResult plexNetSwitchHome(NetClient *net, PlexSession *s, const char *version, const PlexHomeUser *user,
                                   const char *pin, char *message, size_t message_size, const volatile bool *cancel);
