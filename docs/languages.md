# Languages

[Back to the README](../README.md)

![Home in Spanish](screenshots/home_es.png)

KaiVideo comes in nine languages. Choose one in **Settings > Display >
Language**; the change shows at once.

| Language | File |
|----------|------|
| English | built in |
| Español (Latinoamérica) | [lang_es.txt](../data/lang_es.txt) |
| Français | [lang_fr.txt](../data/lang_fr.txt) |
| Italiano | [lang_it.txt](../data/lang_it.txt) |
| Português (Brasil) | [lang_pt.txt](../data/lang_pt.txt) |
| Deutsch | [lang_de.txt](../data/lang_de.txt) |
| 日本語 | [lang_ja.txt](../data/lang_ja.txt) |
| 简体中文 | [lang_zh.txt](../data/lang_zh.txt) |
| 한국어 | [lang_ko.txt](../data/lang_ko.txt) |

Spanish has been checked on the console. The other seven haven't been
reviewed by native speakers yet: corrections are welcome as pull requests.

Names that come from your files or your server (titles, libraries, track
names) are shown as they are.

## Adding a language

A language is a plain text file. Put it in the `lang` folder of KaiVideo's
`config` folder (`sdmc:/switch/kaivideo/config/lang/`), and it shows up in
**Language** the next time KaiVideo starts.

The file starts with its name, then each English text followed by its
translation, with a blank line between pairs:

```
# KaiVideo in Italian.
language = Italiano

Settings
Impostazioni

%d episodes
%d episodi
```

- Lines starting with `#` are comments.
- `\n` is a line break.
- `%d`, `%s` and the like must stay as they are, in the same order. A
  translation whose `%` parts don't match isn't used, and the English text
  shows instead, so a mistake can't break KaiVideo.
- Help lines keep their button names: `[A] open` becomes `[A] apri`.
- A text missing from the file shows in English.

To start a new language, or check one, use `tools/lang_check.py` from this
repository:

```sh
python tools/lang_check.py --template > italiano.txt   # every text, to fill in
python tools/lang_check.py italiano.txt                # what's missing, unused or wrong
```

The languages built into KaiVideo are the `data/lang_*.txt` files above.
New translations are welcome as pull requests.

Japanese, Chinese and Korean text is drawn with the console's own fonts.
