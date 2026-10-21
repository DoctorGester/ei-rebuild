"""game.exe's resources for ei_standalone.exe (S18j; docs/notes/standalone.md, "How it runs"): the leaves of game.exe's
.rsrc tree (icons, cursors, their groups, bitmaps, the string table, the version) written as a compiled resource file
(.res) that the linker builds into the host, so the game's LoadIconA / LoadStringA / FindResourceA ... on the host's
module find them. Generated from game.exe at build time (CMakeLists.txt, from EI_GAME_EXE), not committed.

Each leaf is copied as it is: the .res format keeps the PE tree's type, name and language per entry and the same data
(an RT_GROUP_ICON's entries name their RT_ICON ids, an RT_CURSOR starts with its hot spot, as in the PE file), so the
linker's tree is game.exe's again.

usage: python tools/gen_resources.py [--exe game.exe] --res OUT   write OUT
       python tools/gen_resources.py [--exe game.exe] --list      print the leaves
"""
import argparse, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import exe_path  # noqa: E402

TIMESTAMP = 0x3ABF0DB3   # game.exe 1.06 (src/standalone/host.cpp's kExpectedTimestamp)
TYPE_NAMES = {1: 'RT_CURSOR', 2: 'RT_BITMAP', 3: 'RT_ICON', 4: 'RT_MENU', 5: 'RT_DIALOG', 6: 'RT_STRING',
              9: 'RT_ACCELERATOR', 10: 'RT_RCDATA', 12: 'RT_GROUP_CURSOR', 14: 'RT_GROUP_ICON', 16: 'RT_VERSION',
              24: 'RT_MANIFEST'}


def leaves(exe):
    """game.exe's resources: (type, name, language, codepage, data) per leaf, in the tree's order; type and name are
    an int id or a str."""
    data = open(exe, 'rb').read()
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    if data[pe:pe + 4] != b'PE\0\0':
        sys.exit(f'{exe}: not a PE file')
    if struct.unpack_from('<I', data, pe + 8)[0] != TIMESTAMP:
        sys.exit(f'{exe}: not game.exe 1.06')
    sections = struct.unpack_from('<H', data, pe + 6)[0]
    optional = struct.unpack_from('<H', data, pe + 20)[0]
    rsrc_rva, rsrc_size = struct.unpack_from('<II', data, pe + 24 + 96 + 8 * 2)
    table = pe + 24 + optional
    raw = None
    for i in range(sections):
        _, vsize, va, rsize, rptr = struct.unpack_from('<8sIIII', data, table + 40 * i)
        if va <= rsrc_rva < va + max(vsize, rsize):
            raw = (va, rptr)
    if raw is None:
        sys.exit(f'{exe}: no resource section')

    def offset(rva):
        return raw[1] + rva - raw[0]
    root = offset(rsrc_rva)

    def name_of(entry_name):
        if entry_name & 0x80000000:
            at = root + (entry_name & 0x7FFFFFFF)
            length = struct.unpack_from('<H', data, at)[0]
            return data[at + 2:at + 2 + 2 * length].decode('utf-16-le')
        return entry_name

    def entries(directory):
        named, ids = struct.unpack_from('<HH', data, root + directory + 12)
        for i in range(named + ids):
            name, target = struct.unpack_from('<II', data, root + directory + 16 + 8 * i)
            yield name_of(name), target

    out = []
    for rtype, types in entries(0):
        for name, names in entries(types & 0x7FFFFFFF):
            for language, leaf in entries(names & 0x7FFFFFFF):
                rva, size, codepage, _ = struct.unpack_from('<IIII', data, root + leaf)
                out.append((rtype, name, language, codepage, data[offset(rva):offset(rva) + size]))
    return out


def res_id(value):
    """A .res header's type or name: 0xFFFF and the id, or the name in UTF-16 with its terminator."""
    if isinstance(value, int):
        return struct.pack('<HH', 0xFFFF, value)
    return (value.upper() + '\0').encode('utf-16-le')


def pad4(blob):
    return blob + b'\0' * (-len(blob) % 4)


def res_entry(rtype, name, language, data, flags):
    head = pad4(res_id(rtype) + res_id(name))
    tail = struct.pack('<IHHII', 0, flags, language, 0, 0)
    header_size = 8 + len(head) + len(tail)
    return pad4(struct.pack('<II', len(data), header_size) + head + tail + data)


def write_res(resources, path):
    # The empty first entry marks a 32-bit resource file.
    blob = res_entry(0, 0, 0, b'', 0)
    for rtype, name, language, _, data in resources:
        flags = 0x1010 if rtype in (1, 3) else 0x1030   # MOVEABLE | DISCARDABLE, | PURE for the rest (rc's)
        blob += res_entry(rtype, name, language, data, flags)
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    if os.path.exists(path) and open(path, 'rb').read() == blob:
        return
    with open(path, 'wb') as f:
        f.write(blob)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--exe')
    ap.add_argument('--res')
    ap.add_argument('--list', action='store_true')
    args = ap.parse_args()
    try:
        exe = exe_path.find_exe(args.exe)
    except exe_path.ExeNotFound as e:
        sys.exit(str(e))
    resources = leaves(exe)
    if args.list:
        for rtype, name, language, codepage, data in resources:
            print(f'{TYPE_NAMES.get(rtype, rtype)!s:16} {name!s:>12} lang {language:04X} cp {codepage} {len(data)} bytes')
    if args.res:
        write_res(resources, args.res)
    if not args.list and not args.res:
        ap.error('give --res OUT or --list')


if __name__ == '__main__':
    main()
