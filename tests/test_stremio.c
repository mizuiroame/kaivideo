// Stremio addons: manifests, catalogs, series, streams, locations (made-up addons only).
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "stremio.h"

static const char *const MANIFEST =
    "{\"id\":\"org.example.kites\",\"version\":\"1.0.0\",\"name\":\"Kites\",\"types\":[\"movie\",\"series\"],"
    "\"idPrefixes\":[\"kt\"],"
    "\"resources\":[\"catalog\",{\"name\":\"meta\",\"types\":[\"series\"],\"idPrefixes\":[\"kts\"]},\"stream\"],"
    "\"catalogs\":[{\"type\":\"movie\",\"id\":\"top\",\"name\":\"Top\",\"extra\":[{\"name\":\"search\"}]},"
    "{\"type\":\"series\",\"id\":\"all shows\",\"name\":\"Shows\"},"
    "{\"type\":\"movie\",\"id\":\"find\",\"extra\":[{\"name\":\"search\",\"isRequired\":true}]},"
    "{\"type\":\"movie\",\"id\":\"genre\",\"extra\":[{\"name\":\"genre\",\"isRequired\":true}]},"
    "{\"type\":\"tv\",\"id\":\"channels\"}]}";

static void testAddonList(void)
{
    StremioAddonList l = {0};
    CHECK(stremioAddonListAdd(&l, "  stremio://addon.example/kites/manifest.json "));
    CHECK(!strcmp(l.urls[0], "https://addon.example/kites/manifest.json"));
    CHECK(stremioAddonListAdd(&l, "http://192.0.2.1:7000/"));
    CHECK(!strcmp(l.urls[1], "http://192.0.2.1:7000/manifest.json"));
    CHECK(!stremioAddonListAdd(&l, "https://addon.example/kites/manifest.json"));  // Already there.
    CHECK(!stremioAddonListAdd(&l, "magnet:?xt=urn:btih:abc"));
    char typed[256];  // Without http: on the LAN it's http, else https.
    CHECK(stremioNormalizeUrl("192.0.2.9:8099/stremio", typed, sizeof(typed)));
    CHECK(!strcmp(typed, "http://192.0.2.9:8099/stremio/manifest.json"));
    CHECK(stremioNormalizeUrl("nas.local/addon/", typed, sizeof(typed)));
    CHECK(!strcmp(typed, "http://nas.local/addon/manifest.json"));
    CHECK(stremioNormalizeUrl("addon.example/manifest.json", typed, sizeof(typed)));
    CHECK(!strcmp(typed, "https://addon.example/manifest.json"));
    CHECK(!stremioNormalizeUrl("mailto:someone", typed, sizeof(typed)));
    CHECK(!stremioNormalizeUrl("", typed, sizeof(typed)));
    CHECK(!stremioAddonListAdd(&l, "ftp://addon.example/manifest.json"));
    CHECK(!stremioAddonListAdd(&l, "https://addon.example/a b"));
    char text[4096];
    CHECK(stremioAddonListWrite(&l, text, sizeof(text)));
    StremioAddonList back;
    stremioAddonListRead(&back, text);
    CHECK(back.count == 2 && !strcmp(back.urls[1], l.urls[1]));
    stremioAddonListRemove(&back, 0);
    CHECK(back.count == 1 && !strcmp(back.urls[0], "http://192.0.2.1:7000/manifest.json"));
    stremioAddonListRemove(&back, 5);
    CHECK(back.count == 1);
    for (int i = 0; i < STREMIO_ADDONS_MAX; i++) {
        char url[64];
        snprintf(url, sizeof(url), "https://a%d.example", i);
        stremioAddonListAdd(&back, url);
    }
    CHECK(back.count == STREMIO_ADDONS_MAX);
}

static void testManifest(void)
{
    StremioAddon a;
    CHECK(stremioParseManifest(MANIFEST, "https://addon.example/kites/manifest.json", &a));
    CHECK(!strcmp(a.base, "https://addon.example/kites") && !strcmp(a.name, "Kites"));
    CHECK(a.resources[STREMIO_CATALOG] && a.resources[STREMIO_META] && a.resources[STREMIO_STREAM]);
    CHECK(a.catalog_count == 3);  // The genre-only one and the TV one are left out.
    CHECK(!strcmp(a.catalogs[1].id, "all shows") && !a.catalogs[1].search);
    CHECK(a.catalogs[0].search && !a.catalogs[0].search_only && a.catalogs[2].search_only);
    CHECK(stremioHandles(&a, STREMIO_STREAM, "movie", "kt5"));
    CHECK(!stremioHandles(&a, STREMIO_STREAM, "movie", "tt5"));
    CHECK(stremioHandles(&a, STREMIO_META, "series", "kts9") && !stremioHandles(&a, STREMIO_META, "movie", "kts9"));
    CHECK(!stremioHandles(&a, STREMIO_META, "series", "kt9"));
    char url[512];
    CHECK(stremioCatalogUrl(&a, &a.catalogs[1], NULL, url, sizeof(url)));
    CHECK(!strcmp(url, "https://addon.example/kites/catalog/series/all%20shows.json"));
    CHECK(stremioCatalogUrl(&a, &a.catalogs[0], "paper boats&", url, sizeof(url)));
    CHECK(!strcmp(url, "https://addon.example/kites/catalog/movie/top/search=paper%20boats%26.json"));
    CHECK(stremioResourceUrl(&a, STREMIO_STREAM, "series", "kts9:1:2", url, sizeof(url)));
    CHECK(!strcmp(url, "https://addon.example/kites/stream/series/kts9%3A1%3A2.json"));
    CHECK(!stremioResourceUrl(&a, STREMIO_CATALOG, "series", "x", url, sizeof(url)));
    CHECK(!stremioParseManifest("{\"name\":\"no id\"}", "https://a.example", &a));
    CHECK(!stremioParseManifest(MANIFEST, "not an address", &a));
    CHECK(stremioParseManifest("{\"id\":\"x\",\"behaviorHints\":{\"configurationRequired\":true},"
                               "\"resources\":[\"stream\"],\"types\":[\"movie\"]}",
                               "https://a.example", &a));
    CHECK(a.needs_setup && !stremioHandles(&a, STREMIO_STREAM, "movie", "tt1") && !strcmp(a.name, "x"));
}

static void testAnswers(void)
{
    StremioMeta *m;
    int n;
    CHECK(stremioParseMetas("{\"metas\":[{\"id\":\"kt1\",\"type\":\"movie\",\"name\":\"Paper Boats\","
                            "\"poster\":\"https://img.example/1.jpg\",\"releaseInfo\":\"2019\"},"
                            "{\"id\":\"kts9\",\"type\":\"series\",\"name\":\"Kite Hill\",\"releaseInfo\":\"2015-2018\","
                            "\"poster\":\"file:///x\",\"description\":\"Two\\nlines\"},"
                            "{\"id\":\"c1\",\"type\":\"tv\",\"name\":\"A channel\"},{\"type\":\"movie\",\"name\":\"No id\"}]}",
                            &m, &n));
    CHECK(n == 2 && m[0].year == 2019 && !strcmp(m[0].poster, "https://img.example/1.jpg"));
    CHECK(m[1].year == 2015 && !m[1].poster && !strcmp(m[1].description, "Two lines"));
    stremioMetasFree(m, n);
    CHECK(!stremioParseMetas("[]", &m, &n) && !m);

    StremioVideo *v;
    CHECK(stremioParseVideos("{\"meta\":{\"id\":\"kts9\",\"videos\":["
                             "{\"id\":\"kts9:2:1\",\"season\":2,\"episode\":1,\"title\":\"Return\"},"
                             "{\"id\":\"kts9:0:1\",\"season\":0,\"episode\":1,\"name\":\"Special\"},"
                             "{\"id\":\"kts9:1:2\",\"season\":1,\"number\":2,\"overview\":\"More.\"},"
                             "{\"id\":\"kts9:1:1\",\"season\":1,\"episode\":1}]}}",
                             &v, &n));
    CHECK(n == 4 && !strcmp(v[0].id, "kts9:1:1") && v[1].episode == 2 && !strcmp(v[1].overview, "More."));
    CHECK(v[2].season == 2 && v[3].season == 0 && !strcmp(v[3].title, "Special"));
    stremioVideosFree(v, n);
    CHECK(stremioParseVideos("{\"meta\":{\"id\":\"kt1\"}}", &v, &n) && n == 0);
    stremioVideosFree(v, n);

    StremioStream *s = NULL;
    n = 0;
    CHECK(stremioParseStreams("{\"streams\":[{\"url\":\"https://cdn.example/a.mp4\",\"name\":\"1080p\","
                              "\"title\":\"Paper Boats\\n2.1 GB\"},"
                              "{\"infoHash\":\"abcdef\",\"name\":\"torrent\"},{\"ytId\":\"xyz\"},"
                              "{\"externalUrl\":\"https://service.example/watch\"},"
                              "{\"url\":\"magnet:?xt=urn:btih:abc\"},"
                              "{\"url\":\"http://cdn.example/b.mkv\",\"description\":\"720p\"}]}",
                              "Kites", &s, &n));
    CHECK(n == 2 && !strcmp(s[0].detail, "Paper Boats 2.1 GB") && !strcmp(s[0].addon, "Kites"));
    CHECK(!strcmp(s[1].url, "http://cdn.example/b.mkv") && !s[1].name && !strcmp(s[1].detail, "720p"));
    CHECK(!streamHeadersAny(&s[0].headers));
    CHECK(stremioParseStreams("{\"streams\":[{\"url\":\"https://other.example/c.mp4\"}]}", "Other", &s, &n));
    CHECK(n == 3 && !strcmp(s[2].addon, "Other"));
    CHECK(!stremioParseStreams("{}", "x", &s, &n) && n == 3);
    stremioStreamsFree(s, n);

    // Streams that ask for headers: kept when they're ones KaiVideo sends, with safe values.
    s = NULL;
    n = 0;
    CHECK(stremioParseStreams(
        "{\"streams\":["
        "{\"url\":\"https://cdn.example/h.m3u8\",\"behaviorHints\":{\"proxyHeaders\":{}}},"
        "{\"url\":\"https://cdn.example/r.m3u8\",\"behaviorHints\":{\"proxyHeaders\":{\"request\":"
        "{\"Referer\":\"https://site.example/\",\"user-agent\":\"Mozilla/5.0\",\"Origin\":\"https://site.example\"},"
        "\"response\":{\"Content-Type\":\"video/mp4\"}}}},"
        "{\"url\":\"https://cdn.example/cookie.mp4\",\"behaviorHints\":{\"proxyHeaders\":{\"request\":"
        "{\"Referer\":\"https://site.example/\",\"Cookie\":\"session=1\"}}}},"
        "{\"url\":\"https://cdn.example/split.mp4\",\"behaviorHints\":{\"proxyHeaders\":{\"request\":"
        "{\"Referer\":\"https://site.example/\\r\\nX-Evil: 1\"}}}},"
        "{\"url\":\"https://cdn.example/odd.mp4\",\"behaviorHints\":{\"proxyHeaders\":{\"request\":[\"Referer\"]}}},"
        "{\"url\":\"https://cdn.example/number.mp4\",\"behaviorHints\":{\"proxyHeaders\":{\"request\":"
        "{\"Referer\":7}}}}]}",
        "Kites", &s, &n));
    CHECK(n == 2);  // The cookie one, the line break, a list and a number are left out.
    if (n == 2) {
        CHECK(!strcmp(s[0].url, "https://cdn.example/h.m3u8") && !streamHeadersAny(&s[0].headers));
        CHECK(!strcmp(s[1].url, "https://cdn.example/r.m3u8") &&
              !strcmp(s[1].headers.referer, "https://site.example/") &&
              !strcmp(s[1].headers.user_agent, "Mozilla/5.0") && !strcmp(s[1].headers.origin, "https://site.example"));
    }
    stremioStreamsFree(s, n);
}

static void testLocationsAndLibrary(void)
{
    char loc[128], kind[16], id[128];
    CHECK(stremioLocation(loc, sizeof(loc), "series", "kts9:1:2") && !strcmp(loc, "stremio:series/kts9:1:2"));
    CHECK(stremioLocation(loc, sizeof(loc), "movie", "yt/a b%") && !strcmp(loc, "stremio:movie/yt%2Fa%20b%25"));
    CHECK(stremioParseLocation(loc, kind, sizeof(kind), id, sizeof(id)));
    CHECK(!strcmp(kind, "movie") && !strcmp(id, "yt/a b%"));
    CHECK(!stremioParseLocation("stremio:movie/", kind, sizeof(kind), id, sizeof(id)));
    CHECK(!stremioParseLocation("stremio:movie/a%2", kind, sizeof(kind), id, sizeof(id)));
    CHECK(!stremioParseLocation("stremio:movie/a%00", kind, sizeof(kind), id, sizeof(id)));
    CHECK(!stremioParseLocation("xtream:movie/1.mp4", kind, sizeof(kind), id, sizeof(id)));
    CHECK(!stremioLocation(loc, sizeof(loc), "movie", ""));

    StremioStore st;
    memset(&st, 0, sizeof(st));
    st.list_count = 2;
    snprintf(st.lists[0].title, sizeof(st.lists[0].title), "Top");
    snprintf(st.lists[0].type, sizeof(st.lists[0].type), "movie");
    CHECK(stremioParseMetas("{\"metas\":[{\"id\":\"kt1\",\"type\":\"movie\",\"name\":\"Paper Boats\","
                            "\"poster\":\"https://img.example/1.jpg\"},{\"id\":\"kt2\",\"type\":\"movie\",\"name\":\"Night Desk\"}]}",
                            &st.lists[0].metas, &st.lists[0].count));
    snprintf(st.lists[1].title, sizeof(st.lists[1].title), "Shows");
    snprintf(st.lists[1].type, sizeof(st.lists[1].type), "series");
    CHECK(stremioParseMetas("{\"metas\":[{\"id\":\"kts9\",\"type\":\"series\",\"name\":\"Kite Hill\"},"
                            "{\"id\":\"kts8\",\"type\":\"series\",\"name\":\"Harbor Weather\"}]}",
                            &st.lists[1].metas, &st.lists[1].count));
    CHECK(stremioParseMetas("{\"metas\":[{\"id\":\"kt1\",\"type\":\"movie\",\"name\":\"Paper Boats\"},"
                            "{\"id\":\"kt3\",\"type\":\"movie\",\"name\":\"Morning Lanterns\"}]}",
                            &st.found.metas, &st.found.count));
    StremioVideo *v;
    int n;
    CHECK(stremioParseVideos("{\"meta\":{\"videos\":[{\"id\":\"kts9:1:1\",\"season\":1,\"episode\":1,\"title\":\"One\"},"
                             "{\"id\":\"kts9:1:2\",\"season\":1,\"episode\":2}]}}",
                             &v, &n));
    CHECK(stremioStoreEpisodesState(&st, "kts9") == STREMIO_EPISODES_UNREAD);
    stremioStoreSetEpisodes(&st, "kts9", v, n);
    stremioStoreSetEpisodes(&st, "kts8", NULL, 0);
    CHECK(stremioStoreEpisodesState(&st, "kts9") == STREMIO_EPISODES_READ);
    CHECK(stremioStoreEpisodesState(&st, "kts8") == STREMIO_EPISODES_FAILED);

    Library lib;
    libraryInit(&lib, "stremio:");
    stremioFillLibrary(&lib, &st, "Reading", "Failed");
    libraryBuild(&lib);
    CHECK(lib.collection_count == 2 && lib.film_count == 3 && lib.show_count == 2);
    int film = libraryFind(&lib, "stremio:movie/kt1");
    CHECK(film >= 0 && lib.items[film].collection == 0 && !strcmp(lib.items[film].art, "poster:https://img.example/1.jpg"));
    int found = libraryFind(&lib, "stremio:movie/kt3");
    CHECK(found >= 0 && lib.items[found].collection == -1);
    CHECK(libraryFind(&lib, "stremio:series/kts9:1:2") >= 0 && libraryFind(&lib, "stremio:show/kts9") < 0);
    int stand_in = libraryFind(&lib, "stremio:show/kts8");
    CHECK(stand_in >= 0 && !strcmp(lib.items[stand_in].name.title, "Failed"));
    size_t films = 0;
    for (size_t i = 0; i < lib.count; i++)
        films += !strcmp(lib.items[i].path, "stremio:movie/kt1");
    CHECK(films == 1);  // In a catalog and the search: once.
    libraryFree(&lib);
    stremioStoreFree(&st);
    CHECK(st.list_count == 0 && st.series_count == 0 && !st.found.metas);
}

void testStremio(void)
{
    testAddonList();
    testManifest();
    testAnswers();
    testLocationsAndLibrary();
}
