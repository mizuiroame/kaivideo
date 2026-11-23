// The network folder's server (dav.h): its account, kept for every thread,
// and what's asked of it.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dav.h"

// The account in use (copied). An empty address: no network folder.
void davUseAccount(const DavAccount *a);
void davGetAccount(DavAccount *out);
bool davConfigured(void);

// The account's file in the config folder: read at start, written on a change.
void davLoadAccount(const char *path);
bool davSaveAccount(const char *path);

// True when the folder's videos are read by their own protocol (SMB, FTP or
// SFTP: davFileOpen), not by HTTP ranges (davStreamRequest).
bool davReadsFiles(void);
// True when the folder has a user whose password isn't remembered and hasn't been
// typed since KaiVideo started: listing and reading fail at once, without the server.
bool davPasswordMissing(void);
// A video of an SMB, FTP or SFTP folder, read on a connection of its own (one
// thread uses it at a time). NULL with why if it can't be.
typedef struct DavFile DavFile;
DavFile *davFileOpen(const char *location, char *why, size_t why_size);
int64_t davFileSize(const DavFile *f);
// Up to size bytes at offset (fewer at the end), checking cancel. Bytes read; -1 if it failed or was cancelled.
int64_t davFileRead(DavFile *f, int64_t offset, uint8_t *to, size_t size, const volatile bool *cancel);
void davFileClose(DavFile *f);

// Drops the connections kept for listing (the account changed, or KaiVideo ends).
void davForget(void);

// A small file of the folder (a poster, an .srt) read whole, up to max_bytes (more fails), NUL-terminated
// (free *data). False with why if it can't be.
bool davReadWhole(const char *location, size_t max_bytes, char **data, size_t *size, char *why, size_t why_size);

// A WebDAV (or web index) location's URL and Authorization header, for reading it with ranges. False if
// there's no folder, or it's read with davFileOpen.
bool davStreamRequest(const char *location, char *url, size_t url_size, char *auth, size_t auth_size);

// Lists a folder ("dav:/" or "dav:/Movies"), over WebDAV (or a web server's index), SMB, FTP or SFTP. *out is malloc'd. False with why
// (for the person) if it can't be.
bool davList(const char *location, DavEntry **out, size_t *count, char *why, size_t why_size);

// Asks the server for its top folder: what it says, as a sentence. True if it answered with a listing.
bool davTest(char *out, size_t size);
