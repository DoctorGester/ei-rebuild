"""Runs clang-format (the repository's .clang-format) on the lines a change touched, and on each file's include
block, never on the rest of the file (a whole-file run would rewrite the comments' alignment and the line
breaks all over a file nobody meant to touch).

usage: python tools/format_changed.py [--base REV] [--check] [file ...]
  --base REV  the revision the change is measured against (default HEAD: the uncommitted change)
  --check     list the files that would change; write nothing
  no files    every .h / .cpp / .inc that differs from the base
"""
import difflib
import pathlib
import re
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CLANG_FORMAT = shutil.which('clang-format') or r'E:\bin\vs\buildtools\VC\Tools\Llvm\bin\clang-format.exe'
SUFFIXES = ('.h', '.cpp', '.inc', '.mm')


def git(*args):
    return subprocess.run(['git'] + list(args), cwd=ROOT, capture_output=True, check=True).stdout.decode('utf-8')


def changed_ranges(path, base):
    ranges = []
    for line in git('diff', '-U0', base, '--', path).splitlines():
        m = re.match(r'@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@', line)
        if m:
            start, count = int(m.group(1)), int(m.group(2) or 1)
            if count:
                ranges.append((start, start + count - 1))
    return ranges


def include_range(lines):
    numbers = [i + 1 for i, l in enumerate(lines) if l.startswith('#include')]
    return (min(numbers), max(numbers)) if numbers else None


def main():
    args = sys.argv[1:]
    base = 'HEAD'
    check = '--check' in args
    if '--base' in args:
        base = args[args.index('--base') + 1]
    files = [a for a in args if not a.startswith('--') and a != base]
    if not files:
        files = [f for f in git('diff', '--name-only', base).split() if f.endswith(SUFFIXES)]
    total = 0
    for f in files:
        path = ROOT / f
        if not path.is_file():
            continue
        text = path.read_bytes().decode('utf-8')
        ranges = changed_ranges(f, base)
        inc = include_range(text.splitlines())
        if inc:
            ranges.append(inc)
        if not ranges:
            continue
        cmd = [str(CLANG_FORMAT), '--style=file:' + str(ROOT / '.clang-format'), '--assume-filename=' + f]
        for a, b in ranges:
            cmd.append('--lines=%d:%d' % (a, b))
        out = subprocess.run(cmd, input=text.encode('utf-8'), capture_output=True, cwd=ROOT).stdout.decode('utf-8')
        if out and out != text:
            total += 1
            if check:
                print(f, len([l for l in difflib.unified_diff(text.splitlines(), out.splitlines(), lineterm='', n=0)
                              if l[:1] in '+-' and l[:3] not in ('+++', '---')]), 'lines')
            else:
                path.write_bytes(out.encode('utf-8'))
    print('%d file(s) %s' % (total, 'would change' if check else 'formatted'))


main()
