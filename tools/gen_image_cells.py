"""game.exe's .rdata and .data for the standalone image (S18b, S18d, S18h; docs/notes/standalone.md, "Own data"): the
pointer cells of game.exe's data that point into the range the image moves (src/standalone/image_cells.inc,
committed and reviewed; image_data.cpp fixes them up when it moves the range), and the range's initial bytes
(written into the build folder by the build from the committed cells alone, no game.exe: --bytes).

The moved range is 0073B000-007CD000: .rdata (its import address table first, whose slots ei_hook.dll fills itself:
S18f) and .data with its zero-filled tail. game.exe has no relocations, so the cells are found by a scan: every
aligned dword of .rdata (past the import table) and .data (as the file holds them; the zero-filled tail holds none)
whose value lies in the moved range, less text that happens to read as an address: the tails of strings (the
dword's bytes up to its first zero printable, and the four bytes before it too), and short words (the dword's bytes
up to its zero printable, "Any", "May") whose value would point into the middle of text (the byte before the target
printable ASCII: a pointer to a string points at its start).

The cells inside the ranges our named globals hold (config/named_data.csv, C18) are left out: the image holds those
bytes zero, and named_data.py --check checks the pointer elements of ours in their place (a
string literal, another named range, a function of ours with a registration).

Each cell found must be reviewed: a cell into .rdata is either part of a structure the tool walks (STRUCTURES: the C++
exception tables, RTTI) or in a REVIEWED_RDATA group; a cell into .data is in a REVIEWED_DATA
group; a candidate that is text after all (a short word the filters keep) is in TEXT and left out, and so are the cells
of tables nothing reads (UNREAD: dead code's tables, header constant copies; C25a). Each group says what
the cells are and how many there are: a new cell (another exe, a scan change) or a changed count fails until someone
has looked at it. Every game.exe 1.06 has the same .rdata and .data (vanilla's and Rebirth's differ in one code byte).

Dwords whose value lies in game.exe's code section (vtable slots, the C++ exception tables' and ThrowInfo's
functions, the static initialiser list, MFC's message maps, the CRT's function pointers) are no cells (C20): the image
runs none of game.exe's code and nothing of ours reads a code pointer out of game.exe's data (our code calls by name,
our vtables are the compiler's, the static initialisers are generated code), so their bytes are zero like the rest.

The initial bytes (C25c: the build reads no game.exe): every byte of the moved range is zero except the cells, their
values from the committed image_cells.inc (image_data.cpp moves them as usual); the import table and the named ranges are zero. Everything else the image reads of the range is a named
global of ours (C18).

usage: python tools/gen_image_cells.py [--check] [--exe game.exe] [--bytes OUT]
  (no option)  write src/standalone/image_cells.inc (reads game.exe: the scan and its reviews)
  --check      exit 1 when the committed cells or the retired vtables are wrong (config/vtables.csv against the
               sources; every cell's target in the moved range); no game.exe
               (build.cmd). That the cells are what game.exe has is the scan's, when it is rerun.
  --bytes OUT  write the moved range's initial bytes to OUT as a C++ initialiser (the build; no game.exe)
"""
import argparse, csv, os, re, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import exe_path  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'src', 'standalone', 'image_cells.inc')
VTABLES = os.path.join(ROOT, 'config', 'vtables.csv')
NAMED_DATA = os.path.join(ROOT, 'config', 'named_data.csv')
BASE = 0x00400000
TIMESTAMP = 0x3ABF0DB3   # game.exe 1.06 (src/standalone/host.cpp's kExpectedTimestamp)

# game_data.h's kMovedBegin / kMovedEnd (.rdata and .data, one range, one shift) and kImportsBegin / kImportsEnd.
MOVED = (0x0073B000, 0x007CD000)
IMPORTS = (0x0073B000, 0x0073B820)
TEXT_SECTION = (0x00401000, 0x0073B000)
CODE = (0x00401000, 0x0073A670)   # .text's bytes (include/ei/hook.h's kGameTextBegin / kGameTextEnd)
# The targets by section, and the data that can hold cells (as the file holds it: .data's raw bytes end at 0079C000;
# the import table's file bytes are not the image's: ei_hook.dll fills its own slots).
SECTIONS = {'rdata': (0x0073B000, 0x00783000), 'data': (0x00783000, 0x007CD000)}
SCANNED = [('rdata', IMPORTS[1], 0x00783000), ('data', 0x00783000, 0x0079C000)]

# Cells into .data by group, as looked at in game.exe (who reads the cells, what the targets hold): (first cell,
# last cell, the number of cells, what they are[, the one target they all hold]). A cell belongs to the first group
# that takes it. "Strings" are string literals, which MSVC 6 put in .data.
REVIEWED_DATA = [
    # .rdata
    (0x00746900, 0x00746930, 3, "MFC CCmdTarget's dispatch and event sink maps: their entry counts and stock "
                                "property mask (-1 in .data)"),
    (0x00746C80, 0x00746C80, 1, 'MFC message map entry (ON_REGISTERED_MESSAGE, handler 00706CF4): the message\'s '
                                'variable, set by a static initialiser to RegisterWindowMessageA("commctrl_DragListMsg")'),
    (0x007482F0, 0x0074C60C, 117, 'RTTI (MFC, the CRT, iostream, STLport): complete object locators and base class '
                                  'descriptors -> their type descriptors'),
    (0x00753F04, 0x0077F7CC, 12, 'C++ EH: catchable types (ThrowInfo) -> the thrown pointer types\' type '
                                 'descriptors (".PAV...")'),
    # .data
    (0x00796E48, 0x00796E4C, 2, 'MFC CString\'s shared empty string (afxEmptyString\'s data) and _afxDataNil'),
    (0x00797888, 0x007978C8, 5, 'the CRT\'s locale categories __lc_category (6 x {name, locale, init}): the names\' '
                                 'strings and the static "C" 00797778 (unread: setlocale is the host CRT\'s, C26)'),
    (0x00797A58, 0x00797A58, 1, 'the CRT\'s __lc_time_curr: __lc_time_c (day and month names)'),
    (0x00797DD8, 0x00797DDC, 2, 'the CRT\'s _pctype / _pwctype: the C locale\'s _ctype + 1 (unread: the classes are the '
                                'host CRT\'s, C26)'),
    (0x00797C04, 0x00797C44, 4, 'the CRT\'s lock table (_locktable, _mtinitlocks 006F0741): preallocated critical '
                                'sections'),
    (0x00798060, 0x00798068, 2, '_iob[0] (stdin) _ptr and _base: the static stdin buffer _bufin'),
    (0x00798BF0, 0x00798BFC, 4, 'the CRT\'s small-block heap: the region list head\'s links (to itself)'),
    (0x0079AC10, 0x0079AC10, 1, 'the CRT\'s small-block heap: the starting region (the list head)'),
    (0x0079ACB4, 0x0079ACB4, 1, 'the CRT\'s run-time error messages (number, text): "\\r\\n"'),
    (0x0079AD4C, 0x0079AD50, 2, 'the CRT\'s _tzname: "PST", "PDT"'),
    (0x0079AE20, 0x0079AE24, 2, '__lconv_c (0079AE20): decimal_point ".", thousands_sep __lconv_static_null ("")'),
    (0x0079AE28, 0x0079AE44, 8, '__lconv_c\'s other strings: __lconv_static_null'),
    (0x0079AE50, 0x0079AE50, 1, 'the CRT\'s __lconv: __lconv_c'),
]

# Cells of tables nothing reads (C25a), as REVIEWED_DATA's groups but left out of the pointer cells: only game.exe's dead
# code or no code at all read them, and no port names them. Their bytes are zero in the image.
UNREAD = [
    (0x0073B000, 0x00783000, 72, '".SEC": per-file copies of a header\'s const char* const that nothing reads (the two '
                                 'read ones are named: terrain_sector.cpp 00744164, ai_map_load.cpp 0073FD20)', 0x00783CDC),
    (0x0078F7EC, 0x0078F824, 15, 'the units\' model folders by kind ("heroes\\human\\male", ...), read by the dead '
                                 '00532650 only'),
    (0x007932F4, 0x007932FC, 3, '"War", "Magic", "Islands", read by the dead 00602190 only'),
]

# Kinds of STRUCTURES that are walked and counted but left out of the pointer cells (image_cells.inc): the C++ EH tables read only by __CxxFrameHandler on game.exe's own frames, which gets
# the FuncInfo (unwind map, try block map, handler arrays, the caught types of the handlers) from the function's
# prologue. No such frame runs in the image (our code's FuncInfos are the compiler's: C20a), and nothing in src/ or
# tests/ names the tables; the
# harness's crash report (tests/verify/harness_crash.cpp) reads ThrowInfo only, as CxxThrowException and our frame
# handler do. The cells keep game.exe's values, unshifted. The ThrowInfo kinds (eh-throw, eh-catchable) stay
# cells, with the catchable types' type descriptor cells (the group at 00753F04 below).
LEFT_STRUCTURES = ('eh-unwind', 'eh-try', 'eh-handlers', 'eh-handler-type')

# Cells into .rdata that belong to the structures the tool walks (structures()): kind, the number of cells, what.
STRUCTURES = [
    ('eh-unwind', 2963, "C++ EH: FuncInfo (magic 19930520) -> its unwind map: not pointer cells"),
    ('eh-try', 378, 'C++ EH: FuncInfo -> its try block map: not pointer cells'),
    ('eh-handlers', 380, 'C++ EH: try block -> its catch handler array: not pointer cells'),
    ('eh-handler-type', 26, "C++ EH: catch handler -> the caught type's type descriptor (a pointer type's, \".PAV...\"): not pointer cells"),
    ('eh-throw', 10, 'C++ EH: ThrowInfo -> its catchable type array'),
    ('eh-catchable', 41, 'C++ EH: catchable type array -> its catchable types'),
    ('rtti-locator', 53, "RTTI: a vtable's complete object locator (the vtable's slot -1)"),
    ('rtti-hierarchy', 53, 'RTTI: complete object locator -> class hierarchy descriptor'),
    ('rtti-bases', 53, 'RTTI: class hierarchy descriptor -> base class array'),
    ('rtti-base', 128, 'RTTI: base class array -> base class descriptors'),
]

# Cells into .rdata outside those structures, as looked at in game.exe: as REVIEWED_DATA.
REVIEWED_RDATA = [
    (0x007468DC, 0x0074701C, 11, "MFC CCmdTarget's maps (message, dispatch, event sink, interface (IID_IDispatch), "
                                 "connection, OLE command) and CWnd's and CWinApp's message maps: base map and entries"),
    (0x00783000, 0x0079C000, 74, 'C++ type descriptors (type_info objects): their vtable, type_info\'s (retired: '
                                 'nothing calls a descriptor\'s virtual)', 0x007472B0),
    (0x00797A60, 0x00797B08, 43, 'the CRT\'s __lc_time_c: day and month names, AM / PM, the date and time formats'),
    (0x0079804C, 0x00798050, 2, 'the CRT\'s "(null)" strings of printf (narrow and wide)'),
    (0x00798928, 0x00798BE0, 88, 'getqloc\'s country and language name tables (__rg_country 00798928, __rg_language '
                                 '007989E0): their names (unread: setlocale is the host CRT\'s, C26)'),
    (0x00797888, 0x007978C4, 6, 'the CRT\'s locale categories __lc_category: their names "LC_ALL" .. "LC_TIME" '
                                '(unread: setlocale is the host CRT\'s, C26)'),
    (0x0079847C, 0x007988FC, 54, 'the CRT\'s locale info table (00798470, 27 rows of 0x2C) of 006F5231, getqloc\'s '
                                 'GetLocaleInfoA stand-in off NT (ours calls GetLocaleInfoA): language and country names'),
    (0x0079AC34, 0x0079ACBC, 17, 'the CRT\'s run-time error messages (number, text)'),
]

# Candidates the text filters keep that are text: short words whose bytes read as an address in .rdata.
TEXT = [
    (0x0074742C, 0x0074742C, '"Nov": the CRT\'s month names (__lc_time_c\'s strings)'),
    (0x007474A4, 0x007474A4, '"Thu": the CRT\'s day names (__lc_time_c\'s strings)'),
    (0x007476C4, 0x007476C4, '"pow": the math functions\' names'),
    (0x0078CDBC, 0x0078CDBC, '"Div": a script keyword\'s name'),
    (0x00792438, 0x00792438, '"net": a string'),
    (0x00797608, 0x00797608, '"pow": a string'),
]


def image(path):
    d = open(path, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3C)[0]
    if struct.unpack_from('<I', d, pe + 8)[0] != TIMESTAMP:
        sys.exit(f'gen_image_cells: {path} is not game.exe 1.06')
    count = struct.unpack_from('<H', d, pe + 6)[0]
    optional = struct.unpack_from('<H', d, pe + 20)[0]
    size = struct.unpack_from('<I', d, pe + 24 + 56)[0]
    img = bytearray(size)
    at = pe + 24 + optional
    for i in range(count):
        vsize, va, rsize, raw = struct.unpack_from('<IIII', d, at + 40 * i + 8)
        n = min(vsize, rsize)
        img[va:va + n] = d[raw:raw + n]
    return img


def printable(bs):
    return all(0x20 <= b < 0x7F or b in (9, 10, 13) or b >= 0xC0 for b in bs)


def text_word(img, offset):
    text = img[offset:offset + 4].split(b'\0', 1)[0]
    return len(text) > 0 and printable(text)


def string_tail(img, offset):
    return text_word(img, offset) and printable(img[offset - 4:offset])


def mid_text(img, target):
    return 0x20 <= img[target - BASE - 1] < 0x7F


def section_of(value):
    for name, (lo, hi) in SECTIONS.items():
        if lo <= value < hi:
            return name
    return None


def named_data_ranges():
    """(address, size) of config/named_data.csv's ranges: named globals of ours (C18). Their cells are no cells of the
    image (the bytes are zero there; tools/named_data.py --check compares each pointer element with its target)."""
    if not os.path.exists(NAMED_DATA):
        return []
    with open(NAMED_DATA, newline='') as f:
        return [(int(r['address'], 16), int(r['size'])) for r in csv.DictReader(f)]


def outside_named():
    """A test for a cell address: False inside a named range of config/named_data.csv."""
    ranges = named_data_ranges()
    return lambda a: not any(lo <= a < lo + n for lo, n in ranges)


def scan(img):
    cells = []
    wanted = outside_named()
    for _, begin, end in SCANNED:
        for a in range(begin, end, 4):
            if not wanted(a):
                continue
            o = a - BASE
            v = struct.unpack_from('<I', img, o)[0]
            if not MOVED[0] <= v <= MOVED[1] or string_tail(img, o) or (text_word(img, o) and mid_text(img, v)):
                continue
            cells.append((a, v))
    return cells


def structures(img):
    """{cell: kind} for the cells into .rdata of the structures the tool walks (STRUCTURES)."""
    def dw(a):
        return struct.unpack_from('<I', img, a - BASE)[0]

    def rdata(v):
        return section_of(v) == 'rdata'

    def type_descriptor(v):   # in .data, its decorated name ".?AV...", ".PAV...", ...
        return section_of(v) == 'data' and img[v + 8 - BASE] == ord('.')

    kinds = {}

    def mark(cell, kind):
        if rdata(dw(cell)):
            kinds.setdefault(cell, kind)

    lo, hi = SCANNED[0][1], SCANNED[0][2]
    for a in range(lo, hi - 20, 4):
        if dw(a) == 0x19930520:   # FuncInfo: magic, maxState, pUnwindMap, nTryBlocks, pTryBlockMap
            mark(a + 8, 'eh-unwind')
            mark(a + 16, 'eh-try')
            if rdata(dw(a + 16)):
                for i in range(dw(a + 12)):   # try block: tryLow, tryHigh, catchHigh, nCatches, pHandlerArray
                    block = dw(a + 16) + 20 * i
                    mark(block + 16, 'eh-handlers')
                    if rdata(dw(block + 16)):
                        for j in range(dw(block + 12)):   # handler type: adjectives, pType (0 for catch (...)), ...
                            cell = dw(block + 16) + 16 * j + 4
                            if type_descriptor(dw(cell)):
                                kinds.setdefault(cell, 'eh-handler-type')
        # ThrowInfo: attributes, pmfnUnwind (code or 0), pForwardCompat 0, pCatchableTypeArray (count, entries);
        # a catchable type: properties, pType (a type descriptor), ...
        unwind = dw(a + 4)
        if dw(a) <= 3 and (unwind == 0 or TEXT_SECTION[0] <= unwind < TEXT_SECTION[1]) and dw(a + 8) == 0 \
                and rdata(dw(a + 12)):
            array = dw(a + 12)
            n = dw(array)
            if 1 <= n <= 16 and all(rdata(dw(array + 4 + 4 * i)) and type_descriptor(dw(dw(array + 4 + 4 * i) + 4))
                                    for i in range(n)):
                mark(a + 12, 'eh-throw')
                for i in range(n):
                    mark(array + 4 + 4 * i, 'eh-catchable')
    # RTTI: a complete object locator (signature 0, offset, cdOffset, pTypeDescriptor, pClassHierarchyDescriptor)
    # is what a vtable's slot -1 holds; its class hierarchy descriptor (signature 0, attributes, numBaseClasses,
    # pBaseClassArray), the array's base class descriptors (pTypeDescriptor, ...).
    for a in range(lo, hi, 4):
        col = dw(a)
        if not rdata(col) or dw(col) != 0 or not type_descriptor(dw(col + 12)) or not rdata(dw(col + 16)):
            continue
        hierarchy = dw(col + 16)
        bases = dw(hierarchy + 12)
        if dw(hierarchy) != 0 or not 1 <= dw(hierarchy + 8) <= 64 or not rdata(bases) or \
                not all(type_descriptor(dw(dw(bases + 4 * i))) for i in range(dw(hierarchy + 8))):
            continue
        mark(a, 'rtti-locator')
        mark(col + 16, 'rtti-hierarchy')
        mark(hierarchy + 12, 'rtti-bases')
        for i in range(dw(hierarchy + 8)):
            mark(bases + 4 * i, 'rtti-base')
    return kinds


def group_of(cell, value, kinds):
    """('text' | 'structure' | 'data' | 'rdata', index) or None."""
    for i, (first, last, _) in enumerate(TEXT):
        if first <= cell <= last:
            return ('text', i)
    for i, g in enumerate(UNREAD):
        if g[0] <= cell <= g[1] and (len(g) < 5 or g[4] == value):
            return ('unread', i)
    section = section_of(value)
    if cell in kinds:
        return ('structure', [k for k, _, _ in STRUCTURES].index(kinds[cell]))
    groups = REVIEWED_RDATA if section == 'rdata' else REVIEWED_DATA
    for i, g in enumerate(groups):
        if g[0] <= cell <= g[1] and (len(g) < 5 or g[4] == value):
            return (section, i)
    return None


def left_out(group):
    """Whether a group is no pointer cells: text, an UNREAD table, or a structure of LEFT_STRUCTURES."""
    return group[0] in ('text', 'unread') or group[0] == 'structure' and STRUCTURES[group[1]][0] in LEFT_STRUCTURES


def describe(group):
    kind, i = group
    if kind == 'text':
        return TEXT[i][2]
    if kind == 'structure':
        return STRUCTURES[i][2]
    if kind == 'unread':
        return UNREAD[i][3]
    return (REVIEWED_RDATA if kind == 'rdata' else REVIEWED_DATA)[i][3]


def render(cells, img):
    kinds = structures(img)
    lines = [
        '// Generated by tools/gen_image_cells.py from game.exe. Do not edit.',
        '//',
        "// The pointer cells of game.exe's .rdata and .data that point into the range the image moves, .rdata and .data",
        "// (S18b, S18d, S18h): image_data.cpp adds the shift to each when it moves the range. {cell, the address it",
        "// holds}, by group; each group's review is in the tool.",
    ]
    order = [('structure', i) for i in range(len(STRUCTURES))] + [('rdata', i) for i in range(len(REVIEWED_RDATA))] + \
            [('data', i) for i in range(len(REVIEWED_DATA))]
    by_group = {}
    for a, v in cells:
        g = group_of(a, v, kinds)
        if g is None or not left_out(g):
            by_group.setdefault(g, []).append((a, v))
    for g in order + [None]:
        if g not in by_group:
            continue
        lines.append(f'// {describe(g) if g is not None else "NOT REVIEWED"}')
        for a, v in by_group[g]:
            lines.append(f'{{DataId(0x{a:08X}), DataId(0x{v:08X})}},')
    return '\n'.join(lines) + '\n'


def named_ranges():
    """(address, size) of the ranges our named globals hold now (config/named_data.csv, tools/named_data.py) and of
    the retired vtables (config/vtables.csv, C19)."""
    with open(NAMED_DATA, newline='') as f:
        named = [(int(r['address'], 16), int(r['size'])) for r in csv.DictReader(f)]
    return named + [(v, 4 * n) for v, n, _, _ in retired_vtables()]


def retired_vtables():
    """(vtable, slots, class, file) of config/vtables.csv: game.exe's vtables, every one retired (C19): its class's
    vtable is the compiler's now (docs/notes/standalone.md "Compiler-made vtables"), or only game.exe's own code
    stored it (file '-': base vtables its destructors stored, locals its functions made, dead code's; the note column
    names the storing functions). Nothing installs them and the image holds their bytes zero; tools/named_data.py
    --check proves nothing names or points into them."""
    with open(VTABLES, newline='', encoding='utf-8') as f:
        return [(int(r['vtable'], 16), int(r['slots']), r['class'], r['file']) for r in csv.DictReader(f)]


def stored_vtable(img, v):
    """Whether game.exe's code stores `v` as an immediate (mov r/m32, imm32: C7 /0 with any addressing) or loads it
    into a register (mov r32, imm32: B8+r, then stored from there): a vtable's start. find_vtables.py splits a vtable at every address the code holds, so config/vtables.csv also has tails of
    longer tables, split off at a devirtualised slot call (call [slot]) or a coincidental byte match; those are slots
    of the table before them, never installed."""
    text = bytes(img[CODE[0] - BASE:CODE[1] - BASE])
    pattern = struct.pack('<I', v)
    i = text.find(pattern)
    while i >= 0:
        if i >= 1 and 0xB8 <= text[i - 1] <= 0xBF:
            return True
        for back, mods, rms in ((2, (0,), (0, 1, 2, 3, 6, 7)), (3, (0,), (4,)), (3, (1,), (0, 1, 2, 3, 5, 6, 7)),
                                (4, (1,), (4,)), (6, (0,), (5,)), (6, (2,), (0, 1, 2, 3, 5, 6, 7)), (7, (2,), (4,))):
            if i >= back and text[i - back] == 0xC7:
                m = text[i - back + 1]
                if m >> 6 in mods and m & 7 in rms and (m >> 3) & 7 == 0:
                    return True
        i = text.find(pattern, i + 1)
    return False


def check_retired(problems, img):
    """Each vtable of config/vtables.csv is listed once, names its class (and the file declaring it, or '-'), and is
    the start of a table (stored_vtable) or the tail of the row before it (the row it ends). With no image (`img`
    None: --check) the stored-by-code proof is left out."""
    seen = set()
    # A tail goes with its table: a row's end, when that row is stored or a tail itself.
    ends = set()
    for v, n, _, _ in sorted(retired_vtables()):
        if img is not None and (stored_vtable(img, v) or v in ends):
            ends.add(v + 4 * n)
    for v, n, cls, path in retired_vtables():
        where = f'config/vtables.csv: {v:08X} ({cls})'
        if img is not None and not stored_vtable(img, v) and v not in ends:
            problems.append(f'gen_image_cells: {where}: no code of game.exe stores it: the tail of the vtable '
                            'before it (a slot call or a byte match split it off), not a vtable of its own; '
                            'list it with that vtable')
        if v in seen:
            problems.append(f'gen_image_cells: {where} is listed twice')
        seen.add(v)
        if not cls or not path:
            problems.append(f'gen_image_cells: {where}: no class or file (the class whose vtable is the compiler\'s, '
                            'or file - with a note naming the functions that store it)')
            continue
        if path == '-':   # no class of ours: only game.exe's code stored it (the note says which)
            continue
        if not os.path.exists(os.path.join(ROOT, path)):
            problems.append(f'gen_image_cells: {where}: no file {path}')
        elif not re.search(rf'\b(class|struct)\s+{re.escape(cls)}\b',
                           open(os.path.join(ROOT, path), encoding='utf-8', errors='replace').read()):
            problems.append(f'gen_image_cells: {where}: {path} declares no class {cls}')


def committed_cells():
    """{cell: value} of the committed src/standalone/image_cells.inc (pointer cells, {DataId(cell), DataId(target)}):
    game.exe's dwords there, without reading it."""
    cells = {}
    for m in re.finditer(r'^\{DataId\(0x([0-9A-F]{8})\), DataId\(0x([0-9A-F]{8})\)\},$',
                         open(OUT, encoding='utf-8').read(), re.M):
        cells[int(m.group(1), 16)] = int(m.group(2), 16)
    return cells


def write_bytes(path):
    """The moved range's initial bytes to `path`, from the committed files only (no game.exe): zero but the cells."""
    lo = MOVED[0]
    end = SCANNED[-1][2]   # the file's bytes; the rest of the range is zero
    named = named_ranges()
    data = bytearray(end - lo)
    cells = committed_cells()
    for cell, value in cells.items():
        struct.pack_into('<I', data, cell - lo, value)
    what = f'zero but the {len(cells)} cells of image_cells.inc'
    data[:IMPORTS[1] - IMPORTS[0]] = bytes(IMPORTS[1] - IMPORTS[0])   # our slots, filled by ei_hook.dll
    for a, n in named:   # named globals of ours now (C18): nothing reads these bytes
        b, e = a - lo, min(a - lo + n, len(data))   # a range in the zero-filled tail past the file's bytes: nothing
        if b < e:
            data[b:e] = bytes(e - b)
    while data.endswith(b'\0\0\0\0'):
        data = data[:-4]
    words = struct.unpack(f'<{len(data) // 4}I', data)
    lines = ['// Generated by tools/gen_image_cells.py --bytes at build time (not committed). Do not edit.',
             f'// {what}, up to the last nonzero dword: {len(words)} dwords.']
    for i in range(0, len(words), 8):
        lines.append(','.join(f'0x{w:X}' for w in words[i:i + 8]) + ',')
    text = '\n'.join(lines) + '\n'
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    if os.path.exists(path) and open(path, encoding='utf-8').read() == text:
        return len(words)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    return len(words)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--exe')   # default: tools/exe_path.py
    ap.add_argument('--bytes', metavar='OUT')
    a = ap.parse_args()
    if a.bytes:
        n = write_bytes(a.bytes)
        print(f'gen_image_cells: {n} dwords of .rdata and .data written to {a.bytes}')
        return 0
    if a.check:   # no game.exe: the committed lists against each other
        problems = []
        check_retired(problems, None)
        cells = committed_cells()
        for c, v in cells.items():
            if section_of(v) is None:
                problems.append(f'gen_image_cells: {c:08X} -> {v:08X} is not in the moved range')
        for p in problems:
            print(p)
        print(f'gen_image_cells: {len(cells)} committed cell(s), {len(retired_vtables())} retired vtable(s), '
              f'{len(problems)} problem(s)')
        return 1 if problems else 0
    img = image(exe_path.resolve(a.exe))
    kinds = structures(img)
    cells = scan(img)
    problems = []
    counts = {}
    moved = 0
    for c, v in cells:
        g = group_of(c, v, kinds)
        if g is None:
            problems.append(f'gen_image_cells: {c:08X} -> {v:08X} has no review (tools/gen_image_cells.py)')
            continue
        counts[g] = counts.get(g, 0) + 1
        moved += not left_out(g)
        if section_of(v) is None:
            problems.append(f'gen_image_cells: {c:08X} -> {v:08X} is the end of the moved range: a one-past-the-end '
                            'pointer of its last object, or the next section\'s start? decide and review')
        if IMPORTS[0] <= v < IMPORTS[1]:
            problems.append(f'gen_image_cells: {c:08X} -> {v:08X} points at an import slot: the image\'s slots are '
                            'its own table (image_imports.cpp); review')
    expected = [(('text', i), None, g[2]) for i, g in enumerate(TEXT)]
    for i, (_, n, what) in enumerate(STRUCTURES):
        expected.append((('structure', i), n, what))
    for name, groups in (('unread', UNREAD), ('rdata', REVIEWED_RDATA), ('data', REVIEWED_DATA)):
        for i, g in enumerate(groups):
            expected.append(((name, i), g[2], f'{g[0]:08X}-{g[1]:08X} ({g[3]})'))
    for g, n, what in expected:
        have = counts.get(g, 0)
        if n is None:   # a text group: exactly one candidate per cell of its range
            n = (TEXT[g[1]][1] - TEXT[g[1]][0]) // 4 + 1
        if have != n:
            problems.append(f'gen_image_cells: the group {what[:70]} has {have} cell(s), reviewed {n}')
    check_retired(problems, img)
    outputs = [(OUT, render(cells, img))]
    summary = f'{moved} cell(s) into .rdata and .data'
    if a.check:
        for path, text in outputs:
            current = open(path, encoding='utf-8').read() if os.path.exists(path) else ''
            if current != text:
                problems.append(f'gen_image_cells: {os.path.relpath(path, ROOT)} is not what the scan writes: '
                                'run python tools/gen_image_cells.py')
        for p in problems:
            print(p)
        print(f'gen_image_cells: {summary}, {len(problems)} problem(s)')
        return 1 if problems else 0
    for path, text in outputs:
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
    for p in problems:
        print(p)
    print(f'gen_image_cells: {summary} written to src/standalone')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
