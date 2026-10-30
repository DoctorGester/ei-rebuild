"""The game code is SSE2 code and writes plain float maths: no x87 helpers (`x87::`), no `libcrt::Ftol` and no D()
double registers (C30c) in src/game, src/lib and src/render; the original's x87 maths at its 24-bit precision is
float maths in its order, and src/lib/pc24.h where an operand was wider (docs/notes/cleanup-tooling.md, "The x87 files
to SSE2"). Checked in the sources.

usage: python tools/check_x87.py   (exit 1 when a reference is found)
"""
import glob, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# What the sources must not use (comments left out): the x87 helpers, the CRT's _ftol, D().
BANNED = re.compile(r'\bx87::\w+|\blibcrt::Ftol\b|(?<![\w.>:])D\(')


def sources():
    found = set()
    for pattern in ('src/game/*.cpp', 'src/game/*.h', 'src/lib/*.cpp', 'src/lib/*.h', 'src/render/*.cpp', 'src/render/*.h'):
        found.update(os.path.normpath(f) for f in glob.glob(os.path.join(ROOT, pattern)))
    return sorted(found)


def main():
    problems = 0
    for path in sources():
        for number, line in enumerate(open(path, encoding='utf-8', errors='replace'), 1):
            m = BANNED.search(line.split('//', 1)[0])
            if m:
                print(f'{os.path.relpath(path, ROOT)}:{number}: {m.group(0)} (plain float maths, or src/lib/pc24.h)')
                problems += 1
    print(f'{problems} problem(s)')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
