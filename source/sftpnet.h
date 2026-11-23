// The network folder over SFTP (dav.h), through libssh2 (BSD). The server's
// key is checked: its fingerprint is kept the first time it's met
// (DavAccount.host_key), and a server with another key later is refused, so a
// password is never sent to an impostor. Its calls wait for the server: call
// them off the screen's thread.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dav.h"

// new_key: when a has no host_key yet, the server's fingerprint ("SHA256:..."),
// for the account to keep; otherwise "". It may be NULL.

// Lists a folder ("dav:/" or "dav:/Movies"). *out is malloc'd. False with why
// (for the person) if it can't be.
bool sftpList(const DavAccount *a, const char *location, DavEntry **out, size_t *count, char new_key[DAV_HOST_KEY_MAX],
              char *why, size_t why_size);

typedef struct SftpFile SftpFile;

// Opens a video for reading, on a connection of its own (one thread uses it
// at a time). NULL with why if it can't be.
SftpFile *sftpFileOpen(const DavAccount *a, const char *location, char new_key[DAV_HOST_KEY_MAX], char *why,
                       size_t why_size);
int64_t sftpFileSize(const SftpFile *f);
// Reads up to size bytes at offset (fewer at the end), checking cancel. Bytes read; -1 if it failed or was cancelled.
int64_t sftpFileRead(SftpFile *f, int64_t offset, uint8_t *to, size_t size, const volatile bool *cancel);
void sftpFileClose(SftpFile *f);

// Drops the connection kept for listing (the account changed, or KaiVideo ends).
void sftpForget(void);
