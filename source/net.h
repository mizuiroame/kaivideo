// HTTP(S) with libcurl. A client keeps its connection between requests, so
// asking the same server again is quick. One thread uses a client at a time.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define NET_USER_AGENT "KaiVideo/" KAIVIDEO_VERSION " (+https://github.com/mizuiroame/kaivideo)"
#define NET_ERROR_MAX 128

// Starts sockets and curl. False if the network can't be used.
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
    long long *total_size;        // If not NULL, gets the whole file's size from a range's answer.
    const volatile bool *cancel;  // Stops the transfer when it becomes true; may be NULL.
    long timeout_s;               // For the whole transfer; 0: as long as data keeps coming.
    long stall_s;                 // Gives up connecting, or with no data, after this; 0: the usual.
} NetRequest;

// Takes a piece of the answer. False stops the transfer, which then counts as fine.
typedef bool (*NetSink)(const void *data, size_t size, void *user);

// True for a 2xx answer. status is the HTTP status, 0 without an answer.
bool netFetch(NetClient *c, const NetRequest *request, NetSink sink, void *user, long *status, char *error,
              size_t error_size);

typedef struct {
    char *data;  // NUL-terminated; free with netResponseFree.
    size_t size;
    long status;
    char error[NET_ERROR_MAX];
} NetResponse;

// The whole answer, up to max_bytes (more fails).
bool netFetchText(NetClient *c, const NetRequest *request, size_t max_bytes, NetResponse *out);
void netResponseFree(NetResponse *r);
