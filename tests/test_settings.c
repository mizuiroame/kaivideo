// Settings: its pages, stepping choices, the player's subtitle options, and
// keeping them in settings.txt.
#include <stdio.h>
#include <string.h>

#include "check.h"
#include "settings.h"
#include "theme.h"

void testSettings(void)
{
    Config c;
    configDefaults(&c);
    CHECK(c.thumbnails && c.subtitles == SUBTITLES_MEDIUM && c.theme == 0 && c.subtitle_style == SUBTITLE_OUTLINE);
    CHECK(c.autoplay);  // The next episode plays on its own.

    SettingRow rows[SETTINGS_PAGE_MAX_ROWS];
    CHECK(settingsPageRows(SETTINGS_PAGE_MAIN, false, rows) == 7 && rows[1] == SETTING_OPEN_JELLYFIN &&
          rows[4] == SETTING_AUTOPLAY && rows[5] == SETTING_OPEN_DISPLAY);
    // Signing in is offered until it's done, and signing out only then.
    CHECK(settingsPageRows(SETTINGS_PAGE_JELLYFIN, false, rows) == 3 && rows[1] == SETTING_JF_CODE &&
          rows[2] == SETTING_JF_PASSWORD);
    CHECK(settingsPageRows(SETTINGS_PAGE_JELLYFIN, true, rows) == 3 && rows[1] == SETTING_JF_QUALITY &&
          rows[2] == SETTING_JF_SIGN_OUT);
    Config quality;
    configDefaults(&quality);
    CHECK(settingQualityBitrate(&quality) == 25000000);  // Auto.
    settingStep(&quality, SETTING_JF_QUALITY, -1);       // Wraps to the lowest.
    CHECK(settingQualityBitrate(&quality) == 420000 && settingIsChoice(SETTING_JF_QUALITY));
    CHECK(configReadLine(&quality, "jellyfin_quality=3") && settingQualityBitrate(&quality) == 20000000);
    CHECK(settingsPageRows(SETTINGS_PAGE_DISPLAY, false, rows) == 5 && rows[0] == SETTING_THEME && rows[4] == SETTING_CLOCK);
    CHECK(strcmp(settingsPageTitle(SETTINGS_PAGE_DISPLAY), "Display") == 0);

    settingStep(&c, SETTING_THEME, -1);  // Wraps to the last.
    CHECK(c.theme == THEME_COUNT - 1);
    settingStep(&c, SETTING_ACCENT, 2);
    settingStep(&c, SETTING_THUMBNAILS, 1);
    settingStep(&c, SETTING_CLOCK, 1);
    settingStep(&c, SETTING_AUTOPLAY, 1);
    CHECK(!c.autoplay && settingIsChoice(SETTING_AUTOPLAY));
    snprintf(c.background, sizeof(c.background), "%s", "beach.jpg");
    char value[64];
    settingValue(&c, SETTING_ACCENT, value, sizeof(value));
    CHECK(strcmp(value, "Green") == 0);
    settingValue(&c, SETTING_THUMBNAILS, value, sizeof(value));
    CHECK(strcmp(value, "Off") == 0);
    settingValue(&c, SETTING_AUTOPLAY, value, sizeof(value));
    CHECK(strcmp(value, "Off") == 0);
    settingValue(&c, SETTING_ABOUT, value, sizeof(value));
    CHECK(value[0] == '\0' && !settingIsChoice(SETTING_ABOUT) && settingIsChoice(SETTING_BACKGROUND));

    // The player's subtitle options.
    subtitleOptionStep(&c, SUBOPT_SIZE, 1);
    subtitleOptionStep(&c, SUBOPT_COLOR, -1);
    subtitleOptionStep(&c, SUBOPT_STYLE, 1);
    subtitleOptionValue(&c, SUBOPT_COLOR, value, sizeof(value));
    CHECK(strcmp(value, "Green") == 0 && settingSubtitleColor(&c) != 0xF2F2F5);
    subtitleOptionValue(&c, SUBOPT_STYLE, value, sizeof(value));
    CHECK(strcmp(value, "Dark box") == 0);
    Config small;
    configDefaults(&small);
    subtitleOptionStep(&small, SUBOPT_SIZE, -1);
    CHECK(settingSubtitlePx(&c) > settingSubtitlePx(&small));
    CHECK(settingSubtitleScale(&small) < 1.0);
    Config medium;
    configDefaults(&medium);
    CHECK(settingSubtitleScale(&medium) == 1.0);
    CHECK(subtitleOptionCount(true) == SUBOPT_COUNT);
    CHECK(subtitleOptionCount(false) == 1);  // Only the size, the first.

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
          back.clock_24h && !back.autoplay && back.subtitle_color == c.subtitle_color && back.subtitle_style == SUBTITLE_BOX &&
          strcmp(back.background, "beach.jpg") == 0);
    CHECK(!configReadLine(&back, "folder=sdmc:/Videos"));
    CHECK(configReadLine(&back, "theme=99") && back.theme == 0);
    CHECK(configReadLine(&back, "blur=x") && back.blur == 2);
    CHECK(configReadLine(&back, "background=../../x.jpg") && back.background[0] == '\0');
}
