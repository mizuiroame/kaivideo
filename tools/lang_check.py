"""KaiVideo's translations: the texts the code asks for, against a language file.

  python tools/lang_check.py data/lang_es.txt     what's missing, unused, or prints other placeholders
  python tools/lang_check.py --template           every text, as a new language file to fill in

A text is asked for with T("...") in source/*.c, or marked N_("...") in a table
(adjacent literals, as C joins them, count as one). The language file's format is in source/lang.h.
Exits with 1 if a text is missing or a translation is wrong, so it can gate a build.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
LITERAL = r'"(?:[^"\\]|\\.)*"'
CALL = re.compile(r'\b(?:T|N_)\(\s*((?:' + LITERAL + r'\s*)+)\)')
PLACEHOLDER = re.compile(r'%(?!%)[-+ #0-9.*hlLqjzt]*[a-zA-Z]')


def c_unescape(body):
    """A C literal's text, as the program sees it (UTF-8)."""
    out = bytearray()
    i = 0
    raw = body.encode('utf-8')
    while i < len(raw):
        c = raw[i]
        if c != 0x5C:  # Backslash.
            out.append(c)
            i += 1
            continue
        n = raw[i + 1:i + 2]
        if n == b'x':
            m = re.match(rb'[0-9a-fA-F]{1,2}', raw[i + 2:])
            out.append(int(m.group(0), 16))
            i += 2 + len(m.group(0))
        elif n == b'n':
            out += b'\n'
            i += 2
        else:
            out += {b't': b'\t', b'"': b'"', b'\\': b'\\', b"'": b"'"}.get(n, n)
            i += 2
    return out.decode('utf-8')


def texts_in_code():
    found = {}
    for path in sorted((ROOT / 'source').glob('*.c')):
        source = path.read_text(encoding='utf-8')
        for m in CALL.finditer(source):
            parts = re.findall(LITERAL, m.group(1))
            text = ''.join(c_unescape(p[1:-1]) for p in parts)
            line = source.count('\n', 0, m.start()) + 1
            found.setdefault(text, '%s:%d' % (path.name, line))
    return found


def placeholders(text):
    """The %d, %s... a text prints, in order (a literal %% isn't one)."""
    return PLACEHOLDER.findall(text.replace('%%', ''))


def escape(text):
    return text.replace('\n', '\\n')


def read_language(path):
    lines = [l.rstrip('\r') for l in path.read_text(encoding='utf-8').split('\n')]
    pairs, i = {}, 0
    while i < len(lines) and (not lines[i] or lines[i].startswith('#') or '=' not in lines[i]):
        i += 1
    i += 1  # The "language = ..." line.
    while i + 1 < len(lines):
        a, b = lines[i], lines[i + 1]
        if not a or a.startswith('#'):
            i += 1
            continue
        if b and not b.startswith('#'):
            pairs[a.replace('\\n', '\n')] = b.replace('\\n', '\n')
            i += 2
        else:
            i += 1
    return pairs


def main():
    code = texts_in_code()
    if sys.argv[1:] == ['--template']:
        print('# KaiVideo in <language>. Each English line is followed by its translation;')
        print('# keep %d, %s and the like as they are, in the same order. See source/lang.h.')
        print('language = <the language, in itself>\n')
        for text in sorted(code, key=str.lower):
            print(escape(text))
            print()
            print()
        return 0
    path = pathlib.Path(sys.argv[1])
    pairs = read_language(path)
    missing = [t for t in code if t not in pairs]
    unused = [t for t in pairs if t not in code]
    wrong = [t for t in pairs if t in code and placeholders(t) != placeholders(pairs[t])]
    for t in sorted(missing, key=str.lower):
        print('missing  %-24s %s' % (code[t], escape(t)))
    for t in wrong:
        print('wrong    placeholders: %s  ->  %s' % (escape(t), escape(pairs[t])))
    for t in sorted(unused, key=str.lower):
        print('unused   %s' % escape(t))
    print('%d texts in the code, %d translated, %d missing, %d wrong, %d unused' %
          (len(code), len(code) - len(missing), len(missing), len(wrong), len(unused)))
    return 1 if missing or wrong else 0


if __name__ == '__main__':
    sys.exit(main())
