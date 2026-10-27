"""Lists the game.exe code the standalone image could still reach without a rewrite (docs/notes/standalone.md, S16c).

In the image every address of game.exe's code section without a registration (EI_REPLACE*, the UI input
handlers) has no code: what reaches it lands on CodeAddress's stub, which reports it (S18i). Two ways our
code can get there: code addresses written in src/ (callbacks, function pointers handed to the game's tables:
particle callbacks, qsort comparators, network handlers, actionDone, ...; src/ has no EI_ORIGINAL forwarders since
C29). game.exe's data is no way there since C20: the image keeps none of its code pointers (tools/gen_image_cells.py),
its vtables are the compiler's and its static initialisers generated code. The compiler's exception funclets (kind
"eh") and config/dead_code.csv are left out.
A listed address is not necessarily reachable: a literal may be behind a branch the image never takes.
config/standalone_unreachable.csv names what the image can never run, with the reason (S16d). The line counts
those as "listed unreachable" and prints the rest (open). library.csv rows named "not a function" (strings Ghidra read as code pointers) are no function
starts. The suite under --standalone says which open ones are reached; this lists what to look at.

usage: python tools/standalone_gaps.py [--listed] [--check]
  --listed  print the listed unreachable entries too, with their reason
  --check   exit 1 when an entry is open or a line of the config covers nothing
"""
import argparse, csv, os, re, sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dashboard_data as dd  # noqa: E402

REGISTERED = re.compile(r'\b(?:EI_REPLACE\w*|UI_(?:INPUT|WHEEL)_HANDLER)\(\s*0x([0-9A-Fa-f]+)')
# A rewrite without a registration (C20d): its address is the tools' map, not a literal the image reaches.
REWRITTEN = re.compile(r'\bEI_REWRITTEN(?:_SPECIAL)?\s*\([^;]*\)\s*;')
CODE_LITERAL = re.compile(r'(?<![0-9A-Za-z_])0[xX]00([4-7][0-9A-Fa-f]{5})(?![0-9A-Fa-f])')
TEXT_BEGIN, TEXT_END = 0x401000, 0x73A670


def scan_sources():
    registered, literals = set(), defaultdict(set)
    for dirpath, _, names in os.walk(os.path.join(ROOT, 'src')):
        for n in names:
            if not n.endswith(('.cpp', '.h', '.inc')):
                continue
            path = os.path.join(dirpath, n)
            rel = os.path.relpath(path, ROOT).replace(os.sep, '/')
            code = dd.COMMENT.sub('', open(path, encoding='utf-8', errors='replace').read())
            registered |= {int(a, 16) for a in REGISTERED.findall(code)}
            if not rel.startswith(('src/hook/', 'src/standalone/')):
                for m in CODE_LITERAL.finditer(REWRITTEN.sub('', code)):
                    a = int(m.group(1), 16)
                    if TEXT_BEGIN <= a < TEXT_END:
                        literals[a].add(rel)
    return registered, literals


def read_unreachable(path):
    """config/standalone_unreachable.csv: ({address: line}, lines)."""
    addresses, lines = {}, []
    text = [l for l in open(path, encoding='utf-8') if l.strip() and not l.lstrip().startswith('#')]
    for r in csv.DictReader(text):
        what, reason, where = r['what'].strip(), r['reason'], r['address'].split()
        line = {'address': r['address'], 'what': what, 'reason': reason, 'covers': set()}
        lines.append(line)
        if what != 'address':
            sys.exit(f'standalone_unreachable.csv: unknown kind {what!r} ({r["address"]})')
        for w in where:
            addresses[int(w, 16)] = line
    return addresses, lines


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--listed', action='store_true')
    ap.add_argument('--check', action='store_true')
    args = ap.parse_args()
    functions = {}
    with open(os.path.join(ROOT, 'config', 'functions.csv')) as f:
        for r in csv.DictReader(f):
            functions[int(r['address'], 16)] = r
    library = dd.addresses(open(os.path.join(ROOT, 'config', 'library.csv')).read())
    with open(os.path.join(ROOT, 'config', 'library.csv')) as f:
        for r in csv.DictReader(f):
            if r['name'].startswith('not a function'):
                functions.pop(int(r['address'], 16), None)
    listed, lines = read_unreachable(os.path.join(ROOT, 'config', 'standalone_unreachable.csv'))
    dead = dd.addresses(open(os.path.join(ROOT, 'config', 'dead_code.csv')).read())
    registered, literals = scan_sources()

    def kind(a):
        r = functions.get(a)
        return 'library' if a in library else r['kind'] if r else 'no-function'

    open_total = 0

    def show(title, where):
        nonlocal open_total
        left = sorted(a for a in where if a not in registered and a not in dead and kind(a) != 'eh')
        still, excused = [], []
        for a in left:
            if a in listed:
                excused.append((a, listed[a]))
                listed[a]['covers'].add(a)
            else:
                still.append(a)
        counts = defaultdict(int)
        for a in still:
            counts[kind(a)] += 1
        open_total += len(still)
        print(f'{title}: {len(left)} unregistered, {len(excused)} listed unreachable, {len(still)} open {dict(counts)}')
        for a in still:
            r = functions.get(a, {})
            print(f'  {a:08X} {kind(a):11} {r.get("name", "")[:40]:40} {" ".join(sorted(map(str, where[a]))[:3])}')
        if args.listed:
            for a, line in excused:
                print(f'  listed {a:08X} ({line["what"]} {line["address"]}): {line["reason"][:90]}')

    show('code literals', literals)
    stale = [line for line in lines if not line['covers']]
    for line in stale:
        print(f'warning: standalone_unreachable.csv line {line["what"]} {line["address"]} covers nothing')
    for a in sorted(set(listed) & registered):
        print(f'warning: standalone_unreachable.csv lists {a:08X}, which is registered')
    if args.check and (open_total or stale):
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
