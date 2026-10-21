"""game.exe's data as named C++ globals (C18; docs/notes/standalone.md "Named data (C18)").

A table or object of game.exe's .rdata or .data that only our code reads becomes a typed definition in the source
whose initialiser carries the original's address as a tag right after the `=`:

    constexpr int kSlotXs[7] = /*0074111C*/ {300, 500, 200, 400, 600, 75, 725};
    constexpr GUID kIidDirectDraw7 = /*00745C90*/ {0x15E65EC0, 0x3B9C, 0x11D2, {0xB9, 0x2F, 0x00, 0x60, ...}};

The code then names the global (kSlotXs[i]), not the address (GamePointer<const int>(0x0074111C)), and the range's
bytes leave the image's generated copy of the sections (gen_image_cells.py --bytes writes them zero).

--check (build.cmd):
  * every tagged initialiser is game.exe's bytes at the tag: its numbers are packed by the declared element type
    (TYPES; a struct type its fields' formats, nested braces flattened) and compared with the file's bytes
    (.rdata past the import table, .data and its zero-filled tail; no addon.dll site, no two ranges overlapping);
  * config/named_data.csv lists exactly the tagged ranges (address, size, name, file): `--update` writes it, and
    the build's gen_image_cells.py --bytes reads it;
  * nothing else reaches a named range: no address literal of src/ or tests/ (.cpp, .h, .inc; comments blanked)
    inside it, no pointer cell of game.exe's data inside it or pointing into it (src/standalone/image_cells.inc),
    no code cell inside it (image_code_cells.inc). What the image's code does not name and no cell points at,
    nothing reads: the zero bytes are never seen.

--split [KIND ...]: the game data addresses src/ still names (progress.py's count), by kind and by owner area;
the kinds given also list their addresses.

usage: python tools/named_data.py --check | --update | --split [KIND ...] [--exe game.exe]
"""
import argparse, collections, csv, os, re, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import exe_path  # noqa: E402
import srcscan  # noqa: E402

ROOT = srcscan.ROOT
CSV = os.path.join(ROOT, 'config', 'named_data.csv')
BASE = 0x00400000
IMPORTS = (0x0073B000, 0x0073B820)
RDATA = (0x0073B820, 0x00783000)
DATA = (0x00783000, 0x007CD000)   # .data and its zero-filled tail (the .bss), from 0079C000
DATA_RAW_END = 0x0079C000

# Element types: struct formats of their fields, in order (a struct's nested braces are flattened).
TYPES = {
    'float': 'f', 'double': 'd',
    'int': 'i', 'long': 'i', 'LONG': 'i', 'std::int32_t': 'i', 'int32_t': 'i',
    'unsigned': 'I', 'DWORD': 'I', 'std::uint32_t': 'I', 'uint32_t': 'I',
    'short': 'h', 'std::int16_t': 'h', 'std::uint16_t': 'H', 'WORD': 'H',
    'std::int8_t': 'b', 'std::uint8_t': 'B', 'BYTE': 'B',
    'RECT': 'iiii', 'GUID': 'IHH8B', 'IID': 'IHH8B', 'CLSID': 'IHH8B',
}
DEF = re.compile(r'([\w:]+)\s+(\w+)\s*((?:\[[^\]]*\]\s*)*)=\s*/\*([0-9A-F]{8})\*/\s*\{')
NUMBER = re.compile(r'(-?)\s*(0[xX][0-9A-Fa-f]+|(?:\d+\.\d*|\.\d+|\d+)(?:[eE][+-]?\d+)?)([fFuUlL]*)')
LITERAL = re.compile(r'(?<![0-9A-Za-z_])0[xX]00([0-9A-Fa-f]{6})(?![0-9A-Fa-f])')
CELL = re.compile(r'\{DataId\(0x([0-9A-F]{8})\), DataId\(0x([0-9A-F]{8})\)\}')


def image(path):
    import gen_image_cells
    return gen_image_cells.image(path)


def braced(text, start):
    """The text from `start` (a '{') to its matching '}' included."""
    depth = 0
    for i in range(start, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
    return None


class Definition:
    def __init__(self, path, line, type_, name, dims, address, body):
        self.path, self.line, self.type, self.name, self.dims, self.address, self.body = \
            path, line, type_, name, dims, address, body
        self.size = None
        self.packed = None
        self.problem = None

    def where(self):
        return f'{self.path}:{self.line}'

    def pack(self):
        """Packs the initialiser by the element type; sets size/packed or problem."""
        if self.type == 'char':
            self.pack_names()
            return
        fmt = TYPES.get(self.type)
        if fmt is None:
            self.problem = f'{self.type} is no element type tools/named_data.py knows (TYPES)'
            return
        values = []
        for m in NUMBER.finditer(srcscan.blank_comments(self.body)):
            sign, digits, suffix = m.groups()
            if digits.lower().startswith('0x'):
                values.append(int(sign + digits, 16))
            elif re.search(r'[.eE]', digits) or 'f' in suffix.lower():
                values.append(float(sign + digits))
            else:
                values.append(int(sign + digits))
        if len(values) % len(fmt):
            self.problem = f'{len(values)} number(s) do not fill whole {self.type} elements ({len(fmt)} each)'
            return
        count = len(values) // len(fmt)
        dims = re.findall(r'\[\s*([^\]]*?)\s*\]', self.dims)
        if dims and all(d.isdigit() for d in dims):
            want = 1
            for d in dims:
                want *= int(d)
            if want != count:
                self.problem = f'{count} element(s) for {self.dims.strip()}'
                return
        out = bytearray()
        for i, v in enumerate(values):
            code = fmt[i % len(fmt)]
            if code in 'fd':
                out += struct.pack('<' + code, float(v))
            elif isinstance(v, float):
                self.problem = f'{v} is not an integer ({self.type})'
                return
            else:
                bits = struct.calcsize(code) * 8
                out += struct.pack('<' + code.upper(), v & ((1 << bits) - 1))
        self.packed = bytes(out)
        self.size = len(out)


def _pack_names(self):
    """A char array's rows of names: `char kNames[3][7][4] = /*addr*/ {{"bd", "rh1", ...}, ...}`: each string
    literal fills the last dimension's chars, zero padded; the other dimensions' product is the string count."""
    dims = re.findall(r'\[\s*([^\]]*?)\s*\]', self.dims)
    if not dims or not all(d.isdigit() for d in dims):
        self.problem = 'a char array needs numeric bounds'
        return
    width = int(dims[-1])
    names = re.findall(r'"([^"\\]*)"', srcscan.blank_comments(self.body))
    want = 1
    for d in dims[:-1]:
        want *= int(d)
    if len(names) != want:
        self.problem = f'{len(names)} string(s) for {self.dims.strip()}'
        return
    out = bytearray()
    for n in names:
        if len(n) >= width:
            self.problem = f'"{n}" does not fit char[{width}] with its terminator'
            return
        out += n.encode('latin-1').ljust(width, b'\0')
    self.packed = bytes(out)
    self.size = len(out)


Definition.pack_names = _pack_names


class Retired:
    """A retired vtable (gen_image_cells.retired_vtables): game.exe's vtable of a class whose vtable is the compiler's."""
    def __init__(self, vtable, slots, cls, path):
        self.address, self.size, self.name, self.path = vtable, 4 * slots, f"{cls}'s retired vtable", path

    def where(self):
        return 'config/retired_vtables.csv'


def definitions():
    """Every tagged definition under src/, in path order."""
    found = []
    for s in srcscan.load_sources():
        for m in DEF.finditer(s.raw):
            if not s.code[m.start(1)].strip():   # inside a comment
                continue
            body = braced(s.raw, m.end() - 1)
            found.append(Definition(s.path, s.line_of(m.start()), m.group(1), m.group(2), m.group(3),
                                    int(m.group(4), 16), body or ''))
    return found


def addon_sites():
    """(begin, end) of the cells addon.dll writes (tests/verify/addon_sites.inc)."""
    import rdata_literals
    return [(a, a + n) for a, n in rdata_literals.addon_sites()]


# The addon.dll sites a retired vtable may hold (see check: the image's class reproduces the slot), and the
# generated list of sites that names them (game.exe's addresses, for harness_addon.cpp).
RETIRED_ADDON_SITES = {0x007432F4}   # ResOverride: CMappedResource's slot 5, GetFile (007432E0, C19 batch 3)
ADDON_SITES_INC = 'tests/verify/addon_sites.inc'


def other_files():
    """src/ and tests/ files that can name an address: .cpp, .h, .inc."""
    for top in ('src', 'tests'):
        for dirpath, _, names in os.walk(os.path.join(ROOT, top)):
            for name in names:
                if name.endswith(('.cpp', '.h', '.inc')):
                    full = os.path.join(dirpath, name)
                    yield os.path.relpath(full, ROOT).replace(os.sep, '/'), full


def read_csv():
    if not os.path.exists(CSV):
        return []
    with open(CSV, newline='') as f:
        return [(int(r['address'], 16), int(r['size']), r['name'], r['file']) for r in csv.DictReader(f)]


def csv_text(defs):
    lines = ['address,size,name,file']
    for d in sorted(defs, key=lambda d: d.address):
        lines.append(f'{d.address:08X},{d.size},{d.name},{d.path}')
    return '\n'.join(lines) + '\n'


def check(img, update):
    problems = []
    defs = definitions()
    for d in defs:
        d.pack()
        if d.problem:
            problems.append(f'{d.where()}: {d.name}: {d.problem}')
            continue
        lo, hi = d.address, d.address + d.size
        if not (RDATA[0] <= lo and hi <= RDATA[1] or DATA[0] <= lo and hi <= DATA[1]):
            problems.append(f'{d.where()}: {d.name} {lo:08X}+{d.size} is not in .rdata past the import table or .data')
            continue
        want = bytes(img[lo - BASE:hi - BASE])
        if d.packed != want:
            at = next(i for i in range(d.size) if d.packed[i] != want[i])
            problems.append(f'{d.where()}: {d.name} is not the bytes of game.exe: first difference at {lo + at:08X} '
                            f'(+{at}): ours {d.packed[at:at + 4].hex()}, game.exe {want[at:at + 4].hex()}')
        for a, b in addon_sites():
            if a < hi and lo < b:
                problems.append(f'{d.where()}: {d.name} overlaps the addon.dll site {a:08X}: that cell is written')
    good = sorted((d for d in defs if d.size), key=lambda d: d.address)
    for x, y in zip(good, good[1:]):
        if y.address < x.address + x.size:
            problems.append(f'{y.where()}: {y.name} overlaps {x.name} ({x.where()})')

    # The retired vtables (config/retired_vtables.csv, C19): no definition, the same proof that nothing reaches them.
    import gen_image_cells
    retired = [Retired(v, n, cls, path) for v, n, cls, path in gen_image_cells.retired_vtables()]
    # addon.dll's sites are game.exe's: addon only runs in game.exe (harness_addon.cpp at WinMain), never in the
    # image, which reproduces its patches behind flags. A retired vtable may hold a site whose slot the image's
    # class reproduces (RETIRED_ADDON_SITES: ResOverride's GetFile slot, CMappedResource::GetFileImpl through
    # mod_folders), and addon_sites.inc may name it; any other site in a retired vtable is a problem.
    for r in retired:
        for a, b in addon_sites():
            if a < r.address + r.size and r.address < b and a not in RETIRED_ADDON_SITES:
                problems.append(f'{r.where()}: {r.name} overlaps the addon.dll site {a:08X}: that slot is written')
    reached = good + retired

    def inside(a):
        for d in reached:
            if d.address <= a < d.address + d.size:
                return d
        return None

    for rel, full in other_files():
        with open(full, encoding='utf-8', errors='replace') as f:
            raw = f.read()
        code = srcscan.blank_comments(raw)
        cells = rel.endswith(('image_cells.inc', 'image_code_cells.inc'))
        for m in (CELL if cells else LITERAL).finditer(code):
            if cells:
                cell, value = int(m.group(1), 16), int(m.group(2), 16)
                hits = [(cell, 'holds a pointer cell at'), (value, 'is pointed at by the cell at')]
                if rel.endswith('image_code_cells.inc'):
                    hits = hits[:1]
                for a, what in hits:
                    d = inside(a)
                    if d:
                        problems.append(f'{d.where()}: {d.name} {what} {cell:08X} ({rel}): game.exe\'s data reaches '
                                        'it; it is not ours alone')
            else:
                a = int(m.group(1), 16)
                d = inside(a)
                if d and not (rel == ADDON_SITES_INC and a in RETIRED_ADDON_SITES):
                    line = raw.count('\n', 0, m.start()) + 1
                    problems.append(f'{rel}:{line}: names {a:08X}, inside {d.name} ({d.where()}): use the name')
    text = csv_text(good)
    current = open(CSV, encoding='utf-8').read() if os.path.exists(CSV) else ''
    if update:
        if current != text:
            with open(CSV, 'w', encoding='utf-8', newline='\n') as f:
                f.write(text)
            print(f'named_data: {os.path.relpath(CSV, ROOT)} written')
    elif current != text:
        problems.append(f'{os.path.relpath(CSV, ROOT)} is not the tagged definitions: run '
                        'python tools/named_data.py --update')
    for p in problems:
        print('named_data:', p)
    total = sum(d.size for d in good)
    print(f'named_data: {len(good)} named range(s), {total} byte(s) of game.exe\'s data, {len(problems)} problem(s)')
    return 1 if problems else 0


# ---- the split of what is left (C18's measure) -------------------------------------------------------------------

GENERATED = ('src/standalone/static_init.cpp', 'src/standalone/static_exit.cpp')


def split(img, wanted):
    def dw(a):
        return struct.unpack_from('<I', img, a - BASE)[0]

    vtable_slots = set()
    with open(os.path.join(ROOT, 'config', 'vtables.csv'), newline='') as f:
        for r in csv.DictReader(f):
            v = int(r['vtable'], 16)
            vtable_slots.update(v + 4 * i for i in range(int(r['slots'] or 1)))
    with open(os.path.join(ROOT, 'config', 'runtime_classes.csv'), newline='') as f:
        runtime_classes = {int(r['runtime_class'], 16) for r in csv.DictReader(f)}

    def kind(a):
        if IMPORTS[0] <= a < IMPORTS[1]:
            return 'import slot'
        if a in vtable_slots:
            return 'vtable'
        if RDATA[0] <= a < RDATA[1]:
            if dw(a) == 0x19930520 or a in runtime_classes:
                return 'eh/rtti/runtime class'
            return 'rdata constant'
        if DATA[0] <= a < DATA_RAW_END:
            return 'data object'
        if DATA_RAW_END <= a < DATA[1]:
            return 'bss object'
        return 'other'

    files = collections.defaultdict(set)
    for s in srcscan.load_sources():
        for m in LITERAL.finditer(s.code):
            a = int(m.group(1), 16)
            if 0x730000 <= a <= 0x8FFFFF:
                files[a].add(s.path)
    kinds = {a: kind(a) for a in files}
    owner = {}
    for a, fs in files.items():
        hand = sorted(fs - set(GENERATED))
        owner[a] = 'generated static init / exit only' if not hand else hand[0] if len(hand) == 1 else 'several files'
    print(f'{len(files)} distinct game data addresses in src/ (progress.py)')
    print('by kind: ' + ', '.join(f'{k} {n}' for k, n in collections.Counter(kinds.values()).most_common()))
    areas = collections.defaultdict(collections.Counter)
    for a in files:
        o = owner[a]
        if o.startswith('src/'):
            folder, name = o.rsplit('/', 1)
            o = f'{folder}/{name.split(".")[0].split("_")[0]}*'
        areas[o][kinds[a]] += 1
    print('by owner area (the one hand-written file that names it, else "several files"):')
    for o, c in sorted(areas.items(), key=lambda x: (-sum(x[1].values()), x[0])):
        print(f'  {sum(c.values()):5} {o}: ' + ', '.join(f'{k} {n}' for k, n in c.most_common()))
    for a in sorted(files):
        if kinds[a] in wanted:
            print(f'{a:08X} {kinds[a]:22} {owner[a]:34} {" ".join(sorted(files[a]))}')
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--update', action='store_true')
    ap.add_argument('--split', nargs='*', metavar='KIND')
    ap.add_argument('--exe')
    a = ap.parse_args()
    try:
        img = image(exe_path.find_exe(a.exe))
    except exe_path.ExeNotFound as e:
        sys.exit(f'named_data: {e}')
    if a.split is not None:
        return split(img, set(a.split))
    return check(img, a.update)


if __name__ == '__main__':
    sys.exit(main())
