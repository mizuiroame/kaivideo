// The network folder's listings other than WebDAV's (dav.h): an FTP
// server's MLSD or LIST answer, and a plain web server's folder index (the
// page Apache, nginx and the like make). Pure logic; ftpnet.c and davnet.c
// ask the servers.
//
// Each gives the folder's subfolders and files, without "." and "..";
// names with control characters, or that don't fit, are left out; past
// DAV_LISTING_MAX entries, the rest too. *out is malloc'd (free it).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dav.h"

// MLSD: "type=file;size=1234;modify=20261121003855; name.mkv" a line.
// False if no line is one.
bool folderParseMlsd(const char *text, DavEntry **out, size_t *count);

// LIST, as Unix servers ("-rw-r--r-- 1 user group 1234 Nov 21 00:38 name")
// and Windows' IIS ("11-21-26  12:38AM  1234 name", "<DIR>" for a folder)
// write it. now (seconds since 1970) dates a Unix line that has a time but no
// year. False if there's text but no line can be read.
bool folderParseFtpList(const char *text, int64_t now, DavEntry **out, size_t *count);

// A web page's links into the folder at folder_url: "name/" a subfolder,
// anything else a file. Links that go elsewhere (up, another site, a sort
// order "?C=N") are left out. Sizes and dates aren't known (-1 and 0).
// False if it isn't a web page.
bool folderParseHtmlIndex(const char *html, const char *folder_url, DavEntry **out, size_t *count);
