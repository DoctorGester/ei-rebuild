"""The slots of game.exe's imports our code or the harness goes through (docs/notes/standalone.md, "Own import table"):

  src/platform/win32/import_table.h             ImportTable, one member per slot that a call of ours or the harness reaches
                                     (reached()), and Imports(), the table calls go through: the program's own
                                     (src/platform/win32/image_imports.cpp). A member's type is its function's: the
                                     Windows SDK's, or src/platform/win32/import_prototypes.h's (read, not written: Winsock 1.1's
                                     by ordinal).
  src/platform/win32/gameapi.h       the slots our code calls as `gameapi::X` (called()): a reference to each,
                                     bound in gameapi.cpp. Only a slot: a Windows function that no harness or UTF-8
                                     route takes is called as ::X.
  src/platform/win32/gameapi.cpp     gameapi.h's references, bound before every other static constructor
                                     (init_seg(lib)) and after the program resolves its table (init_seg(compiler)).
  src/platform/win32/image_imports.inc   the slots' imports: ImportTable member, DLL and function (or ordinal),
                                     which image_imports.cpp resolves.

usage: python tools/gen_iat.py [--check]
  (no option)  write the four files (reads game.exe's import directory)
  --check      exit 1 when a file differs from what the committed table (image_imports.inc) and the routes give; no
               game.exe (build.cmd). Catches gameapi.h / .cpp edited by hand, a new harness route on a slot, a
               gameapi:: call of an import gameapi.h does not have yet (or no longer one of one it has), and a slot
               nothing reaches any more.
"""
import argparse, os, re, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import exe_path  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAMEAPI = os.path.join(ROOT, 'src', 'platform', 'win32', 'gameapi.h')
GAMEAPI_CPP = os.path.join(ROOT, 'src', 'platform', 'win32', 'gameapi.cpp')
IMAGE_IMPORTS = os.path.join(ROOT, 'src', 'platform', 'win32', 'image_imports.inc')
IMPORT_TABLE = os.path.join(ROOT, 'src', 'platform', 'win32', 'import_table.h')
PROTOTYPES = os.path.join(ROOT, 'src', 'platform', 'win32', 'import_prototypes.h')
DLLS = {'KERNEL32.dll', 'USER32.dll', 'WINMM.dll'}   # the SDK's prototypes; WSOCK32's are import_prototypes.h's
# Imports by ordinal: the exports they name (WSOCK32's are Winsock 1.1's fixed ordinals).
ORDINALS = {('WSOCK32.dll', 1): 'accept', ('WSOCK32.dll', 2): 'bind', ('WSOCK32.dll', 3): 'closesocket',
            ('WSOCK32.dll', 4): 'connect', ('WSOCK32.dll', 9): 'htons', ('WSOCK32.dll', 10): 'inet_addr',
            ('WSOCK32.dll', 12): 'ioctlsocket', ('WSOCK32.dll', 13): 'listen', ('WSOCK32.dll', 15): 'ntohs',
            ('WSOCK32.dll', 16): 'recv', ('WSOCK32.dll', 17): 'recvfrom', ('WSOCK32.dll', 18): 'select',
            ('WSOCK32.dll', 19): 'send', ('WSOCK32.dll', 20): 'sendto', ('WSOCK32.dll', 21): 'setsockopt',
            ('WSOCK32.dll', 23): 'socket', ('WSOCK32.dll', 51): 'gethostbyaddr', ('WSOCK32.dll', 52): 'gethostbyname',
            ('WSOCK32.dll', 57): 'gethostname', ('WSOCK32.dll', 115): 'WSAStartup', ('WSOCK32.dll', 116): 'WSACleanup',
            ('WSOCK32.dll', 151): 'WSAFDIsSet', ('DSOUND.dll', 2): 'DirectSoundEnumerateA',
            ('COMCTL32.dll', 17): 'InitCommonControls'}
# The files that define the slots and name their members rather than call through them.
DEFINITIONS = (GAMEAPI, GAMEAPI_CPP, IMPORT_TABLE, IMAGE_IMPORTS, PROTOTYPES)


def imports(exe):
    """[(dll, function or None, ordinal)] in the import directory's order."""
    d = open(exe, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3C)[0]
    opt = pe + 24
    nsec = struct.unpack_from('<H', d, pe + 6)[0]
    optsz = struct.unpack_from('<H', d, pe + 20)[0]
    imp_rva = struct.unpack_from('<I', d, opt + 96 + 8)[0]
    secs = []
    for i in range(nsec):
        so = opt + optsz + 40 * i
        vsz, va, rsz, rptr = struct.unpack_from('<IIII', d, so + 8)
        secs.append((va, max(vsz, rsz), rptr))
    r2o = lambda rva: next(rva - va + rp for va, sz, rp in secs if va <= rva < va + sz)
    cstr = lambda off: d[off:d.index(b'\0', off)].decode()
    out = []
    p = r2o(imp_rva)
    while True:
        oft, _, _, name, ft = struct.unpack_from('<IIIII', d, p)
        if not name:
            break
        dll = cstr(r2o(name))
        i = 0
        while True:
            v = struct.unpack_from('<I', d, r2o(oft or ft) + 4 * i)[0]
            if not v:
                break
            if v & 0x80000000:
                out.append((dll, None, v & 0xFFFF))
            else:
                out.append((dll, cstr(r2o(v) + 2), 0))
            i += 1
        p += 20
    return out


def member(dll, fn, ordinal):
    """The ImportTable member of an import: its name without the stdcall decoration (`_AIL_startup@0` ->
    AIL_startup), an ordinal import by the export it names (ORDINALS)."""
    if fn is None:
        return ORDINALS[(dll, ordinal)]
    return re.sub(r'^_(\w+)@\d+$', r'\1', fn)


def prototypes():
    """The imports src/platform/win32/import_prototypes.h declares: the names of its `using`s in ei::import_fn."""
    return set(prototype_text())


def prototype_text():
    """{name: its `using` statement} of import_prototypes.h's ei::import_fn."""
    text = read(PROTOTYPES)
    start = text.index('namespace ei::import_fn {')
    return {name: statement for statement, name in re.findall(r'^(using (\w+) = [^;]*;)', text[start:], re.M)}


def member_type(dll, fn, name, declared):
    if name in declared:
        return f'import_fn::{name}'
    if dll in DLLS:
        return f'decltype(&::{fn or name})'
    sys.exit(f'gen_iat: no prototype for {dll}!{name}: add it to src/platform/win32/import_prototypes.h')


def committed_table():
    """[(dll, function or None, ordinal)] read back from src/platform/win32/image_imports.inc (what imports() gave,
    cut to the reached ones, when it was written)."""
    entry = re.compile(r'\{offsetof\(ei::ImportTable, (\w+)\), "([^"]+)", (?:"([^"]+)"|nullptr), (\d+)\},')
    return [(dll, fn or None, int(ordinal)) for _, dll, fn, ordinal in entry.findall(read(IMAGE_IMPORTS))]


def read(path):
    return open(path, encoding='utf-8', errors='replace').read()


def sources(folder, extensions):
    for root, _, files in os.walk(os.path.join(ROOT, folder)):
        for f in sorted(files):
            if f.endswith(extensions):
                yield os.path.join(root, f)


def routed(table):
    """The imports whose slot can hold something other than the export in the image (docs/notes/standalone.md,
    "Own import table"): the harness's slot routes (any `ImportTable::X` tests/ names), the image's
    routes (the UTF-8 routes of platform/win32/platform_win32.cpp) and a slot our code takes (`&gameapi::X`)."""
    names = {member(dll, fn, ordinal): fn for dll, fn, ordinal in table}
    out = set()
    for path in sources('tests', ('.cpp', '.h', '.inc')):
        for name in re.findall(r'\bImportTable::(\w+)', read(path)):
            if names.get(name):
                out.add(names[name])
    out.update(text_routes())
    for path in sources('src', ('.cpp', '.h')):
        out.update(re.findall(r'&gameapi::(\w+)', read(path)))
    return out


def text_routes():
    """The imports the image's UTF-8 routes take (platform/win32/platform_win32.cpp's TEXT_ROUTE list)."""
    return set(re.findall(r'TEXT_ROUTE\((\w+)\),', read(os.path.join(ROOT, 'src', 'platform', 'win32', 'platform_win32.cpp'))))


def code(text):
    """C++ source without its comments."""
    return re.sub(r'//[^\n]*', '', re.sub(r'/\*.*?\*/', ' ', text, flags=re.S))


def called(table):
    """The imports our code or the harness calls as `gameapi::X` (comments left out): what gameapi.h declares."""
    members = {member(dll, fn, ordinal) for dll, fn, ordinal in table}
    skip = {os.path.normcase(p) for p in DEFINITIONS}
    out = set()
    for folder in ('src', 'tests'):
        for path in sources(folder, ('.cpp', '.h', '.inc')):
            if os.path.normcase(path) not in skip:
                out.update(n for n in re.findall(r'\bgameapi::(\w+)', code(read(path))) if n in members)
    return out


def slot_calls(table):
    """The imports gameapi.h calls through the slot: of called(), import_prototypes.h's and routed()'s."""
    declared, routes = prototypes(), routed(table)
    return {name for name in called(table) if name in declared or name in routes}


def reached(table):
    """{member: the folders ('src', 'tests') whose code reaches its slot}: the slots a call can go through in the
    image (docs/notes/standalone.md, "Own import table"). Our code and the harness reach a slot only by its member:
    `ImportTable::X` (the harness's routes, the static initialisers' Import), `Imports().X`, or a gameapi.h function
    that is the slot (slot_calls(): an `extern ...& X` there) called as `gameapi::X`. Every other import is called as
    the Windows function itself or not at all, so the image leaves its slot a trap."""
    members = {member(dll, fn, ordinal) for dll, fn, ordinal in table}
    slots = slot_calls(table)
    skip = {os.path.normcase(p) for p in DEFINITIONS}
    out = {}
    for folder in ('src', 'tests'):
        for path in sources(folder, ('.cpp', '.h', '.inc')):
            if os.path.normcase(path) in skip:
                continue
            text = code(read(path))
            names = re.findall(r'\bImportTable::(\w+)', text) + re.findall(r'\bImports\(\)\.(\w+)', text)
            names += [n for n in re.findall(r'\bgameapi::(\w+)', text) if n in slots]
            for name in names:
                if name in members:
                    out.setdefault(name, set()).add(folder)
    return out


def import_table_text(table):
    declared = prototypes()
    lines = []
    for dll, fn, ordinal in table:
        name = member(dll, fn, ordinal)
        what = f'{dll}!{fn}' if fn else f'{dll}!#{ordinal}'
        lines.append(f'    {member_type(dll, fn, name, declared)} {name};   // {what}')
    return ('#pragma once\n\n// Generated by tools/gen_iat.py from game.exe\'s import directory. Do not edit.\n'
            '// The slots of game.exe\'s imports that a call of ours or the harness goes through (gen_iat.py\'s\n'
            '// reached()), in the import directory\'s order. Imports() is the table: the program\'s own\n'
            '// (src/platform/win32/image_imports.cpp). A slot holds the export, its UTF-8 version or the harness\'s\n'
            '// wrapper of either. A member\'s type is its function\'s: the Windows SDK\'s, or platform/win32/import_prototypes.h\'s\n'
            '// for an import the SDK does not declare.\n\n'
            '#include <windows.h>\n#include <mmsystem.h>\n\n'
            '#include <cstdint>\n\n#include "platform/win32/import_prototypes.h"\n\n'
            'namespace ei {\n\n'
            'struct ImportTable {\n' + '\n'.join(lines) + '\n};\n\n'
            '// The table calls go through: the program\'s own (src/platform/win32/image_imports.cpp).\n'
            'ImportTable& Imports();\n\n'
            '} // namespace ei\n')


def gameapi_text(table, routes, calls):
    """gameapi.h's text, gameapi.cpp's, and how many functions the header names: the imports of `calls`
    (called()), in the import directory's order."""
    lines, slots = [], []
    prototype = prototype_text()
    for dll, fn, ordinal in table:
        name = member(dll, fn, ordinal)
        if name not in calls:
            continue
        if name not in prototype and fn not in routes:
            sys.exit(f'gen_iat: gameapi::{name} ({dll}) is no slot: call ::{name}')
        lines.append(f'extern decltype(ei::ImportTable::{name})& {name};  // {dll}')
        slots.append(f'decltype(ei::ImportTable::{name})& {name} = ei::Imports().{name};')
    text = ('#pragma once\n\n// Generated by tools/gen_iat.py. Do not edit.\n'
            '// The import slots our code calls (gameapi::X): the imports whose slot can hold something other than\n'
            '// the export (the harness\'s slot routes, the UTF-8 routes; gen_iat.py\'s routed()) and Winsock 1.1\'s.\n'
            '// Each is a reference to its slot (platform/win32/import_table.h), bound in gameapi.cpp.\n\n'
            '#include "platform/win32/import_table.h"\n\nnamespace gameapi {\n\n')
    cpp = ('// Generated by tools/gen_iat.py. Do not edit.\n'
           '// gameapi.h\'s references to the import slots, bound once here rather than by an initialiser in every\n'
           '// file that includes the header. init_seg(lib): after the program resolves its table (image_imports.cpp,\n'
           '// init_seg(compiler)), and before every other static constructor, which may call through them.\n'
           '#include "platform/win32/gameapi.h"\n\n'
           '#pragma warning(disable : 4073)   // initialisers put in the library initialisation area: the point\n'
           '#pragma init_seg(lib)\n\nnamespace gameapi {\n\n')
    return (text + '\n'.join(lines) + '\n\n} // namespace gameapi\n',
            cpp + '\n'.join(slots) + '\n\n} // namespace gameapi\n', len(lines))


def image_imports_text(table):
    lines = ['// Generated by tools/gen_iat.py from game.exe\'s import directory. Do not edit.',
             '// The imports of platform/win32/import_table.h\'s ImportTable: {its member, DLL, function (nullptr: by ordinal),',
             '// ordinal}, which src/platform/win32/image_imports.cpp resolves into the table.']
    for dll, fn, ordinal in table:
        name = f'"{fn}"' if fn else 'nullptr'
        lines.append(f'{{offsetof(ei::ImportTable, {member(dll, fn, ordinal)}), "{dll}", {name}, {ordinal}}},')
    return '\n'.join(lines) + '\n'


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--check', action='store_true')
    args = ap.parse_args()
    table = committed_table() if args.check else imports(exe_path.resolve())
    reach = reached(table)
    table = [t for t in table if member(*t) in reach]
    gameapi, gameapi_cpp, functions = gameapi_text(table, routed(table), called(table))
    outputs = [(IMPORT_TABLE, import_table_text(table)), (GAMEAPI, gameapi), (GAMEAPI_CPP, gameapi_cpp),
               (IMAGE_IMPORTS, image_imports_text(table))]
    if args.check:
        stale = []
        for path, text in outputs:
            old = open(path, encoding='utf-8', newline='').read() if os.path.exists(path) else None
            if old != text:
                stale.append(os.path.relpath(path, ROOT))
        if stale:
            print('gen_iat: out of date (rerun python tools/gen_iat.py): ' + ', '.join(stale))
            return 1
        print(f'gen_iat: {len(table)} import slots, {functions} gameapi functions: up to date')
        return 0
    for path, text in outputs:
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
    print(f'{len(table)} import slots -> {IMPORT_TABLE}, {IMAGE_IMPORTS}\n{functions} functions -> {GAMEAPI}, {GAMEAPI_CPP}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
