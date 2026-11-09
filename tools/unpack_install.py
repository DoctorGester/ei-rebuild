"""Unpacks an install's archives into the folders that stand over them (docs/notes/mods.md, "Loose files").

usage: python tools/unpack_install.py INSTALL TARGET [--folders-only] [--force]
  INSTALL         the game folder; only read
  TARGET          where the unpacked copy goes: every file of INSTALL (but its saves, mp, cache and logs) with each
                  archive (a .res, .mpr or .mq file that is one: Res\\textures.res, Maps\\zone1.mpr, Maps\\z3q1.mq, a
                  mod's starter\\Mods\\<mod>\\Res\\*.res, ...) replaced by the folder of its name holding its files
                  (Res\\textures\\, Maps\\zone1\\, an entry "z3q1\\quest.reg" as Maps\\z3q1\\z3q1\\quest.reg). The
                  copy plays as the install does (run_all.py --unpacked runs the suites on it).
  --folders-only  only the archives' folders, no other file (a modder's start)
  --force         an existing TARGET is removed first (refused when it holds a link); without it a TARGET made from
                  the same INSTALL (its unpacked.txt) is kept as it is, and any other TARGET is refused
Names in the archives are the asset code page's (1251); the files get them in Unicode. Each file keeps its entry's
time. Nothing is ever written into INSTALL.
"""
import argparse, mmap, os, shutil, struct, sys

RES_MAGIC = 0x019CE23C
RES_HEADER = struct.Struct('<IIII')       # magic, count, table offset, names size
RES_ENTRY = struct.Struct('<iIIIHI')      # next, size, offset, time, name length, name offset
ARCHIVE_TYPES = ('.res', '.mpr', '.mq')
LEFT_OUT = ('saves', 'mp', 'cache')       # the install's own folders a copy leaves out (top level)
STAMP = 'unpacked.txt'                    # in TARGET: the install it was made from


def entries(data):
    """The archive's entries in `data` as (name, offset, size, time), or None when it is not an archive."""
    if len(data) < RES_HEADER.size:
        return None
    magic, count, table, names_size = RES_HEADER.unpack_from(data, 0)
    names = table + count * RES_ENTRY.size
    if magic != RES_MAGIC or names + names_size > len(data):
        return None
    out = []
    for i in range(count):
        _, size, offset, time, name_length, name_offset = RES_ENTRY.unpack_from(data, table + i * RES_ENTRY.size)
        if offset + size > len(data) or name_offset + name_length > names_size:
            return None
        name = bytes(data[names + name_offset:names + name_offset + name_length]).split(b'\0')[0].decode('cp1251')
        out.append((name, offset, size, time))
    return out


def unpack(path, folder):
    """The archive at path into folder; False (nothing written) when it is not an archive. Returns the file count."""
    with open(path, 'rb') as f:
        if os.fstat(f.fileno()).st_size == 0:
            return False
        with mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as data:
            found = entries(data)
            if found is None:
                return False
            os.makedirs(folder)
            for name, offset, size, time in found:
                parts = name.split('\\')
                if not name or any(p in ('', '.', '..') or ':' in p or '/' in p for p in parts):
                    sys.exit(f'unpack_install: {path}: an entry name no file can have: {name!r}')
                target = os.path.join(folder, *parts)
                if os.path.exists(target):
                    sys.exit(f'unpack_install: {path}: two entries for {target}')
                os.makedirs(os.path.dirname(target), exist_ok=True)
                with open(target, 'wb') as out:
                    out.write(data[offset:offset + size])
                if time > 0:
                    os.utime(target, (time, time))
            return len(found)


def inside(a, b):
    return os.path.normcase(os.path.abspath(a) + os.sep).startswith(os.path.normcase(os.path.abspath(b) + os.sep))


def remove_tree(path):
    """Removes a copy this tool made; refuses one holding a link (never through a junction into an install)."""
    for folder, dirs, files in os.walk(path):
        for name in dirs + files:
            p = os.path.join(folder, name)
            if os.path.islink(p) or (hasattr(os.path, 'isjunction') and os.path.isjunction(p)):
                sys.exit(f'unpack_install: {p} is a link; remove it by hand first')
    shutil.rmtree(path)


def make(install, target, folders_only=False, force=False, say=print):
    """TARGET made from INSTALL (see the module's text); True when it was made, False when it was there already."""
    install, target = os.path.abspath(install), os.path.abspath(target)
    if inside(install, target) or inside(target, install):
        sys.exit('unpack_install: the target and the install must not hold each other')
    stamp = os.path.join(target, STAMP)
    made_from = f'{install}\n{"folders only" if folders_only else "whole"}\n'
    if os.path.exists(target):
        if not force:
            if os.path.exists(stamp) and open(stamp, encoding='utf-8').read() == made_from:
                return False
            sys.exit(f'unpack_install: {target} is there and not made from {install} (--force replaces it)')
        remove_tree(target)
    say(f'unpack_install: {install} -> {target}')
    archives = files = 0
    for folder, dirs, names in os.walk(install):
        relative = os.path.relpath(folder, install)
        if relative == '.':
            dirs[:] = [d for d in dirs if d.lower() not in LEFT_OUT]
        dirs[:] = [d for d in dirs if not os.path.islink(os.path.join(folder, d))]
        out = os.path.join(target, relative)
        os.makedirs(out, exist_ok=True)
        for name in names:
            source = os.path.join(folder, name)
            stem, ext = os.path.splitext(name)
            if ext.lower() in ARCHIVE_TYPES:
                if os.path.isdir(os.path.join(folder, stem)):
                    sys.exit(f'unpack_install: {source} has a folder of its name in the install already')
                count = unpack(source, os.path.join(out, stem))
                if count is not False:
                    archives += 1
                    files += count
                    continue
            if folders_only or ext.lower() == '.log':
                continue
            shutil.copy2(source, os.path.join(out, name))
    with open(stamp, 'w', encoding='utf-8') as f:
        f.write(made_from)
    say(f'unpack_install: {archives} archives, {files} files')
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('install')
    parser.add_argument('target')
    parser.add_argument('--folders-only', action='store_true')
    parser.add_argument('--force', action='store_true')
    args = parser.parse_args()
    if not make(args.install, args.target, args.folders_only, args.force):
        print(f'unpack_install: {args.target} is made from {args.install} already (--force makes it again)')


if __name__ == '__main__':
    main()
