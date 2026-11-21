// The network folder: a WebDAV server (a NAS, Nextcloud, rclone) or an SMB
// share (a Windows shared folder, Samba), browsed in Files, a library source,
// and played from there. Its videos are "dav:/path" locations whatever the
// protocol, read ahead in blocks like a Jellyfin file (mediafile.c): with
// HTTP ranges, or SMB reads (smbnet.c).
//
// dav.c is pure: locations, addresses, the account's file and the server's
// listing. davnet.c asks the server.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DAV_PREFIX "dav:/"
#define DAV_ROOT DAV_PREFIX         // The folder's top, as Files has it.
#define DAV_ADDRESS_MAX 256
#define DAV_TEXT_MAX 128            // A user name or a password.
#define DAV_NAME_MAX 256
#define DAV_ACCOUNT_FILE "network.txt"
#define DAV_LISTING_MAX 5000      // Entries a folder shows at most.

typedef struct {
    // "http://192.168.0.10:8098/dav/" or "smb://PC/Videos/sub/", always ending in '/'; "" for none.
    char address[DAV_ADDRESS_MAX];
    char user[DAV_TEXT_MAX];        // "" for none: the server asks for nothing.
    char password[DAV_TEXT_MAX];
} DavAccount;

typedef struct {
    char name[DAV_NAME_MAX];
    bool is_dir;
    int64_t size;      // Bytes; -1 if the server didn't say.
    int64_t modified;  // Seconds since 1970 (UTC); 0 if the server didn't say.
} DavEntry;

// An HTTP date ("Sat, 21 Nov 2026 00:38:55 GMT") in seconds since 1970; 0 if it isn't one.
int64_t davParseDate(const char *text);

// True for "dav:/..." locations.
bool davIsLocation(const char *path);

// What was typed, as an address: "192.168.0.10:8098" -> "http://192.168.0.10:8098/",
// "smb://PC/Videos" or "\\PC\Videos" -> "smb://PC/Videos/". False if it isn't
// one (another scheme, spaces in a WebDAV address, nothing after the scheme,
// an SMB address without its share, or "user:password@" in it: those go in
// their own row).
bool davNormalizeAddress(const char *typed, char *out, size_t size);
// True for an SMB share's address.
bool davIsSmbAddress(const char *address);
// An SMB address's parts: the server ("PC" or "PC:4450"), the share, and the
// folder in it ("" or "Media/Videos", no slash at either end). False if it isn't one.
bool davSmbParts(const char *address, char *server, size_t server_size, char *share, size_t share_size, char *folder,
                 size_t folder_size);
// A location's path in its share ("Media/Videos/Movies/a.mkv"; "" for the share's top). False if too long.
bool davSmbPath(const char *address, const char *location, char *out, size_t size);
// "DOMAIN\user" -> "DOMAIN" and "user"; a plain user has no domain ("").
void davSplitUser(const char *user, char *domain, size_t domain_size, char *name, size_t name_size);
// The address shown: without "http://" and the last '/' ("192.168.0.10:8098/dav").
void davShownAddress(const char *address, char *out, size_t size);

// The URL of a location: the address, then the path with each piece
// percent-encoded ("dav:/A B/c.mkv" -> ".../A%20B/c.mkv"). False if too long.
bool davUrlOf(const char *address, const char *location, char *out, size_t size);
// Same scheme, host and port: a password typed for one isn't sent to the other.
bool davSameServer(const char *a, const char *b);
// "Basic <base64 of user:password>", or "" without a user. False if too long.
bool davAuthorization(const DavAccount *a, char *out, size_t size);

// The account's file: "address=", "user=", "password=" lines.
void davAccountRead(DavAccount *a, const char *text);
// Writes the account as the file's text. False if it doesn't fit.
bool davAccountWrite(const DavAccount *a, char *out, size_t size);

// A PROPFIND answer (Depth: 1) for the folder at folder_url's path: its
// subfolders and files, without the folder itself. Any namespace prefix.
// Names with control characters, or too long to read whole, are left out;
// past DAV_LISTING_MAX entries, the rest too.
// *out is malloc'd (free it), *count entries. False if it isn't a listing.
bool davParseListing(const char *xml, const char *folder_url, DavEntry **out, size_t *count);
