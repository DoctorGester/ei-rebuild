"""Writes game.exe's string constants as C++ literals (C11; docs/notes/standalone.md, section 1).

Code that passes a string of the exe's data by address (`game::Text(0x0078D848)`, a table of name
addresses) can use the literal instead: vanilla's and Rebirth's game.exe hold the same data. This
reads each address's NUL-terminated string from both exes (they must agree) and prints it, or with
--apply replaces the address in the source by the escaped literal. Addresses outside the exe's
initialised .rdata / .data (the .bss, code) are left alone and reported as SKIP; a string that starts
inside another one is marked MID-STRING. Read the output before committing: an address in the data
range is not always a string (vtables, floats, tables), so restrict the pass with --wrap / --before
/ --lines to the places that take text, then fix the parameter types (std::uintptr_t -> const char*).

usage: python tools/exe_strings.py <source file> [--apply] [--lines 10-20,31]
           [--wrap game::Text] [--before REGEX]
  --wrap X      only `X(0x...)`; the call is replaced by the literal
  --before RE   only addresses that RE matches right before (e.g. "LogLine\\(")
  --lines       only these lines
  --skip A,B    leave these addresses alone (e.g. the empty string 0079B298, whose pointer is compared)
  --exe PATH    the exes to read (default run\\vanilla and run\\rebirth's game.exe; repeatable)
"""
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_EXES = [os.path.join(ROOT, 'run', 'vanilla', 'game.exe'), os.path.join(ROOT, 'run', 'rebirth', 'game.exe')]
# .rdata and .data of game.exe 1.06 (0x0073B000-0x007CC298; the strings are in both).
ADDRESS = r'0x(00(?:73[B-Fb-f][0-9A-Fa-f]{3}|7[4-9A-Ca-c][0-9A-Fa-f]{4}))\b'


def load(path):
    data = open(path, 'rb').read()
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    count = struct.unpack_from('<H', data, pe + 6)[0]
    optional = struct.unpack_from('<H', data, pe + 20)[0]
    base = struct.unpack_from('<I', data, pe + 24 + 28)[0]
    sections = []
    for i in range(count):
        vsize, va, rsize, raw = struct.unpack_from('<IIII', data, pe + 24 + optional + 40 * i + 8)
        sections.append((base + va, min(vsize, rsize), raw))
    return data, sections


def read_string(image, address):
    data, sections = image
    for va, size, raw in sections:
        if va <= address < va + size:
            offset = raw + address - va
            return data[offset:data.index(b'\0', offset)], data[offset - 1] == 0
    return None, None


def literal(text):
    out = []
    octal = False   # the last piece is an octal escape (a digit after it is a new literal: MSVC C4125)
    for b in text:
        c = chr(b)
        if octal and c.isdigit():
            out.append('" "')
        octal = (b >= 127 or b < 32) and b not in (9, 10, 13)
        if c in '\\"':
            out.append('\\' + c)
        elif c == '\n':
            out.append('\\n')
        elif c == '\t':
            out.append('\\t')
        elif c == '\r':
            out.append('\\r')
        elif 32 <= b < 127:
            out.append(c)
        else:
            out.append('\\%03o' % b)   # octal: a following hex digit cannot extend it
    return '"' + ''.join(out) + '"'


def option(argv, name, default=None):
    return argv[argv.index(name) + 1] if name in argv else default


def main(argv):
    if not argv or argv[0].startswith('-'):
        print(__doc__)
        return 2
    path = argv[0]
    exes = [argv[i + 1] for i, a in enumerate(argv) if a == '--exe'] or DEFAULT_EXES
    images = [load(p) for p in exes]
    wrap, before = option(argv, '--wrap'), option(argv, '--before')
    skip = {int(a, 16) for a in option(argv, '--skip', '').split(',') if a}
    lines = None
    if '--lines' in argv:
        lines = set()
        for part in option(argv, '--lines').split(','):
            a, _, b = part.partition('-')
            lines.update(range(int(a), int(b or a) + 1))
    # `X(0x...)` and `X(std::uintptr_t(0x...))` (the inner cast goes with the call)
    rx = re.compile((re.escape(wrap) + r'\(\s*(?:std::uintptr_t\(\s*)?' if wrap else r'\b') + ADDRESS
                    + (r'(?:\s*\))?\s*\)' if wrap else ''))
    source = open(path, encoding='utf-8').read().split('\n')
    for number, line in enumerate(source, 1):
        if lines and number not in lines:
            continue

        def replace(m):
            if before and not re.search('(?:' + before + r')\s*$', line[:m.start()]):
                return m.group(0)
            address = int(m.group(1), 16)
            if address in skip:
                print(f'{number}: {address:08X} kept (--skip)')
                return m.group(0)
            found = [read_string(image, address) for image in images]
            text, starts = found[0]
            if text is None:
                print(f'{number}: {address:08X} SKIP (not in the initialised data)')
                return m.group(0)
            if any(f[0] != text for f in found):
                print(f'{number}: {address:08X} SKIP (the exes differ)')
                return m.group(0)
            print(f'{number}: {address:08X} {literal(text)}{"" if starts else "  (MID-STRING)"}')
            return literal(text)
        source[number - 1] = rx.sub(replace, line)
    if '--apply' in argv:
        open(path, 'w', encoding='utf-8', newline='\n').write('\n'.join(source))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
