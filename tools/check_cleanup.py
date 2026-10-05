"""Ratchet on cleanup-plan patterns (docs/cleanup-plan.md, "Where the code is now").

New code is written the clean way: members instead of raw offsets, virtual calls instead of VCall,
named globals and plain C++ floats. This counts, per file under src/, the patterns the cleanup
removes and fails when a file has more than config/cleanup_baseline.csv allows (a new file: none).
Counts may only go down; run with --update after a cleanup to lower the baseline.

usage: python tools/check_cleanup.py [--update]
"""
import csv, glob, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASELINE = os.path.join(ROOT, 'config', 'cleanup_baseline.csv')
PATTERNS = {
    'offset access (At<>/Field<>)': re.compile(r'\b(?:At|Field)\s*<'),
    'VCall slot call': re.compile(r'\bVCall\b'),
    '__asm block': re.compile(r'\b__asm\b'),
    'float_control pragma': re.compile(r'#\s*pragma\s+float_control'),
    'raw game address (0x004..0x007 literal)': re.compile(r'\b0x00[4-7][0-9A-Fa-f]{5}\b(?!\s*\))'),
    '*At view struct': re.compile(r'\bstruct\s+\w+At\b'),
}
# A literal address directly inside EI_ORIGINAL(...)/EI_REPLACE(...)/EI_VERIFY(...) is how hooks are
# written and is not counted; the lookahead above skips "0x...)" and the prefix test below the rest.
REGISTRATION = re.compile(r'EI_(?:ORIGINAL|REPLACE|REPLACE_METHOD|VERIFY|REBIRTH_IMPLEMENTS)\s*\(\s*$')


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', lambda m: re.sub(r'[^\n]', ' ', m.group(0)), text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def counts():
    out = {}
    for path in glob.glob(os.path.join(ROOT, 'src', '**', '*.*'), recursive=True):
        if not path.endswith(('.cpp', '.h', '.inc')) or os.sep + 'hook' + os.sep in path:
            continue
        text = strip_comments(open(path, encoding='utf-8', errors='replace').read())
        rel = os.path.relpath(path, ROOT).replace(os.sep, '/')
        row = {}
        for name, rx in PATTERNS.items():
            n = 0
            for m in rx.finditer(text):
                if name.startswith('raw game address') and REGISTRATION.search(text[max(0, m.start() - 40):m.start()]):
                    continue
                n += 1
            row[name] = n
        out[rel] = row
    return out


def main(argv):
    now = counts()
    base = {}
    if os.path.exists(BASELINE):
        for r in csv.DictReader(open(BASELINE, newline='')):
            base[r['file']] = {k: int(r[k]) for k in PATTERNS}
    if '--update' in argv:
        with open(BASELINE, 'w', newline='') as f:
            w = csv.writer(f, lineterminator='\n')
            w.writerow(['file'] + list(PATTERNS))
            for rel in sorted(now):
                row = now[rel]
                if rel in base or not base:
                    row = {k: min(row[k], base.get(rel, row)[k]) if base else row[k] for k in PATTERNS}
                else:
                    row = {k: 0 for k in PATTERNS}
                if any(row.values()):
                    w.writerow([rel] + [row[k] for k in PATTERNS])
        print(f'wrote {os.path.relpath(BASELINE, ROOT)}')
        return 0
    problems = 0
    for rel, row in sorted(now.items()):
        allowed = base.get(rel, {k: 0 for k in PATTERNS})
        for k in PATTERNS:
            if row[k] > allowed[k]:
                problems += 1
                print(f'{rel}: {row[k]} {k} (allowed {allowed[k]}); write it the clean way '
                      f'(docs/rewrite-guide.md, "Clean code")')
    totals = {k: sum(r[k] for r in now.values()) for k in PATTERNS}
    print('cleanup counts: ' + ', '.join(f'{v:,} {k}' for k, v in totals.items()) + f'; {problems} problem(s)')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
