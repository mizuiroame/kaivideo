// Films and episodes from file names, shaped like a real library's (the titles are made up).
#include <string.h>

#include "check.h"
#include "naming.h"

static VideoName parse(const char *path, const char *root)
{
    VideoName v;
    namingParse(path, root, &v);
    return v;
}

static bool isFilm(VideoName v, const char *title, int year)
{
    return !v.is_episode && strcmp(v.title, title) == 0 && v.year == year;
}

static bool isEpisode(VideoName v, const char *show, int season, int episode, const char *title)
{
    return v.is_episode && strcmp(v.show, show) == 0 && v.season == season && v.episode == episode &&
           strcmp(v.title, title) == 0;
}

static void testFilms(void)
{
    const char *root = "sdmc:/Media";
    // In a "Title (Year)" folder: the folder's name, whatever the file is called.
    CHECK(isFilm(parse("sdmc:/Media/Movies/Quiet Harbor (2017)/a1b2c3d4e5f60718293a4b5c6d7e8f90a1b2c3d4.mp4", root),
                 "Quiet Harbor", 2017));
    CHECK(isFilm(parse("sdmc:/Media/Movies/The Silver 7 - First Light (2025)/"
                       "The.Silver.Seven.First.Light.2025.2160p.UHD.BluRay.REMUX.DV.P7.HDR.MULTi-GRP.mkv",
                       root),
                 "The Silver 7 - First Light", 2025));
    // From the file's name: tags dropped, the year kept apart.
    CHECK(isFilm(parse("sdmc:/Media/Downloads/Luna.Verde.2024.MULTi.FRENCH.HDR.2160p.WEB.H265-GRP.mkv", root),
                 "Luna Verde", 2024));
    CHECK(isFilm(parse("sdmc:/Media/Paper.Lanterns.1998.1080p.REMASTERED.BluRay.ENG.LATINO.JAP.DD5.1.H265-GRP.mkv",
                       root),
                 "Paper Lanterns", 1998));
    CHECK(isFilm(parse("sdmc:/Media/The Lantern Keeper (1993) 2160p Bluray Remux h265 HDR10 DTS-HD MA 5.1-GRP_Rips.mkv", root),
                 "The Lantern Keeper", 1993));
    // A year-like number that starts the title, or one that isn't a year, stays.
    CHECK(isFilm(parse("sdmc:/Media/1999.Nights.In.Kyoto.1975.1080p.mkv", root), "1999 Nights In Kyoto", 1975));
    CHECK(isFilm(parse("sdmc:/Media/Station Nine (2017).mkv", root), "Station Nine", 2017));
    CHECK(isFilm(parse("sdmc:/Media/Home Video.mp4", root), "Home Video", 0));
    // A folder without a year above the file doesn't name it.
    CHECK(isFilm(parse("sdmc:/Media/Movies/Ashfall.2010.2160p.UHD.Blu-ray.Remux.HDR10.HEVC.mkv", root),
                 "Ashfall", 2010));
}

static void testEpisodes(void)
{
    const char *root = "sdmc:/Media";
    // The series' folder names it; the episode's title comes after SxxEyy.
    CHECK(isEpisode(parse("sdmc:/Media/TV_Shows/Prairie Road/"
                          "Prairie.Road.S09E06.Dry.Creek.1080p.DSNP.WEB-DL.AAC2.0.H.264-GRP.mkv",
                          root),
                    "Prairie Road", 9, 6, "Dry Creek"));
    CHECK(isEpisode(parse("sdmc:/Media/TV_Shows/Prairie Road/"
                          "Prairie Road 2003 S01E11 Harvest Fair 1080p DSNP WEB-DL DUAL AAC2 0.mkv",
                          root),
                    "Prairie Road", 1, 11, "Harvest Fair"));
    CHECK(isEpisode(parse("sdmc:/Media/TV_Shows/Sky Pilots - The Long War/LongWar_S06E09.mkv", root),
                    "Sky Pilots - The Long War", 6, 9, ""));
    CHECK(isEpisode(parse("sdmc:/Media/TV_Shows/Dustland/"
                          "www.example.org    -    Dustland.S01E07.The.Signal.1080p.HEVC.x265-GRP.mkv",
                          root),
                    "Dustland", 1, 7, "The Signal"));
    CHECK(isEpisode(parse("sdmc:/Media/TV_Shows/Dustland/Dustland 2024 S02E05 1080p HEVC x265 GRP.mkv", root),
                    "Dustland", 2, 5, ""));
    // A "Season 2" folder: the series is the folder above.
    CHECK(isEpisode(parse("sdmc:/Media/Rook/Season 2/Rook.S02E03.1080p.mkv", root), "Rook", 2, 3, ""));
    CHECK(isEpisode(parse("sdmc:/Media/Rook/Temporada 1/03 - Lamentia.mkv", root), "Rook", 1, 3, "Lamentia"));
    // Loose in a mixed folder: the name says the series.
    CHECK(isEpisode(parse("sdmc:/Media/Downloads/[GRP] The Paper Lantern - S01E02 [1080p][HEVC x265 10bit]"
                          "[Dual-Audio][Multi-Subs][A9D9DAB5].mkv",
                          root),
                    "The Paper Lantern", 1, 2, ""));
    CHECK(isEpisode(parse("sdmc:/Media/Downloads/[Group] THE PAPER LANTERN - 05 [WebRip][HEVC-10bit 1080p]"
                          "[CHI_JPN].mkv",
                          root),
                    "THE PAPER LANTERN", 1, 5, ""));
    CHECK(isEpisode(parse("sdmc:/Media/Downloads/Dustland.2024.S01E01.1080p.x265-GRP.mkv", root), "Dustland", 1, 1,
                    ""));
    CHECK(isEpisode(parse("sdmc:/Media/Show 1x04 Name.mkv", root), "Show", 1, 4, "Name"));
    // Release folders name the series once their tags and season are dropped.
    CHECK(isEpisode(parse("sdmc:/Media/Downloads/Prairie Road S02 1080p DSNP WEB-DL AAC 2.0 x264-GRP/"
                          "Prairie.Road.S02E17.Hank's.Dirty.Laundry.1080p.DSNP.WEB-DL.AAC2.0.H.264-GRP.mkv",
                          root),
                    "Prairie Road", 2, 17, "Hank's Dirty Laundry"));
    CHECK(isEpisode(parse("sdmc:/Media/Downloads/North Peak Season 3 (1080p x265 q22 GRP)/"
                          "North Peak S03E10 The Groovy Pirate Ghost Mystery (1080p x265 q22 GRP).mkv",
                          root),
                    "North Peak", 3, 10, "The Groovy Pirate Ghost Mystery"));
    CHECK(isEpisode(parse("sdmc:/Media/Downloads/North Peak S20 (2016)/01 Old Berries.mkv", root), "North Peak",
                    20, 1, "Old Berries"));
    CHECK(isEpisode(parse("sdmc:/Media/TV_Shows/Coach Mabel (2020)/Coach.Mabel.S01E03.1080p.mkv", root), "Coach Mabel", 1,
                    3, ""));
    CHECK(isEpisode(parse("sdmc:/Media/TV_Shows/The Bureau (US)/The.Bureau.US.S02E16.EXTENDED.1080p.PCOK.WEB-DL.mkv",
                          root),
                    "The Bureau (US)", 2, 16, ""));
    CHECK(isEpisode(parse("sdmc:/Media/Downloads/Sky Pilots The Long War S04e01-22 (1080p Ita Eng h265 10bit SubS) "
                          "byGRP/Sky.Pilots.The.Long.War.S04e15.Ita.Eng.1080p.h265.10bit.SubS-GRP.mkv",
                          root),
                    "Sky Pilots The Long War", 4, 15, ""));
    CHECK(isEpisode(parse("sdmc:/Media/Rook (2021) - S01E03 - Lamentia (1080p DSNP WEB-DL x265 GRP).mkv", root),
                    "Rook", 1, 3, "Lamentia"));
    // An episode with no series anywhere in its name or folders is a film.
    CHECK(isFilm(parse("sdmc:/Media/S01E02.mkv", root), "S01E02", 0));
    // Glued words are split, when no folder names the series.
    CHECK(isEpisode(parse("sdmc:/kaivideo-test/real/PrairieRoad_S12E13.mkv", "sdmc:/kaivideo-test"),
                    "Prairie Road", 12, 13, ""));
    // A folder that isn't the series' (no shared name) doesn't name it.
    CHECK(isEpisode(parse("sdmc:/Media/TV/NorthPeak_S28E01.mkv", root), "North Peak", 28, 1, ""));
}

static void testKeys(void)
{
    char key[64];
    namingKey("Pokémon: The Movie", key, sizeof(key));
    CHECK(strcmp(key, "pokemonthemovie") == 0);
    namingKey("Sky Pilots - The Long War", key, sizeof(key));
    CHECK(strcmp(key, "skypilotsthelongwar") == 0);
    namingKey("Año Ñandú", key, 6);  // Cut to fit.
    CHECK(strcmp(key, "anona") == 0);
}

void testNaming(void)
{
    testFilms();
    testEpisodes();
    testKeys();
}
