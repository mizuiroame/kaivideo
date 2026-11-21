// The network folder over SMB (dav.h): a Windows shared folder or Samba,
// through libsmb2 (LGPL 2.1). Its sync calls wait for the server: call them
// off the screen's thread, or with its few seconds' wait in mind.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dav.h"

#define SMB_TIMEOUT_S 8

// Lists a folder of the share ("dav:/" or "dav:/Movies"). *out is malloc'd.
// False with why (for the person) if it can't be.
bool smbList(const DavAccount *a, const char *location, DavEntry **out, size_t *count, char *why, size_t why_size);

typedef struct SmbFile SmbFile;

// Opens a video for reading, on a connection of its own (one thread uses it
// at a time). NULL with why if it can't be.
SmbFile *smbFileOpen(const DavAccount *a, const char *location, char *why, size_t why_size);
int64_t smbFileSize(const SmbFile *f);
// Reads up to size bytes at offset (fewer at the end), in the server's
// largest reads, checking cancel between them. Bytes read; -1 if it failed or was cancelled.
int64_t smbFileRead(SmbFile *f, int64_t offset, uint8_t *to, size_t size, const volatile bool *cancel);
void smbFileClose(SmbFile *f);

// Drops the connection kept for listing (the account changed, or KaiVideo ends).
void smbForget(void);
// libsmb2's own words for the last failure ("" if none), for the log.
void smbLastError(char *out, size_t size);
