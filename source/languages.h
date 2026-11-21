// The languages KaiVideo can be shown in: English, Spanish (in the app), and
// any language file put in config/lang (a .txt, see lang.h), by its name.
// Chosen in Settings > Display > Language and kept as its id: "en", "es" or
// the file's name.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define LANGUAGES_MAX 32
#define LANGUAGE_ID_MAX 64

// Looks in folder (config/lang) for language files. Returns how many languages there are.
int languagesFind(const char *folder);
int languagesCount(void);
// A language's id and its name, by its place in the list (0 is English).
const char *languageId(int index);
const char *languageName(int index);
// Its place in the list, 0 (English) if there's none with that id.
int languageIndex(const char *id);
// Puts a language in use. False (English in use) if it can't be read.
bool languageUse(const char *id);
