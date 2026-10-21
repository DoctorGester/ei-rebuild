"""game.exe's .rdata and .data for the standalone image (S18b, S18d, S18h; docs/notes/standalone.md, "Own data"): the
pointer cells of game.exe's data that point into the range the image moves (src/standalone/image_cells.inc,
committed and reviewed; image_data.cpp fixes them up when it moves the range), and the range's initial bytes
(written into the build folder by the build, not committed: --bytes).

The moved range is 0073B000-007CD000: .rdata (its import address table first, whose slots ei_hook.dll fills itself:
S18f) and .data with its zero-filled tail. game.exe has no relocations, so the cells are found by a scan: every
aligned dword of .rdata (past the import table) and .data (as the file holds them; the zero-filled tail holds none)
whose value lies in the moved range, less text that happens to read as an address: the tails of strings (the
dword's bytes up to its first zero printable, and the four bytes before it too), and short words (the dword's bytes
up to its zero printable, "Any", "May") whose value would point into the middle of text (the byte before the target
printable ASCII: a pointer to a string points at its start).

Each cell found must be reviewed: a cell into .rdata is either part of a structure the tool walks (STRUCTURES: the C++
exception tables, RTTI, MFC's runtime classes) or in a REVIEWED_RDATA group; a cell into .data is in a REVIEWED_DATA
group; a candidate that is text after all (a short word the filters keep) is in TEXT and left out. Each group says what
the cells are and how many there are: a new cell (another exe, a scan change) or a changed count fails until someone
has looked at it. Every game.exe 1.06 has the same .rdata and .data (vanilla's and Rebirth's differ in one code byte).

usage: python tools/gen_image_cells.py [--check] [--exe game.exe] [--bytes OUT]
  (no option)  write src/standalone/image_cells.inc
  --check      exit 1 when the file differs from what the scan writes, or a cell has no review (build.cmd)
  --bytes OUT  write the moved range's initial bytes (.rdata's and .data's file bytes, the import table zero) to OUT
               as a C++ initialiser (the build)
"""
import argparse, csv, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import exe_path  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'src', 'standalone', 'image_cells.inc')
RUNTIME_CLASSES = os.path.join(ROOT, 'config', 'runtime_classes.csv')
BASE = 0x00400000
TIMESTAMP = 0x3ABF0DB3   # game.exe 1.06 (src/standalone/host.cpp's kExpectedTimestamp)

# game_data.h's kMovedBegin / kMovedEnd (.rdata and .data, one range, one shift) and kImportsBegin / kImportsEnd.
MOVED = (0x0073B000, 0x007CD000)
IMPORTS = (0x0073B000, 0x0073B820)
TEXT_SECTION = (0x00401000, 0x0073B000)
# The targets by section, and the data that can hold cells (as the file holds it: .data's raw bytes end at 0079C000;
# the import table's file bytes are not the image's: ei_hook.dll fills its own slots).
SECTIONS = {'rdata': (0x0073B000, 0x00783000), 'data': (0x00783000, 0x007CD000)}
SCANNED = [('rdata', IMPORTS[1], 0x00783000), ('data', 0x00783000, 0x0079C000)]

# Cells into .data by group, as looked at in game.exe (who reads the cells, what the targets hold): (first cell,
# last cell, the number of cells, what they are[, the one target they all hold]). A cell belongs to the first group
# that takes it. "Strings" are string literals, which MSVC 6 put in .data.
REVIEWED_DATA = [
    # .rdata
    (0x0073B000, 0x00783000, 74, '".SEC": per-file copies of a const char* const from a header, some read by code '
                                 '(005B5042 reads 0073FD20)', 0x00783CDC),
    (0x0073D080, 0x0073D080, 1, '"C:\\*.*": a const char* const read by 0049F660'),
    (0x0073F918, 0x0073F918, 1, '"SMessBase.reg": a const char* const'),
    (0x0073FD1C, 0x0073FD1C, 1, '".MP": a const char* const'),
    (0x00743BFC, 0x00743C34, 8, 'the material files by material ("leather.mat", ...): a const char* const table'),
    (0x00744090, 0x00744094, 2, '"music.reg", ".mp3": const char* consts'),
    (0x007441D4, 0x007441D4, 1, '".MP": a const char* const'),
    (0x00746900, 0x00746930, 3, "MFC CCmdTarget's dispatch and event sink maps: their entry counts and stock "
                                "property mask (-1 in .data)"),
    (0x00746C80, 0x00746C80, 1, 'MFC message map entry (ON_REGISTERED_MESSAGE, handler 00706CF4): the message\'s '
                                'variable, set by a static initialiser to RegisterWindowMessageA("commctrl_DragListMsg")'),
    (0x007482F0, 0x0074C60C, 117, 'RTTI (MFC, the CRT, iostream, STLport): complete object locators and base class '
                                  'descriptors -> their type descriptors'),
    (0x00753F04, 0x0077F7CC, 38, 'C++ EH: catch handler types (FuncInfo) and catchable types (ThrowInfo) -> the '
                                 'caught / thrown pointer types\' type descriptors (".PAV...")'),
    # .data
    (0x007857B4, 0x0078706C, 88, 'the key bindings\' command table (007857B0, 0x48-byte entries): names'),
    (0x00787074, 0x0078768C, 86, 'the action definitions (00787070) and the settings table after them: names'),
    (0x0078B120, 0x0078B414, 64, 'the script\'s particle types (0078B120, 12-byte entries): names'),
    (0x0078BC8C, 0x0078CAA8, 442, 'the script language: comment tokens (0078BC8C) and the keyword / function table '
                                  '(0078BCA0): names and argument type strings'),
    (0x0078F7EC, 0x0078F824, 15, 'a table of folder names ("animals\\", ...; "" at 0079B298)'),
    (0x0078F974, 0x0078F9A8, 14, 'a table of strings read by 005370F0'),
    (0x00790270, 0x007902EC, 18, 'the zone names ("zone2", ...) after the save versions (00790260)'),
    (0x007907AC, 0x007907D0, 10, 'a table of strings ("bz1g", ...) read by 00591800'),
    (0x00791EE0, 0x00791EE0, 1, 'the loading movie\'s name ("progres.bik")'),
    (0x007932F4, 0x007932FC, 3, '"War", "Magic", "Islands": strings read by 00602190'),
    (0x00793378, 0x0079337C, 2, 'two strings read by 00604C50'),
    (0x00793BD4, 0x00793C0C, 15, 'the skill names ("Melee", ... "Encumbrance"), read by 00625880'),
    (0x007947A8, 0x007947D8, 13, 'the log channel names ("video", "debug1", ...), read by 006503C0'),
    (0x007969D0, 0x00796B38, 16, 'the ambient sound classes (007969D0, 24-byte entries): names ("Grass", ...)'),
    (0x00796CD0, 0x00796CD0, 1, 'CDib\'s CRuntimeClass: the class name'),
    (0x00796E48, 0x00796E4C, 2, 'MFC CString\'s shared empty string (afxEmptyString\'s data) and _afxDataNil'),
    (0x00797898, 0x007978C8, 5, 'the CRT\'s locale categories (00797888): their locale names'),
    (0x00797A58, 0x00797A58, 1, 'the CRT\'s __lc_time_curr: __lc_time_c (day and month names)'),
    (0x00797C04, 0x00797C44, 4, 'the CRT\'s lock table (_locktable, _mtinitlocks 006F0741): preallocated critical '
                                'sections'),
    (0x00797CC4, 0x00797CC4, 1, 'a table of (number, string) pairs read by 006F0FBF: its one string in .data ("exp")'),
    (0x00797DD8, 0x00797DDC, 2, 'the CRT\'s _pctype and _pwctype: _ctype + 1'),
    (0x00798060, 0x00798068, 2, '_iob[0] (stdin) _ptr and _base: the static stdin buffer _bufin'),
    (0x00798BF0, 0x00798BFC, 4, 'the CRT\'s small-block heap: the region list head\'s links (to itself)'),
    (0x0079AC10, 0x0079AC10, 1, 'the CRT\'s small-block heap: the starting region (the list head)'),
    (0x0079ACB4, 0x0079ACB4, 1, 'the CRT\'s run-time error messages (number, text): "\\r\\n"'),
    (0x0079AD4C, 0x0079AD50, 2, 'the CRT\'s _tzname: "PST", "PDT"'),
    (0x0079AE20, 0x0079AE24, 2, '__lconv_c (0079AE20): decimal_point ".", thousands_sep __lconv_static_null ("")'),
    (0x0079AE28, 0x0079AE44, 8, '__lconv_c\'s other strings: __lconv_static_null'),
    (0x0079AE50, 0x0079AE50, 1, 'the CRT\'s __lconv: __lconv_c'),
]

# Cells into .rdata that belong to the structures the tool walks (structures()): kind, the number of cells, what.
STRUCTURES = [
    ('eh-unwind', 2963, "C++ EH: FuncInfo (magic 19930520) -> its unwind map"),
    ('eh-try', 378, 'C++ EH: FuncInfo -> its try block map'),
    ('eh-handlers', 380, 'C++ EH: try block -> its catch handler array'),
    ('eh-throw', 10, 'C++ EH: ThrowInfo -> its catchable type array'),
    ('eh-catchable', 41, 'C++ EH: catchable type array -> its catchable types'),
    ('rtti-locator', 53, "RTTI: a vtable's complete object locator (the vtable's slot -1)"),
    ('rtti-hierarchy', 53, 'RTTI: complete object locator -> class hierarchy descriptor'),
    ('rtti-bases', 53, 'RTTI: class hierarchy descriptor -> base class array'),
    ('rtti-base', 128, 'RTTI: base class array -> base class descriptors'),
    ('mfc-name', 40, "MFC CRuntimeClass (config/runtime_classes.csv): the class name"),
    ('mfc-base', 40, 'MFC CRuntimeClass: the base class\'s CRuntimeClass'),
]

# Cells into .rdata outside those structures, as looked at in game.exe: as REVIEWED_DATA.
REVIEWED_RDATA = [
    (0x007468DC, 0x0074701C, 11, "MFC CCmdTarget's maps (message, dispatch, event sink, interface (IID_IDispatch), "
                                 "connection, OLE command) and CWnd's and CWinApp's message maps: base map and entries"),
    (0x00783000, 0x0079C000, 74, 'C++ type descriptors (type_info objects): their vtable, type_info\'s', 0x007472B0),
    (0x00797888, 0x007978C4, 6, 'the CRT\'s locale categories (00797888): their names ("LC_ALL", ...)'),
    (0x00797A60, 0x00797B08, 43, 'the CRT\'s __lc_time_c: day and month names, AM / PM, the date and time formats'),
    (0x00797CCC, 0x00797D94, 26, 'a table of (number, string) pairs read by 006F0FBF: the math functions\' names '
                                 '("log", "pow", ...)'),
    (0x0079804C, 0x00798050, 2, 'the CRT\'s "(null)" strings of printf (narrow and wide)'),
    (0x0079847C, 0x00798BE0, 142, 'the CRT\'s setlocale tables: language and country names'),
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


def scan(img):
    cells = []
    for _, begin, end in SCANNED:
        for a in range(begin, end, 4):
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
                    mark(dw(a + 16) + 20 * i + 16, 'eh-handlers')
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
    # MFC's CRuntimeClass: lpszClassName, size, schema, pfnCreateObject, pBaseClass, pNextClass.
    with open(RUNTIME_CLASSES, newline='') as f:
        for row in csv.DictReader(f):
            c = int(row['runtime_class'], 16)
            mark(c, 'mfc-name')
            mark(c + 16, 'mfc-base')
    return kinds


def group_of(cell, value, kinds):
    """('text' | 'structure' | 'data' | 'rdata', index) or None."""
    for i, (first, last, _) in enumerate(TEXT):
        if first <= cell <= last:
            return ('text', i)
    section = section_of(value)
    if section == 'rdata' and cell in kinds:
        return ('structure', [k for k, _, _ in STRUCTURES].index(kinds[cell]))
    groups = REVIEWED_RDATA if section == 'rdata' else REVIEWED_DATA
    for i, g in enumerate(groups):
        if g[0] <= cell <= g[1] and (len(g) < 5 or g[4] == value):
            return (section, i)
    return None


def describe(group):
    kind, i = group
    if kind == 'text':
        return TEXT[i][2]
    if kind == 'structure':
        return STRUCTURES[i][2]
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
        if g is None or g[0] != 'text':
            by_group.setdefault(g, []).append((a, v))
    for g in order + [None]:
        if g not in by_group:
            continue
        lines.append(f'// {describe(g) if g is not None else "NOT REVIEWED"}')
        for a, v in by_group[g]:
            lines.append(f'{{DataId(0x{a:08X}), DataId(0x{v:08X})}},')
    return '\n'.join(lines) + '\n'


def write_bytes(img, path):
    lo = MOVED[0]
    end = SCANNED[-1][2]   # the file's bytes; the rest of the range is zero
    data = bytearray(img[lo - BASE:end - BASE])
    data[:IMPORTS[1] - IMPORTS[0]] = bytes(IMPORTS[1] - IMPORTS[0])   # our slots, filled by ei_hook.dll
    while data.endswith(b'\0\0\0\0'):
        data = data[:-4]
    words = struct.unpack(f'<{len(data) // 4}I', data)
    lines = ['// Generated by tools/gen_image_cells.py --bytes from game.exe at build time (not committed). Do not edit.',
             f"// game.exe's .rdata and .data from {lo:08X} as its file holds them (the import table zero), up to the "
             f"last nonzero dword: {len(words)} dwords."]
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
    img = image(exe_path.resolve(a.exe))
    if a.bytes:
        n = write_bytes(img, a.bytes)
        print(f'gen_image_cells: {n} dwords of .rdata and .data written to {a.bytes}')
        return 0
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
        moved += g[0] != 'text'
        if section_of(v) is None:
            problems.append(f'gen_image_cells: {c:08X} -> {v:08X} is the end of the moved range: a one-past-the-end '
                            'pointer of its last object, or the next section\'s start? decide and review')
        if IMPORTS[0] <= v < IMPORTS[1]:
            problems.append(f'gen_image_cells: {c:08X} -> {v:08X} points at an import slot: the image\'s slots are '
                            'its own table (image_imports.cpp); review')
    expected = [(('text', i), None, g[2]) for i, g in enumerate(TEXT)]
    for i, (_, n, what) in enumerate(STRUCTURES):
        expected.append((('structure', i), n, what))
    for name, groups in (('rdata', REVIEWED_RDATA), ('data', REVIEWED_DATA)):
        for i, g in enumerate(groups):
            expected.append(((name, i), g[2], f'{g[0]:08X}-{g[1]:08X} ({g[3]})'))
    for g, n, what in expected:
        have = counts.get(g, 0)
        if n is None:   # a text group: exactly one candidate per cell of its range
            n = (TEXT[g[1]][1] - TEXT[g[1]][0]) // 4 + 1
        if have != n:
            problems.append(f'gen_image_cells: the group {what[:70]} has {have} cell(s), reviewed {n}')
    text = render(cells, img)
    if a.check:
        current = open(OUT, encoding='utf-8').read() if os.path.exists(OUT) else ''
        if current != text:
            problems.append(f'gen_image_cells: {os.path.relpath(OUT, ROOT)} is not what the scan writes: '
                            'run python tools/gen_image_cells.py')
        for p in problems:
            print(p)
        print(f'gen_image_cells: {moved} cell(s) into .rdata and .data, {len(problems)} problem(s)')
        return 1 if problems else 0
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    for p in problems:
        print(p)
    print(f'{moved} cell(s) into .rdata and .data written to {os.path.relpath(OUT, ROOT)}')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
