// Looking for a newer version of the app on GitHub, and what to do with it:
// pure decisions, tested on the PC. The asking (the network, the JSON) and
// the screens are each app's. The same file is in KaiPlayer, KaiRadio and
// KaiVideo.
//
// Where the app runs decides what an update can do:
//   sdmc:/brewstore/apps/<app>/<App>.nro: Brewstore updates it; the app only
//     says a new version is there.
//   sdmc:/switch/...: the app can download the new .nro, check its SHA-256
//     against GitHub's, and put it in place of the old one.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UPDATE_OWNER "mizuiroame"
#define UPDATE_EVERY_S (24 * 60 * 60)  // At most once a day.
#define UPDATE_HEX_SIZE 65             // A SHA-256 in hex, and the NUL.
#define UPDATE_TAG_MAX 32
#define UPDATE_URL_MAX 512
#define UPDATE_PATH_MAX 512

typedef enum {
    UPDATE_PLACE_UNKNOWN,   // No usable path: only say a new version is there.
    UPDATE_PLACE_BREWSTORE, // Brewstore updates it.
    UPDATE_PLACE_LOOSE,     // In sdmc:/switch/: the app can update itself.
} UpdatePlace;

// Where the NRO at nro_path ("sdmc:/switch/kairadio/KaiRadio.nro") runs from.
UpdatePlace updatePlaceOf(const char *nro_path);

// True when a check is due: never done (last 0), a day since, or the clock went back.
bool updateDue(int64_t last_check, int64_t now);

// True when tag ("v1.13.0" or "1.13.0") is a newer release than current
// ("1.12.0"). Pre-releases ("v2.0.0-rc1") and tags that aren't x.y.z never are.
bool updateIsNewer(const char *tag, const char *current);

// The 64 hex digits of GitHub's "sha256:<hex>" digest, in lowercase. False if it isn't one.
bool updateDigest(const char *digest, char out[UPDATE_HEX_SIZE]);

// True when url is exactly this app's release file:
// https://github.com/mizuiroame/<repo>/releases/download/<tag>/<name>.
// Any other address (another host, owner, tag or file) is refused.
bool updateUrlOk(const char *url, const char *repo, const char *tag, const char *name);

// The new version's path while it downloads (nro_path + ".new"), and the old
// one's while they're swapped (nro_path + ".old"). False if too long.
bool updateNewPath(const char *nro_path, char *out, size_t size);
bool updateOldPath(const char *nro_path, char *out, size_t size);

// Puts the downloaded new_path at nro_path: the old file is renamed aside,
// the new one takes its name, then the old one is removed. If the second step
// fails the old one is put back. False (with the old file in place) if it
// couldn't be done. Uses rename and remove: the console's SD card can't rename
// onto an existing file.
bool updateSwap(const char *nro_path, const char *new_path);
