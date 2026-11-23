#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "dav.h"
#include "md4.h"

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
    CHECK(!davNormalizeAddress("gopher://pc", out, sizeof(out)));
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

    // FTP and SFTP.
    CHECK(davNormalizeAddress("ftp://nas/Videos", out, sizeof(out)));
    CHECK_STR(out, "ftp://nas/Videos/");
    CHECK(davProtocolOf(out) == DAV_FTP);
    CHECK(davNormalizeAddress("FTPES://nas:2121", out, sizeof(out)) && davProtocolOf(out) == DAV_FTP);
    CHECK(davNormalizeAddress("ftps://nas/", out, sizeof(out)) && davProtocolOf(out) == DAV_FTP);
    CHECK(davNormalizeAddress("sftp://[fe80::1]:2222/media", out, sizeof(out)));
    CHECK_STR(out, "sftp://[fe80::1]:2222/media/");
    CHECK(davProtocolOf(out) == DAV_SFTP);
    CHECK(davProtocolOf("smb://PC/V/") == DAV_SMB && davProtocolOf("https://h/") == DAV_WEBDAV);
    CHECK(davProtocolOf("") == DAV_NONE && davProtocolOf("gopher://h/") == DAV_NONE);
    CHECK(!davNormalizeAddress("sftp://me:pw@nas/", out, sizeof(out)));
    CHECK(!davNormalizeAddress("ftp://nas/My Videos", out, sizeof(out)));
    CHECK(!davNormalizeAddress("sftp://", out, sizeof(out)));
    CHECK(davSameServer("sftp://nas:22/a/", "SFTP://NAS:22/b/") && !davSameServer("ftp://nas/", "sftp://nas/"));

    char host[64];
    int port = 0;
    CHECK(davHostPort("sftp://nas/media/", host, sizeof(host), &port) && !strcmp(host, "nas") && port == 22);
    CHECK(davHostPort("ftp://10.0.0.2:2121/", host, sizeof(host), &port) && !strcmp(host, "10.0.0.2") && port == 2121);
    CHECK(davHostPort("ftps://nas/", host, sizeof(host), &port) && port == 990);
    CHECK(davHostPort("ftpes://nas/", host, sizeof(host), &port) && port == 21);
    CHECK(davHostPort("sftp://[fe80::1]:2222/", host, sizeof(host), &port) && !strcmp(host, "[fe80::1]") &&
          port == 2222);
    CHECK(!davHostPort("sftp://nas:99999/", host, sizeof(host), &port));
    CHECK(!davHostPort("sftp://nas:22x/", host, sizeof(host), &port));
    CHECK(!davHostPort("http://nas/", host, sizeof(host), &port));
    CHECK(davServerPath("sftp://nas/media/", "dav:/A B/c.mkv", out, sizeof(out)));
    CHECK_STR(out, "/media/A B/c.mkv");
    CHECK(davServerPath("sftp://nas/media/", "dav:/", out, sizeof(out)));
    CHECK_STR(out, "/media/");
    CHECK(davServerPath("ftp://nas/", "dav:/Movies", out, sizeof(out)));
    CHECK_STR(out, "/Movies");
    CHECK(!davServerPath("smb://PC/V/", "dav:/a", out, sizeof(out)));
    CHECK(!davServerPath("sftp://nas/", "sdmc:/a", out, sizeof(out)));

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

    DavAccount a = {.address = "http://pc/", .user = "kai", .password = "pass:word"};
    CHECK(davAuthorization(&a, out, sizeof(out)));
    CHECK_STR(out, "Basic a2FpOnBhc3M6d29yZA==");
    DavAccount none = {.address = "http://pc/", .user = "", .password = ""};
    CHECK(davAuthorization(&none, out, sizeof(out)));
    CHECK_STR(out, "");
}

static void testMd4(void)
{
    // RFC 1320's test suite.
    static const struct {
        const char *text, *hex;
    } CASES[] = {
        {"", "31d6cfe0d16ae931b73c59d7e0c089c0"},
        {"abc", "a448017aaf21d8525fc10ae87aa6729d"},
        {"message digest", "d9130a8164549fe818874806e1c7014b"},
        {"12345678901234567890123456789012345678901234567890123456789012345678901234567890",
         "e33b4ddc9c38f2199c3e7b164fcc0536"},
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++) {
        uint8_t digest[MD4_DIGEST_SIZE];
        char hex[2 * MD4_DIGEST_SIZE + 1];
        md4(CASES[i].text, strlen(CASES[i].text), digest);
        for (int k = 0; k < MD4_DIGEST_SIZE; k++)
            snprintf(hex + 2 * k, 3, "%02x", digest[k]);
        CHECK_STR(hex, CASES[i].hex);
    }
}

static void testNtHash(void)
{
    char hash[DAV_NT_HASH_SIZE];
    CHECK(davNtHash("password", hash) && strcmp(hash, "ntlm:8846F7EAEE8FB117AD06BDD830B7586C") == 0);
    // Outside ASCII it hashes the UTF-16 the server does: "contraseña", and a character past U+FFFF.
    CHECK(davNtHash("contrase\xC3\xB1" "a", hash) && strcmp(hash, "ntlm:305A42A96D4DF77C1F0434F63A28239A") == 0);
    CHECK(davNtHash("\xF0\x9F\x8E\xB5", hash) && strcmp(hash, "ntlm:F76B2FD531F187BC8854CB2045241955") == 0);
    CHECK(!davNtHash("bad \xC3", hash) && !davNtHash("\xED\xA0\x80", hash));  // Cut short; a lone surrogate.
}

static void testAccountFile(void)
{
    DavAccount a = {.address = "http://pc:8098/", .user = "kai", .password = "se=cret"}, b;
    char text[1024];
    CHECK(davAccountWrite(&a, text, sizeof(text)));
    CHECK(strstr(text, "se=cret") == NULL);  // Not as typed.
    CHECK(!davAccountRead(&b, text));        // Nothing to write again.
    CHECK_STR(b.address, a.address);
    CHECK_STR(b.user, a.user);
    CHECK_STR(b.password, a.password);
    CHECK(!b.forget_password && !b.allow_unsigned);
    CHECK(!davAccountRead(&b, "address=http://x/\r\nuser=u\r\nnonsense\r\n"));
    CHECK_STR(b.address, "http://x/");
    CHECK_STR(b.user, "u");
    CHECK_STR(b.password, "");
    davAccountRead(&b, "address=not an address\n");
    CHECK_STR(b.address, "");
    DavAccount bad = {.address = "http://pc/", .user = "line\nbreak", .password = ""};
    CHECK(!davAccountWrite(&bad, text, sizeof(text)));

    // SFTP: the password encoded (it's sent as it is), and the server's key's fingerprint kept.
    DavAccount s = {.address = "sftp://nas/media/", .user = "kai", .password = "pa ss",
                    .host_key = "SHA256:AbCdEf0123456789+/xyz="};
    CHECK(davAccountWrite(&s, text, sizeof(text)));
    CHECK(strstr(text, "pa ss") == NULL && strstr(text, "host_key=SHA256:AbCdEf0123456789+/xyz=\n"));
    CHECK(!davAccountRead(&b, text));
    CHECK_STR(b.password, "pa ss");
    CHECK_STR(b.host_key, s.host_key);
    CHECK(davAccountWrite(&a, text, sizeof(text)) && !strstr(text, "host_key"));  // None: no line.
    DavAccount bad_key = {.address = "sftp://nas/", .host_key = "SHA256:x\ny"};
    CHECK(!davAccountWrite(&bad_key, text, sizeof(text)));
}

// A file from before: its password as typed is read, and the file is to be written again.
static void testOldAccountFiles(void)
{
    DavAccount a;
    char text[1024];
    CHECK(davAccountRead(&a, "address=http://pc/\nuser=kai\npassword=plain\n"));
    CHECK_STR(a.password, "plain");
    CHECK(davAccountWrite(&a, text, sizeof(text)) && !strstr(text, "plain"));

    // SMB: the password becomes its NT hash at once.
    CHECK(davAccountRead(&a, "address=smb://PC/Music/\nuser=kai\npassword=password\n"));
    CHECK_STR(a.password, "ntlm:8846F7EAEE8FB117AD06BDD830B7586C");
    CHECK(davAccountWrite(&a, text, sizeof(text)) && !strstr(text, "=password"));
    CHECK(!davAccountRead(&a, text));
    CHECK_STR(a.password, "ntlm:8846F7EAEE8FB117AD06BDD830B7586C");

    // A WebDAV address can't use a hash: dropped.
    CHECK(davAccountRead(&a, "address=http://pc/\nuser=kai\npassword=ntlm:8846F7EAEE8FB117AD06BDD830B7586C\n"));
    CHECK_STR(a.password, "");
    // A damaged encoding: no password, rather than a wrong one.
    CHECK(davAccountRead(&a, "address=http://pc/\nuser=kai\npassword=b64:%%%\n"));
    CHECK_STR(a.password, "");
}

static void testAccountChoices(void)
{
    DavAccount a = {.address = "smb://NAS/Music/", .user = "kai", .forget_password = true, .allow_unsigned = true}, b;
    CHECK(davSetPassword(&a, "password"));
    CHECK_STR(a.password, "ntlm:8846F7EAEE8FB117AD06BDD830B7586C");
    char text[1024];
    CHECK(davAccountWrite(&a, text, sizeof(text)));
    CHECK(!strstr(text, "ntlm:"));  // Not remembered: not on the card in any form.
    CHECK(!davAccountRead(&b, text));
    CHECK(b.forget_password && b.allow_unsigned && b.password[0] == '\0');
    CHECK_STR(b.user, "kai");
    // A file not to remember that still has one: dropped, and written again.
    CHECK(davAccountRead(&b, "address=http://pc/\nuser=u\npassword=b64:cA==\nforget_password=1\n"));
    CHECK_STR(b.password, "");

    DavAccount web = {.address = "http://pc/", .user = "kai"};
    CHECK(davSetPassword(&web, "as typed") && strcmp(web.password, "as typed") == 0);  // Basic needs it.
    CHECK(!davSetPassword(&a, "bad \xC3"));
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
    testMd4();
    testNtHash();
    testAccountFile();
    testOldAccountFiles();
    testAccountChoices();
    testListing();
}
