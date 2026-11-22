#include "settings.h"

#include <stdlib.h>
#include <string.h>

#include "background.h"
#include "lang.h"
#include "theme.h"

static const char *const LABELS[SETTING_COUNT] = {
    N_("SD card folder"), N_("Update the library"), N_("Thumbnails"), N_("Play next episode"), N_("Display"),
    N_("About"), N_("Theme"), N_("Accent color"), N_("Background"), N_("Background blur"), N_("Clock"),
    N_("Jellyfin"), N_("Server"), N_("Sign in with a code"), N_("Sign in with a password"), N_("Sign out"),
    N_("Server video quality"), N_("Find servers on this network"), N_("Test the connection"),
    N_("Certificate check"), N_("Sign in at plex.tv/link"), N_("Server"), N_("Emby"), N_("Plex"), N_("Library"),
    N_("Playback"), N_("Color"), N_("Language"), N_("Network folder"), N_("Address"), N_("User and password"),
    N_("Test the connection"), N_("Forget this folder"), N_("Remember the password"),
    N_("Allow without signing (old NAS)"), N_("Credits and licenses"), N_("Check once a day"),
    N_("Look for an update"),
};
#define SERVER_COLOR_COUNT (THEME_ACCENT_COUNT + 1)  // Its own, then each accent.
static const char *const SERVER_COLOR_KEYS[SERVER_KIND_COUNT] = {"jellyfin_color", "emby_color", "plex_color"};
static const SettingRow PAGE_ROWS[SETTINGS_PAGE_COUNT][SETTINGS_PAGE_MAX_ROWS] = {
    {SETTING_OPEN_LIBRARY, SETTING_OPEN_NETWORK, SETTING_OPEN_JELLYFIN, SETTING_OPEN_EMBY, SETTING_OPEN_PLEX,
     SETTING_OPEN_PLAYBACK, SETTING_OPEN_DISPLAY, SETTING_ABOUT},
    {SETTING_LANGUAGE, SETTING_THEME, SETTING_ACCENT, SETTING_BACKGROUND, SETTING_BLUR, SETTING_CLOCK, SETTING_COUNT},
    {SETTING_JF_SERVER, SETTING_JF_FIND, SETTING_JF_TEST, SETTING_JF_CODE, SETTING_JF_PASSWORD, SETTING_JF_CERTIFICATE,
     SETTING_SERVER_COLOR, SETTING_COUNT},
    {SETTING_LIBRARY, SETTING_RESCAN, SETTING_THUMBNAILS, SETTING_COUNT},
    {SETTING_AUTOPLAY, SETTING_JF_QUALITY, SETTING_COUNT},
    {SETTING_NET_ADDRESS, SETTING_NET_ACCOUNT, SETTING_NET_REMEMBER, SETTING_NET_UNSIGNED, SETTING_NET_TEST,
     SETTING_NET_FORGET, SETTING_COUNT},
    {SETTING_CREDITS, SETTING_UPDATE_AUTO, SETTING_UPDATE_NOW, SETTING_COUNT},
};
static const SettingRow SIGNED_IN_ROWS[] = {SETTING_JF_SERVER, SETTING_JF_TEST, SETTING_JF_CERTIFICATE,
                                            SETTING_JF_SIGN_OUT, SETTING_SERVER_COLOR, SETTING_COUNT};
static const SettingRow PLEX_ROWS[] = {SETTING_PLEX_CODE, SETTING_SERVER_COLOR, SETTING_COUNT};
static const SettingRow PLEX_SIGNED_IN_ROWS[] = {SETTING_PLEX_SERVER, SETTING_JF_SIGN_OUT, SETTING_SERVER_COLOR,
                                                 SETTING_COUNT};
// Emby has no Quick Connect: a user name and password.
static const SettingRow EMBY_ROWS[] = {SETTING_JF_SERVER, SETTING_JF_FIND, SETTING_JF_TEST, SETTING_JF_PASSWORD,
                                       SETTING_JF_CERTIFICATE, SETTING_SERVER_COLOR, SETTING_COUNT};
static const char *const PAGE_TITLES[SETTINGS_PAGE_COUNT] = {N_("Settings"), N_("Display"), "", N_("Library"),
                                                              N_("Playback"), N_("Network folder"), N_("About")};
static const char *const SERVER_NOTES[SERVER_KIND_COUNT] = {
    N_("Your Jellyfin server: sign in with Quick Connect or a password."),
    N_("Your Emby server: sign in with its user name and password."),
    N_("Your Plex account: sign in at plex.tv/link, and its server is found."),
};

// Auto: the original when the console can play it, else a good conversion.
static const struct {
    const char *name;
    long long bitrate;
} QUALITIES[] = {
    {N_("Auto"), 25000000},    {N_("40 Mb/s"), 40000000}, {N_("30 Mb/s"), 30000000},  {N_("20 Mb/s"), 20000000},
    {N_("15 Mb/s"), 15000000}, {N_("10 Mb/s"), 10000000}, {N_("8 Mb/s"), 8000000},     {N_("6 Mb/s"), 6000000},
    {N_("4 Mb/s"), 4000000},   {N_("3 Mb/s"), 3000000},   {N_("2 Mb/s"), 2000000},     {N_("1.5 Mb/s"), 1500000},
    {N_("1 Mb/s"), 1000000},   {N_("720 kb/s"), 720000},  {N_("420 kb/s"), 420000},
};
#define QUALITY_COUNT ((int)(sizeof(QUALITIES) / sizeof(QUALITIES[0])))

static const char *const SIZE_NAMES[SUBTITLES_SIZE_COUNT] = {N_("Small"), N_("Medium"), N_("Large")};
static const int SIZE_PX[SUBTITLES_SIZE_COUNT] = {28, 34, 42};
static const char *const COLOR_NAMES[SUBTITLE_COLOR_COUNT] = {N_("White"), N_("Yellow"), N_("Cyan"), N_("Green")};
static const unsigned COLORS[SUBTITLE_COLOR_COUNT] = {0xF2F2F5, 0xFFE45C, 0x6CE4FF, 0x7CF29A};
static const char *const STYLE_NAMES[SUBTITLE_STYLE_COUNT] = {N_("Outline"), N_("Dark box")};
static const char *const OPTION_LABELS[SUBOPT_COUNT] = {N_("Size"), N_("Color"), N_("Style"), N_("Font")};

static int inRange(int value, int count)
{
    return value >= 0 && value < count ? value : 0;
}

void configDefaults(Config *c)
{
    memset(c, 0, sizeof(*c));
    c->blur = BLUR_MEDIUM;
    c->thumbnails = true;
    c->autoplay = true;
    c->jf_check_certificate = true;
    c->subtitles = SUBTITLES_MEDIUM;
    c->update_check = true;
}

// A number in 0..count-1, or fallback.
static int readChoice(const char *value, int count, int fallback)
{
    char *end;
    long n = strtol(value, &end, 10);
    return end != value && *end == '\0' && n >= 0 && n < count ? (int)n : fallback;
}

static bool isKey(const char *line, size_t length, const char *key)
{
    return strlen(key) == length && strncmp(line, key, length) == 0;
}

static bool readServerColor(Config *c, const char *line, size_t length, const char *value)
{
    for (int k = 0; k < SERVER_KIND_COUNT; k++)
        if (isKey(line, length, SERVER_COLOR_KEYS[k])) {
            c->server_color[k] = readChoice(value, SERVER_COLOR_COUNT, 0);
            return true;
        }
    return false;
}

bool configReadLine(Config *c, const char *line)
{
    const char *eq = strchr(line, '=');
    if (!eq)
        return false;
    size_t length = (size_t)(eq - line);
    const char *value = eq + 1;
    if (isKey(line, length, "theme"))
        c->theme = readChoice(value, THEME_COUNT, 0);
    else if (isKey(line, length, "accent"))
        c->accent = readChoice(value, THEME_ACCENT_COUNT, 0);
    else if (isKey(line, length, "background"))  // A name in the Backgrounds folder, never a path.
        snprintf(c->background, sizeof(c->background), "%s", strchr(value, '/') ? "" : value);
    else if (isKey(line, length, "blur"))
        c->blur = readChoice(value, BLUR_COUNT, BLUR_MEDIUM);
    else if (isKey(line, length, "clock24"))
        c->clock_24h = strcmp(value, "1") == 0;
    else if (isKey(line, length, "thumbnails"))
        c->thumbnails = strcmp(value, "0") != 0;
    else if (isKey(line, length, "autoplay"))
        c->autoplay = strcmp(value, "0") != 0;
    else if (isKey(line, length, "jellyfin"))
        c->use_jellyfin = strcmp(value, "1") == 0;
    else if (isKey(line, length, "network_view"))
        c->use_network = strcmp(value, "1") == 0;
    else if (isKey(line, length, "server"))
        c->server_kind = !strcmp(value, "plex") ? SERVER_PLEX : !strcmp(value, "emby") ? SERVER_EMBY : SERVER_JELLYFIN;
    else if (isKey(line, length, "view"))
        c->server_view = readChoice(value, SERVER_KIND_COUNT, SERVER_JELLYFIN);
    else if (isKey(line, length, "jellyfin_quality"))
        c->jf_quality = readChoice(value, QUALITY_COUNT, 0);
    else if (isKey(line, length, "jellyfin_check_certificate"))
        c->jf_check_certificate = strcmp(value, "0") != 0;
    else if (isKey(line, length, "subtitles"))
        c->subtitles = readChoice(value, SUBTITLES_SIZE_COUNT, SUBTITLES_MEDIUM);
    else if (isKey(line, length, "subtitle_color"))
        c->subtitle_color = readChoice(value, SUBTITLE_COLOR_COUNT, 0);
    else if (isKey(line, length, "subtitle_style"))
        c->subtitle_style = readChoice(value, SUBTITLE_STYLE_COUNT, SUBTITLE_OUTLINE);
    else if (isKey(line, length, "subtitle_font"))  // A name in the fonts folder, never a path.
        snprintf(c->subtitle_font, sizeof(c->subtitle_font), "%s", strpbrk(value, "/\\") ? "" : value);
    else if (isKey(line, length, "update_check"))
        c->update_check = strcmp(value, "0") != 0;
    else if (isKey(line, length, "update_checked"))
        c->update_checked = strtoll(value, NULL, 10);
    else if (isKey(line, length, "update_tag"))  // A version, as GitHub names it.
        snprintf(c->update_tag, sizeof(c->update_tag), "%s", strlen(value) < sizeof(c->update_tag) ? value : "");
    else if (isKey(line, length, "language"))  // An id, or a name in the lang folder: never a path.
        snprintf(c->language, sizeof(c->language), "%s", strpbrk(value, "/\\") ? "" : value);
    else
        return readServerColor(c, line, length, value);
    return true;
}

void configWrite(const Config *c, FILE *f)
{
    fprintf(f, "theme=%d\naccent=%d\n", c->theme, c->accent);
    if (c->background[0] && !strpbrk(c->background, "\r\n"))
        fprintf(f, "background=%s\n", c->background);
    fprintf(f, "clock24=%d\nautoplay=%d\njellyfin=%d\njellyfin_quality=%d\n", c->clock_24h ? 1 : 0, c->autoplay ? 1 : 0,
            c->use_jellyfin ? 1 : 0, c->jf_quality);
    fprintf(f, "jellyfin_check_certificate=%d\nserver=%s\n", c->jf_check_certificate ? 1 : 0,
            c->server_kind == SERVER_PLEX ? "plex" : c->server_kind == SERVER_EMBY ? "emby" : "jellyfin");
    fprintf(f, "view=%d\nnetwork_view=%d\n", c->server_view, c->use_network ? 1 : 0);
    fprintf(f, "blur=%d\nthumbnails=%d\nsubtitles=%d\nsubtitle_color=%d\nsubtitle_style=%d\n", c->blur,
            c->thumbnails ? 1 : 0, c->subtitles, c->subtitle_color, c->subtitle_style);
    if (c->subtitle_font[0] && !strpbrk(c->subtitle_font, "\r\n"))
        fprintf(f, "subtitle_font=%s\n", c->subtitle_font);
    for (int k = 0; k < SERVER_KIND_COUNT; k++)
        fprintf(f, "%s=%d\n", SERVER_COLOR_KEYS[k], c->server_color[k]);
    if (c->language[0] && !strpbrk(c->language, "\r\n"))
        fprintf(f, "language=%s\n", c->language);
    fprintf(f, "update_check=%d\nupdate_checked=%lld\n", c->update_check ? 1 : 0, (long long)c->update_checked);
    if (c->update_tag[0] && !strpbrk(c->update_tag, "\r\n"))
        fprintf(f, "update_tag=%s\n", c->update_tag);
}

int settingsPageRows(SettingsPage page, bool signed_in, ServerKind kind, SettingRow rows[SETTINGS_PAGE_MAX_ROWS])
{
    int n = 0;
    if (page < 0 || page >= SETTINGS_PAGE_COUNT)
        return 0;
    const SettingRow *list = PAGE_ROWS[page];
    if (page == SETTINGS_PAGE_JELLYFIN && kind == SERVER_PLEX)
        list = signed_in ? PLEX_SIGNED_IN_ROWS : PLEX_ROWS;
    else if (page == SETTINGS_PAGE_JELLYFIN && kind == SERVER_EMBY && !signed_in)
        list = EMBY_ROWS;
    else if (page == SETTINGS_PAGE_JELLYFIN && signed_in)
        list = SIGNED_IN_ROWS;
    while (n < SETTINGS_PAGE_MAX_ROWS && list[n] != SETTING_COUNT) {
        rows[n] = list[n];
        n++;
    }
    return n;
}

const char *settingsPageTitle(SettingsPage page, ServerKind kind)
{
    if (page == SETTINGS_PAGE_JELLYFIN)
        return serverKindName(kind);
    return page >= 0 && page < SETTINGS_PAGE_COUNT ? T(PAGE_TITLES[page]) : "";
}

const char *settingsPageNote(SettingsPage page, ServerKind kind)
{
    if (page == SETTINGS_PAGE_NETWORK)
        return T("A shared folder (SMB) or WebDAV folder on your network. X shows its library; Files shows it first.");
    return page == SETTINGS_PAGE_JELLYFIN && kind >= 0 && kind < SERVER_KIND_COUNT ? T(SERVER_NOTES[kind]) : NULL;
}

bool settingOpensPage(SettingRow row, SettingsPage *page, ServerKind *kind)
{
    *kind = SERVER_JELLYFIN;
    switch (row) {
    case SETTING_OPEN_DISPLAY: *page = SETTINGS_PAGE_DISPLAY; return true;
    case SETTING_OPEN_LIBRARY: *page = SETTINGS_PAGE_LIBRARY; return true;
    case SETTING_OPEN_PLAYBACK: *page = SETTINGS_PAGE_PLAYBACK; return true;
    case SETTING_OPEN_NETWORK: *page = SETTINGS_PAGE_NETWORK; return true;
    case SETTING_ABOUT: *page = SETTINGS_PAGE_ABOUT; return true;
    case SETTING_OPEN_JELLYFIN: *page = SETTINGS_PAGE_JELLYFIN; return true;
    case SETTING_OPEN_EMBY: *page = SETTINGS_PAGE_JELLYFIN; *kind = SERVER_EMBY; return true;
    case SETTING_OPEN_PLEX: *page = SETTINGS_PAGE_JELLYFIN; *kind = SERVER_PLEX; return true;
    default: return false;
    }
}

SettingRow settingPageOpener(SettingsPage page, ServerKind kind)
{
    switch (page) {
    case SETTINGS_PAGE_DISPLAY: return SETTING_OPEN_DISPLAY;
    case SETTINGS_PAGE_LIBRARY: return SETTING_OPEN_LIBRARY;
    case SETTINGS_PAGE_PLAYBACK: return SETTING_OPEN_PLAYBACK;
    case SETTINGS_PAGE_NETWORK: return SETTING_OPEN_NETWORK;
    case SETTINGS_PAGE_ABOUT: return SETTING_ABOUT;
    case SETTINGS_PAGE_JELLYFIN:
        return kind == SERVER_PLEX ? SETTING_OPEN_PLEX : kind == SERVER_EMBY ? SETTING_OPEN_EMBY : SETTING_OPEN_JELLYFIN;
    default: return SETTING_OPEN_LIBRARY;
    }
}

const char *settingLabel(SettingRow row)
{
    return row >= 0 && row < SETTING_COUNT ? T(LABELS[row]) : "";
}

bool settingIsChoice(SettingRow row)
{
    return row == SETTING_THEME || row == SETTING_ACCENT || row == SETTING_BACKGROUND || row == SETTING_BLUR ||
           row == SETTING_THUMBNAILS || row == SETTING_CLOCK || row == SETTING_AUTOPLAY || row == SETTING_JF_QUALITY ||
           row == SETTING_JF_CERTIFICATE || row == SETTING_SERVER_COLOR || row == SETTING_LANGUAGE ||
           row == SETTING_NET_REMEMBER || row == SETTING_NET_UNSIGNED || row == SETTING_UPDATE_AUTO;
}

static int wrap(int value, int delta, int count)
{
    return ((value + delta) % count + count) % count;
}

void settingStep(Config *c, SettingRow row, int delta)
{
    switch (row) {
    case SETTING_THEME: c->theme = wrap(c->theme, delta, THEME_COUNT); break;
    case SETTING_ACCENT: c->accent = wrap(c->accent, delta, THEME_ACCENT_COUNT); break;
    case SETTING_BLUR: c->blur = wrap(c->blur, delta, BLUR_COUNT); break;
    case SETTING_THUMBNAILS: c->thumbnails = !c->thumbnails; break;
    case SETTING_CLOCK: c->clock_24h = !c->clock_24h; break;
    case SETTING_AUTOPLAY: c->autoplay = !c->autoplay; break;
    case SETTING_UPDATE_AUTO: c->update_check = !c->update_check; break;
    case SETTING_JF_QUALITY: c->jf_quality = wrap(c->jf_quality, delta, QUALITY_COUNT); break;
    case SETTING_JF_CERTIFICATE: c->jf_check_certificate = !c->jf_check_certificate; break;
    case SETTING_SERVER_COLOR: {  // For the server Settings shows.
        int k = inRange(c->server_kind, SERVER_KIND_COUNT);
        c->server_color[k] = wrap(c->server_color[k], delta, SERVER_COLOR_COUNT);
        break;
    }
    default: break;
    }
}

void settingValue(const Config *c, SettingRow row, char *out, size_t size)
{
    switch (row) {
    case SETTING_THEME: snprintf(out, size, "%s", themeName(c->theme)); break;
    case SETTING_ACCENT: snprintf(out, size, "%s", themeAccentName(c->accent)); break;
    case SETTING_BACKGROUND: snprintf(out, size, "%s", c->background[0] ? c->background : T("None")); break;
    case SETTING_BLUR: snprintf(out, size, "%s", backgroundBlurName(c->blur)); break;
    case SETTING_THUMBNAILS: snprintf(out, size, "%s", c->thumbnails ? T("On") : T("Off")); break;
    case SETTING_CLOCK: snprintf(out, size, "%s", c->clock_24h ? T("24-hour") : T("12-hour")); break;
    case SETTING_AUTOPLAY: snprintf(out, size, "%s", c->autoplay ? T("On") : T("Off")); break;
    case SETTING_UPDATE_AUTO: snprintf(out, size, "%s", c->update_check ? T("On") : T("Off")); break;
    case SETTING_JF_QUALITY:
        snprintf(out, size, "%s", T(QUALITIES[inRange(c->jf_quality, QUALITY_COUNT)].name));
        break;
    case SETTING_JF_CERTIFICATE:
        snprintf(out, size, "%s", c->jf_check_certificate ? T("On") : T("Off: any certificate"));
        break;
    case SETTING_LANGUAGE: snprintf(out, size, "%s", c->language_name[0] ? c->language_name : "English"); break;
    case SETTING_SERVER_COLOR: {
        int k = inRange(c->server_kind, SERVER_KIND_COUNT), choice = c->server_color[k];
        if (choice <= 0)
            snprintf(out, size, T("%s's own"), serverKindName((ServerKind)k));
        else
            snprintf(out, size, "%s", themeAccentName(choice - 1));
        break;
    }
    default: out[0] = '\0'; break;
    }
}

int settingServerColor(const Config *c, ServerKind kind)
{
    return kind >= 0 && kind < SERVER_KIND_COUNT ? c->server_color[kind] - 1 : -1;
}

int subtitleOptionCount(bool color_applies)
{
    return color_applies ? SUBOPT_COUNT : SUBOPT_SIZE + 1;
}

const char *subtitleOptionLabel(SubtitleOption option)
{
    return option >= 0 && option < SUBOPT_COUNT ? T(OPTION_LABELS[option]) : "";
}

void subtitleOptionValue(const Config *c, SubtitleOption option, char *out, size_t size)
{
    switch (option) {
    case SUBOPT_SIZE: snprintf(out, size, "%s", T(SIZE_NAMES[inRange(c->subtitles, SUBTITLES_SIZE_COUNT)])); break;
    case SUBOPT_COLOR:
        snprintf(out, size, "%s", T(COLOR_NAMES[inRange(c->subtitle_color, SUBTITLE_COLOR_COUNT)]));
        break;
    case SUBOPT_STYLE:
        snprintf(out, size, "%s", T(STYLE_NAMES[inRange(c->subtitle_style, SUBTITLE_STYLE_COUNT)]));
        break;
    case SUBOPT_FONT: {  // The file's name without its extension.
        const char *dot = strrchr(c->subtitle_font, '.');
        int n = dot && dot != c->subtitle_font ? (int)(dot - c->subtitle_font) : (int)strlen(c->subtitle_font);
        snprintf(out, size, "%.*s", n, c->subtitle_font[0] ? c->subtitle_font : T("Built-in"));
        if (!c->subtitle_font[0])
            snprintf(out, size, "%s", T("Built-in"));
        break;
    }
    default: out[0] = '\0'; break;
    }
}

void subtitleOptionStep(Config *c, SubtitleOption option, int delta)
{
    switch (option) {
    case SUBOPT_SIZE: c->subtitles = wrap(c->subtitles, delta, SUBTITLES_SIZE_COUNT); break;
    case SUBOPT_COLOR: c->subtitle_color = wrap(c->subtitle_color, delta, SUBTITLE_COLOR_COUNT); break;
    case SUBOPT_STYLE: c->subtitle_style = wrap(c->subtitle_style, delta, SUBTITLE_STYLE_COUNT); break;
    default: break;
    }
}

long long settingQualityBitrate(const Config *c)
{
    return QUALITIES[inRange(c->jf_quality, QUALITY_COUNT)].bitrate;
}

int settingSubtitlePx(const Config *c)
{
    return SIZE_PX[inRange(c->subtitles, SUBTITLES_SIZE_COUNT)];
}

double settingSubtitleScale(const Config *c)
{
    return (double)settingSubtitlePx(c) / SIZE_PX[SUBTITLES_MEDIUM];
}

unsigned settingSubtitleColor(const Config *c)
{
    return COLORS[inRange(c->subtitle_color, SUBTITLE_COLOR_COUNT)];
}
