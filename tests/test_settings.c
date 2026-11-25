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
    // The main page opens the others: the library, each server, playback, display.
    // Every source's page in one order: address, sign-in, options (its color last), test, forget.
    CHECK(settingsPageRows(SETTINGS_PAGE_NETWORK, false, SERVER_JELLYFIN, rows) == 8 && rows[0] == SETTING_NET_ADDRESS &&
          rows[2] == SETTING_NET_REMEMBER && rows[3] == SETTING_NET_UNSIGNED && rows[4] == SETTING_NET_CERTIFICATE &&
          rows[5] == SETTING_NET_COLOR && rows[6] == SETTING_NET_TEST && rows[7] == SETTING_NET_FORGET);
    {  // The network folder's color: its own, or an accent, kept.
        Config colors;
        configDefaults(&colors);
        CHECK(settingSourceColor(&colors, SOURCE_COLOR_NETWORK) == -1 && settingIsChoice(SETTING_NET_COLOR));
        settingStep(&colors, SETTING_NET_COLOR, 1);
        settingStep(&colors, SETTING_ADDONS_COLOR, -1);
        CHECK(settingSourceColor(&colors, SOURCE_COLOR_NETWORK) == 0 &&
              settingSourceColor(&colors, SOURCE_COLOR_ADDONS) == THEME_ACCENT_COUNT - 1);
        CHECK(configReadLine(&colors, "iptv_color=3") && settingSourceColor(&colors, SOURCE_COLOR_IPTV) == 2);
    }
    // The two switches change with the arrows, as Certificate check.
    CHECK(settingIsChoice(SETTING_NET_REMEMBER) && settingIsChoice(SETTING_NET_UNSIGNED) &&
          !settingIsChoice(SETTING_NET_FORGET));
    CHECK(settingsPageNote(SETTINGS_PAGE_NETWORK, SERVER_JELLYFIN) != NULL);
    CHECK(settingsPageRows(SETTINGS_PAGE_MAIN, false, SERVER_JELLYFIN, rows) == 10 && rows[0] == SETTING_OPEN_LIBRARY &&
          rows[1] == SETTING_OPEN_NETWORK && rows[2] == SETTING_OPEN_JELLYFIN && rows[3] == SETTING_OPEN_EMBY &&
          rows[4] == SETTING_OPEN_PLEX && rows[5] == SETTING_OPEN_IPTV && rows[6] == SETTING_OPEN_ADDONS &&
          rows[7] == SETTING_OPEN_PLAYBACK && rows[8] == SETTING_OPEN_DISPLAY && rows[9] == SETTING_ABOUT);
    // The addons' page: Add, a row an addon, then Load.
    settingsSetAddonCount(0);
    CHECK(settingsPageRows(SETTINGS_PAGE_ADDONS, false, SERVER_JELLYFIN, rows) == 3 && rows[0] == SETTING_ADDONS_ADD &&
          rows[1] == SETTING_ADDONS_COLOR && rows[2] == SETTING_ADDONS_LOAD);
    settingsSetAddonCount(99);
    CHECK(settingsPageRows(SETTINGS_PAGE_ADDONS, false, SERVER_JELLYFIN, rows) == SETTINGS_ADDONS_MAX + 3 &&
          rows[1] == SETTING_ADDON_FIRST && rows[SETTINGS_ADDONS_MAX] == SETTING_ADDON_LAST);
    settingsSetAddonCount(0);
    SettingsPage opened;
    ServerKind opened_kind;
    CHECK(settingOpensPage(SETTING_OPEN_ADDONS, &opened, &opened_kind) && opened == SETTINGS_PAGE_ADDONS);
    CHECK(settingPageOpener(SETTINGS_PAGE_ADDONS, SERVER_JELLYFIN) == SETTING_OPEN_ADDONS);
    CHECK(settingsPageNote(SETTINGS_PAGE_ADDONS, SERVER_JELLYFIN) != NULL);
    // The IPTV list's page: its type (a choice), address, account, guide, load and forget.
    CHECK(settingsPageRows(SETTINGS_PAGE_IPTV, false, SERVER_JELLYFIN, rows) == 8 && rows[0] == SETTING_IPTV_KIND &&
          rows[1] == SETTING_IPTV_ADDRESS && rows[2] == SETTING_IPTV_ACCOUNT && rows[3] == SETTING_IPTV_GUIDE &&
          rows[4] == SETTING_IPTV_CERTIFICATE && rows[5] == SETTING_IPTV_COLOR && rows[6] == SETTING_IPTV_LOAD &&
          rows[7] == SETTING_IPTV_FORGET);
    CHECK(settingIsChoice(SETTING_IPTV_KIND) && settingIsChoice(SETTING_IPTV_CERTIFICATE) &&
          !settingIsChoice(SETTING_IPTV_LOAD) &&
          !strcmp(settingsPageTitle(SETTINGS_PAGE_IPTV, SERVER_JELLYFIN), "IPTV") &&
          settingsPageNote(SETTINGS_PAGE_IPTV, SERVER_JELLYFIN) != NULL);
    SettingsPage iptv_page;
    ServerKind iptv_kind;
    CHECK(settingOpensPage(SETTING_OPEN_IPTV, &iptv_page, &iptv_kind) && iptv_page == SETTINGS_PAGE_IPTV &&
          settingPageOpener(SETTINGS_PAGE_IPTV, SERVER_JELLYFIN) == SETTING_OPEN_IPTV);
    // Every page ends where its list does (none takes an empty row as the first setting).
    for (int p = 0; p < SETTINGS_PAGE_COUNT; p++)
        for (int k = 0; k < SERVER_KIND_COUNT; k++)
            for (int in = 0; in < 2; in++) {
                int n = settingsPageRows((SettingsPage)p, in, (ServerKind)k, rows);
                for (int r = 1; r < n; r++)
                    CHECK(rows[r] != SETTING_LIBRARY);
            }
    CHECK(!strcmp(settingLabel(SETTING_OPEN_JELLYFIN), "Jellyfin") && !strcmp(settingLabel(SETTING_OPEN_PLEX), "Plex"));
    CHECK(settingsPageRows(SETTINGS_PAGE_LIBRARY, false, SERVER_JELLYFIN, rows) == 3 && rows[0] == SETTING_LIBRARY &&
          rows[1] == SETTING_RESCAN && rows[2] == SETTING_THUMBNAILS);
    CHECK(settingsPageRows(SETTINGS_PAGE_PLAYBACK, false, SERVER_JELLYFIN, rows) == 2 && rows[0] == SETTING_AUTOPLAY &&
          rows[1] == SETTING_JF_QUALITY);
    SettingsPage page;
    ServerKind opens;
    CHECK(settingOpensPage(SETTING_OPEN_EMBY, &page, &opens) && page == SETTINGS_PAGE_JELLYFIN && opens == SERVER_EMBY);
    CHECK(settingOpensPage(SETTING_OPEN_LIBRARY, &page, &opens) && page == SETTINGS_PAGE_LIBRARY);
    CHECK(settingOpensPage(SETTING_OPEN_NETWORK, &page, &opens) && page == SETTINGS_PAGE_NETWORK &&
          settingPageOpener(SETTINGS_PAGE_NETWORK, SERVER_JELLYFIN) == SETTING_OPEN_NETWORK);
    CHECK(!settingOpensPage(SETTING_THUMBNAILS, &page, &opens));
    // About opens a page: the credits, and updates (no page of their own).
    CHECK(settingOpensPage(SETTING_ABOUT, &page, &opens) && page == SETTINGS_PAGE_ABOUT &&
          settingPageOpener(SETTINGS_PAGE_ABOUT, SERVER_JELLYFIN) == SETTING_ABOUT);
    CHECK(settingsPageRows(SETTINGS_PAGE_ABOUT, false, SERVER_JELLYFIN, rows) == 3 && rows[0] == SETTING_CREDITS &&
          rows[1] == SETTING_UPDATE_AUTO && rows[2] == SETTING_UPDATE_NOW);
    CHECK(settingIsChoice(SETTING_UPDATE_AUTO) && !settingIsChoice(SETTING_UPDATE_NOW) &&
          !settingIsChoice(SETTING_CREDITS) && !strcmp(settingsPageTitle(SETTINGS_PAGE_ABOUT, SERVER_JELLYFIN), "About"));
    CHECK(!strcmp(settingLabel(SETTING_UPDATE_NOW), "Look for an update"));
    CHECK(settingPageOpener(SETTINGS_PAGE_JELLYFIN, SERVER_PLEX) == SETTING_OPEN_PLEX &&
          settingPageOpener(SETTINGS_PAGE_PLAYBACK, SERVER_JELLYFIN) == SETTING_OPEN_PLAYBACK);
    // A server's page is its own: its name, a line about it, and only its rows.
    CHECK(!strcmp(settingsPageTitle(SETTINGS_PAGE_JELLYFIN, SERVER_EMBY), "Emby"));
    CHECK(settingsPageNote(SETTINGS_PAGE_JELLYFIN, SERVER_PLEX) && !settingsPageNote(SETTINGS_PAGE_MAIN, SERVER_PLEX));
    // Signing in is offered until it's done, and signing out only then.
    // A server's page: address, sign-in, options with its color, then the test (and signing out last).
    CHECK(settingsPageRows(SETTINGS_PAGE_JELLYFIN, false, SERVER_JELLYFIN, rows) == 7 && rows[0] == SETTING_JF_SERVER &&
          rows[1] == SETTING_JF_FIND && rows[2] == SETTING_JF_CODE && rows[3] == SETTING_JF_PASSWORD &&
          rows[4] == SETTING_JF_CERTIFICATE && rows[5] == SETTING_SERVER_COLOR && rows[6] == SETTING_JF_TEST);
    CHECK(settingsPageRows(SETTINGS_PAGE_JELLYFIN, true, SERVER_JELLYFIN, rows) == 5 && rows[1] == SETTING_JF_CERTIFICATE &&
          rows[2] == SETTING_SERVER_COLOR && rows[3] == SETTING_JF_TEST && rows[4] == SETTING_JF_SIGN_OUT);
    CHECK(settingsPageRows(SETTINGS_PAGE_JELLYFIN, false, SERVER_PLEX, rows) == 2 && rows[0] == SETTING_PLEX_CODE);
    CHECK(settingsPageRows(SETTINGS_PAGE_JELLYFIN, true, SERVER_PLEX, rows) == 3 && rows[0] == SETTING_PLEX_SERVER &&
          rows[1] == SETTING_SERVER_COLOR && rows[2] == SETTING_JF_SIGN_OUT);
    // Emby has no Quick Connect: a password only.
    CHECK(settingsPageRows(SETTINGS_PAGE_JELLYFIN, false, SERVER_EMBY, rows) == 6 && rows[2] == SETTING_JF_PASSWORD &&
          rows[5] == SETTING_JF_TEST);
    CHECK(settingsPageRows(SETTINGS_PAGE_JELLYFIN, true, SERVER_EMBY, rows) == 5 && rows[4] == SETTING_JF_SIGN_OUT);

    // A server's color: its own at first, then each accent, for the server the page is for; kept.
    Config colors;
    configDefaults(&colors);
    char color_name[64];
    colors.server_kind = SERVER_PLEX;
    settingValue(&colors, SETTING_SERVER_COLOR, color_name, sizeof(color_name));
    CHECK(!strcmp(color_name, "Plex's own") && settingServerColor(&colors, SERVER_PLEX) == -1);
    settingStep(&colors, SETTING_SERVER_COLOR, 2);
    settingValue(&colors, SETTING_SERVER_COLOR, color_name, sizeof(color_name));
    CHECK(!strcmp(color_name, themeAccentName(1)) && settingServerColor(&colors, SERVER_PLEX) == 1 &&
          settingServerColor(&colors, SERVER_JELLYFIN) == -1);
    settingStep(&colors, SETTING_SERVER_COLOR, -3);  // Wraps to the last accent.
    CHECK(settingServerColor(&colors, SERVER_PLEX) == THEME_ACCENT_COUNT - 1);
    FILE *cf = fopen(TMP_ROOT "/colors.txt", "w");
    if (cf) {
        configWrite(&colors, cf);
        fclose(cf);
    }
    Config colors_back;
    configDefaults(&colors_back);
    cf = fopen(TMP_ROOT "/colors.txt", "r");
    char color_line[256];
    while (cf && fgets(color_line, sizeof(color_line), cf)) {
        color_line[strcspn(color_line, "\r\n")] = '\0';
        configReadLine(&colors_back, color_line);
    }
    if (cf)
        fclose(cf);
    CHECK(settingServerColor(&colors_back, SERVER_PLEX) == THEME_ACCENT_COUNT - 1 &&
          settingServerColor(&colors_back, SERVER_EMBY) == -1);
    CHECK(configReadLine(&colors_back, "emby_color=99") && settingServerColor(&colors_back, SERVER_EMBY) == -1);
    Config kind;
    configDefaults(&kind);
    CHECK(configReadLine(&kind, "server=plex") && kind.server_kind == SERVER_PLEX);
    CHECK(configReadLine(&kind, "server=emby") && kind.server_kind == SERVER_EMBY);
    CHECK(configReadLine(&kind, "server=jellyfin") && kind.server_kind == SERVER_JELLYFIN);
    CHECK(kind.server_view == SERVER_JELLYFIN && configReadLine(&kind, "view=2") && kind.server_view == SERVER_PLEX);
    CHECK(configReadLine(&kind, "view=9") && kind.server_view == SERVER_JELLYFIN);  // Unknown: the first.
    // Certificates are checked unless turned off, for a server with its own.
    Config cert;
    configDefaults(&cert);
    char shown[32];
    settingValue(&cert, SETTING_JF_CERTIFICATE, shown, sizeof(shown));
    CHECK(cert.check_certificate[SERVER_JELLYFIN] && !strcmp(shown, "On") && settingIsChoice(SETTING_JF_CERTIFICATE));
    settingStep(&cert, SETTING_JF_CERTIFICATE, 1);
    settingValue(&cert, SETTING_JF_CERTIFICATE, shown, sizeof(shown));
    CHECK(!cert.check_certificate[SERVER_JELLYFIN] && !strcmp(shown, "Off: any certificate"));
    CHECK(cert.check_certificate[SERVER_EMBY] && cert.net_check_certificate);  // Each its own.
    CHECK(configReadLine(&cert, "jellyfin_check_certificate=1") && cert.check_certificate[SERVER_JELLYFIN]);
    // An older file's only key was every server's: Emby and the network folder start from it.
    CHECK(configReadLine(&cert, "jellyfin_check_certificate=0") && !cert.check_certificate[SERVER_JELLYFIN]);
    CHECK(!cert.check_certificate[SERVER_EMBY] && !cert.net_check_certificate);
    CHECK(configReadLine(&cert, "emby_check_certificate=1") && cert.check_certificate[SERVER_EMBY]);
    CHECK(configReadLine(&cert, "network_check_certificate=1") && cert.net_check_certificate);
    cert.server_kind = SERVER_EMBY;  // The row is for the server Settings shows.
    settingStep(&cert, SETTING_JF_CERTIFICATE, 1);
    CHECK(!cert.check_certificate[SERVER_EMBY] && !cert.check_certificate[SERVER_JELLYFIN]);
    settingValue(&cert, SETTING_JF_CERTIFICATE, shown, sizeof(shown));
    CHECK(!strcmp(shown, "Off: any certificate"));
    settingStep(&cert, SETTING_NET_CERTIFICATE, 1);
    settingValue(&cert, SETTING_NET_CERTIFICATE, shown, sizeof(shown));
    CHECK(!cert.net_check_certificate && !strcmp(shown, "Off: any certificate") &&
          settingIsChoice(SETTING_NET_CERTIFICATE));
    cert.server_kind = SERVER_JELLYFIN;
    Config quality;
    configDefaults(&quality);
    CHECK(settingQualityBitrate(&quality) == 25000000);  // Auto.
    settingStep(&quality, SETTING_JF_QUALITY, -1);       // Wraps to the lowest.
    CHECK(settingQualityBitrate(&quality) == 420000 && settingIsChoice(SETTING_JF_QUALITY));
    CHECK(configReadLine(&quality, "jellyfin_quality=3") && settingQualityBitrate(&quality) == 20000000);
    CHECK(settingsPageRows(SETTINGS_PAGE_DISPLAY, false, SERVER_JELLYFIN, rows) == 6 && rows[0] == SETTING_LANGUAGE &&
          rows[1] == SETTING_ACCENT && rows[4] == SETTING_CLOCK && rows[5] == SETTING_THEME);
    CHECK(strcmp(settingsPageTitle(SETTINGS_PAGE_DISPLAY, SERVER_JELLYFIN), "Display") == 0);

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
    // The font: built in, or a file of the fonts folder (shown without its extension).
    subtitleOptionValue(&c, SUBOPT_FONT, value, sizeof(value));
    CHECK(!strcmp(value, "Built-in") && !strcmp(subtitleOptionLabel(SUBOPT_FONT), "Font"));
    snprintf(c.subtitle_font, sizeof(c.subtitle_font), "%s", "Atkinson Hyperlegible.ttf");
    subtitleOptionValue(&c, SUBOPT_FONT, value, sizeof(value));
    CHECK(!strcmp(value, "Atkinson Hyperlegible"));

    // Written and read back; other keys and broken values are left alone.
    CHECK(c.update_check);  // On by default.
    settingStep(&c, SETTING_UPDATE_AUTO, 1);
    c.update_checked = 1795000000;
    snprintf(c.update_tag, sizeof(c.update_tag), "%s", "v1.14.0");
    CHECK(!c.welcomed);  // The first run asks where the videos are.
    c.welcomed = true;
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
          strcmp(back.background, "beach.jpg") == 0 && !strcmp(back.subtitle_font, "Atkinson Hyperlegible.ttf"));
    CHECK(!back.update_check && back.update_checked == 1795000000 && !strcmp(back.update_tag, "v1.14.0"));
    CHECK(back.welcomed);
    CHECK(configReadLine(&back, "update_tag=v123456789012345678901234567890123") && back.update_tag[0] == '\0');
    CHECK(configReadLine(&back, "subtitle_font=../x.ttf") && back.subtitle_font[0] == '\0');  // A name, never a path.
    CHECK(!configReadLine(&back, "folder=sdmc:/Videos"));
    CHECK(configReadLine(&back, "theme=99") && back.theme == 0);
    CHECK(configReadLine(&back, "blur=x") && back.blur == 2);
    CHECK(configReadLine(&back, "background=../../x.jpg") && back.background[0] == '\0');
}
