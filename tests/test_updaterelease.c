// GitHub's answer about the latest release, read for an update (updaterelease.h).
#include <stdio.h>
#include <string.h>

#include "check.h"
#include "updaterelease.h"

// The shape of GitHub's answer (trimmed). extra adds "draft" or "prerelease" (each key once:
// JSON readers differ on a key given twice).
#define RELEASE(tag, extra, asset_name, url, digest, size)                                                    \
    "{\"url\":\"https://api.github.com/repos/mizuiroame/kaivideo/releases/1\",\"tag_name\":\"" tag "\","       \
    "\"name\":\"KaiVideo\"" extra ",\"assets\":["                        \
    "{\"name\":\"notes.txt\",\"size\":10,\"digest\":null,\"browser_download_url\":\"https://example.com/x\"}," \
    "{\"name\":\"" asset_name "\",\"content_type\":\"application/octet-stream\",\"size\":" size              \
    ",\"digest\":\"" digest "\",\"browser_download_url\":\"" url "\"}],\"body\":\"What's new...\"}"

#define URL "https://github.com/mizuiroame/kaivideo/releases/download/v1.3.0/KaiVideo.nro"
#define DIGEST "sha256:682ba5d410544f2d9bc1558a5239cb8c94af874825528fd3862a06b60e3ada3f"

static void testGood(void)
{
    UpdateRelease r;
    CHECK(updateReadRelease(RELEASE("v1.3.0", "", "KaiVideo.nro", URL, DIGEST, "21000000"), "kaivideo",
                            "KaiVideo.nro", &r));
    CHECK(strcmp(r.tag, "v1.3.0") == 0 && strcmp(r.url, URL) == 0 && r.size == 21000000);
    CHECK(strcmp(r.sha256, "682ba5d410544f2d9bc1558a5239cb8c94af874825528fd3862a06b60e3ada3f") == 0);
}

static void testRefused(void)
{
    UpdateRelease r;
    // Another host, another file's address, no digest, a pre-release, a draft, sizes out of bounds.
    CHECK(!updateReadRelease(RELEASE("v1.3.0", "", "KaiVideo.nro",
                                     "https://evil.example/KaiVideo.nro", DIGEST, "1000"),
                             "kaivideo", "KaiVideo.nro", &r));
    CHECK(!updateReadRelease(RELEASE("v1.3.0", "", "KaiVideo.nro",
                                     "https://github.com/mizuiroame/kaivideo/releases/download/v1.2.9/KaiVideo.nro",
                                     DIGEST, "1000"),
                             "kaivideo", "KaiVideo.nro", &r));
    CHECK(!updateReadRelease(RELEASE("v1.3.0", "", "KaiVideo.nro", URL, "", "1000"), "kaivideo", "KaiVideo.nro",
                             &r));
    CHECK(!updateReadRelease(RELEASE("v1.3.0", ",\"prerelease\":true", "KaiVideo.nro", URL, DIGEST, "1000"),
                             "kaivideo", "KaiVideo.nro", &r));
    CHECK(!updateReadRelease(RELEASE("v1.3.0", ",\"draft\":true", "KaiVideo.nro", URL, DIGEST, "1000"), "kaivideo",
                             "KaiVideo.nro", &r));
    CHECK(!updateReadRelease(RELEASE("v1.3.0", "", "KaiVideo.nro", URL, DIGEST, "0"), "kaivideo", "KaiVideo.nro", &r));
    CHECK(!updateReadRelease(RELEASE("v1.3.0", "", "KaiVideo.nro", URL, DIGEST, "1e12"), "kaivideo", "KaiVideo.nro",
                             &r));
    // No such file in it, a tag that isn't x.y.z, not JSON at all.
    CHECK(!updateReadRelease(RELEASE("v1.3.0", "", "Other.nro", URL, DIGEST, "1000"), "kaivideo", "KaiVideo.nro", &r));
    CHECK(!updateReadRelease(RELEASE("nightly", "", "KaiVideo.nro", URL, DIGEST, "1000"), "kaivideo", "KaiVideo.nro",
                             &r));
    CHECK(!updateReadRelease("<html>rate limited</html>", "kaivideo", "KaiVideo.nro", &r));
    CHECK(!updateReadRelease(NULL, "kaivideo", "KaiVideo.nro", &r));
    CHECK(r.tag[0] == '\0' && r.url[0] == '\0');  // Nothing kept from a refused answer.
}

void testUpdateRelease(void)
{
    testGood();
    testRefused();
}
