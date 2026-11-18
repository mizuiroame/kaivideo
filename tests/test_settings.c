// Settings: stepping choices and keeping them in settings.txt.
#include <stdio.h>
#include <string.h>

#include "check.h"
#include "settings.h"
#include "theme.h"

void testSettings(void)
{
    Config c;
    configDefaults(&c);
    CHECK(c.thumbnails && c.subtitles == SUBTITLES_MEDIUM && c.theme == 0);
    settingStep(&c, SETTING_THEME, -1);  // Wraps to the last.
    CHECK(c.theme == THEME_COUNT - 1);
    settingStep(&c, SETTING_ACCENT, 2);
    settingStep(&c, SETTING_THUMBNAILS, 1);
    settingStep(&c, SETTING_SUBTITLES, 1);
    snprintf(c.background, sizeof(c.background), "%s", "beach.jpg");
    char value[64];
    settingValue(&c, SETTING_ACCENT, value, sizeof(value));
    CHECK(strcmp(value, "Green") == 0);
    settingValue(&c, SETTING_THUMBNAILS, value, sizeof(value));
    CHECK(strcmp(value, "Off") == 0);
    settingValue(&c, SETTING_ABOUT, value, sizeof(value));
    CHECK(value[0] == '\0' && !settingIsChoice(SETTING_ABOUT) && settingIsChoice(SETTING_BACKGROUND));
    CHECK(settingSubtitlePx(SUBTITLES_LARGE) > settingSubtitlePx(SUBTITLES_SMALL));

    // Written and read back; other keys and broken values are left alone.
    const char *file = TMP_ROOT "/settings_test.txt";
    FILE *f = fopen(file, "w");
    CHECK(f != NULL);
    if (f) {
        configWrite(&c, f);
        fclose(f);
    }
    Config back;
    configDefaults(&back);
    f = fopen(file, "r");
    char line[256];
    while (f && fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        configReadLine(&back, line);
    }
    if (f)
        fclose(f);
    CHECK(back.theme == c.theme && back.accent == c.accent && !back.thumbnails && back.subtitles == SUBTITLES_LARGE &&
          strcmp(back.background, "beach.jpg") == 0);
    CHECK(!configReadLine(&back, "folder=sdmc:/Videos"));
    CHECK(configReadLine(&back, "theme=99") && back.theme == 0);
    CHECK(configReadLine(&back, "blur=x") && back.blur == 2);
    CHECK(configReadLine(&back, "background=../../x.jpg") && back.background[0] == '\0');
}
