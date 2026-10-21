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
  * a pointer element (TYPES 'P': `const char* const kX[] = /*addr*/ {"a", "b"};`, a struct's pointer field, or
    one pointer `constexpr const char* kX = /*addr*/ "text";`) names what game.exe's pointer in its place points at,
    and that is compared: a string literal the text at the target, the name of another named range its address, the
    name of a function of ours its registration (EI_REPLACE*), nullptr a zero;
  * nothing else reaches a named range: no address literal of src/ or tests/ (.cpp, .h, .inc; comments blanked)
    inside it, no pointer cell of game.exe's data pointing into it, no pointer or code cell inside it but at its
    pointer elements (gen_image_cells' scans; its cell lists leave the named ranges' cells out). What the image's
    code does not name and no cell points at, nothing reads: the zero bytes are never seen.

A .data or .bss object (C18d) takes the same form; it is constexpr when no code of ours writes it (most .data tables:
the original's arrays lacked const), plain when the code writes it (`float g_bagSizes[12] = /*007A9BFC*/ {};`).
Elements the initialiser leaves out are zero, as in C++ (`{}`: all of them; the zero-filled tail's objects). A range
in .rdata must be const.

--split [KIND ...]: the game data addresses src/ still names (progress.py's count), by kind and by owner area;
the kinds given also list their addresses.

--refs BEGIN [END]: the original's code reaching game.exe's data in [BEGIN, END] (its absolute operands), by function
and our registration of it: the extent of an object and every function that reads or writes it directly.

usage: python tools/named_data.py --check | --update | --split [KIND ...] | --refs BEGIN [END] [--exe game.exe]
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
    # Pointers ('P', a dword of game.exe's: what it points at is compared, not the address; see Definition.pack).
    'char*': 'P',
    'MaterialKindFile': 'iP',
}
# `T name[dims] = /*address*/ {` or, for one pointer, `const char* const name = /*address*/ "text";`. A pointer
# type's `*` (and a const after it) joins the element type: `const char* const kX[]` is a table of `char*`.
DEF = re.compile(r'(?<![\w:])([\w:]+)(\s*\*\s*(?:const\s+)?|\s+)(\w+)\s*((?:\[[^\]]*\]\s*)*)=\s*/\*([0-9A-F]{8})\*/\s*([{"])')
TAGGED = re.compile(r'=\s*/\*[0-9A-F]{8}\*/\s*[{"]')   # a file with no such tag has no definition
# The initialiser's tokens where its element type holds a pointer: a string literal (adjacent ones joined), a number,
# a name (`&` optional: another named range, a function of ours with a registration, or nullptr).
TOKEN = re.compile(r'"((?:\\.|[^"\\\n])*)"|(-?)\s*(0[xX][0-9A-Fa-f]+|(?:\d+\.\d*|\.\d+|\d+)(?:[eE][+-]?\d+)?)'
                   r'([fFuUlL]*)|&?\s*((?:::)?[A-Za-z_]\w*(?:::\w+)*)')
# Registrations of our functions at game.exe's addresses (ei/hook.h): what a code pointer element may name.
REGISTRATION = re.compile(r'\bEI_REPLACE(?:_STANDALONE|_SHORT|_METHOD|_DISPATCH)?\s*\(\s*0[xX]([0-9A-Fa-f]{8})\s*,'
                          r'\s*&?\s*((?:::)?[A-Za-z_][\w:]*)\s*\)')
NUMBER = re.compile(r'(-?)\s*(0[xX][0-9A-Fa-f]+|(?:\d+\.\d*|\.\d+|\d+)(?:[eE][+-]?\d+)?)([fFuUlL]*)')
LITERAL = re.compile(r'(?<![0-9A-Za-z_])0[xX]00([0-9A-Fa-f]{6})(?![0-9A-Fa-f])')


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
    def __init__(self, path, line, type_, name, dims, address, body, const=True):
        self.path, self.line, self.type, self.name, self.dims, self.address, self.body, self.const = \
            path, line, type_, name, dims, address, body, const
        self.size = None
        self.packed = None
        self.problem = None

    def where(self):
        return f'{self.path}:{self.line}'

    def pack(self, img=None, targets=None):
        """Packs the initialiser by the element type; sets size/packed or problem. A type with pointers packs
        through pack_pointers (img: game.exe's image; targets: Targets)."""
        self.pointers = set()   # offsets of the pointer elements
        if self.type == 'char':
            self.pack_names()
            return
        fmt = TYPES.get(self.type)
        if fmt is None:
            self.problem = f'{self.type} is no element type tools/named_data.py knows (TYPES)'
            return
        if 'P' in fmt:
            self.pack_pointers(fmt, img, targets)
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
        if all(d.isdigit() for d in dims):   # no bound: one element
            want = 1
            for d in dims:
                want *= int(d)
            if count > want:
                self.problem = f'{count} element(s) for {self.dims.strip() or "one"}'
                return
            values += [0] * (len(fmt) * (want - count))   # the rest zero, as C++ initialises it (`{}`: all of it)
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


class Targets:
    """What a pointer element may name: the named ranges (name -> address) and our functions with a registration at
    game.exe's address (name -> addresses; ei/hook.h's EI_REPLACE*)."""
    def __init__(self, defs, sources):
        self.ranges = {d.name: d.address for d in defs}
        self.functions = collections.defaultdict(set)
        for s in sources:
            for m in REGISTRATION.finditer(s.code):
                self.functions[m.group(2).lstrip(':')].add(int(m.group(1), 16))


def tokens(body):
    """The initialiser's values: ('text', bytes) for a string literal (adjacent ones joined, escapes decoded),
    ('number', int | float), ('name', text)."""
    out = []
    code = srcscan.blank_comments(body)
    last_end = None
    for m in TOKEN.finditer(code):
        text, sign, digits, suffix, name = m.groups()
        if text is not None:
            value = text.encode('latin-1').decode('unicode_escape').encode('latin-1')
            if out and out[-1][0] == 'text' and not code[last_end:m.start()].strip():
                out[-1] = ('text', out[-1][1] + value)
            else:
                out.append(('text', value))
        elif digits is not None:
            if digits.lower().startswith('0x'):
                out.append(('number', int(sign + digits, 16)))
            elif re.search(r'[.eE]', digits) or 'f' in suffix.lower():
                out.append(('number', float(sign + digits)))
            else:
                out.append(('number', int(sign + digits)))
        else:
            out.append(('name', name.lstrip(':')))
        last_end = m.end()
    return out


def c_string(img, address):
    """The zero-terminated bytes at a game address of .rdata or .data's file bytes, or None."""
    if not (RDATA[0] <= address < DATA_RAW_END):
        return None
    o = address - BASE
    end = img.find(b'\0', o, DATA_RAW_END - BASE)
    return bytes(img[o:end]) if end >= 0 else None


def _pack_pointers(self, fmt, img, targets):
    """A type with pointer elements ('P'). A pointer element is not compared as a number: it names what game.exe's
    pointer at that place points at, and the check compares that: a string literal the text at the target (game.exe's
    string literals, in .data), a name of another named range its address, a name of a function of ours the address
    of its registration (EI_REPLACE*: the code game.exe's pointer called), nullptr a zero. The packed bytes are then
    game.exe's pointer; its offset is in `pointers` (the cells of game.exe's data the range may hold)."""
    values = tokens(self.body)
    if len(values) % len(fmt):
        self.problem = f'{len(values)} value(s) do not fill whole {self.type} elements ({len(fmt)} each)'
        return
    count = len(values) // len(fmt)
    dims = re.findall(r'\[\s*([^\]]*?)\s*\]', self.dims)
    want = 1
    for d in dims:
        if not d.isdigit():
            want = None
            break
        want *= int(d)
    if want is not None and want != count:
        self.problem = f'{count} element(s) for {self.dims.strip() or "one"}'
        return
    out = bytearray()
    for i, (kind, v) in enumerate(values):
        code = fmt[i % len(fmt)]
        at = self.address + len(out)
        if code != 'P':
            if kind != 'number':
                self.problem = f'{v!r} at {at:08X} is no number ({self.type}\'s field {i % len(fmt)})'
                return
            if code in 'fd':
                out += struct.pack('<' + code, float(v))
            elif isinstance(v, float):
                self.problem = f'{v} is not an integer ({self.type})'
                return
            else:
                bits = struct.calcsize(code) * 8
                out += struct.pack('<' + code.upper(), v & ((1 << bits) - 1))
            continue
        cell = struct.unpack_from('<I', img, at - BASE)[0] if RDATA[0] <= at < DATA[1] else None
        if cell is None:
            self.problem = f'the pointer at {at:08X} is outside game.exe\'s data'
            return
        if kind == 'text':
            target = c_string(img, cell)
            if target != v:
                self.problem = (f'the pointer at {at:08X} points at {cell:08X} ({target!r}), not at the text {v!r}')
                return
        elif kind == 'name' and v == 'nullptr' or kind == 'number' and v == 0:
            if cell != 0:
                self.problem = f'the pointer at {at:08X} is {cell:08X}, not null'
                return
        elif kind == 'name' and v in targets.ranges:
            if cell != targets.ranges[v]:
                self.problem = (f'the pointer at {at:08X} points at {cell:08X}, not at {v} '
                                f'({targets.ranges[v]:08X})')
                return
        elif kind == 'name' and v in targets.functions:
            if cell not in targets.functions[v]:
                where = ', '.join(f'{a:08X}' for a in sorted(targets.functions[v]))
                self.problem = f'the pointer at {at:08X} is the code {cell:08X}, not {v}\'s registration ({where})'
                return
        else:
            self.problem = (f'the pointer at {at:08X} names {v!r}: no string literal, named range, function of '
                            'ours with a registration (EI_REPLACE*) or nullptr')
            return
        self.pointers.add(len(out))
        out += struct.pack('<I', cell)
    self.packed = bytes(out)
    self.size = len(out)


Definition.pack_pointers = _pack_pointers


class Retired:
    """A retired vtable (gen_image_cells.retired_vtables): game.exe's vtable of a class whose vtable is the compiler's."""
    def __init__(self, vtable, slots, cls, path):
        self.address, self.size, self.name, self.path = vtable, 4 * slots, f"{cls}'s retired vtable", path

    def where(self):
        return 'config/retired_vtables.csv'


def definitions(sources=None):
    """Every tagged definition under src/, in path order."""
    found = []
    for s in sources or srcscan.load_sources():
        if not TAGGED.search(s.raw):
            continue
        for m in DEF.finditer(s.raw):
            if not s.code[m.start(1)].strip():   # inside a comment
                continue
            if m.group(6) == '{':
                body = braced(s.raw, m.end() - 1)
            else:   # one pointer: the string literal(s) up to the `;`
                end = next((e.start() for e in re.finditer(r'"(?:\\.|[^"\\\n])*"|;', s.code[m.end() - 1:])
                            if e.group() == ';'), None)
                body = s.raw[m.end() - 1:m.end() - 1 + end] if end is not None else None
            type_ = m.group(1) + ('*' if '*' in m.group(2) else '')
            start = max(s.code.rfind(c, 0, m.start(1)) for c in ';{}')   # where the declaration begins
            const = bool(re.search(r'\bconst(?:expr)?\b', s.code[start + 1:m.start(1)] + m.group(2)))
            found.append(Definition(s.path, s.line_of(m.start()), type_, m.group(3), m.group(4),
                                    int(m.group(5), 16), body or '', const))
    return found


def addon_sites():
    """(begin, end) of the cells addon.dll writes (tests/verify/addon_sites.inc)."""
    import rdata_literals
    return [(a, a + n) for a, n in rdata_literals.addon_sites()]


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
    sources = srcscan.load_sources()
    defs = definitions(sources)
    targets = Targets(defs, sources)
    for d in defs:
        d.pack(img, targets)
        if d.problem:
            problems.append(f'{d.where()}: {d.name}: {d.problem}')
            continue
        lo, hi = d.address, d.address + d.size
        if not (RDATA[0] <= lo and hi <= RDATA[1] or DATA[0] <= lo and hi <= DATA[1]):
            problems.append(f'{d.where()}: {d.name} {lo:08X}+{d.size} is not in .rdata past the import table or .data')
            continue
        if hi <= RDATA[1] and not d.const:
            problems.append(f'{d.where()}: {d.name} is in .rdata, which nothing writes: declare it constexpr')
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
    for r in retired:
        for a, b in addon_sites():
            if a < r.address + r.size and r.address < b:
                problems.append(f'{r.where()}: {r.name} overlaps the addon.dll site {a:08X}: that slot is written')
    reached = good + retired

    def inside(a):
        for d in reached:
            if d.address <= a < d.address + d.size:
                return d
        return None

    # game.exe's pointer and code cells (gen_image_cells' scans, the named ranges' own cells included): a cell inside a
    # named range is one of its pointer elements (compared by pack_pointers; the image's cell lists leave it out), and
    # no cell outside the named ranges points into one.
    kinds = gen_image_cells.structures(img)
    data_cells = [(c, v) for c, v in gen_image_cells.scan(img, keep_named=True)
                  if (gen_image_cells.group_of(c, v, kinds) or ('',))[0] != 'text']
    code_cells = [(c, v) for c, v, _ in gen_image_cells.code_check(img, [], keep_named=True)[0]]
    for cells, what in ((data_cells, 'pointer cell'), (code_cells, 'code cell')):
        for cell, value in cells:
            d = inside(cell)
            if d is not None and cell - d.address not in getattr(d, 'pointers', ()):
                problems.append(f'{d.where()}: {d.name} holds game.exe\'s {what} at {cell:08X} (-> {value:08X}) where '
                                'it has no pointer element: game.exe\'s data reaches it; it is not ours alone')
            t = inside(value) if what == 'pointer cell' and d is None else None
            if t is not None:
                problems.append(f'{t.where()}: {t.name} is pointed at by game.exe\'s cell at {cell:08X}: game.exe\'s '
                                'data reaches it; it is not ours alone')

    for rel, full in other_files():
        if rel.endswith(('image_cells.inc', 'image_code_cells.inc')):
            continue
        with open(full, encoding='utf-8', errors='replace') as f:
            raw = f.read()
        code = srcscan.blank_comments(raw)
        for m in LITERAL.finditer(code):
            a = int(m.group(1), 16)
            d = inside(a)
            if d:
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


TEXT = (0x00401000, 0x0073B000)


def refs(img, begin, end):
    """The dwords of game.exe's code section whose value lies in [begin, end] (end included: a loop's end compare),
    each with the function it sits in (the nearest start of config/functions.csv or of a registration) and our
    registration of that function. An instruction's absolute operand shows where the original reads or writes the
    range: the starts it indexes from, the extent its loops give, and every function that reaches it directly. Some
    hits are no operand (a call's rel32, an immediate): read the instruction where one looks wrong."""
    import bisect
    sources = srcscan.load_sources()
    registered = collections.defaultdict(list)
    for s in sources:
        for m in REGISTRATION.finditer(s.code):
            registered[int(m.group(1), 16)].append(f'{m.group(2)} ({s.path})')
    with open(os.path.join(ROOT, 'config', 'functions.csv'), newline='') as f:
        starts = sorted({int(r['address'], 16) for r in csv.DictReader(f)} | set(registered))
    for o in range(TEXT[0] - BASE, TEXT[1] - BASE - 3):
        v = struct.unpack_from('<I', img, o)[0]
        if begin <= v <= end:
            fn = starts[bisect.bisect_right(starts, o + BASE) - 1]
            ours = ', '.join(registered.get(fn, [])) or 'no registration'
            print(f'{o + BASE:08X} -> {v:08X}  in {fn:08X}: {ours}')
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--update', action='store_true')
    ap.add_argument('--split', nargs='*', metavar='KIND')
    ap.add_argument('--refs', nargs='+', metavar='ADDRESS', help='BEGIN [END]: the code reaching game.exe\'s data there')
    ap.add_argument('--exe')
    a = ap.parse_args()
    try:
        img = image(exe_path.find_exe(a.exe))
    except exe_path.ExeNotFound as e:
        sys.exit(f'named_data: {e}')
    if a.split is not None:
        return split(img, set(a.split))
    if a.refs:
        return refs(img, int(a.refs[0], 16), int(a.refs[-1], 16))
    return check(img, a.update)


if __name__ == '__main__':
    sys.exit(main())
