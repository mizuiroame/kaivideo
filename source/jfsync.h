// Talking to the Jellyfin server, in a thread of its own so the screens stay
// live: signing in with Quick Connect, reading the library, and telling the
// server what's being watched.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "jellyfin.h"

typedef enum {
    JF_SIGNED_OUT,
    JF_SIGN_IN_STARTING,
    JF_SIGN_IN_WAITING,  // The code is showing, for approval on another device.
    JF_SIGNED_IN,
} JfSignState;

typedef struct {
    JfSignState state;
    char server[JF_SERVER_MAX], user[JF_NAME_MAX];
    char code[16];
    char error[128];   // Why the last thing asked failed; "" if it didn't.
    bool refreshing;
    int loaded, total; // Videos read so far while refreshing.
} JfStatus;

typedef enum { JF_REPORT_START, JF_REPORT_PROGRESS, JF_REPORT_STOP } JfReportKind;

// Starts the network and the thread (once), reading the session from its file.
// load_library: also reads the kept library and asks for a new one. False if
// the network can't be used.
bool jfSyncStart(const char *session_file, const char *catalog_file, const char *version, bool load_library);
// Sends what's waiting to be told and stops.
void jfSyncStop(void);
bool jfSyncRunning(void);

void jfSyncGetStatus(JfStatus *out);
// False if text can't be a server's address.
bool jfSyncSetServer(const char *text);
void jfSyncSignIn(void);
void jfSyncCancelSignIn(void);
void jfSyncSignOut(void);
void jfSyncRefresh(void);

// True once for each library read: c gets it (the caller frees it with jfCatalogFree).
bool jfSyncTakeCatalog(JfCatalog *c);

void jfSyncReport(JfReportKind kind, const char *location, int64_t position_ns, bool paused);
void jfSyncSetPlayed(const char *location, bool played);

// The address and header for reading a location's file; for its picture.
bool jfSyncStreamRequest(const char *location, char *url, size_t url_size, char *authorization, size_t auth_size);
bool jfSyncImageRequest(const char *location, const char *kind, int max_width, char *url, size_t url_size,
                        char *authorization, size_t auth_size);
