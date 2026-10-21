"""game.exe's .data for the standalone image (S18b, S18d; docs/notes/standalone.md, "Own data"): the pointer cells
of game.exe's data that point into the range the image moves (src/standalone/image_cells.inc, committed and
reviewed; image_data.cpp fixes them up when it moves the range), and the range's initial bytes (written into the
build folder by the build, not committed: --bytes).

game.exe has no relocations, so the cells are found by a scan: every aligned dword of .rdata and .data (as the
file holds them; the zero-filled tail holds none) whose value lies in the moved range, less text that happens to
read as an address: the tails of strings (the dword's bytes up to its first zero printable, and the four bytes
before it too), and short words (the dword's bytes up to its zero printable, "Any", "May") whose value would point
into the middle of text (the byte before the target printable ASCII: a pointer to a string points at its start).
Each cell found must be in a REVIEWED group below, which says what the cells are and how many there are: a new
cell (another exe, a scan change) or a changed count fails until someone has looked at it.

usage: python tools/gen_image_cells.py [--check] [--exe game.exe] [--bytes OUT]
  (no option)  write src/standalone/image_cells.inc
  --check      exit 1 when the file differs from what the scan writes, or a cell has no review (build.cmd)
  --bytes OUT  write the moved range's initial bytes (.data's file bytes) to OUT as a C++ initialiser (the build)
"""
import argparse, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import exe_path  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'src', 'standalone', 'image_cells.inc')
BASE = 0x00400000
TIMESTAMP = 0x3ABF0DB3   # game.exe 1.06 (src/standalone/host.cpp's kExpectedTimestamp)

# The data that can hold cells (as the file holds it: .data's raw bytes end at 0079C000), and the moved range
# (game_data.h's kMovedBegin / kMovedEnd: game.exe's whole .data section, its zero-filled tail included).
SCANNED = [('rdata', 0x0073B000, 0x00783000), ('data', 0x00783000, 0x0079C000)]
MOVED = ('data', 0x00783000, 0x007CD000)

# The cells by group, as looked at in game.exe (who reads the cells, what the targets hold): (first cell, last cell,
# the number of cells, what they are[, the one target they all hold]). A cell belongs to the first group that
# takes it. "Strings" are string literals, which MSVC 6 put in .data.
REVIEWED = [
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


def scan(img):
    _, lo, hi = MOVED
    cells = []
    for _, begin, end in SCANNED:
        for a in range(begin, end, 4):
            o = a - BASE
            v = struct.unpack_from('<I', img, o)[0]
            if not lo <= v <= hi or string_tail(img, o) or (text_word(img, o) and mid_text(img, v)):
                continue
            cells.append((a, v))
    return cells


def group_of(cell, value):
    for i, g in enumerate(REVIEWED):
        if g[0] <= cell <= g[1] and (len(g) < 5 or g[4] == value):
            return i
    return None


def render(cells):
    lines = [
        '// Generated by tools/gen_image_cells.py from game.exe. Do not edit.',
        '//',
        "// The pointer cells of game.exe's .rdata and .data that point into its .data (S18b, S18d): image_data.cpp",
        '// adds the shift to each when the image moves the section. {cell, the address it holds}; each group\'s',
        '// review is in the tool.',
    ]
    last = None
    for a, v in cells:
        g = group_of(a, v)
        if g != last:
            lines.append(f'// {REVIEWED[g][3] if g is not None else "NOT REVIEWED"}')
            last = g
        lines.append(f'{{DataId(0x{a:08X}), DataId(0x{v:08X})}},')
    return '\n'.join(lines) + '\n'


def write_bytes(img, path):
    _, lo, _ = MOVED
    end = SCANNED[-1][2]   # the file's bytes; the rest of the range is zero
    data = bytes(img[lo - BASE:end - BASE])
    while data.endswith(b'\0\0\0\0'):
        data = data[:-4]
    words = struct.unpack(f'<{len(data) // 4}I', data)
    lines = ['// Generated by tools/gen_image_cells.py --bytes from game.exe at build time (not committed). Do not edit.',
             f"// game.exe's .data from {lo:08X} as its file holds it, up to its last nonzero dword: {len(words)} dwords."]
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
        print(f'gen_image_cells: {n} dwords of .data written to {a.bytes}')
        return 0
    cells = scan(img)
    problems = []
    counts = [0] * len(REVIEWED)
    for c, v in cells:
        g = group_of(c, v)
        if g is None:
            problems.append(f'gen_image_cells: {c:08X} -> {v:08X} has no review (tools/gen_image_cells.py REVIEWED)')
        else:
            counts[g] += 1
        if v == MOVED[2]:
            problems.append(f'gen_image_cells: {c:08X} -> {v:08X} is the end of .{MOVED[0]}: a one-past-the-end '
                            'pointer of its last object, or the next section\'s start? decide and review')
    for g, n in zip(REVIEWED, counts):
        if n != g[2]:
            problems.append(f'gen_image_cells: the group {g[0]:08X}-{g[1]:08X} ({g[3][:50]}) has {n} cell(s), '
                            f'reviewed {g[2]}')
    text = render(cells)
    if a.check:
        current = open(OUT, encoding='utf-8').read() if os.path.exists(OUT) else ''
        if current != text:
            problems.append(f'gen_image_cells: {os.path.relpath(OUT, ROOT)} is not what the scan writes: '
                            'run python tools/gen_image_cells.py')
        for p in problems:
            print(p)
        print(f'gen_image_cells: {len(cells)} cell(s) into .{MOVED[0]}, {len(problems)} problem(s)')
        return 1 if problems else 0
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    for p in problems:
        print(p)
    print(f'{len(cells)} cell(s) into .{MOVED[0]} written to {os.path.relpath(OUT, ROOT)}')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
