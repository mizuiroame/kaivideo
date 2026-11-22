// The network folder's server (dav.h): its account, kept for every thread,
// and what's asked of it.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "dav.h"

// The account in use (copied). An empty address: no network folder.
void davUseAccount(const DavAccount *a);
void davGetAccount(DavAccount *out);
bool davConfigured(void);

// The account's file in the config folder: read at start, written on a change.
void davLoadAccount(const char *path);
bool davSaveAccount(const char *path);

// True when the folder is an SMB share (read with smbnet.h), not WebDAV.
bool davIsSmb(void);
// True when the folder has a user whose password isn't remembered and hasn't been
// typed since KaiVideo started: listing and reading fail at once, without the server.
bool davPasswordMissing(void);
// Opens a video of an SMB folder (smbnet.h). NULL with why if it can't be.
struct SmbFile *davSmbOpen(const char *location, char *why, size_t why_size);

// A WebDAV location's URL and Authorization header, for reading it with ranges. False if there's no folder
// (or it's an SMB share).
bool davStreamRequest(const char *location, char *url, size_t url_size, char *auth, size_t auth_size);

// Lists a folder ("dav:/" or "dav:/Movies"), over WebDAV or SMB. *out is malloc'd. False with why
// (for the person) if it can't be.
bool davList(const char *location, DavEntry **out, size_t *count, char *why, size_t why_size);

// Asks the server for its top folder: what it says, as a sentence. True if it answered as a WebDAV folder.
bool davTest(char *out, size_t size);
