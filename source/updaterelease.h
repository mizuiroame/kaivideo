// GitHub's answer about the latest release (GET /repos/<owner>/<repo>/releases/latest),
// read into what an update needs. Pure, tested on the PC.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "update.h"

#define UPDATE_NRO_MAX (96u * 1024 * 1024)  // A release's .nro is never bigger.

typedef struct {
    char tag[UPDATE_TAG_MAX];       // "v1.3.0"
    char url[UPDATE_URL_MAX];       // Its .nro: always this app's release file on github.com.
    char sha256[UPDATE_HEX_SIZE];   // GitHub's digest of it, lowercase hex.
    int64_t size;                   // Its size in bytes.
} UpdateRelease;

// Reads json (the answer's text) for repo ("kairadio") and its file ("KaiRadio.nro").
// False if it isn't a usable release: no x.y.z tag, no such file, a download
// address that isn't the release's own, no SHA-256, or a size out of bounds.
bool updateReadRelease(const char *json, const char *repo, const char *nro_name, UpdateRelease *out);
