#include "util.h"

#include <ctype.h>
#include <stdio.h>

int utilCompareNames(const char *a, const char *b)
{
    for (;; a++, b++) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb || ca == '\0')
            return ca - cb;
    }
}

unsigned utilNextCodePoint(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    unsigned cp;
    int extra;
    if (p[0] < 0x80) {
        cp = p[0];
        extra = 0;
    } else if ((p[0] & 0xE0) == 0xC0) {
        cp = p[0] & 0x1F;
        extra = 1;
    } else if ((p[0] & 0xF0) == 0xE0) {
        cp = p[0] & 0x0F;
        extra = 2;
    } else if ((p[0] & 0xF8) == 0xF0) {
        cp = p[0] & 0x07;
        extra = 3;
    } else {
        *s += 1;
        return '?';
    }
    for (int i = 1; i <= extra; i++) {
        if ((p[i] & 0xC0) != 0x80) {  // Also stops at the NUL terminator.
            *s += i;
            return '?';
        }
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    *s += extra + 1;
    return cp;
}

void utilFormatClock(char *out, size_t size, int hour, int minute, bool use_24h)
{
    hour = ((hour % 24) + 24) % 24;
    minute = ((minute % 60) + 60) % 60;
    if (use_24h) {
        snprintf(out, size, "%02d:%02d", hour, minute);
        return;
    }
    int h12 = hour % 12 == 0 ? 12 : hour % 12;
    snprintf(out, size, "%d:%02d %s", h12, minute, hour < 12 ? "AM" : "PM");
}

void utilFormatTime(char *out, size_t size, double seconds)
{
    if (seconds < 0)
        seconds = 0;
    unsigned total = (unsigned)seconds;
    unsigned h = total / 3600, m = (total / 60) % 60, s = total % 60;
    if (h > 0)
        snprintf(out, size, "%u:%02u:%02u", h, m, s);
    else
        snprintf(out, size, "%u:%02u", m, s);
}

bool utilUrlEscape(const char *text, char *out, size_t size)
{
    static const char HEX[] = "0123456789ABCDEF";
    size_t o = 0;
    for (const unsigned char *s = (const unsigned char *)text; *s; s++) {
        bool plain = (*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '-' ||
                     *s == '_' || *s == '.' || *s == '~';
        size_t need = plain ? 1 : 3;
        if (o + need >= size)
            return false;
        if (plain) {
            out[o++] = (char)*s;
        } else {
            out[o++] = '%';
            out[o++] = HEX[*s >> 4];
            out[o++] = HEX[*s & 15];
        }
    }
    if (o >= size)
        return false;
    out[o] = '\0';
    return true;
}
