"""Where the tools find game.exe (S18e3): one lookup for every tool that reads it.

  1. an explicit path (the tool's --exe, or the EI_EXE environment variable),
  2. else run_all.run_dir(root)'s vanilla\\game.exe, then rebirth\\game.exe. run_dir is the root's run\\ folder, or
     the main checkout's when the worktree has no junction of its own.

Vanilla's and Rebirth's game.exe 1.06 differ in one code byte (a Rebirth fix); the data the tools read is the same.
The lookup never writes and never reads F:\\games. Nothing found: one error naming everything looked for.

usage: python tools/exe_path.py [--exe PATH] [--all]   prints the path (--all: every install's, one per line)
(CMakeLists.txt asks it for the image's EI_GAME_EXE when none is given.)
"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INSTALLS = ('vanilla', 'rebirth')   # in this order


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
        return [find_exe(p, root) for p in explicit]
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
