// The HTTP headers a stream asks to be sent along (Referer, User-Agent,
// Origin), as an IPTV list or a Stremio addon says. Only these three are
// taken, and only values that are safe in a header. Pure logic.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define STREAM_HEADER_MAX 256

typedef struct {
    char referer[STREAM_HEADER_MAX];
    char user_agent[STREAM_HEADER_MAX];
    char origin[STREAM_HEADER_MAX];
} StreamHeaders;

bool streamHeadersAny(const StreamHeaders *h);

// Sets one by its name ("Referer", "Referrer", "User-Agent", "Origin", any
// case). False, and nothing set, for another name or a value that isn't safe
// (empty, too long, a line break or another control character, not ASCII).
bool streamHeadersSet(StreamHeaders *h, const char *name, const char *value);

// An M3U's "#EXTVLCOPT:http-referrer=...", "http-user-agent=..." or
// "http-origin=..." line (length bytes, not NUL-terminated). True if it set one.
bool streamHeadersFromVlcOpt(StreamHeaders *h, const char *line, size_t length);

// Kodi's form, "address|User-Agent=x&Referer=y" (values may be
// percent-encoded): sets what follows the "|" and cuts it off url. True if
// url had a "|" (even with nothing usable after it).
bool streamHeadersFromPipe(StreamHeaders *h, char *url);

// The names of those set, for the log ("Referer, User-Agent"); never their values.
void streamHeadersNames(const StreamHeaders *h, char *out, size_t size);
