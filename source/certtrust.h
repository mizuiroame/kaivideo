// Which servers' certificates are checked. Each source with a Certificate
// check of its own in Settings (Jellyfin, Emby, the network folder) has its
// address here: with its check off, only requests to that address's host and
// port take any certificate; every other server's is still checked. (An IPTV
// list's choice goes with each of its requests, NetRequest.any_certificate.)
// Thread-safe; pure logic.
#pragma once

#include <stdbool.h>

typedef enum { TRUST_JELLYFIN, TRUST_EMBY, TRUST_NETWORK, TRUST_COUNT } TrustSource;

// The source's address ("https://host:8920", "ftps://nas"); NULL or "" for none.
void trustSetAddress(TrustSource source, const char *url);
// Whether the source's certificate is checked (on at start).
void trustSetChecked(TrustSource source, bool check);
// Whether a request to url checks the certificate: false only for the host and port of a source whose
// check is off.
bool trustChecks(const char *url);
// Whether two addresses are the same server: their host (any case) and port, the scheme's when none is
// written (http 80, https 443, ftp and ftpes 21, ftps 990, sftp 22).
bool trustSameServer(const char *a, const char *b);
