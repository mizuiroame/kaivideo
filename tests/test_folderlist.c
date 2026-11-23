// The network folder's FTP and web index listings, made-up names only.
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "folderlist.h"

#define NOV_23_2026 1795392000LL  // 2026-11-23 00:00:00 UTC.

static const DavEntry *find(const DavEntry *e, size_t n, const char *name)
{
    for (size_t i = 0; i < n; i++)
        if (!strcmp(e[i].name, name))
            return &e[i];
    return NULL;
}

static void testMlsd(void)
{
    const char *text = "type=cdir;modify=20261120101010; .\r\n"
                       "type=pdir;modify=20261120101010; ..\r\n"
                       "type=dir;modify=20261121003855;perm=flcdmpe; Paper Boats (2024)\r\n"
                       "type=file;size=1234567;modify=20261121003855.123;perm=r; Kites S01E02.mkv\r\n"
                       "Type=File;Size=10;Modify=20261121003855; spaces  in name.mp4\r\n"
                       "type=OS.unix=slink:/x;size=3; link\r\n"
                       "type=file;size=5; bad\x01name.mkv\r\n"
                       "type=file;size=5; sub/dir.mkv\r\n"
                       "garbage line\r\n";
    DavEntry *e;
    size_t n;
    CHECK(folderParseMlsd(text, &e, &n));
    CHECK(n == 3);
    const DavEntry *d = find(e, n, "Paper Boats (2024)");
    CHECK(d && d->is_dir && d->size == -1 && d->modified == NOV_23_2026 - 2 * 86400 + 38 * 60 + 55);
    const DavEntry *f = find(e, n, "Kites S01E02.mkv");
    CHECK(f && !f->is_dir && f->size == 1234567 && f->modified == d->modified);
    CHECK(find(e, n, "spaces  in name.mp4") != NULL);  // Spaces inside a name are kept.
    free(e);
    CHECK(folderParseMlsd("", &e, &n) && n == 0);  // An empty folder.
    free(e);
    CHECK(!folderParseMlsd("230 Not a listing at all\r\n", &e, &n));
}

static void testUnixList(void)
{
    const char *text = "total 12\r\n"
                       "drwxr-xr-x    2 ftp      ftp          4096 Nov 21 00:38 Paper Boats (2024)\r\n"
                       "-rw-r--r--    1 ftp      ftp       1234567 Nov 21 00:38 Kites S01E02.mkv\r\n"
                       "-rw-r--r--    1 1000     1000           42 Jan  2  2025 old one.mp4\r\n"
                       "-rw-r--r--    1 ftp            7 Dec 30 23:59 no group.mkv\r\n"
                       "lrwxrwxrwx    1 ftp      ftp            9 Nov 20 10:00 latest -> Kites S01E02.mkv\r\n"
                       "drwxr-xr-x    2 ftp      ftp          4096 Nov 21 00:38 .\r\n"
                       "drwxr-xr-x    2 ftp      ftp          4096 Nov 21 00:38 ..\r\n";
    DavEntry *e;
    size_t n;
    CHECK(folderParseFtpList(text, NOV_23_2026, &e, &n));
    CHECK(n == 5);
    const DavEntry *d = find(e, n, "Paper Boats (2024)");
    CHECK(d && d->is_dir && d->modified == NOV_23_2026 - 2 * 86400 + 38 * 60);
    const DavEntry *f = find(e, n, "Kites S01E02.mkv");
    CHECK(f && !f->is_dir && f->size == 1234567);
    const DavEntry *old = find(e, n, "old one.mp4");
    CHECK(old && old->size == 42 && old->modified > 0 && old->modified < NOV_23_2026 - 300 * 86400);
    const DavEntry *last_year = find(e, n, "no group.mkv");  // Dec 30 with a time, seen in November: last year's.
    CHECK(last_year && last_year->size == 7 && last_year->modified < NOV_23_2026 && last_year->modified > 0);
    CHECK(find(e, n, "latest") && !find(e, n, "latest -> Kites S01E02.mkv"));
    free(e);
}

static void testDosList(void)
{
    const char *text = "11-21-26  12:38AM       <DIR>          Paper Boats (2024)\r\n"
                       "11-21-26  01:05PM              1234567 Kites S01E02.mkv\r\n";
    DavEntry *e;
    size_t n;
    CHECK(folderParseFtpList(text, NOV_23_2026, &e, &n) && n == 2);
    const DavEntry *d = find(e, n, "Paper Boats (2024)");
    CHECK(d && d->is_dir && d->modified == NOV_23_2026 - 2 * 86400 + 38 * 60);
    const DavEntry *f = find(e, n, "Kites S01E02.mkv");
    CHECK(f && f->size == 1234567 && f->modified == NOV_23_2026 - 2 * 86400 + 13 * 3600 + 5 * 60);
    free(e);
    CHECK(folderParseFtpList("", NOV_23_2026, &e, &n) && n == 0);
    free(e);
    CHECK(!folderParseFtpList("<html>not a listing</html>\r\n", NOV_23_2026, &e, &n));
}

static void testHtmlIndex(void)
{
    const char *html = "<html><head><title>Index of /media/</title></head><body>"
                       "<a href=\"?C=N;O=D\">Name</a> <a href=\"/\">Parent Directory</a>"
                       "<a href=\"../\">../</a>"
                       "<a href=\"Paper%20Boats%20(2024)/\">Paper Boats (2024)/</a>"
                       "<a HREF='Kites%20S01E02.mkv'>Kites S01E02.mkv</a>"
                       "<a href=Plain.mp4>Plain</a>"
                       "<a href=\"Tom&amp;Jerry.mkv\">Tom&amp;Jerry.mkv</a>"
                       "<a href=\"/media/Absolute.mkv\">abs</a>"
                       "<a href=\"/elsewhere/Other.mkv\">other</a>"
                       "<a href=\"http://nas:8080/media/Full.mkv\">full</a>"
                       "<a href=\"http://evil.example/media/x.mkv\">evil</a>"
                       "<a href=\"Deeper/inside.mkv\">deeper</a>"
                       "<a href=\"a%2Fb.mkv\">slash</a>"
                       "<a href=\"Kites%20S01E02.mkv\">again</a>"
                       "<a href=\"#top\">top</a><a href=\"mailto:x@y\">mail</a>"
                       "</body></html>";
    DavEntry *e;
    size_t n;
    CHECK(folderParseHtmlIndex(html, "http://nas:8080/media/", &e, &n));
    CHECK(n == 6);
    const DavEntry *d = find(e, n, "Paper Boats (2024)");
    CHECK(d && d->is_dir && d->size == -1 && d->modified == 0);
    CHECK(find(e, n, "Kites S01E02.mkv") && !find(e, n, "Kites S01E02.mkv")->is_dir);
    CHECK(find(e, n, "Plain.mp4") && find(e, n, "Tom&Jerry.mkv"));
    CHECK(find(e, n, "Absolute.mkv") && find(e, n, "Full.mkv"));
    CHECK(!find(e, n, "Other.mkv") && !find(e, n, "x.mkv") && !find(e, n, "inside.mkv") && !find(e, n, "a/b.mkv"));
    free(e);
    CHECK(!folderParseHtmlIndex("just text", "http://nas/", &e, &n));
    CHECK(folderParseHtmlIndex("<a href=\"../\">up</a>", "http://nas/x/", &e, &n) && n == 0);
    free(e);
}

void testFolderList(void)
{
    testMlsd();
    testUnixList();
    testDosList();
    testHtmlIndex();
}
