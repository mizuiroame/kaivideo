// The network folder over SMB against a real share (tests/host_live):
//   SMB_ADDRESS='\\host:4450\Videos' SMB_USER=... SMB_PASS=... ./host_smb
// Tests the account, lists a folder, reads a video's first and last blocks,
// and checks that a wrong password, a wrong share and nobody there each say so.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "davnet.h"
#include "net.h"
#include "smbnet.h"

static int g_failed;

#define EXPECT(cond, ...)                 \
    do {                                  \
        if (!(cond)) {                    \
            g_failed++;                   \
            printf("FAIL: " __VA_ARGS__); \
            printf("\n");                 \
        }                                 \
    } while (0)

static void use(const char *address, const char *user, const char *pass)
{
    DavAccount a = {0};
    EXPECT(davNormalizeAddress(address, a.address, sizeof(a.address)), "address %s", address);
    snprintf(a.user, sizeof(a.user), "%s", user);
    snprintf(a.password, sizeof(a.password), "%s", pass);
    davUseAccount(&a);
    smbForget();
}

static void readVideo(const char *location)
{
    char why[256];
    SmbFile *f = davSmbOpen(location, why, sizeof(why));
    EXPECT(f, "open %s: %s", location, why);
    if (!f)
        return;
    int64_t size = smbFileSize(f);
    static uint8_t block[4 << 20];
    int64_t n = smbFileRead(f, 0, block, sizeof(block), NULL);
    EXPECT(n == (int64_t)sizeof(block) || n == size, "first block: %lld of %lld", (long long)n, (long long)size);
    printf("  %s: %lld bytes, first block %lld, starts %02x%02x%02x%02x\n", location, (long long)size, (long long)n,
           block[0], block[1], block[2], block[3]);
    n = smbFileRead(f, size - 1000, block, sizeof(block), NULL);
    EXPECT(n == 1000, "last block: %lld", (long long)n);
    EXPECT(smbFileRead(f, size, block, 10, NULL) == 0, "past the end");
    smbFileClose(f);
}

int main(void)
{
    const char *address = getenv("SMB_ADDRESS"), *user = getenv("SMB_USER"), *pass = getenv("SMB_PASS");
    if (!address || !user || !pass || !netInit())
        return 2;
    use(address, user, pass);
    char text[256];
    EXPECT(davTest(text, sizeof(text)), "test: %s", text);
    printf("test: %s\n", text);
    DavEntry *e;
    size_t n;
    EXPECT(davList("dav:/Movies", &e, &n, text, sizeof(text)) && n > 0, "Movies: %s", text);
    for (size_t i = 0; i < n; i++)
        printf("  %s%s %lld\n", e[i].name, e[i].is_dir ? "/" : "", (long long)e[i].modified);
    free(e);
    EXPECT(davList("dav:/Movies/Paper Lanterns (1998)", &e, &n, text, sizeof(text)) && n == 1, "a film's folder: %s",
           text);
    free(e);
    readVideo("dav:/Movies/Paper Lanterns (1998)/Paper Lanterns (1998).mkv");
    EXPECT(!davList("dav:/Not a folder", &e, &n, text, sizeof(text)), "a missing folder listed");
    printf("missing folder: %s\n", text);

    use(address, user, "wrong");
    EXPECT(!davTest(text, sizeof(text)) && strstr(text, "password"), "wrong password: %s", text);
    printf("wrong password: %s\n", text);
    char other[512];
    snprintf(other, sizeof(other), "%s", address);
    char *last = strrchr(other, '\\') ? strrchr(other, '\\') : strrchr(other, '/');
    if (last)
        snprintf(last + 1, sizeof(other) - (size_t)(last + 1 - other), "NoSuchShare");
    use(other, user, pass);
    EXPECT(!davTest(text, sizeof(text)), "a missing share answered");
    printf("wrong share: %s\n", text);
    use("\\\\127.0.0.1:1\\Videos", user, pass);
    EXPECT(!davTest(text, sizeof(text)), "nobody there answered");
    printf("nobody there: %s\n", text);
    smbForget();
    netExit();
    printf(g_failed ? "%d FAILED\n" : "all good\n", g_failed);
    return g_failed ? 1 : 0;
}
