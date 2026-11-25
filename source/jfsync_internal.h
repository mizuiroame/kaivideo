// What jfsync.c shares with jfrequest.c: the sessions of the servers signed
// in, read under jfsync's lock, and what Plex's reports need to know.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "jellyfin.h"
#include "plex.h"

// Whether the thread runs, and the version KaiVideo names itself with.
bool jfSyncIsUp(void);
const char *jfSyncVersionText(void);
// A copy of the session of a Jellyfin or Emby server, or of Plex's.
JfSession jfSyncSessionFor(ServerKind kind);
PlexSession jfSyncPlexSession(void);
// A Plex Home profile switched to: its session replaces Plex's (and is kept),
// Plex's library goes at once and is read again as the profile sees it.
void jfSyncPlexUseProfile(const PlexSession *s);
// The Plex video last asked about and its length (its timeline reports want it).
void jfSyncNotePlexDuration(const char *item, int64_t duration_ns);
