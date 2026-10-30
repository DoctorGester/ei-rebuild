"""Where the tools find game.exe (S18e3): one lookup for every tool that reads it.

  1. an explicit path (the tool's --exe, or the EI_EXE environment variable),
  2. else run_all.run_dir(root)'s vanilla\\game.exe, then rebirth\\game.exe. run_dir is the root's run\\ folder, or
     the main checkout's when the worktree has no junction of its own.

Vanilla's and Rebirth's game.exe 1.06 differ in one code byte (a Rebirth fix); the data the tools read is the same.
The lookup never writes and never reads F:\\games. Nothing found: one error naming everything looked for.

usage: python tools/exe_path.py [--exe PATH] [--all]   prints the path (--all: every install's, one per line)
(The build reads no game.exe: C25c, C25d. The generators, find_* and check_signatures / check_stolen read it.)
"""
import os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INSTALLS = ('vanilla', 'rebirth')   # in this order
TIMESTAMP = 0x3ABF0DB3   # game.exe 1.06 (src/standalone/host.cpp's kExpectedTimestamp)


class ExeNotFound(Exception):
    pass


def install_exes(root=ROOT):
    """run\\vanilla\\game.exe and run\\rebirth\\game.exe of root (or of the main checkout), existing or not."""
    import run_all
    run = run_all.run_dir(root)
    return [os.path.join(run, name, 'game.exe') for name in INSTALLS]


def find_exe(explicit=None, root=ROOT):
    """The game.exe to read: `explicit` (or $EI_EXE), else the first of the installs'. ExeNotFound names what was looked for."""
    explicit = explicit or os.environ.get('EI_EXE')
    if explicit:
        if not os.path.isfile(explicit):
            raise ExeNotFound(f'game.exe: {explicit} (--exe / EI_EXE) is not a file')
        return explicit
    wanted = install_exes(root)
    for path in wanted:
        if os.path.isfile(path):
            return path
    raise ExeNotFound('game.exe not found: looked for ' + ' and '.join(wanted) + ' (run tools\\setup_vanilla_run.cmd, '
                      'or give --exe PATH or set EI_EXE)')


def find_exes(explicit=(), root=ROOT):
    """Every game.exe to read: the explicit paths, else all the installs' that exist (at least one)."""
    if explicit:
        return [find_exe(p, root) for p in explicit or [None]]
    found = [p for p in install_exes(root) if os.path.isfile(p)]
    if not found:
        find_exe(None, root)
    return found


def resolve(explicit=None, root=ROOT):
    """find_exe for a tool's main: the error ends the tool with the message."""
    try:
        return find_exe(explicit, root)
    except ExeNotFound as e:
        sys.exit(f'{os.path.basename(sys.argv[0])}: {e}')


def image(path):
    """game.exe 1.06 at `path` as the loader lays it out: a bytearray of the image's size, offset = address - 0x400000,
    each section's file bytes in place (the rest zero). Another exe ends the tool."""
    d = open(path, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3C)[0]
    if struct.unpack_from('<I', d, pe + 8)[0] != TIMESTAMP:
        sys.exit(f'{os.path.basename(sys.argv[0])}: {path} is not game.exe 1.06')
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


def main(argv):
    explicit = argv[argv.index('--exe') + 1] if '--exe' in argv else None
    try:
        paths = find_exes([explicit] if explicit else []) if '--all' in argv else [find_exe(explicit)]
    except ExeNotFound as e:
        print(f'exe_path: {e}', file=sys.stderr)
        return 1
    print('\n'.join(paths))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
