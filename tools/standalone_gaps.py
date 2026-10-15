"""Lists the code the standalone image could still reach as INT3 (docs/notes/standalone.md, S16c).

In the image every address of game.exe's code section without a registration (EI_REPLACE*, the UI input
handlers) holds INT3. Three ways our code can get there:
  forwarders   EI_ORIGINAL targets in src/ (a forwarder runs the address)
  literals     code addresses written in src/ (callbacks, function pointers handed to the game's tables:
               particle callbacks, qsort comparators, network handlers, actionDone, ...); the five
               code-byte reads of S14 (CALL targets, the keyword table's immediate) are among them and
               are read only in the hook build
  data         function starts (config/functions.csv) that game.exe's data sections point at: vtable slots
               and pointer tables the image keeps (library code mostly: MFC's vtables, the CRT's tables).
               The compiler's exception funclets (kind "eh") and config/dead_code.csv are left out: the
               image runs none of game.exe's own exception tables.
A listed address is not necessarily reachable: a forwarder may run in the hook build only (behind
ei::Standalone()), a vtable may belong to a class the game never makes. The suite under --standalone says
which ones are; this lists what to look at.

usage: python tools/standalone_gaps.py [--exe game.exe] [forwarders|literals|data ...]
"""
import argparse, csv, os, re, struct, sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dashboard_data as dd  # noqa: E402

REGISTERED = re.compile(r'\b(?:EI_REPLACE\w*|UI_(?:INPUT|WHEEL)_HANDLER)\(\s*0x([0-9A-Fa-f]+)')
CODE_LITERAL = re.compile(r'(?<![0-9A-Za-z_])0[xX]00([4-7][0-9A-Fa-f]{5})(?![0-9A-Fa-f])')
TEXT_BEGIN, TEXT_END = 0x401000, 0x73A670


def scan_sources():
    registered, forwarders, literals = set(), defaultdict(set), defaultdict(set)
    for dirpath, _, names in os.walk(os.path.join(ROOT, 'src')):
        for n in names:
            if not n.endswith(('.cpp', '.h', '.inc')):
                continue
            path = os.path.join(dirpath, n)
            rel = os.path.relpath(path, ROOT).replace(os.sep, '/')
            code = dd.COMMENT.sub('', open(path, encoding='utf-8', errors='replace').read())
            registered |= {int(a, 16) for a in REGISTERED.findall(code)}
            for a in dd.ORIGINAL.findall(code):
                forwarders[int(a, 16)].add(rel)
            if not rel.startswith(('src/hook/', 'src/standalone/')):
                for m in CODE_LITERAL.finditer(code):
                    a = int(m.group(1), 16)
                    if TEXT_BEGIN <= a < TEXT_END:
                        literals[a].add(rel)
    return registered, forwarders, literals


def data_references(exe_path, functions):
    data = open(exe_path, 'rb').read()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    nsec, opt = struct.unpack_from('<H', data, pe + 6)[0], struct.unpack_from('<H', data, pe + 20)[0]
    refs = defaultdict(list)
    for i in range(nsec):
        o = pe + 24 + opt + 40 * i
        va, rawsz, rawptr = struct.unpack_from('<III', data, o + 12)
        if struct.unpack_from('<I', data, o + 36)[0] & 0x20:   # the code section
            continue
        for off in range(0, rawsz - 3, 4):
            v = struct.unpack_from('<I', data, rawptr + off)[0]
            if v in functions:
                refs[v].append(0x400000 + va + off)
    return refs


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--exe', default=os.path.join(ROOT, 'run', 'vanilla', 'game.exe'))
    ap.add_argument('kinds', nargs='*', default=['forwarders', 'literals', 'data'])
    args = ap.parse_args()
    functions = {}
    with open(os.path.join(ROOT, 'config', 'functions.csv')) as f:
        for r in csv.DictReader(f):
            functions[int(r['address'], 16)] = r
    library = dd.addresses(open(os.path.join(ROOT, 'config', 'library.csv')).read())
    dead = dd.addresses(open(os.path.join(ROOT, 'config', 'dead_code.csv')).read())
    registered, forwarders, literals = scan_sources()

    def kind(a):
        r = functions.get(a)
        return 'library' if a in library else r['kind'] if r else 'no-function'

    def show(title, where):
        left = sorted(a for a in where if a not in registered and a not in dead and kind(a) != 'eh')
        counts = defaultdict(int)
        for a in left:
            counts[kind(a)] += 1
        print(f'{title}: {len(left)} unregistered {dict(counts)}')
        for a in left:
            r = functions.get(a, {})
            print(f'  {a:08X} {kind(a):11} {r.get("name", "")[:40]:40} {" ".join(sorted(map(str, where[a]))[:3])}')

    if 'forwarders' in args.kinds:
        show('forwarders', forwarders)
    if 'literals' in args.kinds:
        # (A forwarder's own address is listed above; the code-byte reads of S14 have no function.)
        show('code literals', {a: v for a, v in literals.items() if a not in forwarders})
    if 'data' in args.kinds:
        refs = data_references(args.exe, functions)
        show('data references', {a: [f'{x:08X}' for x in v] for a, v in refs.items()})
    return 0


if __name__ == '__main__':
    sys.exit(main())
