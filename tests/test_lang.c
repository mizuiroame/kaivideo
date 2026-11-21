// Translations: reading a language's file, finding a text in it, and keeping
// English where a translation would break a formatted text.
#include <stdio.h>
#include <string.h>

#include "check.h"
#include "lang.h"

static const char SPANISH[] =
    "# KaiVideo in Spanish.\n"
    "language = Espa\xC3\xB1ol\n"
    "\n"
    "Downloads\n"
    "Descargas\n"
    "\n"
    "# A comment between two.\n"
    "%d episodes added to Downloads.\n"
    "Se agregaron %d episodios a Descargas.\n"
    "\n"
    "Downloading %d%%\n"
    "Descargando %d%%\n"
    "\n"
    "Press X again\\nto delete it.\n"
    "Pulsa X otra vez\\npara borrarlo.\n"
    "\n"
    "Bad: %s of %s\n"
    "Malo: %s\n"
    "\n"
    "Swapped %d %s\n"
    "Cambiado %s %d\n"
    "\n"
    "Untranslated\n"
    "\n"
    "Settings\r\n"
    "Configuraci\xC3\xB3n\r\n";

static void testLookup(void)
{
    langUseEnglish();
    CHECK(!strcmp(T("Downloads"), "Downloads") && !strcmp(langName(), "English"));
    CHECK(langLoadText(SPANISH, sizeof(SPANISH) - 1));
    CHECK(!strcmp(langName(), "Espa\xC3\xB1ol"));
    CHECK(!strcmp(T("Downloads"), "Descargas"));
    CHECK(!strcmp(T("%d episodes added to Downloads."), "Se agregaron %d episodios a Descargas."));
    CHECK(!strcmp(T("Downloading %d%%"), "Descargando %d%%"));
    CHECK(!strcmp(T("Press X again\nto delete it."), "Pulsa X otra vez\npara borrarlo."));  // \n in the file.
    CHECK(!strcmp(T("Settings"), "Configuraci\xC3\xB3n"));                               // Windows line ends.
    // A translation that would print other things than the English stays English.
    CHECK(!strcmp(T("Bad: %s of %s"), "Bad: %s of %s"));
    CHECK(!strcmp(T("Swapped %d %s"), "Swapped %d %s"));
    CHECK(!strcmp(T("Untranslated"), "Untranslated"));  // A pair without its second line.
    CHECK(!strcmp(T("Not in the file"), "Not in the file"));
    CHECK(T(NULL) == NULL && !strcmp(T(""), ""));
    langUseEnglish();
    CHECK(!strcmp(T("Downloads"), "Downloads"));
    CHECK(!langLoadText("no language line\n\nA\nB\n", 22));  // Without its name: not a translation.
}

static void testPlaceholders(void)
{
    CHECK(langSamePlaceholders("%d of %s", "%d de %s"));
    CHECK(langSamePlaceholders("100%% %.1f GB", "%.1f GB al 100%%"));
    CHECK(!langSamePlaceholders("%d of %s", "%s de %d"));
    CHECK(!langSamePlaceholders("%d", "%ld"));
    CHECK(!langSamePlaceholders("%d", "%d %d"));
    CHECK(langSamePlaceholders("%-12s|%5.2lf", "%-12s|%5.2lf"));
    CHECK(!langSamePlaceholders("text", "texto %n"));
}

void testLang(void)
{
    testLookup();
    testPlaceholders();
    langFreeAll();
}
