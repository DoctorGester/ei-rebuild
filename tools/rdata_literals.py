"""game.exe's .rdata constants as literals (S18e; docs/notes/standalone.md "Own data (S18)").

A float, double or integer of game.exe's .rdata that no code writes needs no storage of its own:
the source can hold its value. This rewrites the reads by address (a per-file helper
`GameFloat(0x0073B86C)`, `ei::GameData<const float>(0x0073B86C)`, a constant that holds the address)
into a literal followed by a comment with the address, `0.5f /*0073B86C*/`, and checks every such
tagged literal against the bytes of game.exe (`--check`, in build.cmd). Strings were C11's
(tools/exe_strings.py).

What stays data (the cell is written at run time):
  * the sites addon.dll writes (tests/verify/addon_sites.inc, generated from config/rebirth_patches.csv):
    007428AC and 00742B08 (the interface's widescreen layout widths), 007432F4 (the mod-folder
    string pointer), 0073B720 (the movies path pointer);
  * the import address table (0073B000-0073B820), and any cell NaN / infinite (kept by bits).
Vanilla's and Rebirth's .rdata are the same bytes (checked here), and the section is read-only, so
the only writers are those sites.

The tag decides what --check compares: `1.5f` is the dword at the address as a float, `1.5` the
qword as a double, `1.5 /*0073B86C f*/` the float widened to a double (a helper that returned double),
an integer the dword. `--asserts FILE` writes a translation unit of static_asserts on the same
literals for the real compiler (cl /std:c++20 /c), because the check itself parses with Python.

usage: python tools/rdata_literals.py --list | --apply [--files F...] | --check [--asserts FILE] [--exe game.exe ...]
"""
import argparse, decimal as decimal_module, os, re, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import data_seam as ds
from exe_strings import load
import exe_path

ROOT = ds.ROOT
RDATA = (0x0073B820, 0x00783000)
IAT = (0x0073B000, 0x0073B820)
EXES = []   # --exe (repeatable); none: every install's game.exe (tools/exe_path.py)

TYPES = {'float': ('f', 4), 'double': ('d', 8), 'int': ('i', 4), 'std::uint32_t': ('I', 4), 'std::size_t': ('I', 4),
         'unsigned': ('I', 4), 'std::int32_t': ('i', 4), 'std::int8_t': None}
TAG = re.compile(r'(\(?-?(?:\d+\.\d*|\.\d+|\d+)(?:[eE][+-]?\d+)?[fFuU]?\)?|\(?-?0[xX][0-9A-Fa-f]+[uU]?\)?)\s*/\*([0-9A-F]{8})( f)?\*/')


def addon_sites():
    out = []
    with open(os.path.join(ROOT, 'tests', 'verify', 'addon_sites.inc')) as f:
        for line in f:
            m = re.search(r'0x([0-9A-Fa-f]{8})\)?,\s*(\d+)', line)   # 0x.. or ei::DataId(0x..)
            if m:
                out.append((int(m.group(1), 16), int(m.group(2))))
    return out


class Exe:
    def __init__(self):
        images = [load(p) for p in exe_path.find_exes(EXES)]
        self.data, self.sections = images[0]
        self.same = True
        for other, secs in images[1:]:
            for va, size, raw in secs:
                if va == RDATA[0] - 0x820 and other[raw:raw + size] != self.data[raw:raw + size]:
                    self.same = False
        sites = addon_sites()
        self.written = [(a, a + n) for a, n in sites if RDATA[0] - 0x820 <= a < RDATA[1]]

    def bytes(self, address, n):
        for va, size, raw in self.sections:
            if va <= address and address + n <= va + size:
                return self.data[raw + address - va:raw + address - va + n]
        return None

    def readable(self, address, n):
        """The cell is constant .rdata: in the range, clear of the IAT and of every written site."""
        if not (RDATA[0] <= address and address + n <= RDATA[1]):
            return False
        return not any(address < hi and lo < address + n for lo, hi in self.written)


def shortest(value, digits):
    code = '<f' if digits == 9 else '<d'
    for n in range(1, digits + 1):
        text = '%.*g' % (n, value)
        try:
            back = struct.unpack(code, struct.pack(code, float(text)))[0]
        except OverflowError:
            continue
        if back == value:
            return text
    return '%.*g' % (digits, value)


def decimal(text, suffix):
    d = decimal_module.Decimal(text)
    if -5 <= d.adjusted() < 10:
        text = format(d, 'f')
    else:
        text = text.replace('e+0', 'e').replace('e-0', 'e-').replace('e+', 'e')
    if not re.search(r'[.eE]', text):
        text += '.0'
    return text + suffix


def literal_of(exe, address, kind, widen=False):
    """(source text, tag suffix) of the n bytes at `address` read as `kind`, or None."""
    code, n = TYPES[kind]
    raw = exe.bytes(address, n)
    if raw is None or not exe.readable(address, n):
        return None
    value = struct.unpack('<' + code, raw)[0]
    if code == 'f':
        if value != value or value in (float('inf'), float('-inf')):
            return None
        if widen:
            text, tag = decimal(shortest(value, 17), ''), ' f'
            if struct.unpack('<d', struct.pack('<d', float(text)))[0] != value:
                return None
        else:
            text, tag = decimal(shortest(value, 9), 'f'), ''
    elif code == 'd':
        if value != value or value in (float('inf'), float('-inf')):
            return None
        text, tag = decimal(shortest(value, 17), ''), ''
    elif code == 'i':
        text, tag = str(value), ''
    else:
        text, tag = ('%d' % value if value < 10 else '0x%X' % value) + 'u', ''
    if text.startswith('-'):
        return '(' + text + ' /*%08X%s*/)' % (address, tag)
    return text + ' /*%08X%s*/' % (address, tag)


# ---- helpers ---------------------------------------------------------------------------------------------

HELPER_DEF = re.compile(r'(?:inline\s+|static\s+)*(const\s+)?(float|double)\s*(&?)\s+(\w+)\s*\(\s*(?:const\s+)?std::uintptr_t\s+(\w+)\s*\)\s*\{')
READ_TYPE = re.compile(r'GameData\s*<\s*(?:const\s+|volatile\s+)*([\w:]+)\s*>')
LOCAL_TYPE = re.compile(r'\b(float|double)\s+\w+\s*;')
CONST_DEF = re.compile(r'\b(?:constexpr|const)\s+(?:static\s+)?(?:const\s+)?(?:std::)?(?:uintptr_t|uint32_t)\s+(\w+)\s*=\s*'
                       r'(?:[\w:]+\s*\(\s*)?(0[xX][0-9A-Fa-f]+)')


class Tree:
    def __init__(self):
        self.scan = ds.Scan.__new__(ds.Scan)
        self.paths = ds.files()
        self.text = {p: ds.read(p) for p in self.paths}
        self.code = {p: ds.blank(t) for p, t in self.text.items()}
        self.helpers = {}   # name -> {'read': type, 'ret': type, 'ref': bool, defs: [(path, start, end)]}
        self.consts = {}    # name -> set of values
        self.find_helpers()
        self.find_consts()

    def find_helpers(self):
        for p, code in self.code.items():
            if p.startswith('src/standalone/'):
                continue
            for m in HELPER_DEF.finditer(code):
                body = ds.Scan.body(code, m.end() - 1)
                rm = READ_TYPE.search(body)
                read = rm.group(1) if rm else None
                if read is None and 'memcpy' in body:
                    lm = LOCAL_TYPE.search(body)
                    read = lm.group(1) if lm else None
                if read not in ('float', 'double'):
                    continue
                if re.search(r'GameData|GamePointer|GameAddress', body) is None:
                    continue
                name, ret = m.group(4), m.group(2)
                start = m.start()
                end = m.end() - 1 + len(body)
                h = self.helpers.setdefault(name, {'read': set(), 'ret': {}, 'defs': []})
                h['read'].add(read)
                h['ret'][p] = (ret, bool(m.group(3)))
                h['defs'].append((p, start, end))

    def find_consts(self):
        for p, code in self.code.items():
            for m in CONST_DEF.finditer(code):
                self.consts.setdefault(m.group(1), set()).add(int(m.group(2), 16))


def includes_of(tree, path):
    return {path} | {p for p in tree.paths if p.endswith('.h')}


def make_patterns(tree):
    names = '|'.join(sorted((re.escape(n) for n, h in tree.helpers.items() if len(h['read']) == 1), key=len, reverse=True))
    addr = r'(0[xX][0-9A-Fa-f]{1,8}|(?:[\w]+::)*k\w+)'
    arg = r'\s*(?:(?:std::uintptr_t|static_cast<std::uintptr_t>)\s*\(\s*' + addr + r'\s*\)|' + addr + r')\s*'
    helper = re.compile(r'(?<![\w.])((?:::)?(?:[A-Za-z_]\w*::)*)(' + names + r')\s*\(' + arg + r'\)')
    data = re.compile(r'(?<![\w])((?:ei::)?GameData)\s*<\s*((?:const\s+|volatile\s+)+)(float|double|int|std::uint32_t|std::size_t|unsigned|std::int32_t)\s*>\s*\(' + arg + r'\)')
    return helper, data


def resolve(tree, token):
    if token.lower().startswith('0x'):
        return int(token, 16)
    vals = tree.consts.get(token.split('::')[-1])
    if vals and len(vals) == 1:
        return next(iter(vals))
    return None


def rewrite(tree, exe, path, stats):
    text, code = tree.text[path], tree.code[path]
    helper_re, data_re = make_patterns(tree)
    edits = []
    seen_ids = {}
    for m in helper_re.finditer(code):
        name = m.group(2)
        h = tree.helpers[name]
        read = next(iter(h['read']))
        ret = h['ret'].get(path) or next(iter(h['ret'].values()))
        if ret[1]:
            pass   # a reference result: the literal is a temporary, fine for reads
        address = resolve(tree, (m.group(3) or m.group(4)))
        if address is None:
            stats['unresolved'] += 1
            continue
        if not RDATA[0] <= address < RDATA[1]:
            continue
        lit = literal_of(exe, address, read, widen=(read == 'float' and ret[0] == 'double'))
        if lit is None:
            stats['kept'].append((path, address))
            continue
        edits.append((m.start(), m.end(), lit, (m.group(3) or m.group(4)).split('::')[-1] if not (m.group(3) or m.group(4)).lower().startswith('0x') else None, name))
    for m in data_re.finditer(code):
        kind = m.group(3)
        address = resolve(tree, (m.group(4) or m.group(5)))
        if address is None:
            continue
        if not RDATA[0] <= address < RDATA[1]:
            continue
        lit = literal_of(exe, address, kind)
        if lit is None:
            stats['kept'].append((path, address))
            continue
        edits.append((m.start(), m.end(), lit, (m.group(4) or m.group(5)).split('::')[-1] if not (m.group(4) or m.group(5)).lower().startswith('0x') else None, None))
    return edits


def apply_edits(text, edits):
    for s, e, lit, _, _ in sorted(edits, reverse=True):
        text = text[:s] + lit + text[e:]
    return text


def whole_lines(text, start, end):
    """The line range of text[start:end] when it stands alone on its lines (a trailing // comment goes too)."""
    ls = text.rfind(chr(10), 0, start) + 1
    le = text.find(chr(10), end)
    le = len(text) if le < 0 else le + 1
    if text[ls:start].strip() or text[end:le].strip() and not text[end:le].strip().startswith('//'):
        return None
    return ls, le


def scope_of(tree, new_code, path):
    """The files that can see a definition of `path`: itself, and for a header the files including it."""
    if not path.endswith('.h'):
        return [path]
    base = os.path.basename(path)
    rx = re.compile(r'#\s*include\s*["<][^">]*' + re.escape(base) + r'[">]')
    return [p for p in new_code if p == path or rx.search(tree.text[p])]


def mentions(new_code, scope, name):
    rx = re.compile(r'(?<![\w])' + re.escape(name) + r'\b')
    return sum(len(rx.findall(new_code[p])) for p in scope)


def cleanup(tree, new_text, names):
    """Removes the helper definitions and the address constants that nothing mentions any more."""
    removed = 0
    for rounds in range(1):
        new_code = {p: ds.blank(t) for p, t in new_text.items()}
        for path in tree.paths:
            text, code = new_text[path], new_code[path]
            todo = []
            for d in HELPER_DEF.finditer(code):
                name = d.group(4)
                if name not in names or path.startswith('src/standalone/'):
                    continue
                body = ds.Scan.body(code, d.end() - 1)
                todo.append((d.start(), d.end() - 1 + len(body), name, 1))
            for c in CONST_DEF.finditer(code):
                value = int(c.group(2), 16)
                if not RDATA[0] <= value < RDATA[1] or c.group(1) not in names:
                    continue
                todo.append((c.start(), code.find(';', c.end()) + 1, c.group(1), 1))
            for start, end, name, own in sorted(todo, reverse=True):
                if mentions(new_code, scope_of(tree, new_code, path), name) - own > 0:
                    continue
                r = whole_lines(text, start, end)
                if r is None:
                    continue
                text = text[:r[0]] + text[r[1]:]
                removed += 1
            new_text[path] = text
    return removed


def run_apply(files):
    exe = Exe()
    if not exe.same:
        print('rdata_literals: vanilla and Rebirth .rdata differ; stopping')
        return 1
    tree = Tree()
    stats = {'unresolved': 0, 'kept': []}
    total = 0
    new_text = dict(tree.text)
    names = set()
    for path in tree.paths:
        if (files and path not in files) or skipped(path):
            continue
        edits = rewrite(tree, exe, path, stats)
        if edits:
            new_text[path] = apply_edits(tree.text[path], edits)
            total += len(edits)
            names |= {e[3] for e in edits if e[3]} | {e[4] for e in edits if e[4]}
    removed = cleanup(tree, new_text, names) if total else 0
    for path, t in new_text.items():
        if t != tree.text[path]:
            with open(os.path.join(ROOT, path), 'w', encoding='utf-8', newline='') as f:
                f.write(t)
    print(f'rdata_literals: {total} reads replaced, {removed} definitions removed, {len(stats["kept"])} kept as data, '
          f'{stats["unresolved"]} unresolved')
    kept = {}
    for p, a in stats['kept']:
        kept.setdefault(a, set()).add(p)
    for a, ps in sorted(kept.items()):
        print(f'  kept {a:08X}: {", ".join(sorted(ps))}')
    return 0


# Files other workers have open (S18e's brief): SV10j's unit_ai_*, script_effects, combat and armour files.
SKIP = re.compile(r'src/game/(unit_ai|script_effects|combat_|armo)|^src/standalone/|^tools/')


def skipped(path):
    return SKIP.search(path) is not None


def run_list():
    exe = Exe()
    tree = Tree()
    print('same bytes in vanilla and Rebirth:', exe.same)
    print('written sites in .rdata:', ', '.join('%08X+%d' % (lo, hi - lo) for lo, hi in exe.written))
    for name, h in sorted(tree.helpers.items()):
        print(f'helper {name}: reads {sorted(h["read"])}, defined in {sorted({d[0] for d in h["defs"]})}')
    stats = {'unresolved': 0, 'kept': []}
    n = 0
    for path in tree.paths:
        if skipped(path):
            continue
        e = rewrite(tree, exe, path, stats)
        n += len(e)
        if e:
            print(f'{path}: {len(e)}')
    print(f'{n} reads would be replaced; kept: {len(stats["kept"])}')
    kept = {}
    for p, a in stats['kept']:
        kept.setdefault(a, set()).add(p)
    for a, ps in sorted(kept.items()):
        print(f'  kept {a:08X}: {", ".join(sorted(ps)[:3])}')
    return 0


# ---- check ------------------------------------------------------------------------------------------------

def classify(lit, widened):
    """'f' float, 'd' double, 'w' float widened to double, 'i' integer: from the literal's spelling."""
    bare = lit.lstrip('-')
    if bare.lower().startswith('0x') or re.fullmatch(r'\d+[uU]?', bare):
        return 'i'
    if bare[-1] in 'fF':
        return 'f'
    return 'w' if widened else 'd'


def run_check(asserts):
    exe = Exe()
    problems = []
    count = 0
    lines = ['// Generated by tools/rdata_literals.py --asserts: the compiler reads each tagged literal as game.exe has it.',
             '#include <bit>', '#include <cstdint>']
    for path in ds.files():
        text = ds.read(path)
        for m in TAG.finditer(text):
            lit, address, widened = m.group(1).strip('()'), int(m.group(2), 16), bool(m.group(3))
            where = f'{path}:{text.count(chr(10), 0, m.start()) + 1}'
            kind = classify(lit, widened)
            n = 8 if kind == 'd' else 4
            if not exe.readable(address, n):
                problems.append(f'{where}: {address:08X} is not constant .rdata (a written site, the import table or out of range)')
                continue
            raw = exe.bytes(address, n)
            count += 1
            if kind == 'f':
                want = struct.unpack('<I', raw)[0]
                try:
                    got = struct.unpack('<I', struct.pack('<f', float(lit[:-1])))[0]
                except OverflowError:
                    got = None
                lines.append(f'static_assert(std::bit_cast<std::uint32_t>({lit}) == 0x{want:08X}u);   // {where}')
            elif kind in 'dw':
                value = struct.unpack('<d', raw)[0] if kind == 'd' else struct.unpack('<f', raw)[0]
                want = struct.unpack('<Q', struct.pack('<d', value))[0]
                got = struct.unpack('<Q', struct.pack('<d', float(lit)))[0]
                lines.append(f'static_assert(std::bit_cast<std::uint64_t>({lit}) == 0x{want:016X}ull);   // {where}')
            else:
                want = struct.unpack('<I', raw)[0]
                got = int(lit.rstrip('uU'), 0) & 0xFFFFFFFF
                lines.append(f'static_assert((std::uint32_t)({lit.rstrip("uU")}) == 0x{want:08X}u);   // {where}')
            if got != want:
                problems.append(f'{where}: {m.group(0)} is not the bytes of game.exe ({want:#x})')
    if not exe.same:
        problems.append('vanilla and Rebirth .rdata differ')
    if asserts:
        with open(asserts, 'w') as f:
            f.write(chr(10).join(lines) + chr(10))
    for p in problems:
        print('rdata_literals:', p)
    if problems:
        return 1
    print(f'rdata_literals: OK ({count} literals equal the bytes of game.exe)')
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--apply', action='store_true')
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--asserts')
    ap.add_argument('--files', nargs='+')
    ap.add_argument('--exe', action='append', default=[])
    a = ap.parse_args()
    EXES[:] = a.exe
    try:
        exe_path.find_exes(EXES)
    except exe_path.ExeNotFound as e:
        sys.exit(f'rdata_literals: {e}')
    if a.apply:
        return run_apply(set(a.files or []))
    if a.check or a.asserts:
        return run_check(a.asserts)
    return run_list()


if __name__ == '__main__':
    sys.exit(main())
