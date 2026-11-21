#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "dav.h"

#define CHECK_STR(a, b) CHECK(strcmp((a), (b)) == 0)

static void testAddresses(void)
{
    char out[DAV_ADDRESS_MAX];
    CHECK(davNormalizeAddress("192.168.0.10:8098", out, sizeof(out)));
    CHECK_STR(out, "http://192.168.0.10:8098/");
    CHECK(davNormalizeAddress("  https://nas.local/remote.php/dav/files/me  ", out, sizeof(out)));
    CHECK_STR(out, "https://nas.local/remote.php/dav/files/me/");
    CHECK(davNormalizeAddress("HTTP://Box:80/share/", out, sizeof(out)));
    CHECK_STR(out, "HTTP://Box:80/share/");
    CHECK(!davNormalizeAddress("", out, sizeof(out)));
    CHECK(!davNormalizeAddress("http://", out, sizeof(out)));
    CHECK(!davNormalizeAddress("my pc:8098", out, sizeof(out)));
    CHECK(!davNormalizeAddress("ftp://pc", out, sizeof(out)));
    CHECK(!davNormalizeAddress("http://me:secret@pc:8098/", out, sizeof(out)));
    CHECK(davNormalizeAddress("pc:8098/a@b", out, sizeof(out)));  // An '@' in the path is a name.
    CHECK(davSameServer("http://pc:8098/a/", "HTTP://PC:8098/b/"));
    CHECK(!davSameServer("http://pc:8098/", "http://pc:8099/"));
    CHECK(!davSameServer("http://pc/", "https://pc/"));

    // SMB: a Windows shared folder, typed either way.
    CHECK(davNormalizeAddress("smb://PC/Videos", out, sizeof(out)));
    CHECK_STR(out, "smb://PC/Videos/");
    CHECK(davNormalizeAddress("\\\\PC\\Mis Videos\\Pelis\\", out, sizeof(out)));
    CHECK_STR(out, "smb://PC/Mis Videos/Pelis/");
    CHECK(davNormalizeAddress(" //192.168.0.10:4450/Videos ", out, sizeof(out)));
    CHECK_STR(out, "smb://192.168.0.10:4450/Videos/");
    CHECK(davIsSmbAddress(out) && !davIsSmbAddress("http://pc/"));
    CHECK(!davNormalizeAddress("smb://PC", out, sizeof(out)));
    CHECK(!davNormalizeAddress("\\\\PC\\", out, sizeof(out)));
    CHECK(!davNormalizeAddress("smb://me:pw@PC/Videos", out, sizeof(out)));
    CHECK(!davNormalizeAddress("smb:///Videos", out, sizeof(out)));

    char server[64], share[64], folder[64];
    CHECK(davSmbParts("smb://PC:4450/Videos/Media/Movies/", server, sizeof(server), share, sizeof(share), folder,
                      sizeof(folder)));
    CHECK_STR(server, "PC:4450");
    CHECK_STR(share, "Videos");
    CHECK_STR(folder, "Media/Movies");
    CHECK(davSmbParts("smb://PC/Videos/", server, sizeof(server), share, sizeof(share), folder, sizeof(folder)));
    CHECK_STR(folder, "");
    CHECK(!davSmbParts("http://PC/Videos/", server, sizeof(server), share, sizeof(share), folder, sizeof(folder)));
    CHECK(davSmbPath("smb://PC/Videos/", "dav:/", out, sizeof(out)));
    CHECK_STR(out, "");
    CHECK(davSmbPath("smb://PC/Videos/", "dav:/Movies/a b.mkv", out, sizeof(out)));
    CHECK_STR(out, "Movies/a b.mkv");
    CHECK(davSmbPath("smb://PC/Videos/Media/", "dav:/Movies/", out, sizeof(out)));
    CHECK_STR(out, "Media/Movies");
    CHECK(davSmbPath("smb://PC/Videos/Media/", "dav:/", out, sizeof(out)));
    CHECK_STR(out, "Media");
    char domain[32], name[32];
    davSplitUser("WORK\\kai", domain, sizeof(domain), name, sizeof(name));
    CHECK_STR(domain, "WORK");
    CHECK_STR(name, "kai");
    davSplitUser("kai@outlook.com", domain, sizeof(domain), name, sizeof(name));
    CHECK_STR(domain, "");
    CHECK_STR(name, "kai@outlook.com");

    davShownAddress("http://192.168.0.10:8098/", out, sizeof(out));
    CHECK_STR(out, "192.168.0.10:8098");
    davShownAddress("https://nas.local/dav/", out, sizeof(out));
    CHECK_STR(out, "https://nas.local/dav");
}

static void testUrls(void)
{
    char out[512];
    CHECK(davIsLocation("dav:/Movies/a.mkv"));
    CHECK(davIsLocation("dav:/"));
    CHECK(!davIsLocation("sdmc:/dav:/a.mkv"));
    CHECK(!davIsLocation("jellyfin:abc"));
    CHECK(davUrlOf("http://pc:8098/", "dav:/", out, sizeof(out)));
    CHECK_STR(out, "http://pc:8098/");
    CHECK(davUrlOf("http://pc:8098/dav/", "dav:/Kite Hill (2019)/Kite Hill #1 100%.mkv", out, sizeof(out)));
    CHECK_STR(out, "http://pc:8098/dav/Kite%20Hill%20%282019%29/Kite%20Hill%20%231%20100%25.mkv");
    CHECK(davUrlOf("http://pc/", "dav:/Pel\xc3\xad" "culas", out, sizeof(out)));
    CHECK_STR(out, "http://pc/Pel%C3%ADculas");
    CHECK(!davUrlOf("http://pc/", "sdmc:/a.mkv", out, sizeof(out)));
    CHECK(!davUrlOf("http://pc/", "dav:/abcdef", out, 18));

    DavAccount a = {"http://pc/", "kai", "pass:word"};
    CHECK(davAuthorization(&a, out, sizeof(out)));
    CHECK_STR(out, "Basic a2FpOnBhc3M6d29yZA==");
    DavAccount none = {"http://pc/", "", ""};
    CHECK(davAuthorization(&none, out, sizeof(out)));
    CHECK_STR(out, "");
}

static void testAccountFile(void)
{
    DavAccount a = {"http://pc:8098/", "kai", "se=cret"}, b;
    char text[1024];
    CHECK(davAccountWrite(&a, text, sizeof(text)));
    davAccountRead(&b, text);
    CHECK_STR(b.address, a.address);
    CHECK_STR(b.user, a.user);
    CHECK_STR(b.password, a.password);
    davAccountRead(&b, "address=http://x/\r\nuser=u\r\nnonsense\r\n");
    CHECK_STR(b.address, "http://x/");
    CHECK_STR(b.user, "u");
    CHECK_STR(b.password, "");
    davAccountRead(&b, "address=not an address\n");
    CHECK_STR(b.address, "");
    DavAccount bad = {"http://pc/", "line\nbreak", ""};
    CHECK(!davAccountWrite(&bad, text, sizeof(text)));
}

// What rclone answers, prefix D:, folder hrefs with '/', one file.
static const char RCLONE[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?><D:multistatus xmlns:D=\"DAV:\">"
    "<D:response><D:href>/Movies/</D:href><D:propstat><D:prop><D:resourcetype><D:collection xmlns:D=\"DAV:\"/>"
    "</D:resourcetype><D:displayname>Movies</D:displayname></D:prop><D:status>HTTP/1.1 200 OK</D:status>"
    "</D:propstat></D:response>"
    "<D:response><D:href>/Movies/Kite%20Hill%20(2019)/</D:href><D:propstat><D:prop><D:resourcetype>"
    "<D:collection xmlns:D=\"DAV:\"/></D:resourcetype></D:prop></D:propstat></D:response>"
    "<D:response><D:href>/Movies/Tom%20&amp;%20Jerry.mkv</D:href><D:propstat><D:prop><D:resourcetype>"
    "</D:resourcetype><D:getcontentlength>123456789012</D:getcontentlength></D:prop></D:propstat></D:response>"
    "</D:multistatus>";

// Apache and Nextcloud: prefix d: or lp1:, full URLs, a self without '/'.
static const char APACHE[] =
    "<?xml version=\"1.0\"?><d:multistatus xmlns:d=\"DAV:\" xmlns:lp1=\"DAV:\">"
    "<d:response><d:href>https://nas.local/dav/Series</d:href><d:propstat><d:prop>"
    "<lp1:resourcetype><d:collection/></lp1:resourcetype></d:prop></d:propstat></d:response>"
    "<d:response><d:href>https://nas.local/dav/Series/Rook%20S01E01.mp4</d:href><d:propstat><d:prop>"
    "<lp1:resourcetype/><lp1:getcontentlength>900</lp1:getcontentlength></d:prop></d:propstat></d:response>"
    "<d:response><d:href>https://nas.local/dav/Series/Season%202/</d:href><d:propstat><d:prop>"
    "<lp1:resourcetype><d:collection/></lp1:resourcetype></d:prop></d:propstat></d:response>"
    "</d:multistatus>";

static void testListing(void)
{
    DavEntry *e = NULL;
    size_t n = 0;
    CHECK(davParseListing(RCLONE, "http://pc:8098/Movies/", &e, &n));
    CHECK(n == 2);
    if (n == 2) {
        CHECK_STR(e[0].name, "Kite Hill (2019)");
        CHECK(e[0].is_dir);
        CHECK_STR(e[1].name, "Tom & Jerry.mkv");
        CHECK(!e[1].is_dir);
        CHECK(e[1].size == 123456789012LL);
    }
    free(e);
    e = NULL;
    CHECK(davParseListing(APACHE, "https://nas.local/dav/Series/", &e, &n));
    CHECK(n == 2);
    if (n == 2) {
        CHECK_STR(e[0].name, "Rook S01E01.mp4");
        CHECK(e[0].size == 900);
        CHECK_STR(e[1].name, "Season 2");
        CHECK(e[1].is_dir);
        CHECK(e[1].size == -1);
    }
    free(e);
    e = NULL;
    CHECK(davParseListing("<D:multistatus xmlns:D=\"DAV:\"></D:multistatus>", "http://pc/", &e, &n));
    CHECK(n == 0);
    free(e);
    e = NULL;
    CHECK(!davParseListing("<html>Not found</html>", "http://pc/", &e, &n));
    CHECK(e == NULL);
    // A name that would climb out of the folder is left out.
    CHECK(davParseListing("<D:multistatus><D:response><D:href>/a/..</D:href></D:response>"
                          "<D:response><D:href>/a/b%2Fc</D:href></D:response></D:multistatus>",
                          "http://pc/a/", &e, &n));
    CHECK(n == 0);
    free(e);
    e = NULL;
    // A lock's root href inside a response isn't its name; a name with a
    // line break or a NUL is left out.
    CHECK(davParseListing("<D:multistatus><D:response><D:href>/a/x.mkv</D:href><D:lockdiscovery><D:lockroot>"
                          "<D:href>/a/</D:href></D:lockroot></D:lockdiscovery></D:response>"
                          "<D:response><D:href>/a/b%0Ac.mkv</D:href></D:response>"
                          "<D:response><D:href>/a/b%00c.mkv</D:href></D:response></D:multistatus>",
                          "http://pc/a/", &e, &n));
    CHECK(n == 1 && !strcmp(e[0].name, "x.mkv"));
    free(e);
    e = NULL;
    // An href too long to read whole is left out, not cut into another name.
    static char longer[6000];
    char *w = longer + sprintf(longer, "<D:multistatus><D:response><D:href>/a/");
    for (int i = 0; i < 1500; i++)
        w += sprintf(w, "%%41");
    sprintf(w, ".mkv</D:href></D:response></D:multistatus>");
    CHECK(davParseListing(longer, "http://pc/a/", &e, &n) && n == 0);
    free(e);
}

static void testDates(void)
{
    CHECK(davParseDate("Sat, 21 Nov 2026 00:38:55 GMT") == 1795221535);
    CHECK(davParseDate("Thu, 01 Jan 1970 00:00:00 GMT") == 0);
    CHECK(davParseDate("Tue, 29 Feb 2028 12:00:00 GMT") == 1835438400);
    CHECK(davParseDate("2026-11-21T00:38:55Z") == 0);
    CHECK(davParseDate("Sat, 21 Foo 2026 00:38:55 GMT") == 0);
    CHECK(davParseDate("Sat, 21 Nov 2026 25:38:55 GMT") == 0);
    CHECK(davParseDate("") == 0);
    DavEntry *e = NULL;
    size_t n = 0;
    CHECK(davParseListing("<D:multistatus><D:response><D:href>/a/x.mkv</D:href><D:prop><D:getlastmodified>"
                          "Sat, 21 Nov 2026 00:38:55 GMT</D:getlastmodified></D:prop></D:response></D:multistatus>",
                          "http://pc/a/", &e, &n));
    CHECK(n == 1 && e[0].modified == 1795221535);
    free(e);
}

void testDav(void)
{
    testDates();
    testAddresses();
    testUrls();
    testAccountFile();
    testListing();
}
