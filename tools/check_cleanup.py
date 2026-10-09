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
    # Ways around the rules above: a virtual called through a placeholder name (SlotNN()) instead of a
    # named slot, and byte arithmetic on a pointer with a literal offset instead of a member.
    'placeholder virtual (SlotNN)': re.compile(r'\bSlot\d+\s*\('),
    'byte offset arithmetic': re.compile(r'(?:char|uint8_t|byte)\s*\*\s*>\s*\([^()]*(?:\([^()]*\))?[^()]*\)\s*\+\s*0x'),
    # A container layout declared in a game file (struct FooArray { ... }, struct BarList : ...)
    # instead of the shared templates in src/lib (mfc::CArray / CList, lib::AlignedArray, stlp::).
    # Definitions only; src/lib itself is not counted.
    'per-file container struct': re.compile(r'(?<!template )\bstruct\s+\w*(?:Array|List)\b(?=\s*[:{])'),
    # The same for STLport-shaped layouts (stlp::vector / map / set / hash_map views declared per
    # file). Some matches are grids or managers named *Map; those go when they are renamed.
    'per-file vector/map/set struct': re.compile(r'(?<!template )\bstruct\s+\w*(?:Vector|Map|Set)\b(?=\s*[:{])'),
}
# Patterns not counted under these directories.
EXEMPT = {'per-file container struct': 'src/lib/', 'per-file vector/map/set struct': 'src/lib/'}
# A literal address directly inside EI_ORIGINAL(...)/EI_REPLACE(...)/EI_VERIFY(...)/UI_INPUT_HANDLER(...) is how hooks are
# written and is not counted; the lookahead above skips "0x...)" and the prefix test below the rest.
REGISTRATION = re.compile(r'(?:EI_(?:ORIGINAL|REPLACE|REPLACE_METHOD|VERIFY|REBIRTH_IMPLEMENTS)|UI_(?:INPUT|WHEEL)_HANDLER)\s*\(\s*$')


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
            if name in EXEMPT and rel.startswith(EXEMPT[name]):
                row[name] = 0
                continue
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
    header = []
    if os.path.exists(BASELINE):
        reader = csv.DictReader(open(BASELINE, newline=''))
        header = reader.fieldnames or []
        for r in reader:
            base[r['file']] = {k: int(r[k]) for k in PATTERNS if r.get(k) not in (None, '')}
    # A pattern the baseline has no column for yet is seeded from the current counts of every file.
    seeded = [k for k in PATTERNS if k not in header]
    if '--update' in argv:
        with open(BASELINE, 'w', newline='') as f:
            w = csv.writer(f, lineterminator='\n')
            w.writerow(['file'] + list(PATTERNS))
            for rel in sorted(now):
                row = now[rel]
                if rel in base or not base:
                    old = base.get(rel, {})
                    row = {k: min(row[k], old.get(k, row[k])) for k in PATTERNS}
                else:
                    row = {k: row[k] if k in seeded else 0 for k in PATTERNS}
                if any(row.values()):
                    w.writerow([rel] + [row[k] for k in PATTERNS])
        print(f'wrote {os.path.relpath(BASELINE, ROOT)}')
        return 0
    problems = 0
    for rel, row in sorted(now.items()):
        allowed = base.get(rel, {})
        for k in PATTERNS:
            limit = allowed.get(k, 0 if rel not in base else row[k])
            if row[k] > limit:
                problems += 1
                print(f'{rel}: {row[k]} {k} (allowed {limit}); write it the clean way '
                      f'(docs/rewrite-guide.md, "Clean code")')
    totals = {k: sum(r[k] for r in now.values()) for k in PATTERNS}
    print('cleanup counts: ' + ', '.join(f'{v:,} {k}' for k, v in totals.items()) + f'; {problems} problem(s)')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
