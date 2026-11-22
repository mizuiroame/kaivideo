#include "updaterelease.h"

#include <stdio.h>
#include <string.h>

#include <jansson.h>

static const char *text(const json_t *object, const char *name)
{
    const json_t *item = json_object_get(object, name);
    return json_is_string(item) ? json_string_value(item) : NULL;
}

bool updateReadRelease(const char *json, const char *repo, const char *nro_name, UpdateRelease *out)
{
    memset(out, 0, sizeof(*out));
    json_t *root = json ? json_loads(json, 0, NULL) : NULL;
    const char *tag = text(root, "tag_name");
    bool ok = json_is_object(root) && tag && strlen(tag) < sizeof(out->tag) && updateIsNewer(tag, "0.0.0") &&
              !json_is_true(json_object_get(root, "prerelease")) && !json_is_true(json_object_get(root, "draft"));
    const json_t *asset = NULL;
    if (ok) {
        const json_t *assets = json_object_get(root, "assets");
        for (size_t i = 0; json_is_array(assets) && i < json_array_size(assets); i++) {
            const json_t *each = json_array_get(assets, i);
            const char *name = text(each, "name");
            if (json_is_object(each) && name && strcmp(name, nro_name) == 0) {
                asset = each;
                break;
            }
        }
        ok = asset != NULL;
    }
    if (ok) {
        const char *url = text(asset, "browser_download_url");
        const json_t *size = json_object_get(asset, "size");
        double bytes = json_is_number(size) ? json_number_value(size) : 0;
        ok = updateUrlOk(url, repo, tag, nro_name) && updateDigest(text(asset, "digest"), out->sha256) && bytes > 0 &&
             bytes <= UPDATE_NRO_MAX;
        if (ok) {
            snprintf(out->tag, sizeof(out->tag), "%s", tag);
            snprintf(out->url, sizeof(out->url), "%s", url);
            out->size = (int64_t)bytes;
        }
    }
    json_decref(root);
    if (!ok)
        memset(out, 0, sizeof(*out));
    return ok;
}
