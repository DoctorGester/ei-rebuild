"""What still reads each registration's game.exe address (C20c; docs/notes/standalone.md, "Registration rules").

A registration (EI_REPLACE, _METHOD, _DISPATCH, _STANDALONE, _SHORT, ui_hud.cpp's UI_INPUT_HANDLER /
UI_WHEEL_HANDLER) makes our function the code for a game.exe address: in the image CodeAddress(address) is
the function (image_code.cpp), in ei_vectors.exe a forwarder to the address jumps to it (hook_stub.cpp).
Our code that calls the function by name needs none of that; the registration matters only where something
holds the address itself. The readers, each a set of addresses:

  run time (the image; ei_vectors for forwarders and code literals):
    cell:<group>     a code cell of game.exe's data holding it (src/standalone/image_code_cells.inc: vtable
                     slots, ThrowInfo, MFC CreateObject / message maps, the static initialiser list, CRT tables)
    static init      a call or store of the generated static initialisers / exit functions
                     (src/standalone/static_init.cpp, static_exit.cpp)
    forwarder        an EI_ORIGINAL(address) in src/ (the image) or tests/ (the harness, the vector replay)
    code literal     the address written in src/ outside a registration (ei::Code<F>(0x...), handler tables,
                     callbacks, src/hook's and src/standalone's own: WinMain for HasCode, ...)
    named data       a function named in a named range's initialiser (config/named_data.csv, C18): named_data.py
                     --check compares its registration's address with game.exe's pointer
  the harness (verify build) and the vectors:
    harness          the address written in tests/verify (RedirectCode, Code<F> of harness hooks, address tables)
    vectors          the address written in tests/vectors (EI_RECORD recorders, ReplayStandIn)
    scenario / golden  a code address in a scenario's steps (comments left out) or a golden file
  tools only (they read every registration, so they are listed, not counted as readers):
    check_signatures, check_thunks, check_registrations, check_fp_fold (matches a registered function with
    the original's constants), coverage.py (the watch list of rewrites), direct_calls.py, standalone_gaps.py,
    save_trace.py, ui_census_report.py, progress.py, gen_static_init.py (registered callees are called, not
    inlined: covered by "static init"), gen_short_functions.py (a short game function registered by a rewrite:
    listed as "short generator", kept: the generator would write an EI_REPLACE_SHORT for it).

A registration none of these read is "unread": nothing reaches its address; our code calls the function by name,
or nothing does (then the function is dead in the image).

usage: python tools/registration_readers.py [--list READER] [--unread] [--files]
                                           [--drop [--exclude RE ...] [--exclude-name RE ...] [--kinds K,...]]
  (no option)  the split: per reader the registrations it reads (a registration can have several), and per
               first reader in the order above (each counted once)
  --list R     the registrations whose first reader is R (a prefix: "cell", "cell:vtable", "forwarder", ...)
  --unread     the unread registrations, file:line kind address name
  --files      the unread registrations per file
  --drop       deletes the unread registrations' lines (files matching an --exclude regex and names matching an
               --exclude-name regex left alone; only --kinds, by default all but the generated EI_REPLACE_SHORT)
"""
import argparse, collections, csv, os, re, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import srcscan  # noqa: E402

TEXT_BEGIN, TEXT_END = 0x401000, 0x73A670
REGISTRATION = re.compile(
    r'^[ \t]*(EI_REPLACE(?:_METHOD|_DISPATCH|_SHORT|_STANDALONE)?|UI_(?:INPUT|WHEEL)_HANDLER)\s*\(\s*0[xX]([0-9A-Fa-f]+)'
    r'\s*,\s*([^;]*?)\)\s*;[ \t]*(//[^\n]*)?$', re.M)
ANY_REGISTRATION = re.compile(r'\b(?:EI_REPLACE(?:_METHOD|_DISPATCH|_SHORT|_STANDALONE)?|UI_(?:INPUT|WHEEL)_HANDLER)\s*\(')
ORIGINAL = re.compile(r'\bEI_ORIGINAL\s*\(\s*0[xX]([0-9A-Fa-f]+)\s*\)')
LITERAL = re.compile(r'(?<![0-9A-Za-z_])0[xX]00([4-7][0-9A-Fa-f]{5})(?![0-9A-Fa-f])')
WORD = re.compile(r'(?<![0-9A-Za-z_])00([4-7][0-9A-Fa-f]{5})(?![0-9A-Za-z_])')
CODE_CELLS = os.path.join(ROOT, 'src', 'standalone', 'image_code_cells.inc')
STATIC = ('src/standalone/static_init.cpp', 'src/standalone/static_exit.cpp')
# Generated tables of addresses that are no readers: every function's size, the image's data cells and import
# slots (data), the sites addon.dll patches in game.exe (the original's runs only).
NOT_READERS = ('src/hook/function_sizes.inc', 'src/standalone/image_code_cells.inc', 'src/standalone/image_cells.inc',
               'src/standalone/image_imports.inc', 'tests/verify/addon_sites.inc')
ORDER = ['cell', 'static init', 'forwarder', 'code literal', 'named data', 'harness', 'vectors', 'scenario',
         'golden', 'short generator']
# The code cells' groups (image_code_cells.inc's comment lines) by a short name.
CELL_GROUPS = [('vtable', ('vtable',)), ('throwinfo', ('ThrowInfo',)), ('mfc', ('MFC',)),
               ('static init list', ('initialiser list',)), ('crt', ('CRT',))]


def rel(path):
    return os.path.relpath(path, ROOT).replace(os.sep, '/')


def files(top, exts=('.cpp', '.h', '.inc')):
    for dirpath, _, names in os.walk(os.path.join(ROOT, top)):
        for n in sorted(names):
            if n.endswith(exts):
                yield os.path.join(dirpath, n)


class Registration:
    def __init__(self, path, line, kind, address, name, start, end):
        self.path, self.line, self.kind, self.address, self.name = path, line, kind, address, name
        self.start, self.end = start, end   # the statement's span in the file
        self.readers = set()

    def first(self):
        for r in ORDER:
            for x in sorted(self.readers):
                if x == r or x.startswith(r + ':'):
                    return x
        return None


def registrations():
    out = []
    for path in files('src'):
        raw = open(path, encoding='utf-8', errors='replace').read()
        code = srcscan.blank_comments(raw)
        found = 0
        for m in REGISTRATION.finditer(code):
            if '#define' in code[code.rfind('\n', 0, m.start()) + 1:m.start()]:
                continue
            found += 1
            name = re.sub(r'\s+', ' ', m.group(3)).lstrip('&').strip()
            out.append(Registration(rel(path), raw.count('\n', 0, m.start()) + 1, m.group(1), int(m.group(2), 16), name,
                                    m.start(), m.end()))
        total = sum(1 for m in ANY_REGISTRATION.finditer(code)
                    if '#define' not in code[code.rfind('\n', 0, m.start()) + 1:m.start()])
        if total != found:
            raise SystemExit(f'{rel(path)}: {total} registration(s), {found} on a line of their own (the tool reads '
                             'only `MACRO(0x..., name);` statements)')
    return out


def code_cells():
    """address -> {cell:<group>}"""
    out = collections.defaultdict(set)
    group = 'other'
    for line in open(CODE_CELLS, encoding='utf-8'):
        if line.startswith('// ') and not line.startswith('// Generated') and ':' not in line[:3]:
            text = line[3:]
            group = next((g for g, words in CELL_GROUPS if any(w in text for w in words)), 'other')
        m = re.match(r'\{DataId\(0x([0-9A-Fa-f]+)\), 0x([0-9A-Fa-f]+)\}', line)
        if m:
            out[int(m.group(2), 16)].add('cell:' + group)
    return out


def literal_readers(regs):
    """address -> {reader} from the sources: static init, forwarders, literals in src/ and tests/."""
    out = collections.defaultdict(set)
    spans = collections.defaultdict(list)
    for r in regs:
        spans[r.path].append((r.start, r.end))
    for top in ('src', 'tests/verify', 'tests/vectors'):
        for path in files(top):
            p = rel(path)
            if p in NOT_READERS:
                continue
            code = srcscan.blank_comments(open(path, encoding='utf-8', errors='replace').read())
            own = spans.get(p, [])
            for m in ORIGINAL.finditer(code):
                out[int(m.group(1), 16)].add('forwarder' if top == 'src' else 'forwarder:' + top.split('/')[1])
            for m in LITERAL.finditer(code):
                a = int(m.group(1), 16)
                if not TEXT_BEGIN <= a < TEXT_END or any(s <= m.start() < e for s, e in own):
                    continue
                if ORIGINAL.match(code, max(0, code.rfind('EI_ORIGINAL', max(0, m.start() - 20), m.start()))):
                    continue
                if p in STATIC:
                    out[a].add('static init')
                elif top == 'src':
                    out[a].add('code literal')
                else:
                    out[a].add(top.split('/')[1] if top != 'tests/verify' else 'harness')
    return out


def text_readers():
    """address -> {scenario | golden}: code addresses in the scenarios' steps and the golden files."""
    out = collections.defaultdict(set)
    for path in files('tests/scenarios', ('.txt',)):
        for line in open(path, encoding='utf-8', errors='replace'):
            if line.lstrip().startswith('#'):
                continue
            for m in WORD.finditer(line):
                out[int(m.group(1), 16)].add('scenario')
    for dirpath, _, names in os.walk(os.path.join(ROOT, 'tests', 'golden')):
        for n in names:
            text = open(os.path.join(dirpath, n), encoding='utf-8', errors='replace').read()
            for m in WORD.finditer(text):
                out[int(m.group(1), 16)].add('golden')
    return out


def named_data_readers(regs):
    """address -> {named data}: our functions a named range's initialiser names (named_data.py's pointer elements)."""
    import named_data
    by_name = collections.defaultdict(set)
    for r in regs:
        by_name[r.name.lstrip(':')].add(r.address)
    out = collections.defaultdict(set)
    for d in named_data.definitions():
        for kind, v in named_data.tokens(d.body):
            if kind == 'name' and v in by_name:
                for a in by_name[v]:
                    out[a].add('named data')
    return out


def short_generator_readers(regs):
    """Short game functions (under five bytes) a rewrite registers: gen_short_functions would write them."""
    import gen_short_functions as g
    sizes = g.load_sizes()
    game = g.game_functions()
    out = collections.defaultdict(set)
    for r in regs:
        if r.kind != 'EI_REPLACE_SHORT' and r.address in game and sizes.get(r.address, 0) < g.SHORT:
            out[r.address].add('short generator')
    return out


def measure():
    regs = registrations()
    readers = [code_cells(), literal_readers(regs), named_data_readers(regs), text_readers(),
               short_generator_readers(regs)]
    for r in regs:
        for table in readers:
            r.readers |= table.get(r.address, set())
    return regs


def excluded(r, exclude, exclude_names):
    return any(re.search(x, r.path) for x in exclude) or any(re.search(x, r.name) for x in exclude_names)


def drop(regs, exclude, exclude_names, kinds):
    by_file = collections.defaultdict(list)
    for r in regs:
        if not r.readers and r.kind in kinds and not excluded(r, exclude, exclude_names):
            by_file[r.path].append(r)
    dropped = comments = 0
    for path, rs in sorted(by_file.items()):
        full = os.path.join(ROOT, path)
        raw = open(full, encoding='utf-8', newline='').read()
        lines = raw.splitlines(keepends=True)
        dead = set()
        for r in rs:
            first = raw.count('\n', 0, r.start)
            dead.update(range(first, first + raw.count('\n', r.start, r.end) + 1))
            dropped += 1
        before = len(dead)
        dead |= orphan_comments(lines, dead)
        comments += len(dead) - before
        out = []
        for i, line in enumerate(lines):
            if i in dead:
                continue
            # Two blank lines meeting where lines went: one is enough.
            if not line.strip() and out and not out[-1].strip() and i > 0 and i - 1 in dead:
                continue
            out.append(line)
        with open(full, 'w', encoding='utf-8', newline='') as f:
            f.write(''.join(out))
    print(f'dropped {dropped} registration(s) in {len(by_file)} file(s), {comments} comment line(s) that introduced only them')


def orphan_comments(lines, dead):
    """Comment blocks (whole // lines) that only introduce or follow dropped registrations: between a boundary (a
    blank line, the file's start or end, a brace line, a dropped line) and a run of dropped lines that itself ends at
    one. Section rules (// --- ...) stay."""
    found = set()

    def comment(i):
        s = lines[i].strip()
        return s.startswith('//') and '---' not in s and '===' not in s

    def boundary(i):
        if i < 0 or i >= len(lines) or i in dead or i in found:
            return True
        s = lines[i].strip()
        return not s or s in ('{', '}', '};') or s.startswith(('} //', 'namespace'))

    def run_ends_at_boundary(i, step):
        while 0 <= i < len(lines) and (i in dead or i in found):
            i += step
        return boundary(i) or comment(i)

    changed = True
    while changed:
        changed = False
        i = 0
        while i < len(lines):
            if not comment(i) or i in dead or i in found:
                i += 1
                continue
            j = i
            while j + 1 < len(lines) and comment(j + 1) and j + 1 not in dead and j + 1 not in found:
                j += 1
            gone = dead | found
            above, below = i - 1, j + 1
            introduces = below in gone and below < len(lines) and boundary(above) and run_ends_at_boundary(below, 1)
            follows = above in gone and above >= 0 and boundary(below) and run_ends_at_boundary(above, -1)
            if introduces or follows:
                found.update(range(i, j + 1))
                changed = True
            i = j + 1
    return found


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--list')
    ap.add_argument('--unread', action='store_true')
    ap.add_argument('--files', action='store_true')
    ap.add_argument('--drop', action='store_true')
    ap.add_argument('--exclude', action='append', default=[], help='leave files whose path matches')
    ap.add_argument('--exclude-name', action='append', default=[], help='leave registrations whose name matches')
    ap.add_argument('--kinds', default='EI_REPLACE,EI_REPLACE_METHOD,EI_REPLACE_DISPATCH,EI_REPLACE_STANDALONE',
                    help='the kinds --drop takes (EI_REPLACE_SHORT is generated: gen_short_functions.py)')
    args = ap.parse_args(argv)
    regs = measure()
    if args.drop:
        drop(regs, args.exclude, args.exclude_name, args.kinds.split(','))
        return 0
    if args.list or args.unread:
        for r in regs:
            first = r.first()
            if args.unread and first is None or args.list and first and (first == args.list or first.startswith(args.list + ':')):
                extra = ' [' + ', '.join(sorted(r.readers)) + ']' if r.readers else ''
                print(f'{r.path}:{r.line} {r.kind} {r.address:08X} {r.name}{extra}')
        return 0
    if args.files:
        per = collections.Counter(r.path for r in regs if not r.readers)
        for path, n in per.most_common():
            print(f'{n:5} {path}')
        return 0
    any_reader = collections.Counter(x for r in regs for x in r.readers)
    first = collections.Counter(r.first() or 'unread' for r in regs)
    kinds = collections.Counter(r.kind for r in regs)
    print(f'{len(regs)} registrations at {len({r.address for r in regs})} addresses '
          f'({", ".join(f"{k} {n}" for k, n in kinds.most_common())})')
    print('first reader (each registration once, in the order of the docstring):')
    for name, n in sorted(first.items(), key=lambda kv: (kv[0] == 'unread', -kv[1])):
        print(f'  {name:28} {n:5}')
    print('every reader (a registration can have several):')
    for name, n in any_reader.most_common():
        print(f'  {name:28} {n:5}')
    unread = [r for r in regs if not r.readers]
    ukinds = collections.Counter(r.kind for r in unread)
    print(f'unread: {len(unread)} ({", ".join(f"{k} {n}" for k, n in ukinds.most_common())}) in '
          f'{len({r.path for r in unread})} file(s)')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
