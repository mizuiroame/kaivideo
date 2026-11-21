// KaiVideo's text in other languages. English is the text in the code and the
// key: T("Downloads") gives the language in use's word for it, or "Downloads".
//
// A language is a text file: "# comments", a line "language = <its name>",
// then pairs of lines, the English and its translation, with a blank line
// between pairs. "\n" in a line is a line break. A translation must print
// the same placeholders (%d, %s...) as its English, in the same order, or the
// English is used: a mistake can't crash KaiVideo.
//
// Lookups may come from any thread; a new language is put in place whole, and
// the one before is kept (a reader may still hold its text).
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define LANG_NAME_MAX 48

#define T(english) langText(english)
// Marks a text kept in a table, to be shown with T() where it's used
// (T(LABELS[row])): tools/lang_check.py finds it. It does nothing.
#define N_(english) (english)

// The language in use's text for english (english itself if it has none).
const char *langText(const char *english);
// The name of the language in use ("English", "Español (Latinoamérica)").
const char *langName(void);

// Puts in use the language in text (a file's contents). False, with nothing
// changed, if it isn't one.
bool langLoadText(const char *text, size_t size);
// Back to English.
void langUseEnglish(void);
// Frees every language loaded, English in use: only when nothing reads them (at exit).
void langFreeAll(void);

// True if b prints the same placeholders as a, in the same order.
bool langSamePlaceholders(const char *a, const char *b);
