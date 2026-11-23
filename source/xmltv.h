// An XMLTV guide (an IPTV list's), read as it downloads: gzipped or not,
// keeping only the programs of the channels asked for, in a window of
// time. Guides are often tens of megabytes; this never holds one whole.
// Pure logic.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "livetv.h"

#define XMLTV_PER_CHANNEL LIVE_PROGRAMS_PER_CHANNEL

typedef struct XmltvReader XmltvReader;

// ids: each channel's guide id (may repeat, or be ""), count of them. Keeps
// programs that end after from_s and start before until_s.
XmltvReader *xmltvBegin(const char *const *ids, int count, int64_t from_s, int64_t until_s);
// More of the guide, as it came (gzip is found by its first bytes). False if
// it can't be read on (memory, a broken gzip stream).
bool xmltvFeed(XmltvReader *r, const void *data, size_t size);
// The programs kept for channel (an index of the ids), by start; how many in count.
const LiveProgram *xmltvPrograms(const XmltvReader *r, int channel, int *count);
// Programs read in all (to say whether the guide had anything for the list).
int xmltvMatched(const XmltvReader *r);
void xmltvFree(XmltvReader *r);

// "20240101120000 +0100" (seconds and zone optional) in seconds since 1970; 0 if unreadable.
int64_t xmltvParseTime(const char *text);
