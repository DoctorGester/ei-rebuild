"""game.exe's imports, from its import directory (docs/notes/standalone.md, "Own import table"):

  src/lib/import_table.h             game.exe's import address table as a struct (ImportTable, one member per
                                     slot and terminator) and Imports(), the table through the data seam
                                     (game/game_data.h): in game.exe its own table, in the image ei_hook.dll's.
  src/lib/gameapi.h                  the Windows API our code calls where the original called it through a slot:
                                     an import whose slot can hold something other than the export in the image
                                     (routed(): the harness's slot routes, the image's routes, a slot our code
                                     takes) through Imports(), every other one as the Windows function itself.
  src/standalone/image_imports.inc   every import of game.exe with its ImportTable member, DLL and function (or
                                     ordinal): the standalone image's own import table, which ei_hook.dll resolves
                                     into its storage (src/standalone/image_imports.cpp, S18f).

usage: python tools/gen_iat.py [--check]
  (no option)  write the three files
  --check      exit 1 when a file differs from what the import directory and the routes give (build.cmd)
"""
import argparse, os, re, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import exe_path  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAMEAPI = os.path.join(ROOT, 'src', 'lib', 'gameapi.h')
IMAGE_IMPORTS = os.path.join(ROOT, 'src', 'standalone', 'image_imports.inc')
IMPORT_TABLE = os.path.join(ROOT, 'src', 'lib', 'import_table.h')
DLLS = {'KERNEL32.dll', 'USER32.dll', 'GDI32.dll', 'ADVAPI32.dll', 'WINMM.dll', 'SHELL32.dll', 'ole32.dll'}
# Functions whose prototypes differ from the Windows SDK headers we compile against, or that are macros: FARPROC
# in the table.
SKIP = {'GetVersion', 'InterlockedIncrement', 'InterlockedDecrement'}
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
# Called through the slot although nothing routes them: what the Windows SDK declares deprecated (a direct call
# warns, C4996).
KEEP = {'GetVersionExA'}
# game_data.h's kImportsBegin / kImportsEnd: the import address table, the first 0x820 bytes of .rdata.
IAT_BEGIN, IAT_END = 0x0073B000, 0x0073B820


def imports(exe):
    """[(slot, dll, function or None, ordinal)] in the import directory's order."""
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
            slot = 0x400000 + ft + 4 * i
            assert IAT_BEGIN <= slot < IAT_END, hex(slot)
            if v & 0x80000000:
                out.append((slot, dll, None, v & 0xFFFF))
            else:
                out.append((slot, dll, cstr(r2o(v) + 2), 0))
            i += 1
        p += 20
    return out


def member(dll, fn, ordinal):
    """The ImportTable member of an import: its name without the stdcall decoration (`_AIL_startup@0` ->
    AIL_startup), an ordinal import by the export it names (ORDINALS)."""
    if fn is None:
        return ORDINALS[(dll, ordinal)]
    return re.sub(r'^_(\w+)@\d+$', r'\1', fn)


def member_type(dll, fn):
    if dll in DLLS and fn and fn not in SKIP:
        return f'decltype(&::{fn})'
    return 'FARPROC'


def layout(table):
    """[(member, type, dll, fn, ordinal)] for every dword of the import address table, in address order: each
    DLL's slots, then its zero terminator (type None)."""
    by_slot = {slot: (dll, fn, ordinal) for slot, dll, fn, ordinal in table}
    out = []
    last = None
    for slot in range(IAT_BEGIN, IAT_END, 4):
        if slot in by_slot:
            dll, fn, ordinal = last = by_slot[slot]
            out.append((member(dll, fn, ordinal), member_type(dll, fn), dll, fn, ordinal))
        else:
            out.append((re.sub(r'\W', '_', last[0].split('.')[0]) + '_end', None, last[0], None, None))
    return out


def read(path):
    return open(path, encoding='utf-8', errors='replace').read()


def sources(folder, extensions):
    for root, _, files in os.walk(os.path.join(ROOT, folder)):
        for f in sorted(files):
            if f.endswith(extensions):
                yield os.path.join(root, f)


def routed(table):
    """The imports whose slot can hold something other than the export in the image (docs/notes/standalone.md,
    "Own import table"): the harness's slot routes (any import slot address tests/ names), the image's routes
    (ImageRoute's table in hook/standalone_main.cpp, the UTF-8 routes of game/system_text.cpp), a slot our code takes
    (`&gameapi::X`), and KEEP."""
    names = {slot: fn for slot, dll, fn, _ in table}
    out = set(KEEP)
    literal = re.compile(r'0[xX]00(73B[0-9A-Fa-f]{3})\b')
    for path in sources('tests', ('.cpp', '.h', '.inc')):
        for m in literal.finditer(read(path)):
            fn = names.get(int(m.group(1), 16))
            if fn:
                out.add(fn)
    out.update(re.findall(r'EI_UTF8_ROUTE\((\w+)\)', read(os.path.join(ROOT, 'src', 'game', 'system_text.cpp'))))
    out.update(re.findall(r'\{"\w+\.dll", "(\w+)", reinterpret_cast',
                          read(os.path.join(ROOT, 'src', 'hook', 'standalone_main.cpp'))))
    for path in sources('src', ('.cpp', '.h')):
        out.update(re.findall(r'&gameapi::(\w+)', read(path)))
    return out


def import_table_text(table):
    lines = []
    for name, type_, dll, fn, ordinal in layout(table):
        if type_ is None:
            lines.append(f'    std::uint32_t {name};   // zero: the end of {dll}\'s slots')
        else:
            what = f'{dll}!{fn}' if fn else f'{dll}!#{ordinal}'
            lines.append(f'    {type_} {name};   // {what}')
    return ('#pragma once\n\n// Generated by tools/gen_iat.py from game.exe\'s import directory. Do not edit.\n'
            '// game.exe\'s import address table as a struct: one member per slot, in address order (each DLL\'s\n'
            '// slots, then its zero terminator). Imports() is the table through the data seam (game/game_data.h):\n'
            '// in game.exe its own table, in the image ei_hook.dll\'s (src/standalone/image_imports.cpp, S18f).\n'
            '// A slot holds the export, a route of the image (ImageRoute) or the harness\'s wrapper of either.\n'
            '// An import the Windows SDK headers do not declare is a FARPROC: its caller casts it to its type\n'
            '// (ImportAs).\n\n'
            '#include <windows.h>\n#include <mmsystem.h>\n#include <shellapi.h>\n#include <objbase.h>\n\n'
            '#include <cstdint>\n\n#include "game/game_data.h"\n\n#undef GetObject\n\nnamespace ei {\n\n'
            'struct ImportTable {\n' + '\n'.join(lines) + '\n};\n'
            'static_assert(sizeof(ImportTable) == kImportsEnd - kImportsBegin);\n\n'
            'inline ImportTable& Imports() { return GameData<ImportTable>(kImportsBegin); }\n\n'
            '// An import the table holds as a FARPROC, as its function type.\n'
            'template <typename F>\nF ImportAs(FARPROC import) {\n    return reinterpret_cast<F>(import);\n}\n\n'
            '} // namespace ei\n')


def gameapi_text(table, routes):
    lines = []
    for slot, dll, fn, _ in table:
        if dll not in DLLS or not fn or fn in SKIP:
            continue
        if fn in routes:
            lines.append(f'inline auto& {fn} = ei::Imports().{fn};  // {dll}, through the slot')
        else:
            lines.append(f'using ::{fn};  // {dll}')
    libs = ''.join(f'#pragma comment(lib, "{d.split(".")[0].lower()}.lib")\n' for d in sorted(DLLS))
    text = ('#pragma once\n\n// Generated by tools/gen_iat.py. Do not edit.\n'
            '// The Windows API our code calls where game.exe called it through its import table. An import\n'
            '// whose slot can hold something other than the export in the image (the harness\'s slot routes, the\n'
            '// image\'s UTF-8 and command-line routes, a slot our code takes; gen_iat.py\'s routed()) is called\n'
            '// through the slot (lib/import_table.h); every other one is the Windows function itself.\n\n'
            '#include "lib/import_table.h"\n\n' + libs + '\nnamespace gameapi {\n\n')
    return text + '\n'.join(lines) + '\n\n} // namespace gameapi\n', len(lines)


def image_imports_text(table):
    lines = ['// Generated by tools/gen_iat.py from game.exe\'s import directory. Do not edit.',
             '// Every import of game.exe (its import address table): {its member of lib/import_table.h\'s ImportTable,',
             '// DLL, function (nullptr: by ordinal), ordinal}, in the directory\'s order. The image resolves them into',
             '// ei_hook.dll\'s storage (src/standalone/image_imports.cpp, S18f).']
    for slot, dll, fn, ordinal in table:
        name = f'"{fn}"' if fn else 'nullptr'
        lines.append(f'{{offsetof(ei::ImportTable, {member(dll, fn, ordinal)}), "{dll}", {name}, {ordinal}}},')
    return '\n'.join(lines) + '\n'


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--check', action='store_true')
    args = ap.parse_args()
    table = imports(exe_path.resolve())
    gameapi, functions = gameapi_text(table, routed(table))
    outputs = [(IMPORT_TABLE, import_table_text(table)), (GAMEAPI, gameapi),
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
    print(f'{len(table)} import slots -> {IMPORT_TABLE}, {IMAGE_IMPORTS}\n{functions} functions -> {GAMEAPI}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
