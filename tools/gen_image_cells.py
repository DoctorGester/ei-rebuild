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

The cells inside the ranges our named globals hold (config/named_data.csv, C18) are left out: the image holds those
bytes zero, and tools/named_data.py --check compares each such cell with the pointer element of ours in its place (a
string literal, another named range, a function of ours with a registration).

Each cell found must be reviewed: a cell into .rdata is either part of a structure the tool walks (STRUCTURES: the C++
exception tables, RTTI, MFC's runtime classes) or in a REVIEWED_RDATA group; a cell into .data is in a REVIEWED_DATA
group; a candidate that is text after all (a short word the filters keep) is in TEXT and left out. Each group says what
the cells are and how many there are: a new cell (another exe, a scan change) or a changed count fails until someone
has looked at it. Every game.exe 1.06 has the same .rdata and .data (vanilla's and Rebirth's differ in one code byte).

The code cells (S18i, src/standalone/image_code_cells.inc; image_code.cpp points each at our code once the
registrations are in, or at a stub that reports there is no code at its address): the aligned dwords of the same data
whose value lies in game.exe's code section and that are code pointers: the cells of the structures the tool walks
(CODE_STRUCTURES: vtable slots from config/vtables.csv less the retired vtables' (config/retired_vtables.csv, C19:
classes whose vtable is the compiler's; their bytes are zero in the image), the C++ exception tables' (ThrowInfo) destructors and copy
constructors, MFC's CreateObject, the static initialiser list) and the CODE_REVIEWED groups (MFC's message maps, the
CRT's function pointers). The C++ exception tables' unwind actions and catch handlers (funclets, C20a) are walked and
counted but are no code cells: only __CxxFrameHandler on game.exe's own frames reads them (LEFT_KINDS). The rest of
the dwords in that range are left as they are: NOT_CODE groups (text, the
import names' hints, the CRT's SEH scope tables, which only game.exe's own frames read) and text by the filters above
whose value is no function start (config/functions.csv). A dword that is neither fails until it is reviewed.

usage: python tools/gen_image_cells.py [--check] [--exe game.exe] [--bytes OUT] [--dispatch OUT]
  (no option)  write src/standalone/image_cells.inc and image_code_cells.inc
  --check      exit 1 when the files differ from what the scan writes, or a cell has no review (build.cmd)
  --bytes OUT  write the moved range's initial bytes (.rdata's and .data's file bytes, the import table and the ranges
               our named globals hold zero: config/named_data.csv, C18) to OUT as a C++ initialiser (the build)
  --dispatch OUT write the dispatch-only functions' bytes (MOV EAX,[ECX]; JMP [EAX+n]) to OUT (the build: InstallHooks
               checks the EI_REPLACE_DISPATCH thunks against them, S18k)
"""
import argparse, csv, os, re, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import exe_path  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'src', 'standalone', 'image_cells.inc')
CODE_OUT = os.path.join(ROOT, 'src', 'standalone', 'image_code_cells.inc')
RUNTIME_CLASSES = os.path.join(ROOT, 'config', 'runtime_classes.csv')
VTABLES = os.path.join(ROOT, 'config', 'vtables.csv')
FUNCTIONS = os.path.join(ROOT, 'config', 'functions.csv')
LIBRARY = os.path.join(ROOT, 'config', 'library.csv')
NAMED_DATA = os.path.join(ROOT, 'config', 'named_data.csv')
RETIRED_VTABLES = os.path.join(ROOT, 'config', 'retired_vtables.csv')
BASE = 0x00400000
TIMESTAMP = 0x3ABF0DB3   # game.exe 1.06 (src/standalone/host.cpp's kExpectedTimestamp)

# game_data.h's kMovedBegin / kMovedEnd (.rdata and .data, one range, one shift) and kImportsBegin / kImportsEnd.
MOVED = (0x0073B000, 0x007CD000)
IMPORTS = (0x0073B000, 0x0073B820)
TEXT_SECTION = (0x00401000, 0x0073B000)
CODE = (0x00401000, 0x0073A670)   # .text's bytes (hook/standalone.cpp's kTextBegin / kTextEnd)
STATIC_INIT_LIST = (0x00783004, 0x00783BB0)   # inclusive (tools/gen_static_init.py's LIST_BEGIN / LIST_END)
# The targets by section, and the data that can hold cells (as the file holds it: .data's raw bytes end at 0079C000;
# the import table's file bytes are not the image's: ei_hook.dll fills its own slots).
SECTIONS = {'rdata': (0x0073B000, 0x00783000), 'data': (0x00783000, 0x007CD000)}
SCANNED = [('rdata', IMPORTS[1], 0x00783000), ('data', 0x00783000, 0x0079C000)]

# Cells into .data by group, as looked at in game.exe (who reads the cells, what the targets hold): (first cell,
# last cell, the number of cells, what they are[, the one target they all hold]). A cell belongs to the first group
# that takes it. "Strings" are string literals, which MSVC 6 put in .data.
REVIEWED_DATA = [
    # .rdata
    (0x0073B000, 0x00783000, 73, '".SEC": per-file copies of a const char* const from a header, some read by code '
                                 '(005B5042 reads 0073FD20)', 0x00783CDC),
    (0x0073FD1C, 0x0073FD1C, 1, '".MP": a const char* const'),
    (0x00746900, 0x00746930, 3, "MFC CCmdTarget's dispatch and event sink maps: their entry counts and stock "
                                "property mask (-1 in .data)"),
    (0x00746C80, 0x00746C80, 1, 'MFC message map entry (ON_REGISTERED_MESSAGE, handler 00706CF4): the message\'s '
                                'variable, set by a static initialiser to RegisterWindowMessageA("commctrl_DragListMsg")'),
    (0x007482F0, 0x0074C60C, 117, 'RTTI (MFC, the CRT, iostream, STLport): complete object locators and base class '
                                  'descriptors -> their type descriptors'),
    (0x00753F04, 0x0077F7CC, 12, 'C++ EH: catchable types (ThrowInfo) -> the thrown pointer types\' type '
                                 'descriptors (".PAV...")'),
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

# Kinds of STRUCTURES that are walked and counted but left out of the pointer cells (image_cells.inc), as C20a did for
# the code cells (LEFT_KINDS): the C++ EH tables read only by __CxxFrameHandler on game.exe's own frames, which gets
# the FuncInfo (unwind map, try block map, handler arrays, the caught types of the handlers) from the function's
# prologue. No such frame runs in the image (our code's FuncInfos are the compiler's, and none of game.exe's
# functions has a registration that reaches one: C20a), and nothing in src/ or tests/ names the tables; the
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

# Code cells of the structures the tool walks (code_structures()): kind, the number of cells, what. The vtables'
# 7258 are game.exe's; the retired ones' (config/retired_vtables.csv, C19: classes whose vtable is the compiler's now)
# are not code cells: nothing installs those vtables, and the image holds their bytes zero (write_bytes).
VTABLE_SLOT_CELLS = 7258
CODE_STRUCTURES = [
    ('vtable', None, "a vtable's slots (config/vtables.csv)"),
    ('vtable-retired', None, "a retired vtable's slots (config/retired_vtables.csv): not code cells"),
    ('eh-unwind', 13129, "C++ EH: FuncInfo -> its unwind map: each state's unwind action (a funclet): not code cells"),
    ('eh-handler', 382, 'C++ EH: try block -> its catch handlers: the handler (a funclet): not code cells'),
    ('eh-throw', 2, "C++ EH: ThrowInfo: the thrown object's destructor"),
    ('eh-copy', 5, "C++ EH: ThrowInfo -> catchable type: the thrown object's copy constructor"),
    ('mfc-create', 13, "MFC CRuntimeClass (config/runtime_classes.csv): CreateObject"),
    ('static-init', 748, 'the C++ initialiser list (00783004-00783BB0, __xc_a..__xc_z): the static initialisers'),
]

# Kinds of CODE_STRUCTURES that are walked and counted but left out of the code cells. The C++ EH funclets (C20a):
# __CxxFrameHandler reads a FuncInfo's unwind map and try blocks only for the frame whose prologue hands it that
# FuncInfo, i.e. game.exe's own functions, and none run in the image (our code's FuncInfo is the compiler's); no
# funclet has a registration (all were at stubs, which end the run, and no golden reached one), and nothing in src/
# or tests/ names their tables. Their bytes stay game.exe's, as the NOT_CODE groups' (a stray call through one lands
# in the guarded code section and is reported as no code there). The ThrowInfo cells (eh-throw, eh-copy) stay code
# cells: our frame handler reads them for a throw of game.exe's throw information (CxxThrowException).
LEFT_KINDS = ('vtable-retired', 'eh-unwind', 'eh-handler')

# Code cells outside those structures, as looked at in game.exe: (first cell, last cell, the number of dwords in the
# code section, what). Every dword of the range whose value lies in the code section is a code cell.
CODE_REVIEWED = [
    (0x0073DDE8, 0x0073DE3C, 22, 'slots after vtable 0073DDD4\'s five (config/vtables.csv): a vtable at 0073DDE8 '
                                   '(006E0437 reads it), up to the vtable 0073DE40'),
    (0x00746A14, 0x00747064, 35, "MFC message map entries (CWnd's, CWinApp's; AFX_MSGMAP_ENTRY): their handlers"),
    (0x00783BBC, 0x00783BE4, 6, "the CRT's C initialisers and terminators (__xi_a..__xi_z 00783BB4-00783BCC, __xp, "
                                   "__xt): __initmbctable and the CRT's own"),
    (0x00796E14, 0x00796E14, 1, "MFC's _afxNewHandler: AfxNewHandler (operator new's handler loop calls it)"),
    (0x007975A0, 0x007975C8, 4, "the CRT's floating-point hooks (_FPinit 007975A0, read by _cinit 006E8D7A) and "
                                   "__pInconsistency"),
    (0x00797640, 0x0079768C, 16, "the CRT's math error handling: fmod's dispatch table (into its code)"),
    (0x0079788C, 0x007978E0, 7, "the CRT's locale categories (00797888): each one's init function, and the "
                                   "next hook"),
    (0x00797B10, 0x00797B24, 6, 'the CRT\'s table of six handlers (006F9696, read by 006E72E4)'),
    (0x00797B60, 0x00797B74, 2, "the CRT's exception action table's default handlers"),
]

# Dwords in the code section's range that are no code pointer, or one the image never needs moved: (first cell,
# last cell, the number of dwords in the code section, why).
NOT_CODE = [
    (0x00745D14, 0x00745D14, 1, "c_dfDIMouse's object array (dinput.lib's data inside game.exe's code section; "
                                   "CDirectInputMouse::MouseDataFormat is our copy)"),
    (0x007472B8, 0x007472E0, 4, "the CRT's SEH scope tables ({enclosing level, filter, handler}): read by "
                                   "_except_handler3 for frames of game.exe's own code only"),
    (0x00747324, 0x00747390, 10, "the CRT's SEH scope tables"),
    (0x007474B8, 0x007474B8, 1, '"a/p": __lc_time_c\'s text'),
    (0x007474E0, 0x007474F4, 2, "the CRT's SEH scope tables"),
    (0x00747580, 0x00747614, 17, "the CRT's SEH scope tables"),
    (0x00747748, 0x007477A4, 5, "the CRT's SEH scope tables"),
    (0x00748190, 0x00748244, 13, "the CRT's SEH scope tables"),
    (0x00780000, 0x007828B0, 88, "the import directory's names and hints (text: \"ime\" of timeGetTime, hint "
                                   "words)"),
    (0x00783E1C, 0x00783E1C, 1, 'a table of random dwords read by 004310D9'),
    (0x00783D90, 0x00783D90, 1, '" %s": text'),
    (0x00787EFC, 0x00787EFC, 1, '" %s": text'),
    (0x0078BC28, 0x0078BC28, 1, '" %s": text'),
    (0x0078CF18, 0x0078CF18, 1, '"her": text (UMCorpseWatcher)'),
    (0x0078D73C, 0x0078D73C, 1, '" %s": text'),
    (0x0078FC44, 0x0078FC44, 1, '"her": text (leather)'),
    (0x00792EF8, 0x00792EF8, 1, '" %s": text'),
    (0x00792F84, 0x00792F84, 1, '" %s": text'),
    (0x00797724, 0x00797724, 1, 'a number in the CRT\'s data'),
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


def outside_named(keep_named):
    """A test for a cell address: False inside a named range of config/named_data.csv (unless keep_named)."""
    if keep_named:
        return lambda a: True
    ranges = named_data_ranges()
    return lambda a: not any(lo <= a < lo + n for lo, n in ranges)


def scan(img, keep_named=False):
    cells = []
    wanted = outside_named(keep_named)
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
    # MFC's CRuntimeClass: lpszClassName, size, schema, pfnCreateObject, pBaseClass, pNextClass.
    with open(RUNTIME_CLASSES, newline='') as f:
        for row in csv.DictReader(f):
            c = int(row['runtime_class'], 16)
            mark(c, 'mfc-name')
            mark(c + 16, 'mfc-base')
    return kinds


def function_starts():
    """config/functions.csv's starts, less library.csv's "not a function" rows."""
    with open(FUNCTIONS, newline='') as f:
        starts = {int(r['address'], 16) for r in csv.DictReader(f)}
    with open(LIBRARY, newline='') as f:
        starts -= {int(r['address'], 16) for r in csv.DictReader(f) if r['name'].startswith('not a function')}
    return starts


def in_code(v):
    return CODE[0] <= v < CODE[1]


def code_structures(img):
    """{cell: kind} for the code cells of the structures the tool walks (CODE_STRUCTURES)."""
    def dw(a):
        return struct.unpack_from('<I', img, a - BASE)[0]

    def rdata(v):
        return section_of(v) == 'rdata'

    def type_descriptor(v):
        return section_of(v) == 'data' and img[v + 8 - BASE] == ord('.')

    kinds = {}

    def mark(cell, kind):
        if in_code(dw(cell)):
            kinds.setdefault(cell, kind)

    retired = retired_cells()
    with open(VTABLES, newline='') as f:
        for row in csv.DictReader(f):
            vtable = int(row['vtable'], 16)
            for i in range(int(row['slots'])):
                cell = vtable + 4 * i
                mark(cell, 'vtable-retired' if cell in retired else 'vtable')
    lo, hi = SCANNED[0][1], SCANNED[0][2]
    for a in range(lo, hi - 20, 4):
        if dw(a) == 0x19930520:   # FuncInfo: magic, maxState, pUnwindMap ({toState, action}), nTryBlocks, pTryBlockMap
            if rdata(dw(a + 8)):
                for i in range(dw(a + 4)):
                    mark(dw(a + 8) + 8 * i + 4, 'eh-unwind')
            if rdata(dw(a + 16)):
                for i in range(dw(a + 12)):   # try block: ..., nCatches, pHandlerArray ({adjectives, pType, disp, handler})
                    block = dw(a + 16) + 20 * i
                    if rdata(dw(block + 16)):
                        for j in range(dw(block + 12)):
                            mark(dw(block + 16) + 16 * j + 12, 'eh-handler')
        # ThrowInfo (as structures()): pmfnUnwind; a catchable type: properties, pType, PMD (12 bytes), size, copy.
        unwind = dw(a + 4)
        if dw(a) <= 3 and (unwind == 0 or TEXT_SECTION[0] <= unwind < TEXT_SECTION[1]) and dw(a + 8) == 0 \
                and rdata(dw(a + 12)):
            array = dw(a + 12)
            n = dw(array)
            if 1 <= n <= 16 and all(rdata(dw(array + 4 + 4 * i)) and type_descriptor(dw(dw(array + 4 + 4 * i) + 4))
                                    for i in range(n)):
                mark(a + 4, 'eh-throw')
                for i in range(n):
                    mark(dw(array + 4 + 4 * i) + 24, 'eh-copy')
    with open(RUNTIME_CLASSES, newline='') as f:
        for row in csv.DictReader(f):   # lpszClassName, size, schema, pfnCreateObject, ...
            mark(int(row['runtime_class'], 16) + 12, 'mfc-create')
    for a in range(STATIC_INIT_LIST[0], STATIC_INIT_LIST[1] + 4, 4):
        mark(a, 'static-init')
    return kinds


def code_group_of(cell, value, kinds, starts, img):
    """('structure' | 'code' | 'not', index) for a dword whose value lies in the code section, ('text', None) for
    text the filters keep whose value is no function start, or None (no review)."""
    if cell in kinds:
        return ('structure', [k for k, _, _ in CODE_STRUCTURES].index(kinds[cell]))
    for name, groups in (('code', CODE_REVIEWED), ('not', NOT_CODE)):
        for i, g in enumerate(groups):
            if g[0] <= cell <= g[1]:
                return (name, i)
    o = cell - BASE
    if value not in starts and (string_tail(img, o) or text_word(img, o)):
        return ('text', None)
    return None


def code_scan(img, keep_named=False):
    cells = []
    wanted = outside_named(keep_named)
    for _, begin, end in SCANNED:
        for a in range(begin, end, 4):
            v = struct.unpack_from('<I', img, a - BASE)[0]
            if in_code(v) and wanted(a):
                cells.append((a, v))
    return cells


def code_check(img, problems, keep_named=False):
    """The code cells (S18i) with their groups; problems appended. Returns (cells, groups, counts)."""
    kinds = code_structures(img)
    starts = function_starts()
    counts = {}
    code = []
    for c, v in code_scan(img, keep_named):
        g = code_group_of(c, v, kinds, starts, img)
        if g is None:
            problems.append(f'gen_image_cells: {c:08X} -> {v:08X} (the code section) has no review: a code pointer '
                            '(CODE_REVIEWED) or not (NOT_CODE)? (tools/gen_image_cells.py)')
            continue
        counts[g] = counts.get(g, 0) + 1
        if g[0] == 'code' or g[0] == 'structure' and CODE_STRUCTURES[g[1]][0] not in LEFT_KINDS:
            code.append((c, v, g))
    retired = sum(in_code(struct.unpack_from('<I', img, c - BASE)[0]) for c in retired_cells())
    check_retired(problems, img)
    for name, groups in (('structure', CODE_STRUCTURES), ('code', CODE_REVIEWED), ('not', NOT_CODE)):
        for i, g in enumerate(groups):
            n = g[1] if name == 'structure' else g[2]
            if name == 'structure' and g[0] == 'vtable':
                n = VTABLE_SLOT_CELLS - retired
            elif name == 'structure' and g[0] == 'vtable-retired':
                n = retired
            have = counts.get((name, i), 0)
            what = g[2] if name == 'structure' else f'{g[0]:08X}-{g[1]:08X} ({g[3]})'
            if have != n:
                problems.append(f'gen_image_cells: the code group {what[:70]} has {have} cell(s), reviewed {n}')
    return code, counts


def code_describe(group):
    kind, i = group
    if kind == 'structure':
        return CODE_STRUCTURES[i][2]
    return CODE_REVIEWED[i][3]


def code_render(code):
    lines = [
        '// Generated by tools/gen_image_cells.py from game.exe. Do not edit.',
        '//',
        "// The code cells of game.exe's .rdata and .data (S18i): dwords that point into game.exe's code section.",
        "// image_code.cpp points each at the code registered for its address once the registrations are in, or at",
        "// a stub that reports there is no code there. {cell, the code address it holds}, by group; each group's",
        '// review is in the tool.',
    ]
    order = [('structure', i) for i in range(len(CODE_STRUCTURES))] + [('code', i) for i in range(len(CODE_REVIEWED))]
    by_group = {}
    for c, v, g in code:
        by_group.setdefault(g, []).append((c, v))
    for g in order:
        if g not in by_group:
            continue
        lines.append(f'// {code_describe(g)}')
        for c, v in by_group[g]:
            lines.append(f'{{DataId(0x{c:08X}), 0x{v:08X}}},')
    return '\n'.join(lines) + '\n'


def group_of(cell, value, kinds):
    """('text' | 'structure' | 'data' | 'rdata', index) or None."""
    for i, (first, last, _) in enumerate(TEXT):
        if first <= cell <= last:
            return ('text', i)
    section = section_of(value)
    if cell in kinds:
        return ('structure', [k for k, _, _ in STRUCTURES].index(kinds[cell]))
    groups = REVIEWED_RDATA if section == 'rdata' else REVIEWED_DATA
    for i, g in enumerate(groups):
        if g[0] <= cell <= g[1] and (len(g) < 5 or g[4] == value):
            return (section, i)
    return None


def left_out(group):
    """Whether a group is no pointer cells: text, or a structure of LEFT_STRUCTURES."""
    return group[0] == 'text' or group[0] == 'structure' and STRUCTURES[group[1]][0] in LEFT_STRUCTURES


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
    the retired vtables (config/retired_vtables.csv, C19)."""
    with open(NAMED_DATA, newline='') as f:
        named = [(int(r['address'], 16), int(r['size'])) for r in csv.DictReader(f)]
    return named + [(v, 4 * n) for v, n, _, _ in retired_vtables()]


def retired_vtables():
    """(vtable, slots, class, file) of config/retired_vtables.csv: game.exe's vtables of classes whose vtable is the
    compiler's now (C19; docs/notes/standalone.md "Compiler-made vtables"), or that only game.exe's own code stored
    (file '-': base vtables its destructors stored, locals its functions made, dead code's; the note column names the
    storing functions). Nothing installs them: their slots are no code cells and the image holds their bytes zero;
    tools/named_data.py --check proves nothing names or points into them."""
    if not os.path.exists(RETIRED_VTABLES):
        return []
    with open(RETIRED_VTABLES, newline='') as f:
        return [(int(r['vtable'], 16), int(r['slots']), r['class'], r['file']) for r in csv.DictReader(f)]


def retired_cells():
    """The slots of the retired vtables (config/vtables.csv's vtables overlap: a run of slots that ends another
    table, 0073CA08 / 0073CA0C / 0073CA10; a cell counts once)."""
    return {v + 4 * i for v, n, _, _ in retired_vtables() for i in range(n)}


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
    """Each retired vtable is a game vtable of config/vtables.csv with all its slots, listed once, the start of a
    table (stored_vtable) or the tail of a retired one (the row it ends), and shares no slot with a vtable still in
    use (its bytes are zero in the image)."""
    with open(VTABLES, newline='') as f:
        vtables = {int(r['vtable'], 16): (int(r['slots']), r['kind']) for r in csv.DictReader(f)}
    listed = {v for v, _, _, _ in retired_vtables()}
    live = {}
    for v, (n, _) in vtables.items():
        if v not in listed:
            for i in range(n):
                live.setdefault(v + 4 * i, v)
    seen = set()
    # A tail goes with its table: a retired row's end, when that row is stored or a tail itself.
    ends = set()
    for v, n, _, _ in sorted(retired_vtables()):
        if stored_vtable(img, v) or v in ends:
            ends.add(v + 4 * n)
    for v, n, cls, path in retired_vtables():
        where = f'config/retired_vtables.csv: {v:08X} ({cls})'
        if not stored_vtable(img, v) and v not in ends:
            problems.append(f'gen_image_cells: {where}: no code of game.exe stores it: the tail of the vtable '
                            'before it (a slot call or a byte match split it off), not a vtable of its own; '
                            'retire it with that vtable')
        shared = sorted({live[v + 4 * i] for i in range(n) if v + 4 * i in live})
        if shared:
            problems.append(f'gen_image_cells: {where} shares slots with ' + ' '.join(f'{x:08X}' for x in shared) +
                            ', still in use: retire them together')
        if v in seen:
            problems.append(f'gen_image_cells: {where} is listed twice')
        seen.add(v)
        if v not in vtables:
            problems.append(f'gen_image_cells: {where} is not a vtable of config/vtables.csv')
        elif vtables[v][0] != n:
            problems.append(f'gen_image_cells: {where} has {n} slot(s), config/vtables.csv {vtables[v][0]}')
        if path == '-':   # no class of ours: only game.exe's code stored it (the note says which)
            continue
        if not os.path.exists(os.path.join(ROOT, path)):
            problems.append(f'gen_image_cells: {where}: no file {path}')
        elif not re.search(rf'\b(class|struct)\s+{re.escape(cls)}\b',
                           open(os.path.join(ROOT, path), encoding='utf-8', errors='replace').read()):
            problems.append(f'gen_image_cells: {where}: {path} declares no class {cls}')


def write_bytes(img, path):
    lo = MOVED[0]
    end = SCANNED[-1][2]   # the file's bytes; the rest of the range is zero
    data = bytearray(img[lo - BASE:end - BASE])
    data[:IMPORTS[1] - IMPORTS[0]] = bytes(IMPORTS[1] - IMPORTS[0])   # our slots, filled by ei_hook.dll
    named = named_ranges()
    for a, n in named:   # named globals of ours now (C18): nothing reads these bytes
        b, e = a - lo, min(a - lo + n, len(data))   # a range in the zero-filled tail past the file's bytes: nothing
        if b < e:
            data[b:e] = bytes(e - b)
    while data.endswith(b'\0\0\0\0'):
        data = data[:-4]
    words = struct.unpack(f'<{len(data) // 4}I', data)
    lines = ['// Generated by tools/gen_image_cells.py --bytes from game.exe at build time (not committed). Do not edit.',
             f"// game.exe's .rdata and .data from {lo:08X} as its file holds them (the import table and the {len(named)} "
             f"ranges of config/named_data.csv and config/retired_vtables.csv zero), up to the last nonzero dword: {len(words)} dwords."]
    for i in range(0, len(words), 8):
        lines.append(','.join(f'0x{w:X}' for w in words[i:i + 8]) + ',')
    text = '\n'.join(lines) + '\n'
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    if os.path.exists(path) and open(path, encoding='utf-8').read() == text:
        return len(words)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    return len(words)


def write_dispatch(img, path):
    """The dispatch-only functions (MOV EAX,[ECX]; JMP [EAX], [EAX+disp8] or [EAX+disp32]) among the function starts,
    with their bytes: what InstallHooks compares an EI_REPLACE_DISPATCH thunk with (S18k: the image reads no
    game.exe)."""
    sizes = {0x20: 4, 0x60: 5, 0xA0: 8}
    lines = ['// Generated by tools/gen_image_cells.py --dispatch from game.exe at build time (not committed). Do not edit.',
             "// game.exe's dispatch-only functions (MOV EAX,[ECX]; JMP [EAX+n]): address, bytes (zero after the JMP)."]
    for a in sorted(function_starts()):
        b = img[a - BASE:a - BASE + 8]
        if not in_code(a) or b[0] != 0x8B or b[1] != 0x01 or b[2] != 0xFF or b[3] not in sizes:
            continue
        code = list(b[:sizes[b[3]]]) + [0] * (8 - sizes[b[3]])
        lines.append('{0x%08X, {%s}},' % (a, ', '.join('0x%02X' % x for x in code)))
    text = '\n'.join(lines) + '\n'
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    if not (os.path.exists(path) and open(path, encoding='utf-8').read() == text):
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
    return len(lines) - 2


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--exe')   # default: tools/exe_path.py
    ap.add_argument('--bytes', metavar='OUT')
    ap.add_argument('--dispatch', metavar='OUT')
    a = ap.parse_args()
    img = image(exe_path.resolve(a.exe))
    if a.bytes or a.dispatch:
        if a.bytes:
            n = write_bytes(img, a.bytes)
            print(f'gen_image_cells: {n} dwords of .rdata and .data written to {a.bytes}')
        if a.dispatch:
            n = write_dispatch(img, a.dispatch)
            print(f'gen_image_cells: {n} dispatch-only functions written to {a.dispatch}')
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
    for name, groups in (('rdata', REVIEWED_RDATA), ('data', REVIEWED_DATA)):
        for i, g in enumerate(groups):
            expected.append(((name, i), g[2], f'{g[0]:08X}-{g[1]:08X} ({g[3]})'))
    for g, n, what in expected:
        have = counts.get(g, 0)
        if n is None:   # a text group: exactly one candidate per cell of its range
            n = (TEXT[g[1]][1] - TEXT[g[1]][0]) // 4 + 1
        if have != n:
            problems.append(f'gen_image_cells: the group {what[:70]} has {have} cell(s), reviewed {n}')
    code, code_counts = code_check(img, problems)
    left = sum(n for g, n in code_counts.items() if g[0] in ('not', 'text'))
    outputs = [(OUT, render(cells, img)), (CODE_OUT, code_render(code))]
    summary = (f'{moved} cell(s) into .rdata and .data, {len(code)} code cell(s) ({left} dword(s) in the code '
               'section\'s range left: text, SEH tables, data)')
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
