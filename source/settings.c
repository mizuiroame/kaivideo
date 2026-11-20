#include "settings.h"

#include <stdlib.h>
#include <string.h>

#include "background.h"
#include "theme.h"

static const char *const LABELS[SETTING_COUNT] = {
    "SD card folder", "Update the library", "Thumbnails", "Play next episode", "Display", "About",
    "Theme", "Accent color", "Background", "Background blur", "Clock",
    "Media server", "Server", "Sign in with a code", "Sign in with a password", "Sign out", "Video quality",
    "Find servers on this network", "Test the connection", "Certificate check", "Server type",
    "Sign in at plex.tv/link", "Server",
};
static const SettingRow PAGE_ROWS[SETTINGS_PAGE_COUNT][SETTINGS_PAGE_MAX_ROWS] = {
    {SETTING_LIBRARY, SETTING_OPEN_JELLYFIN, SETTING_RESCAN, SETTING_THUMBNAILS, SETTING_AUTOPLAY, SETTING_OPEN_DISPLAY,
     SETTING_ABOUT},
    {SETTING_THEME, SETTING_ACCENT, SETTING_BACKGROUND, SETTING_BLUR, SETTING_CLOCK, SETTING_COUNT},
    {SETTING_SERVER_KIND, SETTING_JF_SERVER, SETTING_JF_FIND, SETTING_JF_TEST, SETTING_JF_CODE, SETTING_JF_PASSWORD,
     SETTING_JF_CERTIFICATE},
};
static const SettingRow SIGNED_IN_ROWS[] = {SETTING_SERVER_KIND, SETTING_JF_SERVER, SETTING_JF_QUALITY, SETTING_JF_TEST,
                                            SETTING_JF_CERTIFICATE, SETTING_JF_SIGN_OUT, SETTING_COUNT};
static const SettingRow PLEX_ROWS[] = {SETTING_SERVER_KIND, SETTING_PLEX_CODE, SETTING_JF_QUALITY, SETTING_COUNT};
static const SettingRow PLEX_SIGNED_IN_ROWS[] = {SETTING_SERVER_KIND, SETTING_PLEX_SERVER, SETTING_JF_QUALITY,
                                                 SETTING_JF_SIGN_OUT, SETTING_COUNT};
static const char *const PAGE_TITLES[SETTINGS_PAGE_COUNT] = {"Settings", "Display", "Media server"};

// Auto: the original when the console can play it, else a good conversion.
static const struct {
    const char *name;
    long long bitrate;
} QUALITIES[] = {
    {"Auto", 25000000},    {"40 Mb/s", 40000000}, {"30 Mb/s", 30000000}, {"20 Mb/s", 20000000},
    {"15 Mb/s", 15000000}, {"10 Mb/s", 10000000}, {"8 Mb/s", 8000000},   {"6 Mb/s", 6000000},
    {"4 Mb/s", 4000000},   {"3 Mb/s", 3000000},   {"2 Mb/s", 2000000},   {"1.5 Mb/s", 1500000},
    {"1 Mb/s", 1000000},   {"720 kb/s", 720000},  {"420 kb/s", 420000},
};
#define QUALITY_COUNT ((int)(sizeof(QUALITIES) / sizeof(QUALITIES[0])))

static const char *const SIZE_NAMES[SUBTITLES_SIZE_COUNT] = {"Small", "Medium", "Large"};
static const int SIZE_PX[SUBTITLES_SIZE_COUNT] = {28, 34, 42};
static const char *const COLOR_NAMES[SUBTITLE_COLOR_COUNT] = {"White", "Yellow", "Cyan", "Green"};
static const unsigned COLORS[SUBTITLE_COLOR_COUNT] = {0xF2F2F5, 0xFFE45C, 0x6CE4FF, 0x7CF29A};
static const char *const STYLE_NAMES[SUBTITLE_STYLE_COUNT] = {"Outline", "Dark box"};
static const char *const OPTION_LABELS[SUBOPT_COUNT] = {"Size", "Color", "Style", "Font"};

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
    else if (isKey(line, length, "server"))
        c->use_plex = strcmp(value, "plex") == 0;
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
    else
        return false;
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
            c->use_plex ? "plex" : "jellyfin");
    fprintf(f, "blur=%d\nthumbnails=%d\nsubtitles=%d\nsubtitle_color=%d\nsubtitle_style=%d\n", c->blur,
            c->thumbnails ? 1 : 0, c->subtitles, c->subtitle_color, c->subtitle_style);
    if (c->subtitle_font[0] && !strpbrk(c->subtitle_font, "\r\n"))
        fprintf(f, "subtitle_font=%s\n", c->subtitle_font);
}

int settingsPageRows(SettingsPage page, bool signed_in, bool plex, SettingRow rows[SETTINGS_PAGE_MAX_ROWS])
{
    int n = 0;
    if (page < 0 || page >= SETTINGS_PAGE_COUNT)
        return 0;
    const SettingRow *list = PAGE_ROWS[page];
    if (page == SETTINGS_PAGE_JELLYFIN && plex)
        list = signed_in ? PLEX_SIGNED_IN_ROWS : PLEX_ROWS;
    else if (page == SETTINGS_PAGE_JELLYFIN && signed_in)
        list = SIGNED_IN_ROWS;
    while (n < SETTINGS_PAGE_MAX_ROWS && list[n] != SETTING_COUNT) {
        rows[n] = list[n];
        n++;
    }
    return n;
}

const char *settingsPageTitle(SettingsPage page)
{
    return page >= 0 && page < SETTINGS_PAGE_COUNT ? PAGE_TITLES[page] : "";
}

const char *settingLabel(SettingRow row)
{
    return row >= 0 && row < SETTING_COUNT ? LABELS[row] : "";
}

bool settingIsChoice(SettingRow row)
{
    return row == SETTING_THEME || row == SETTING_ACCENT || row == SETTING_BACKGROUND || row == SETTING_BLUR ||
           row == SETTING_THUMBNAILS || row == SETTING_CLOCK || row == SETTING_AUTOPLAY || row == SETTING_JF_QUALITY ||
           row == SETTING_JF_CERTIFICATE || row == SETTING_SERVER_KIND;
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
    case SETTING_JF_QUALITY: c->jf_quality = wrap(c->jf_quality, delta, QUALITY_COUNT); break;
    case SETTING_JF_CERTIFICATE: c->jf_check_certificate = !c->jf_check_certificate; break;
    case SETTING_SERVER_KIND: c->use_plex = !c->use_plex; break;
    default: break;
    }
}

void settingValue(const Config *c, SettingRow row, char *out, size_t size)
{
    switch (row) {
    case SETTING_THEME: snprintf(out, size, "%s", themeName(c->theme)); break;
    case SETTING_ACCENT: snprintf(out, size, "%s", themeAccentName(c->accent)); break;
    case SETTING_BACKGROUND: snprintf(out, size, "%s", c->background[0] ? c->background : "None"); break;
    case SETTING_BLUR: snprintf(out, size, "%s", backgroundBlurName(c->blur)); break;
    case SETTING_THUMBNAILS: snprintf(out, size, "%s", c->thumbnails ? "On" : "Off"); break;
    case SETTING_CLOCK: snprintf(out, size, "%s", c->clock_24h ? "24-hour" : "12-hour"); break;
    case SETTING_AUTOPLAY: snprintf(out, size, "%s", c->autoplay ? "On" : "Off"); break;
    case SETTING_JF_QUALITY: snprintf(out, size, "%s", QUALITIES[inRange(c->jf_quality, QUALITY_COUNT)].name); break;
    case SETTING_JF_CERTIFICATE: snprintf(out, size, "%s", c->jf_check_certificate ? "On" : "Off: any certificate"); break;
    case SETTING_SERVER_KIND: snprintf(out, size, "%s", c->use_plex ? "Plex" : "Jellyfin"); break;
    default: out[0] = '\0'; break;
    }
}

int subtitleOptionCount(bool color_applies)
{
    return color_applies ? SUBOPT_COUNT : SUBOPT_SIZE + 1;
}

const char *subtitleOptionLabel(SubtitleOption option)
{
    return option >= 0 && option < SUBOPT_COUNT ? OPTION_LABELS[option] : "";
}

void subtitleOptionValue(const Config *c, SubtitleOption option, char *out, size_t size)
{
    switch (option) {
    case SUBOPT_SIZE: snprintf(out, size, "%s", SIZE_NAMES[inRange(c->subtitles, SUBTITLES_SIZE_COUNT)]); break;
    case SUBOPT_COLOR: snprintf(out, size, "%s", COLOR_NAMES[inRange(c->subtitle_color, SUBTITLE_COLOR_COUNT)]); break;
    case SUBOPT_STYLE: snprintf(out, size, "%s", STYLE_NAMES[inRange(c->subtitle_style, SUBTITLE_STYLE_COUNT)]); break;
    case SUBOPT_FONT: {  // The file's name without its extension.
        const char *dot = strrchr(c->subtitle_font, '.');
        int n = dot && dot != c->subtitle_font ? (int)(dot - c->subtitle_font) : (int)strlen(c->subtitle_font);
        snprintf(out, size, "%.*s", n, c->subtitle_font[0] ? c->subtitle_font : "Built-in");
        if (!c->subtitle_font[0])
            snprintf(out, size, "Built-in");
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
