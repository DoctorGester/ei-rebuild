"""game.exe's data as named C++ globals (C18; docs/notes/standalone.md "Named data (C18)").

A table or object of game.exe's .rdata or .data that only our code reads becomes a typed definition in the source
whose initialiser carries the original's address as a tag right after the `=`:

    constexpr int kSlotXs[7] = /*0074111C*/ {300, 500, 200, 400, 600, 75, 725};
    constexpr GUID kIidDirectDraw7 = /*00745C90*/ {0x15E65EC0, 0x3B9C, 0x11D2, {0xB9, 0x2F, 0x00, 0x60, ...}};

The code then names the global (kSlotXs[i]), not the address (GamePointer<const int>(0x0074111C)), and nothing reads the
range's bytes (the image's copy of the sections is zero: C20t).

--check (build.cmd; reads no game.exe: the comparisons with its bytes and cells were tools/check_against_exe.cmd's until
C25d, the proof of each definition when it was made):
  * every tagged initialiser is well formed: its numbers pack by the declared element type (TYPES; a struct type its
    fields' formats, nested braces flattened), it lies in .rdata past the import table or .data and its zero-filled
    tail, no addon.dll site but NAMED_ADDON_SITES (the ones the image writes itself), no two ranges overlapping, and one
    in .rdata is constexpr;
  * config/named_data.csv lists exactly the tagged ranges (address, size, name, file): `--update` writes it;
  * a pointer element (TYPES 'P': `const char* const kX[] = /*addr*/ {"a", "b"};`, a struct's pointer field, or
    one pointer `constexpr const char* kX = /*addr*/ "text";`) names a string literal, another named range (qualified
    or not; or `NAME + k` its element k: _pctype's `kCtypeC + 1`, written by hand), a function of ours (any other
    name: the compiler checks it), or nullptr;
  * no address literal of src/ or tests/ (.cpp, .h, .inc; comments blanked) lies inside a named range, a retired vtable
    (config/vtables.csv) or an unread object (config/unread_data.csv). What the image's code does not name,
    nothing reads: the zero bytes are never seen.

The bytes a definition replaced were game.exe's as its static initialisers left them (C18j). The unread objects of
config/unread_data.csv (only their initialiser stored into them) get the same proof that nothing reaches them.

A .data or .bss object (C18d) takes the same form; it is constexpr when no code of ours writes it (most .data tables:
the original's arrays lacked const), plain when the code writes it (`float g_bagSizes[12] = /*007A9BFC*/ {};`).
Elements the initialiser leaves out are zero, as in C++ (`{}`: all of them; the zero-filled tail's objects). A range
in .rdata must be const.

--split [KIND ...]: the game data addresses src/ still names (progress.py's count), by kind and by owner area;
the kinds given also list their addresses.

--refs BEGIN [END]: the original's code reaching game.exe's data in [BEGIN, END] (its absolute operands), by function:
the extent of an object and every function that reads or writes it directly.

--rewrite NAME [--apply] (C18o): in src/, the reads of a named range (in config/named_data.csv: run --update first)
by address, `GamePointer<T>(0x...)` / `GameData<T>(0x...)` (a `DataId(0x...)` argument too) where T is the element
type and the address an element's start, become the name (`NAME`, `(NAME + k)`, `NAME[k]`); the other mentions
(DataId constants, other types, a field's address, tests/) are listed for a person. Without --apply it only lists.
--check stays the proof of its form.

usage: python tools/named_data.py --check | --update | --split [KIND ...] | --refs BEGIN [END]
                                  | --rewrite NAME [--apply]  [--exe game.exe]
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
    'bool': 'B', 'std::int8_t': 'b', 'std::uint8_t': 'B', 'BYTE': 'B',
    'Rect': 'iiii', 'Point': 'ii', 'HINSTANCE': 'I', 'platform::NativeInstance': 'I', 'platform::NativeWindow': 'I', 'HWND': 'I', 'GUID': 'IHH' + 'B' * 8, 'IID': 'IHH' + 'B' * 8, 'CLSID': 'IHH' + 'B' * 8,   # one code per number
    'DecalBuilder': 'B' * 0x7C,   # ground_decal.h
    'SpiralCache': 'if',   # ai_map.h: length, radius
    'Vector3': 'fff', 'Quaternion': 'ffff', 'FogTile': 'ffff',   # effects.h
    'IsaacRandom': 'I' * 0x205,   # random.h: seeded, count, results[256], memory[256], a, b, c
    'WeatherNoiseCurve': 'f' * (128 + 128 * 4),   # weather_noise.h: the samples, then the segments
    # Pointers ('P', a dword of game.exe's: what it points at is compared, not the address; see Definition.pack).
    'char*': 'P',
    'MaterialKindFile': 'iP',
    'OptionDefinition': 'iPiiii', 'ActionDefinition': 'iPii',   # ui.h: id, name, kind, maximum, group, row / id, name, group, row
    'ScriptParticleType': 'Pii', 'AmbientSoundClass': 'Pififi',   # script.h: name, type, lifetime / terrain_ambience.cpp
    'ScriptKeyword': 'PiPP',                 # script.h: name, id, function signature, command signature
    # A char array field is one code 'Ns' (a tuple of codes then): a string literal, zero padded (fewer than N chars).
    'UiKeyName': ('i', 'P', '64s'),          # ui.h: scan code, the file's name, GetKeyNameTextA's text (key_bindings.cpp)
    'std::size_t': 'I',
    'Ld12': 'H' * 6,                         # crt_format.cpp: a 12-byte long double, six words
    # Containers a static initialiser zeroes and an exit function frees (C18k): pointers that start null, compared as
    # numbers. A template's arguments do not change the layout: TYPES names the template (element_format).
    'lib::AlignedArray': 'Iiii',   # containers.h: data, size, capacity, growBy (game.exe's; our std::vector: below)
    'stlp::vector': 'III',         # the original's STLport vector (a key only: none of ours): start, finish, end of storage
    'std::vector': 'III',          # MSVC's: first, last, end of storage (zero is empty, as STLport's)
    'LineBuffer': 'Iiii',          # renderer_lines.h: an aligned array of line vertices
    'Ref': 'I',                    # ref.h: a counted pointer, null until it is assigned
}
# `T name[dims] = /*address*/ {` (T may be a template instantiation: `lib::AlignedArray<TLVertex> g_x = /*addr*/ {};`), for one pointer `const char* const name = /*address*/ "text";`, for one number
# `constexpr float kX = /*address*/ 0.5f;` (a .data object a static initialiser sets, C18j). A pointer
# type's `*` (and a const after it) joins the element type: `const char* const kX[]` is a table of `char*`.
DEF = re.compile(r'(?<![\w:])([\w:]+(?:<(?:[^<>;=(){}]|<(?:[^<>;=(){}]|<[^<>;=(){}]*>)*>)*>)?)(\s*\*\s*(?:const\s+)?|\s+)(\w+)\s*((?:\[[^\]]*\]\s*)*)=\s*/\*([0-9A-F]{8})\*/\s*([{"]|-?[\d.])')
TAGGED = re.compile(r'=\s*/\*[0-9A-F]{8}\*/\s*(?:[{"]|-?[\d.])')   # a file with no such tag has no definition
# The initialiser's tokens where its element type holds a pointer: a string literal (adjacent ones joined), a number,
# a name (`&` optional: another named range, a function of ours, or nullptr), a named range's
# element `NAME + k` (C18q: _pctype's `kCtypeC + 1`).
TOKEN = re.compile(r'"((?:\\.|[^"\\\n])*)"|(-?)\s*(0[xX][0-9A-Fa-f]+|(?:\d+\.\d*|\.\d+|\d+)(?:[eE][+-]?\d+)?)'
                   r'([fFuUlL]*)|&?\s*((?:::)?[A-Za-z_]\w*(?:::\w+)*)(?:\s*\+\s*(\d+))?')
NUMBER = re.compile(r'(-?)\s*(0[xX][0-9A-Fa-f]+|(?:\d+\.\d*|\.\d+|\d+)(?:[eE][+-]?\d+)?)([fFuUlL]*)')
LITERAL = re.compile(r'(?<![0-9A-Za-z_])0[xX]00([0-9A-Fa-f]{6})(?![0-9A-Fa-f])')
ORIGINAL_READ = re.compile(r'\bExeTableId\s*\(\s*$')   # just before a literal


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


# The std::vector globals that replaced game.exe's engine aligned arrays (C14e): their ranges keep the original's 16
# bytes (data, size, capacity, growBy). Any other std::vector range replaced an STLport vector (12 bytes).
ALIGNED_ARRAY_VECTORS = {
    0x007A9CB8, 0x007A9CE0, 0x007AEDE0, 0x007AEE00, 0x007AEE10, 0x007AEE20, 0x007AEE30,
    0x007AEED0, 0x007AEF90, 0x007AEFA0, 0x007AEFB8, 0x007AEFC8, 0x007AEFD8, 0x007AEFE8, 0x007AF000,
    0x007AF010, 0x007AF020, 0x007AF030, 0x007AF040,
    0x007C2E18, 0x007C2E48, 0x007C2E60,
    0x007C88E8, 0x007C88F8, 0x007C8938,
}


def element_format(type_, address=None):
    """TYPES' format of an element type; a template instantiation's is its template's (`lib::AlignedArray<T>`); a
    pointer to another type is a pointer element ('P': `DirectInputMouse* g_mouse = /*0079BA08*/ {nullptr};`).
    A std::vector's is the layout of the game.exe container it replaced, by its address (ALIGNED_ARRAY_VECTORS)."""
    if re.fullmatch(r'std::vector<.*>', type_):
        return TYPES['lib::AlignedArray'] if address in ALIGNED_ARRAY_VECTORS else TYPES['stlp::vector']
    found = TYPES.get(type_)
    if found is None:
        found = TYPES.get(re.sub(r'<.*>$', '', type_))
    if found is None and type_.endswith('*'):
        return 'P'
    return found


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
        fmt = element_format(self.type, self.address)
        if fmt is None:
            self.problem = f'{self.type} is no element type tools/named_data.py knows (TYPES)'
            return
        if any(c == 'P' or c.endswith('s') for c in fmt):   # pointers or char array fields
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
        if any(re.match(r'0[xX]', d) for d in dims):   # [0x22] would read as no bound: the listed elements only
            self.problem = f'{self.dims.strip()}: write the bound as a decimal number'
            return
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
    """What a pointer element may name: the named ranges (name -> address). Any other name is a function of ours."""
    def __init__(self, defs):
        self.ranges = {d.name: d.address for d in defs}
        self.element_sizes = {}   # a named range's element size, for `NAME + k`
        for d in defs:
            fmt = 'B' if d.type == 'char' else element_format(d.type, d.address)
            if fmt is not None:
                self.element_sizes[d.name] = sum(code_size(c) for c in codes_of(fmt))


_ENUM_NUMBERS = {}


def enum_numbers():
    """Names an initialiser writes for a number, from script.h: `ScriptFunction::Name` (the enum class's values) and
    `Lex(kLexName)` (ScriptKeyword's punctuation entries: a ScriptLexeme value) -> the number's text."""
    if not _ENUM_NUMBERS:
        text = srcscan.blank_comments(open(os.path.join(ROOT, 'src', 'game', 'script.h'), encoding='utf-8').read())
        body = re.search(r'enum class ScriptFunction\b[^{]*\{(.*?)\};', text, re.S).group(1)
        for name, value in re.findall(r'(\w+)\s*=\s*(-?\w+)', body):
            _ENUM_NUMBERS['ScriptFunction::' + name] = value
        body = re.search(r'enum ScriptLexeme\b[^{]*\{(.*?)\};', text, re.S).group(1)
        for name, value in re.findall(r'(\w+)\s*=\s*(0[xX][0-9A-Fa-f]+|\d+)', body):
            _ENUM_NUMBERS['Lex(' + name + ')'] = value
    return _ENUM_NUMBERS


def resolve_enums(code):
    numbers = enum_numbers()
    code = code.replace('ScriptFunction{}', '0')
    return re.sub(r'ScriptFunction::\w+|Lex\(\w+\)', lambda m: numbers.get(m.group(0), m.group(0)), code)


def tokens(body):
    """The initialiser's values: ('text', bytes) for a string literal (adjacent ones joined, escapes decoded),
    ('number', int | float), ('name', text), ('element', (name, k)) for `NAME + k`."""
    out = []
    code = resolve_enums(srcscan.blank_comments(body))
    last_end = None
    for m in TOKEN.finditer(code):
        text, sign, digits, suffix, name, offset = m.groups()
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
            out.append(('element', (name.lstrip(':'), int(offset))) if offset else ('name', name.lstrip(':')))
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
    string literals, in .data), a name of another named range its address, a name of a function of ours any code (game.exe's pointer called the function
    this one rewrites; the address is not known since C20l), nullptr a zero. The packed bytes are then
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
    if want is not None and count < want and not any(c.endswith('s') for c in fmt):
        values += [('number', 0)] * ((want - count) * len(fmt))   # left out: zero (null), as in C++
        count = want
    if want is not None and want != count:
        self.problem = f'{count} element(s) for {self.dims.strip() or "one"}'
        return
    out = bytearray()
    for i, (kind, v) in enumerate(values):
        if kind == 'name' and v not in targets.ranges and v.rsplit('::', 1)[-1] in targets.ranges:
            v = v.rsplit('::', 1)[-1]   # a named range in another namespace (`&console::g_aiLogOut`)
        code = fmt[i % len(fmt)]
        at = self.address + len(out)
        if code.endswith('s'):   # a char array field: a string literal, zero padded
            width = int(code[:-1])
            if kind != 'text' or len(v) >= width:
                self.problem = f'{v!r} at {at:08X} is no string literal shorter than {width} ({self.type}\'s field {i % len(fmt)})'
                return
            out += v.ljust(width, b'\0')
            continue
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
        if img is None:   # no game.exe (--check): the element must name something the check can compare; zero stands in
            if not (kind in ('text', 'name') or kind == 'number' and v == 0 or
                    kind == 'element' and v[0] in targets.element_sizes):
                self.problem = (f'the pointer at {at:08X} names {v!r}: no string literal, named range, function of '
                                'ours or nullptr')
                return
            self.pointers.add(len(out))
            out += bytes(4)
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
        elif kind == 'element' and v[0] in targets.element_sizes:
            want = targets.ranges[v[0]] + v[1] * targets.element_sizes[v[0]]
            if cell != want:
                self.problem = (f'the pointer at {at:08X} points at {cell:08X}, not at {v[0]} + {v[1]} ({want:08X})')
                return
        elif kind == 'name':
            if not TEXT[0] <= cell < TEXT[1]:
                self.problem = f'the pointer at {at:08X} is {cell:08X}, no code: {v} is no function'
                return
        else:
            self.problem = (f'the pointer at {at:08X} names {v!r}: no string literal, named range, function of '
                            'ours or nullptr')
            return
        self.pointers.add(len(out))
        out += struct.pack('<I', cell)
    self.packed = bytes(out)
    self.size = len(out)


Definition.pack_pointers = _pack_pointers


class Retired:
    """A retired vtable (retired_vtables.retired_vtables): game.exe's vtable of a class whose vtable is the compiler's."""
    def __init__(self, vtable, slots, cls, path):
        self.address, self.size, self.name, self.path = vtable, 4 * slots, f"{cls}'s retired vtable", path

    def where(self):
        return 'config/vtables.csv'


def definitions(sources=None):
    """Every tagged definition under src/, in path order."""
    found = []
    for s in sources or srcscan.load_sources():
        if not TAGGED.search(s.raw):
            continue
        pos = 0
        while True:
            m = DEF.search(s.raw, pos)
            if m is None:
                break
            if not s.code[m.start(1)].strip():   # inside a comment: a match must not swallow the code after it
                pos = m.start(1) + 1
                continue
            pos = m.end()
            if m.group(6) == '{':
                body = braced(s.raw, m.end() - 1)
            else:   # one pointer (its string literal(s)) or one number, up to the `;`
                at = m.start(6)   # a number's sign included
                end = next((e.start() for e in re.finditer(r'"(?:\\.|[^"\\\n])*"|;', s.code[at:])
                            if e.group() == ';'), None)
                body = s.raw[at:at + end] if end is not None else None
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


# The addon.dll sites a retired vtable may hold (see check: the image's class reproduces the slot), and the
# generated list of sites that names them (game.exe's addresses; the harness's addon fix set read it until H40).
RETIRED_ADDON_SITES = {0x007432F4}   # ResOverride: CMappedResource's slot 5, GetFile (007432E0, C19 batch 3)
# The addon.dll sites a named range may hold (and addon_sites.inc may name, for the harness in game.exe): a patch
# disabled here (config/rebirth_patches.csv), whose cell is a plain global of ours that the image's flag would write,
# or one the image's code reproduces by writing our global (C25a: WideScreen). address -> why. One in .rdata is written
# all the same (not const).
NAMED_ADDON_SITES = {
    0x00742B08: "WideScreen's layout width: screen_layout::g_layoutWidth, written by screen_layout.cpp's Startup",
    0x007428AC: "WideScreen's panels' right column: screen_layout::g_panelRight, written by screen_layout.cpp's Startup",
    0x00783E34: "SetDefaultRate (disabled here) writes Settings\\Rate over net::g_remoteRate (net_session.cpp)",
}
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


UNREAD_CSV = os.path.join(ROOT, 'config', 'unread_data.csv')


def read_unread():
    if not os.path.exists(UNREAD_CSV):
        return []
    with open(UNREAD_CSV, newline='') as f:
        return [(int(r['address'], 16), int(r['size']), r['initialiser']) for r in csv.DictReader(f)]


def csv_text(defs):
    lines = ['address,size,name,file']
    for d in sorted(defs, key=lambda d: d.address):
        lines.append(f'{d.address:08X},{d.size},{d.name},{d.path}')
    return '\n'.join(lines) + '\n'


class Unread:
    """An object of config/unread_data.csv: only its static initialiser stored into it."""
    def __init__(self, address, size, initialiser):
        self.address, self.size = address, size
        self.name, self.path = f'the unread object of initialiser {initialiser}', 'config/unread_data.csv'

    def where(self):
        return self.path


def check(update):
    """--check / --update: the definitions of src/ against each other and config/*.csv; no game.exe (the comparison
    with its bytes and its cells was tools/check_against_exe.cmd's, until C25d). A pointer element is checked for
    what it names, not for its value."""
    problems = []
    sources = srcscan.load_sources()
    defs = definitions(sources)
    targets = Targets(defs)
    for d in defs:
        d.pack(None, targets)
        if d.problem:
            problems.append(f'{d.where()}: {d.name}: {d.problem}')
            continue
        lo, hi = d.address, d.address + d.size
        if not (RDATA[0] <= lo and hi <= RDATA[1] or DATA[0] <= lo and hi <= DATA[1]):
            problems.append(f'{d.where()}: {d.name} {lo:08X}+{d.size} is not in .rdata past the import table or .data')
            continue
        own_site = any(lo <= a < hi for a in NAMED_ADDON_SITES)
        if hi <= RDATA[1] and not d.const and not own_site:
            problems.append(f'{d.where()}: {d.name} is in .rdata, which nothing writes: declare it constexpr')
        for a, b in addon_sites():
            if a < hi and lo < b and a not in NAMED_ADDON_SITES:
                problems.append(f'{d.where()}: {d.name} overlaps the addon.dll site {a:08X}: that cell is written')
    good = sorted((d for d in defs if d.size), key=lambda d: d.address)
    for x, y in zip(good, good[1:]):
        if y.address < x.address + x.size:
            problems.append(f'{y.where()}: {y.name} overlaps {x.name} ({x.where()})')

    # The retired vtables (config/vtables.csv, C19): no definition, the same proof that nothing reaches them.
    import retired_vtables
    retired = [Retired(v, n, cls, path) for v, n, cls, path in retired_vtables.retired_vtables()]
    # addon.dll's sites are game.exe's: addon only ran in game.exe (the harness's addon fix set until H40), never in the
    # image, which reproduces its patches behind flags. A retired vtable may hold a site whose slot the image's
    # class reproduces (RETIRED_ADDON_SITES: ResOverride's GetFile slot, CMappedResource::GetFileImpl through
    # mod_folders), and addon_sites.inc may name it; any other site in a retired vtable is a problem.
    for r in retired:
        for a, b in addon_sites():
            if a < r.address + r.size and r.address < b and a not in RETIRED_ADDON_SITES:
                problems.append(f'{r.where()}: {r.name} overlaps the addon.dll site {a:08X}: that slot is written')
    # The unread objects (config/unread_data.csv): the same proof; no named range overlaps one.
    unread = [Unread(a, n, i) for a, n, i in read_unread()]
    for u in unread:
        for d in good:
            if d.address < u.address + u.size and u.address < d.address + d.size:
                problems.append(f'{d.where()}: {d.name} overlaps {u.name} at {u.address:08X}')
    reached = good + retired + unread

    def inside(a):
        for d in reached:
            if d.address <= a < d.address + d.size:
                return d
        return None

    for rel, full in other_files():
        with open(full, encoding='utf-8', errors='replace') as f:
            raw = f.read()
        code = srcscan.blank_comments(raw)
        for m in LITERAL.finditer(code):
            a = int(m.group(1), 16)
            d = inside(a)
            # The harness keeps a few of game.exe's tables as ids (ExeTableId, tests/verify/harness_digest.cpp).
            original_read = rel.startswith('tests/') and ORIGINAL_READ.search(code, max(0, m.start() - 80), m.start())
            if d and not original_read and not (rel == ADDON_SITES_INC and (a in RETIRED_ADDON_SITES or a in NAMED_ADDON_SITES)):
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


# ---- the initialisers' packing ---------------------------------------------------------------------------------------

def codes_of(fmt):
    """The field codes of a TYPES format: one per character, or the tuple's (a char array field is 'Ns')."""
    return list(fmt)


def code_size(code):
    return 4 if code == 'P' else struct.calcsize('<' + code)


def c_literal(data):
    """A C++ string literal of `data` (bytes): printable ASCII as is, the usual escapes, other bytes as three-digit
    octal escapes (which end where they should, unlike \\x)."""
    out = []
    for b in data:
        ch = chr(b)
        if ch in '"\\':
            out.append('\\' + ch)
        elif ch == '\n':
            out.append('\\n')
        elif ch == '\r':
            out.append('\\r')
        elif ch == '\t':
            out.append('\\t')
        elif 0x20 <= b < 0x7F:
            out.append(ch)
        else:
            out.append(f'\\{b:03o}')
    return '"' + ''.join(out) + '"'


class Namer:
    """What a pointer of game.exe's data names in an initialiser: a named range (its start), a string literal (the zero-terminated text at a string's start),
    nullptr."""
    def __init__(self, img, sources):
        defs = definitions(sources)
        targets = Targets(defs)
        self.img = img
        self.ranges = {}
        for name, address in targets.ranges.items():
            self.ranges.setdefault(address, name)

    def string_at(self, cell):
        """The text at `cell` when it looks like a string's start: the byte before is a terminator (or another
        string's pad) and the text is printable (Windows-1251 letters and the usual control characters allowed)."""
        text = c_string(self.img, cell)
        if text is None:
            return None
        before = self.img[cell - BASE - 1]
        if before != 0 and cell % 4:
            return None
        if any(b < 0x20 and b not in (9, 10, 13) or b == 0x7F for b in text):
            return None
        return text

    def name(self, cell):
        """(the initialiser's token, None) or (a placeholder, the reason a person has to look)."""
        if cell == 0:
            return 'nullptr', None
        if cell in self.ranges:
            return self.ranges[cell], None
        if TEXT[0] <= cell < TEXT[1]:
            return f'/*?{cell:08X}*/ nullptr', f'{cell:08X} is code: name the function of ours that rewrites it'
        text = self.string_at(cell)
        if text is not None:
            return c_literal(text), None
        return f'/*?{cell:08X}*/ nullptr', f'{cell:08X} is game.exe\'s data that is no string start and no named range'


def layout(elements, single, indent='    ', width=120):
    """The initialiser's lines: a scalar table flows, a struct table has one braced element per line."""
    items = [f[0] if single else '{' + ', '.join(f) + '}' for f in elements]
    lines, line = [], indent
    for item in items:
        piece = item + ','
        if not single:
            lines.append(indent + piece)
            continue
        if line.strip() and len(line) + 1 + len(piece) > width:
            lines.append(line)
            line = indent
        line += (' ' if line.strip() else '') + piece
    if line.strip():
        lines.append(line)
    lines[-1] = lines[-1].rstrip(',')
    return lines


READS = re.compile(r'(?:\bei::)?\b(GamePointer|GameData)\s*<\s*([^<>()]*?)\s*>\s*\(\s*(?:(?:ei::)?DataId\s*\(\s*)?'
                   r'0[xX]00([0-9A-Fa-f]{6})\s*\)?\s*\)')
IDS = re.compile(r'(?:\bei::)?\b(DataId|ExeTableId)\s*(?:<[^<>()]*>)?\s*\(\s*0[xX]00([0-9A-Fa-f]{6})\s*\)')
ADDRESS = re.compile(r'0[xX]00([0-9A-Fa-f]{6})\b')


def plain_type(text):
    return re.sub(r'\s+', '', re.sub(r'\b(?:const|volatile|ei::)\s*', '', text))


def rewrite(name, apply_):
    """--rewrite NAME: in src/, `GamePointer<T>(0x...)` and `GameData<T>(0x...)` of an address in NAME's range, where T
    is its element type, become the name (`NAME`, `(NAME + k)`, `NAME[k]`); every other mention of the range (DataId
    constants, field offsets, other types, tests/) is listed for a person. Without --apply it only lists."""
    sources = srcscan.load_sources()
    found = [d for d in definitions(sources) if d.name == name]
    if len(found) != 1:
        sys.exit(f'named_data: {len(found)} tagged definition(s) named {name}')
    d = found[0]
    fmt = element_format(d.type, d.address)
    if fmt is None:
        sys.exit(f'named_data: {d.type} is no element type tools/named_data.py knows (TYPES)')
    esize = sum(code_size(c) for c in codes_of(fmt)) if d.type != 'char' else 1
    rows = [r for r in read_csv() if r[2] == name]
    if not rows:
        sys.exit(f'named_data: {name} is not in config/named_data.csv yet: run --update first')
    lo, size = rows[0][0], rows[0][1]
    hi = lo + size
    element = plain_type(d.type)
    changed, left = 0, []
    for rel, full in other_files():
        with open(full, encoding='utf-8', errors='replace', newline='') as f:   # line ends kept
            raw = f.read()
        code = srcscan.blank_comments(raw)
        if not any(lo <= int(m.group(1), 16) < hi for m in ADDRESS.finditer(code)):
            continue
        edits = []
        for m in READS.finditer(code):
            a = int(m.group(3), 16)
            if not lo <= a < hi:
                continue
            k, r = divmod(a - lo, esize)
            if rel.startswith('tests/') or r or plain_type(m.group(2)) != element:
                continue
            if m.group(1) == 'GamePointer':
                text = name if k == 0 else f'({name} + {k})'
            else:
                text = f'{name}[{k}]'
            edits.append((m.start(), m.end(), text))
        taken = [(s, e) for s, e, _ in edits]
        for m in ADDRESS.finditer(code):
            a = int(m.group(1), 16)
            if lo <= a < hi and not any(s <= m.start() < e for s, e in taken):
                line = raw.count('\n', 0, m.start()) + 1
                text = raw.splitlines()[line - 1].strip()
                left.append(f'{rel}:{line}: {text[:140]}')
        for s, e, text in edits:
            line = raw.count('\n', 0, s) + 1
            print(f'{rel}:{line}: {raw[s:e]} -> {text}' + ('' if rel == d.path else f'  (defined in {d.path})'))
        if edits and apply_:
            for s, e, text in sorted(edits, reverse=True):
                raw = raw[:s] + text + raw[e:]
            with open(full, 'w', encoding='utf-8', newline='') as f:
                f.write(raw)
        changed += len(edits)
    for item in left:
        print(f'for a person: {item}')
    print(f'named_data: {changed} read(s) of {name} {"rewritten" if apply_ else "to rewrite (--apply)"}, '
          f'{len(left)} mention(s) left for a person')
    return 0


# ---- the split of what is left (C18's measure) -------------------------------------------------------------------

def split(img, wanted):
    def dw(a):
        return struct.unpack_from('<I', img, a - BASE)[0]

    vtable_slots = set()
    with open(os.path.join(ROOT, 'config', 'vtables.csv'), newline='') as f:
        for r in csv.DictReader(f):
            v = int(r['vtable'], 16)
            vtable_slots.update(v + 4 * i for i in range(int(r['slots'] or 1)))

    def kind(a):
        if IMPORTS[0] <= a < IMPORTS[1]:
            return 'import slot'
        if a in vtable_slots:
            return 'vtable'
        if RDATA[0] <= a < RDATA[1]:
            if dw(a) == 0x19930520:
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
        hand = sorted(fs)
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
    each with the function it sits in (the nearest start of config/functions.csv). An instruction's absolute operand shows where the original reads or writes the
    range: the starts it indexes from, the extent its loops give, and every function that reaches it directly. Some
    hits are no operand (a call's rel32, an immediate): read the instruction where one looks wrong."""
    import bisect
    with open(os.path.join(ROOT, 'config', 'functions.csv'), newline='') as f:
        rows = {int(r['address'], 16): r['name'] for r in csv.DictReader(f)}
    starts = sorted(rows)
    for o in range(TEXT[0] - BASE, TEXT[1] - BASE - 3):
        v = struct.unpack_from('<I', img, o)[0]
        if begin <= v <= end:
            fn = starts[bisect.bisect_right(starts, o + BASE) - 1]
            print(f'{o + BASE:08X} -> {v:08X}  in {fn:08X}: {rows[fn]}')
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--update', action='store_true')
    ap.add_argument('--split', nargs='*', metavar='KIND')
    ap.add_argument('--refs', nargs='+', metavar='ADDRESS', help='BEGIN [END]: the code reaching game.exe\'s data there')
    ap.add_argument('--rewrite', metavar='NAME', help='list the reads of a named range to rewrite to its name')
    ap.add_argument('--apply', action='store_true', help='with --rewrite: write the rewrites')
    ap.add_argument('--exe')
    a = ap.parse_args()
    if a.rewrite:
        return rewrite(a.rewrite, a.apply)
    if not (a.split is not None or a.refs):
        return check(a.update)   # --check, --update: no game.exe
    try:
        path = exe_path.find_exe(a.exe)
        img = exe_path.image(path)
    except exe_path.ExeNotFound as e:
        sys.exit(f'named_data: {e}')
    if a.split is not None:
        return split(img, set(a.split))
    return refs(img, int(a.refs[0], 16), int(a.refs[-1], 16))


if __name__ == '__main__':
    sys.exit(main())
