"""game.exe's imports, from its import directory (docs/notes/standalone.md, "Own import table"):

  src/lib/import_table.h             game.exe's import address table as a struct (ImportTable, one member per
                                     slot and terminator) and Imports(), the table calls go through: in game.exe
                                     its own import address table (src/hook/original_data.cpp), in the image a
                                     table of its own (src/standalone/image_imports.cpp).
                                     A member's type is its function's: the Windows SDK's, or for an import the
                                     SDK does not declare the prototype of src/lib/import_prototypes.h (read, not
                                     written: a `using` there per such import, by its member's name).
  src/lib/gameapi.h                  the imports our code calls (`gameapi::X` in src/ or tests/: called()), without
                                     the SDK's headers: an import whose slot can hold something other than the
                                     export in the image (routed(): the harness's slot routes, the image's routes, a
                                     slot our code takes) and every import of import_prototypes.h (no import
                                     library; its `using` copied) through Imports(), every other one as the Windows
                                     function itself, which src/lib/win_api.h declares (read, not written). A slot
                                     is a reference declared there and bound in src/lib/gameapi.cpp (one
                                     initialiser, not one in every file that includes the header).
  src/lib/gameapi.cpp                gameapi.h's references, bound before every other static constructor
                                     (init_seg(lib)) and after the image resolves its table (init_seg(compiler)). It sees
                                     ImportTable's types and gameapi.h's declarations together: the compiler checks
                                     win_api.h's and the copied prototypes against the SDK's and import_prototypes.h's.
  src/standalone/image_imports.inc   every import of game.exe with its ImportTable member, DLL and function (or
                                     ordinal) and whether a call reaches its slot (reached()): the standalone
                                     image's own import table, which the image resolves into its table
                                     (src/standalone/image_imports.cpp, S18f); a slot no call reaches is a trap
                                     and a DLL with none of them does not load (S22). The unreached ones stay listed
                                     (names only, no prototype) so that a trap that fires names its import.

usage: python tools/gen_iat.py [--check | --counts]
  (no option)  write the four files (reads game.exe's import directory)
  --counts     print per DLL its slots, the reached ones (of them taken by the image's UTF-8 routes, reached from the
               harness only) and the traps, from the committed files (docs/notes/standalone.md, "Own import table")
  --check      exit 1 when a file differs from what the committed import table (import_table.h: a member per slot, its
               DLL and function in the comment) and the routes give; no game.exe (build.cmd). Catches gameapi.h / .cpp edited
               by hand, a new harness route on a slot, a gameapi:: call of an import gameapi.h does not have yet (or no
               longer one of one it has), and image_imports.inc and import_table.h that disagree.
  Both fail when a route of game.exe's runs only (ORIGINAL) names a slot our code reaches: the image needs that route.
"""
import argparse, os, re, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import exe_path  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAMEAPI = os.path.join(ROOT, 'src', 'lib', 'gameapi.h')
GAMEAPI_CPP = os.path.join(ROOT, 'src', 'lib', 'gameapi.cpp')
IMAGE_IMPORTS = os.path.join(ROOT, 'src', 'standalone', 'image_imports.inc')
IMPORT_TABLE = os.path.join(ROOT, 'src', 'lib', 'import_table.h')
PROTOTYPES = os.path.join(ROOT, 'src', 'lib', 'import_prototypes.h')
WIN_API = os.path.join(ROOT, 'src', 'lib', 'win_api.h')
DLLS = {'KERNEL32.dll', 'USER32.dll', 'GDI32.dll', 'ADVAPI32.dll', 'WINMM.dll', 'SHELL32.dll', 'ole32.dll', 'DSOUND.dll'}
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
# A slot named only for game.exe's runs: a call of a function with "Original" in its name whose first argument is
# `&ImportTable::X` (the harness's PatchOriginalImport / TraceOriginalSlot, import_table.h's OriginalImport). In the
# image those do nothing, so the slot is neither a harness route nor reached.
ORIGINAL = re.compile(r'\b\w*Original\w*\s*(?:<[^<>()]*>)?\(\s*&ImportTable::(\w+)')
# game.exe's import address table (its IAT directory), the first 0x820 bytes of .rdata.
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


def prototypes():
    """The imports src/lib/import_prototypes.h declares: the names of its `using`s in ei::import_fn."""
    return set(prototype_text()) - {''}


def prototype_text():
    """{name: its `using` statement} of import_prototypes.h's ei::import_fn, and under '' what they need before them
    (the Miles and Bink handles: the text between its includes and that namespace)."""
    text = read(PROTOTYPES)
    start = text.index('namespace ei::import_fn {')
    out = {name: statement for statement, name in re.findall(r'^(using (\w+) = [^;]*;)', text[start:], re.M)}
    out[''] = text[text.index('#include <cstdint>') + len('#include <cstdint>'):start].strip()
    return out


def win_api():
    """The Windows functions src/lib/win_api.h declares."""
    return set(re.findall(r'\bWINAPI (\w+)\(', code(read(WIN_API))))


def member_type(dll, fn, name, declared):
    if name in declared:
        return f'import_fn::{name}'
    if dll in DLLS:
        return f'decltype(&::{fn or name})'
    sys.exit(f'gen_iat: no prototype for {dll}!{name}: add it to src/lib/import_prototypes.h')


def committed_table():
    """[(slot, dll, function or None, ordinal)] in the import directory's order, read back from the committed files
    (which is what imports() gave when they were written): the order, DLL, function and ordinal from
    src/standalone/image_imports.inc, the slot from the position of the entry's member in src/lib/import_table.h
    (a member per dword of the import address table, in address order)."""
    text = read(IMPORT_TABLE)
    body = text[text.index('struct ImportTable {') + len('struct ImportTable {'):]
    body = body[:body.index('\n};')]
    slots = {}
    for index, line in enumerate(l for l in body.split('\n') if l.strip()):
        slots[re.fullmatch(r'    .+ (\w+);   // .*', line).group(1)] = IAT_BEGIN + 4 * index
    entry = re.compile(r'\{offsetof\(ei::ImportTable, (\w+)\), "([^"]+)", (?:"([^"]+)"|nullptr), (\d+)(?:, \w+)?\},')
    return [(slots[name], dll, fn or None, int(ordinal)) for name, dll, fn, ordinal in entry.findall(read(IMAGE_IMPORTS))]


def counts(table):
    """Prints per DLL (the import directory's order) its slots, the reached ones, of them the UTF-8 routes and the
    harness-only ones, and the traps, as a Markdown table."""
    reach, routes = reached(table), text_routes()
    rows = {}
    for _, dll, fn, ordinal in table:
        row = rows.setdefault(dll, [0, 0, 0, 0, 0])
        name = member(dll, fn, ordinal)
        row[0] += 1
        if name in reach:
            row[1] += 1
            row[2] += fn in routes
            row[3] += reach[name] == {'tests'}
        else:
            row[4] += 1
    print('| DLL | slots | reached | UTF-8 route | harness only | traps |\n|---|---|---|---|---|---|')
    total = [sum(r[i] for r in rows.values()) for i in range(5)]
    for dll, row in list(rows.items()) + [('all', total)]:
        print(f'| {dll} | ' + ' | '.join(str(n) for n in row) + ' |')
    return 0


def layout(table):
    """[(member, type, dll, fn, ordinal)] for every dword of the import address table, in address order: each
    DLL's slots, then its zero terminator (type None)."""
    by_slot = {slot: (dll, fn, ordinal) for slot, dll, fn, ordinal in table}
    declared = prototypes()
    out = []
    last = None
    for slot in range(IAT_BEGIN, IAT_END, 4):
        if slot in by_slot:
            dll, fn, ordinal = last = by_slot[slot]
            name = member(dll, fn, ordinal)
            out.append((name, member_type(dll, fn, name, declared), dll, fn, ordinal))
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
    "Own import table"): the harness's slot routes (any `ImportTable::X` tests/ names but ORIGINAL's), the image's
    routes (the UTF-8 routes of platform/win32/system_text.cpp) and a slot our code takes (`&gameapi::X`)."""
    names = {member(dll, fn, ordinal): fn for _, dll, fn, ordinal in table}
    out = set()
    for path in sources('tests', ('.cpp', '.h', '.inc')):
        for name in re.findall(r'\bImportTable::(\w+)', ORIGINAL.sub('', read(path))):
            if names.get(name):
                out.add(names[name])
    out.update(text_routes())
    for path in sources('src', ('.cpp', '.h')):
        out.update(re.findall(r'&gameapi::(\w+)', read(path)))
    return out


def text_routes():
    """The imports the image's UTF-8 routes take (platform/win32/system_text.cpp's TEXT_ROUTE list)."""
    return set(re.findall(r'TEXT_ROUTE\((\w+)\),', read(os.path.join(ROOT, 'src', 'platform', 'win32', 'system_text.cpp'))))


def code(text):
    """C++ source without its comments."""
    return re.sub(r'//[^\n]*', '', re.sub(r'/\*.*?\*/', ' ', text, flags=re.S))


def called(table):
    """The imports our code or the harness calls as `gameapi::X` (comments left out): what gameapi.h declares."""
    members = {member(dll, fn, ordinal) for _, dll, fn, ordinal in table}
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
    the Windows function itself or not at all, so the image leaves its slot a trap. ORIGINAL's names are game.exe's
    runs only: left out."""
    members = {member(dll, fn, ordinal) for _, dll, fn, ordinal in table}
    slots = slot_calls(table)
    skip = {os.path.normcase(p) for p in DEFINITIONS}
    out = {}
    for folder in ('src', 'tests'):
        for path in sources(folder, ('.cpp', '.h', '.inc')):
            if os.path.normcase(path) in skip:
                continue
            text = ORIGINAL.sub('', code(read(path)))
            names = re.findall(r'\bImportTable::(\w+)', text) + re.findall(r'\bImports\(\)\.(\w+)', text)
            names += [n for n in re.findall(r'\bgameapi::(\w+)', text) if n in slots]
            for name in names:
                if name in members:
                    out.setdefault(name, set()).add(folder)
    return out


def original_only(table):
    """Errors: the slots a route of game.exe's runs only (ORIGINAL) names that our code reaches in the image."""
    reach = reached(table)
    out = []
    for folder in ('src', 'tests'):
        for path in sources(folder, ('.cpp', '.h', '.inc')):
            for name in ORIGINAL.findall(code(read(path))):
                if 'src' in reach.get(name, ()):
                    out.append(f'gen_iat: {os.path.relpath(path, ROOT)} routes {name} in game.exe only, but our code '
                               'calls through its slot: route it in the image too (PatchImport)')
    return out


def import_table_text(table):
    lines = []
    for name, type_, dll, fn, ordinal in layout(table):
        if type_ is None:
            lines.append(f'    void* {name};   // zero: the end of {dll}\'s slots')
        else:
            what = f'{dll}!{fn}' if fn else f'{dll}!#{ordinal}'
            lines.append(f'    {type_} {name};   // {what}')
    return ('#pragma once\n\n// Generated by tools/gen_iat.py from game.exe\'s import directory. Do not edit.\n'
            '// game.exe\'s import address table as a struct: one member per slot, in address order (each DLL\'s\n'
            '// slots, then its zero terminator). Imports() is the table calls go through: in game.exe its own import\n'
            '// address table (src/hook/original_data.cpp), in the image a table of its own\n'
            '// (src/standalone/image_imports.cpp, S18f).\n'
            '// A slot holds the export, a route of the image (its UTF-8 version) or the harness\'s wrapper of\n'
            '// either; in the image a slot no call reaches is a trap (image_imports.inc).\n'
            '// A member\'s type is its function\'s: the Windows SDK\'s, or lib/import_prototypes.h\'s for an import\n'
            '// the SDK does not declare. Every member is a pointer-sized slot (8 bytes on x64).\n\n'
            '#include <windows.h>\n#include <mmsystem.h>\n#include <shellapi.h>\n#include <objbase.h>\n#include <dsound.h>\n\n'
            '#include <cstdint>\n\n#include "lib/import_prototypes.h"\n\n'
            '#undef GetObject\n\nnamespace ei {\n\n'
            'struct ImportTable {\n' + '\n'.join(lines) + '\n};\n'
            f'static_assert(sizeof(ImportTable) == 0x{(IAT_END - IAT_BEGIN) // 4:X} * sizeof(void*));   // game.exe\'s import '
            'address table\'s slots\n\n'
            '// The table calls go through: game.exe\'s own in game.exe, the image\'s own in the image.\n'
            'ImportTable& Imports();\n\n'
            '// game.exe\'s own slot of `member`, for what runs only there (hook/paths.cpp): tools/gen_iat.py leaves a\n'
            '// slot named through a function with "Original" in its name out of the image\'s reached ones (a trap there).\n'
            'template <class F>\nF& OriginalImport(F ImportTable::* member) {\n    return Imports().*member;\n}\n\n'
            '} // namespace ei\n')


def gameapi_text(table, routes, calls):
    """gameapi.h's text, gameapi.cpp's, and how many functions the header names: the imports of `calls`
    (called()), in the import directory's order."""
    lines, slots, copied, libs = [], [], [], set()
    prototype = prototype_text()
    declared = win_api()

    def slot(type_, name, dll):
        lines.append(f'extern {type_}& {name};  // {dll}, through the slot')
        slots.append(f'{type_}& {name} = ei::Imports().{name};')

    for _, dll, fn, ordinal in table:
        name = member(dll, fn, ordinal)
        if name not in calls:
            continue
        if name in prototype:
            copied.append(name)
            slot(f'ei::import_fn::{name}', name, dll)
        elif name not in declared:
            sys.exit(f'gen_iat: our code calls gameapi::{name} ({dll}): declare it in src/lib/win_api.h')
        elif fn in routes:
            slot(f'decltype(&::{name})', name, dll)
        else:
            lines.append(f'using ::{name};  // {dll}')
            libs.add(dll.split('.')[0].lower())
    text = ('#pragma once\n\n// Generated by tools/gen_iat.py. Do not edit.\n'
            '// The imports our code calls (gameapi::X), without the Windows SDK\'s headers. An import whose slot can\n'
            '// hold something other than the export in the image (the harness\'s slot routes, the image\'s UTF-8\n'
            '// routes, a slot our code takes; gen_iat.py\'s routed()) is called through the slot (lib/import_table.h):\n'
            '// a reference to it, bound in gameapi.cpp. So is every import the Windows SDK does not declare (no import\n'
            '// library), by its prototype of lib/import_prototypes.h, copied below. Every other one is the Windows\n'
            '// function itself, which lib/win_api.h declares.\n\n'
            '#include <cstdint>\n\n#include "lib/win_api.h"\n\n'
            + ''.join(f'#pragma comment(lib, "{lib}.lib")\n' for lib in sorted(libs)))
    if copied:   # in import_prototypes.h's order (one may name another: BinkSetSoundSystem, BinkOpenMiles)
        text += ('\n' + prototype[''] + '\n\nnamespace ei::import_fn {\n\n'
                 + '\n'.join(statement for name, statement in prototype.items() if name in copied)
                 + '\n\n} // namespace ei::import_fn\n')
    text += '\nnamespace gameapi {\n\n'
    cpp = ('// Generated by tools/gen_iat.py. Do not edit.\n'
           '// gameapi.h\'s references to the import slots, bound once here rather than by an initialiser in every\n'
           '// file that includes the header. init_seg(lib): after the image resolves its table\n'
           '// (src/standalone/image_imports.cpp, init_seg(compiler)), and before every other static constructor, which\n'
           '// may call through them. With ImportTable\'s SDK types in view, a declaration of lib/win_api.h or a\n'
           '// prototype gameapi.h copied that differs from the SDK\'s or import_prototypes.h\'s does not compile.\n'
           '#include "lib/gameapi.h"\n#include "lib/import_table.h"\n\n'
           '#pragma warning(disable : 4073)   // initialisers put in the library initialisation area: the point\n'
           '#pragma init_seg(lib)\n\nnamespace gameapi {\n\n')
    return (text + '\n'.join(lines) + '\n\n} // namespace gameapi\n',
            cpp + '\n'.join(slots) + '\n\n} // namespace gameapi\n', len(lines))


def image_imports_text(table, reach):
    lines = ['// Generated by tools/gen_iat.py from game.exe\'s import directory. Do not edit.',
             '// Every import of game.exe (its import address table): {its member of lib/import_table.h\'s ImportTable,',
             '// DLL, function (nullptr: by ordinal), ordinal, reached}, in the directory\'s order. The image resolves',
             '// the reached ones (a call of our code or the harness goes through the slot: gen_iat.py\'s reached())',
             '// into its own table and leaves the rest traps (src/standalone/image_imports.cpp, S18f, S22).']
    for slot, dll, fn, ordinal in table:
        name = f'"{fn}"' if fn else 'nullptr'
        slot_member = member(dll, fn, ordinal)
        flag = 'true' if slot_member in reach else 'false'
        lines.append(f'{{offsetof(ei::ImportTable, {slot_member}), "{dll}", {name}, {ordinal}, {flag}}},')
    return '\n'.join(lines) + '\n'


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument('--check', action='store_true')
    mode.add_argument('--counts', action='store_true')
    args = ap.parse_args()
    if args.counts:
        return counts(committed_table())
    table = committed_table() if args.check else imports(exe_path.resolve())
    errors = original_only(table)
    if errors:
        print('\n'.join(errors))
        return 1
    gameapi, gameapi_cpp, functions = gameapi_text(table, routed(table), called(table))
    outputs = [(IMPORT_TABLE, import_table_text(table)), (GAMEAPI, gameapi), (GAMEAPI_CPP, gameapi_cpp),
               (IMAGE_IMPORTS, image_imports_text(table, reached(table)))]
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
