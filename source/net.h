// HTTP(S) with libcurl. A client keeps its connection between requests, so
// asking the same server again is quick. One thread uses a client at a time.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "streamheaders.h"

#define NET_USER_AGENT "KaiVideo/" KAIVIDEO_VERSION " (+https://github.com/mizuiroame/kaivideo)"
#define NET_ERROR_MAX 128

// Starts sockets and curl. False if the network can't be used. Each user
// (the servers' sync, the network folder) starts it; it stops after the last
// one's netExit.
bool netInit(void);
void netExit(void);

typedef struct NetClient NetClient;

NetClient *netClientOpen(void);
void netClientClose(NetClient *c);

typedef struct {
    const char *url;
    const char *authorization;    // The Authorization header, or NULL.
    const char *post_json;        // POSTs this as JSON; NULL for a GET.
    const char *range;            // "100-199" or "100-"; NULL for everything.
    const char *custom_method;    // "DELETE" and the like, instead of GET or POST.
    const char *depth;            // WebDAV's Depth header ("1"), or NULL.
    long long *total_size;        // If not NULL, gets the whole file's size from a range's answer.
    const volatile bool *cancel;  // Stops the transfer when it becomes true; may be NULL.
    long timeout_s;               // For the whole transfer; 0: as long as data keeps coming.
    long stall_s;                 // Gives up connecting, or with no data, after this; 0: the usual.
    long quiet_s;                 // With no data, gives up after this instead (a server that works
                                  // before it answers); connecting still gives up as stall_s says. 0: none.
    bool accept_json;             // Asks for JSON (Plex answers XML otherwise).
    bool any_certificate;         // Takes any https certificate (an IPTV list with Certificate check off).
    const StreamHeaders *stream;  // The Referer, User-Agent and Origin a stream asks for; may be NULL.
} NetRequest;

// Takes a piece of the answer. False stops the transfer, which then counts as fine.
typedef bool (*NetSink)(const void *data, size_t size, void *user);

// True for a 2xx answer. status is the HTTP status, 0 without an answer.
bool netFetch(NetClient *c, const NetRequest *request, NetSink sink, void *user, long *status, char *error,
              size_t error_size);

// What went wrong with a request, in the steps a connection takes.
typedef enum {
    NET_OK,           // An answer came (see the status for what it says).
    NET_NO_NAME,      // The address's name couldn't be looked up.
    NET_NO_CONNECT,   // Nothing took the connection at that address and port.
    NET_TIMEOUT,      // No answer in time.
    NET_SECURE,       // The https handshake failed.
    NET_CERTIFICATE,  // The server's certificate isn't trusted.
    NET_STATUS,       // It answered, with a status other than 2xx.
    NET_OTHER,
} NetProblem;

typedef struct {
    char *data;  // NUL-terminated; free with netResponseFree.
    size_t size;
    long status;
    NetProblem problem;
    char error[NET_ERROR_MAX];
} NetResponse;

// The whole answer, up to max_bytes (more fails).
bool netFetchText(NetClient *c, const NetRequest *request, size_t max_bytes, NetResponse *out);
void netResponseFree(NetResponse *r);
// Bytes received by every client so far: its growth is the network's speed.
uint64_t netBytesReceived(void);
// Which servers' certificates are checked: certtrust.h (each source's own Certificate check).
// True for hosts whose certificate is checked even when a setting is off: plex.tv and
// *.plex.direct always have valid ones, and they carry the Plex account's token; github.com
// and githubusercontent.com, so an update and its checksum can't both be forged.
bool netAlwaysChecked(const char *url);
