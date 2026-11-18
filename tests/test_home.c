// The Home screen's rows.
#include <string.h>

#include "check.h"
#include "home.h"

void testHome(void)
{
    static Library lib;
    libraryInit(&lib, "sdmc:/Videos");
    libraryAdd(&lib, "sdmc:/Videos/Quiet Harbor (2017)/harbor.mp4", 1, 100);
    libraryAdd(&lib, "sdmc:/Videos/Rook/Rook.S01E01.mkv", 1, 300);
    libraryAdd(&lib, "sdmc:/Videos/Rook/Rook.S01E02.mkv", 1, 200);
    libraryBuild(&lib);

    static ResumeList resume;
    memset(&resume, 0, sizeof(resume));
    static Home home;
    homeCompute(&home, &lib, &resume, NULL);
    // Nothing started: no Continue watching row.
    CHECK(home.count == 3 && home.rows[0].kind == HOME_RECENT && home.rows[0].count == 2 &&
          home.rows[0].items[0].is_show);
    CHECK(home.rows[1].kind == HOME_SERIES && home.rows[2].kind == HOME_FILMS && home.rows[2].count == 1);

    // Started videos, the latest first; one outside the library is left out.
    const int64_t S = 1000000000;
    resumeRemember(&resume, "sdmc:/Videos/Quiet Harbor (2017)/harbor.mp4", 60 * S, 600 * S);
    resumeRemember(&resume, "sdmc:/Elsewhere/clip.mp4", 60 * S, 600 * S);
    resumeRemember(&resume, "sdmc:/Videos/Rook/Rook.S01E02.mkv", 60 * S, 600 * S);
    homeCompute(&home, &lib, &resume, NULL);
    CHECK(home.count == 4 && home.rows[0].kind == HOME_CONTINUE && home.rows[0].count == 2);
    CHECK(!home.rows[0].items[0].is_show && lib.items[home.rows[0].items[0].index].name.episode == 2);
    CHECK(strcmp(homeRowTitle(HOME_CONTINUE), "Continue watching") == 0);

    // An empty library: no rows.
    static Library empty;
    libraryInit(&empty, "sdmc:/Nothing");
    libraryBuild(&empty);
    homeCompute(&home, &empty, &resume, NULL);
    CHECK(home.count == 0);
    libraryFree(&empty);
    libraryFree(&lib);
}
