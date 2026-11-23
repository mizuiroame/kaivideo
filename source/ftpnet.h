// The network folder over FTP (dav.h): ftp:// (plain), ftps:// (TLS from the
// start) or ftpes:// (TLS asked for first, and required), through libcurl.
// Its calls wait for the server: call them off the screen's thread.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dav.h"

// Lists a folder ("dav:/" or "dav:/Movies"): MLSD, or LIST for a server that
// doesn't know it. *out is malloc'd. False with why (for the person) if it can't be.
bool ftpList(const DavAccount *a, const char *location, DavEntry **out, size_t *count, char *why, size_t why_size);

typedef struct FtpFile FtpFile;

// Opens a video for reading, on a connection of its own (one thread uses it
// at a time). NULL with why if it can't be.
FtpFile *ftpFileOpen(const DavAccount *a, const char *location, char *why, size_t why_size);
int64_t ftpFileSize(const FtpFile *f);
// Reads up to size bytes at offset (fewer at the end), checking cancel. Bytes read; -1 if it failed or was cancelled.
int64_t ftpFileRead(FtpFile *f, int64_t offset, uint8_t *to, size_t size, const volatile bool *cancel);
void ftpFileClose(FtpFile *f);

// Drops the connection kept for listing (the account changed, or KaiVideo ends).
void ftpForget(void);
