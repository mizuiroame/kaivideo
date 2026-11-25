// Each source's own Certificate check (made-up addresses only).
#include "certtrust.h"
#include "check.h"

static void testSameServer(void)
{
    CHECK(trustSameServer("https://nas.example:8920", "https://NAS.example:8920/Items?x=1"));
    CHECK(trustSameServer("https://nas.example", "https://nas.example:443/web"));  // The scheme's port.
    CHECK(!trustSameServer("https://nas.example", "https://nas.example:8920"));
    CHECK(!trustSameServer("https://nas.example", "http://nas.example"));          // 443 and 80.
    CHECK(!trustSameServer("https://nas.example", "https://nas.example.evil.example"));
    CHECK(trustSameServer("ftps://nas.example", "ftps://user@nas.example:990/Films"));
    CHECK(trustSameServer("https://[fd00::5]:8096", "https://[FD00::5]:8096/"));
    CHECK(!trustSameServer("nas.example", "nas.example"));  // No scheme: no address.
    CHECK(!trustSameServer("https://nas.example:99999", "https://nas.example:99999"));
    CHECK(!trustSameServer("https://:8096", "https://:8096"));
    CHECK(!trustSameServer(NULL, "https://nas.example"));
}

static void testChecks(void)
{
    CHECK(trustChecks("https://nas.example:8920/"));  // Nothing set: every one is checked.
    trustSetAddress(TRUST_JELLYFIN, "https://nas.example:8920");
    trustSetAddress(TRUST_EMBY, "https://emby.example");
    trustSetAddress(TRUST_NETWORK, "ftps://files.example");
    CHECK(trustChecks("https://nas.example:8920/Items"));
    trustSetChecked(TRUST_JELLYFIN, false);
    CHECK(!trustChecks("https://nas.example:8920/Items"));       // Jellyfin's own server.
    CHECK(trustChecks("https://emby.example/emby"));             // Emby's is still checked,
    CHECK(trustChecks("https://cdn.example/live.m3u8"));         // and so is any other host,
    CHECK(trustChecks("https://nas.example/"));                  // or another port on the same one.
    CHECK(trustChecks("ftps://files.example/Films"));
    trustSetChecked(TRUST_NETWORK, false);
    CHECK(!trustChecks("ftps://files.example/Films"));
    trustSetAddress(TRUST_JELLYFIN, "");  // Signed out of it: nothing of Jellyfin's left unchecked.
    CHECK(trustChecks("https://nas.example:8920/Items"));
    trustSetChecked(TRUST_JELLYFIN, true);
    trustSetChecked(TRUST_NETWORK, true);
    trustSetAddress(TRUST_EMBY, NULL);
    trustSetAddress(TRUST_NETWORK, NULL);
    trustSetChecked(TRUST_COUNT, false);  // Not a source: nothing.
    CHECK(trustChecks("https://emby.example/"));
}

void testCertTrust(void)
{
    testSameServer();
    testChecks();
}
