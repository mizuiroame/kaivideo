// The update check's decisions (update.h). The same file is in KaiPlayer, KaiRadio and KaiVideo.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "check.h"
#include "update.h"

static void testPlaces(void)
{
    CHECK(updatePlaceOf("sdmc:/brewstore/apps/kairadio/KaiRadio.nro") == UPDATE_PLACE_BREWSTORE);
    CHECK(updatePlaceOf("sdmc:/switch/kairadio/KaiRadio.nro") == UPDATE_PLACE_LOOSE);
    CHECK(updatePlaceOf("sdmc:/switch/KaiRadio.nro") == UPDATE_PLACE_LOOSE);
    CHECK(updatePlaceOf("sdmc:/games/KaiRadio.nro") == UPDATE_PLACE_LOOSE);  // Anywhere else on the card.
    CHECK(updatePlaceOf("") == UPDATE_PLACE_UNKNOWN && updatePlaceOf(NULL) == UPDATE_PLACE_UNKNOWN);
    CHECK(updatePlaceOf("romfs:/x.nro") == UPDATE_PLACE_UNKNOWN);   // Not a file it can replace.
    CHECK(updatePlaceOf("sdmc:/switch/KaiRadio") == UPDATE_PLACE_UNKNOWN);  // Not an .nro.
    CHECK(updatePlaceOf("sdmc:/brewstore/apps/../switch/KaiRadio.nro") == UPDATE_PLACE_UNKNOWN);
}

static void testDue(void)
{
    CHECK(updateDue(0, 1795365403));
    CHECK(!updateDue(1795365403, 1795365403 + 3600));
    CHECK(updateDue(1795365403, 1795365403 + UPDATE_EVERY_S));
    CHECK(updateDue(1795365403, 1795365403 - 10));  // The clock went back: ask again.
}

static void testVersions(void)
{
    CHECK(updateIsNewer("v1.13.0", "1.12.0") && updateIsNewer("1.12.1", "1.12.0"));
    CHECK(updateIsNewer("v2.0.0", "1.99.99") && updateIsNewer("v1.10.0", "1.9.0"));  // Numbers, not text.
    CHECK(!updateIsNewer("v1.12.0", "1.12.0") && !updateIsNewer("v1.11.5", "1.12.0"));
    CHECK(!updateIsNewer("v2.0.0-rc1", "1.12.0"));  // Pre-releases aren't offered.
    CHECK(!updateIsNewer("v1.13", "1.12.0") && !updateIsNewer("latest", "1.12.0") && !updateIsNewer("", "1.0.0"));
    CHECK(!updateIsNewer("v1.13.0", "test"));  // A build without a version: never offered one.
    CHECK(!updateIsNewer("v99999999999.0.0", "1.0.0"));  // Absurd numbers are refused.
}

static void testDigest(void)
{
    char hex[UPDATE_HEX_SIZE];
    const char *good = "sha256:79105E3BCC55447B46829322E4C4D9223AC564DC78FCAD7A41B1AC4784243309";
    CHECK(updateDigest(good, hex) && strcmp(hex, "79105e3bcc55447b46829322e4c4d9223ac564dc78fcad7a41b1ac4784243309") == 0);
    CHECK(!updateDigest("sha1:79105e3bcc55447b46829322e4c4d9223ac564dc", hex));
    CHECK(!updateDigest("sha256:79105e3b", hex) && !updateDigest("", hex) && !updateDigest(NULL, hex));
    CHECK(!updateDigest("sha256:79105e3bcc55447b46829322e4c4d9223ac564dc78fcad7a41b1ac4784243309ff", hex));
    CHECK(!updateDigest("sha256:79105e3bcc55447b46829322e4c4d9223ac564dc78fcad7a41b1ac47842433zz", hex));
}

static void testUrls(void)
{
    const char *url = "https://github.com/mizuiroame/kairadio/releases/download/v1.2.1/KaiRadio.nro";
    CHECK(updateUrlOk(url, "kairadio", "v1.2.1", "KaiRadio.nro"));
    CHECK(!updateUrlOk(url, "kairadio", "v1.2.2", "KaiRadio.nro"));  // Another tag.
    CHECK(!updateUrlOk(url, "kaivideo", "v1.2.1", "KaiRadio.nro"));  // Another app.
    CHECK(!updateUrlOk("http://github.com/mizuiroame/kairadio/releases/download/v1.2.1/KaiRadio.nro", "kairadio",
                       "v1.2.1", "KaiRadio.nro"));  // Not https.
    CHECK(!updateUrlOk("https://github.com.evil.example/mizuiroame/kairadio/releases/download/v1.2.1/KaiRadio.nro",
                       "kairadio", "v1.2.1", "KaiRadio.nro"));
    CHECK(!updateUrlOk("https://github.com/someone/kairadio/releases/download/v1.2.1/KaiRadio.nro", "kairadio",
                       "v1.2.1", "KaiRadio.nro"));
    CHECK(!updateUrlOk(NULL, "kairadio", "v1.2.1", "KaiRadio.nro"));
    CHECK(!updateUrlOk("https://github.com/mizuiroame/kairadio/releases/download/v1.2.1/../x/KaiRadio.nro",
                       "kairadio", "v1.2.1", "KaiRadio.nro"));
}

static void testPaths(void)
{
    char path[UPDATE_PATH_MAX];
    CHECK(updateNewPath("sdmc:/switch/kairadio/KaiRadio.nro", path, sizeof(path)) &&
          strcmp(path, "sdmc:/switch/kairadio/KaiRadio.nro.new") == 0);
    CHECK(updateOldPath("sdmc:/switch/kairadio/KaiRadio.nro", path, sizeof(path)) &&
          strcmp(path, "sdmc:/switch/kairadio/KaiRadio.nro.old") == 0);
    char small[8];
    CHECK(!updateNewPath("sdmc:/switch/kairadio/KaiRadio.nro", small, sizeof(small)));
}

static void writeText(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

static bool hasText(const char *path, const char *text)
{
    char buf[64] = "";
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    return strcmp(buf, text) == 0;
}

static void testSwap(void)
{
    char dir[] = "/tmp/kai_update_XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char nro[256], fresh[256], old[256];
    snprintf(nro, sizeof(nro), "%s/App.nro", dir);
    CHECK(updateNewPath(nro, fresh, sizeof(fresh)) && updateOldPath(nro, old, sizeof(old)));
    writeText(nro, "old version");
    writeText(fresh, "new version");
    writeText(old, "left from a stopped swap");  // Removed first.
    CHECK(updateSwap(nro, fresh));
    CHECK(hasText(nro, "new version") && access(fresh, F_OK) != 0 && access(old, F_OK) != 0);

    CHECK(!updateSwap(nro, fresh));  // Nothing downloaded: nothing changes.
    CHECK(hasText(nro, "new version"));
    remove(nro);
    rmdir(dir);
}

void testUpdate(void)
{
    testPlaces();
    testDue();
    testVersions();
    testDigest();
    testUrls();
    testPaths();
    testSwap();
}
