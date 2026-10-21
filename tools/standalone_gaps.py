"""Lists the code the standalone image could still reach as INT3 (docs/notes/standalone.md, S16c).

In the image every address of game.exe's code section without a registration (EI_REPLACE*, the UI input
handlers) holds INT3. Three ways our code can get there:
  forwarders   EI_ORIGINAL targets in src/ (a forwarder runs the address)
  literals     code addresses written in src/ (callbacks, function pointers handed to the game's tables:
               particle callbacks, qsort comparators, network handlers, actionDone, ...); the five
               code-byte reads of S14 (CALL targets, the keyword table's immediate) are among them and
               are read only by game.exe's own code (not in the image)
  data         function starts (config/functions.csv) that game.exe's data sections point at: vtable slots
               and pointer tables the image keeps (library code mostly: MFC's vtables, the CRT's tables).
               The compiler's exception funclets (kind "eh") and config/dead_code.csv are left out: the
               image runs none of game.exe's own exception tables.
A listed address is not necessarily reachable: a forwarder may be behind a branch the image never takes
(ei::Standalone()), a vtable may belong to a class the game never makes. config/standalone_unreachable.csv
names what the image can never run, with the reason (S16d): a whole vtable, data words or ranges, or an
address wherever it is listed. Each kind's line counts those as "listed unreachable" and prints the rest
(open). library.csv rows named "not a function" (strings Ghidra read as code pointers) are no function
starts. The suite under --standalone says which open ones are reached; this lists what to look at.

usage: python tools/standalone_gaps.py [--exe game.exe] [--listed] [--check] [forwarders|literals|data ...]
  --listed  print the listed unreachable entries too, with their reason
  --check   exit 1 when an entry is open or a line of the config covers nothing
"""
import argparse, csv, os, re, struct, sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import exe_path  # noqa: E402
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


def read_unreachable(path, vtables):
    """config/standalone_unreachable.csv: ({address: reason}, [(start, end, reason)] data ranges, lines)."""
    addresses, ranges, lines = {}, [], []
    text = [l for l in open(path, encoding='utf-8') if l.strip() and not l.lstrip().startswith('#')]
    for r in csv.DictReader(text):
        what, reason, where = r['what'].strip(), r['reason'], r['address'].split()
        line = {'address': r['address'], 'what': what, 'reason': reason, 'covers': set()}
        lines.append(line)
        for w in where:
            if what == 'address':
                addresses[int(w, 16)] = line
            elif what == 'vtable':
                start = int(w, 16)
                if start not in vtables:
                    sys.exit(f'standalone_unreachable.csv: {w} is not in config/vtables.csv')
                ranges.append((start, start + 4 * vtables[start], line))
            elif what == 'data':
                lo, _, hi = w.partition('-')
                ranges.append((int(lo, 16), int(hi, 16) if hi else int(lo, 16) + 4, line))
            else:
                sys.exit(f'standalone_unreachable.csv: unknown kind {what!r} ({r["address"]})')
    return addresses, ranges, lines


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--exe')   # default: tools/exe_path.py
    ap.add_argument('--listed', action='store_true')
    ap.add_argument('--check', action='store_true')
    ap.add_argument('kinds', nargs='*', default=['forwarders', 'literals', 'data'])
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
    with open(os.path.join(ROOT, 'config', 'vtables.csv')) as f:
        vtables = {int(r['vtable'], 16): int(r['slots']) for r in csv.DictReader(f)}
    listed, ranges, lines = read_unreachable(os.path.join(ROOT, 'config', 'standalone_unreachable.csv'), vtables)
    dead = dd.addresses(open(os.path.join(ROOT, 'config', 'dead_code.csv')).read())
    registered, forwarders, literals = scan_sources()

    def kind(a):
        r = functions.get(a)
        return 'library' if a in library else r['kind'] if r else 'no-function'

    def covering(a, ref):
        """The config line that excuses `a` (at data word `ref`, for a data reference), or None."""
        if a in listed:
            return listed[a]
        if ref is not None:
            for lo, hi, line in ranges:
                if lo <= ref < hi:
                    return line
        return None

    open_total = 0

    def show(title, where, refs=None):
        nonlocal open_total
        left = sorted(a for a in where if a not in registered and a not in dead and kind(a) != 'eh')
        still, excused = [], []
        for a in left:
            found = [covering(a, ref) for ref in (refs[a] if refs else [None])]
            if all(found):
                excused.append((a, found[0]))
                for line in found:
                    line['covers'].add(a)
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

    if 'forwarders' in args.kinds:
        show('forwarders', forwarders)
    if 'literals' in args.kinds:
        # (A forwarder's own address is listed above; the code-byte reads of S14 have no function.)
        show('code literals', {a: v for a, v in literals.items() if a not in forwarders})
    if 'data' in args.kinds:
        refs = data_references(exe_path.resolve(args.exe), functions)
        show('data references', {a: [f'{x:08X}' for x in v] for a, v in refs.items()}, refs)
    stale = [line for line in lines if not line['covers']]
    if set(args.kinds) >= {'forwarders', 'literals', 'data'}:
        for line in stale:
            print(f'warning: standalone_unreachable.csv line {line["what"]} {line["address"]} covers nothing')
    for a in sorted(set(listed) & registered):
        print(f'warning: standalone_unreachable.csv lists {a:08X}, which is registered')
    if args.check and (open_total or stale):
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
