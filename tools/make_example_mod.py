"""Makes the example mod of docs/modding.md: `ei-convert --example-mod`, the command a player runs (MF12).

usage: python tools/make_example_mod.py <folder> [--install <game folder>] [--convert <ei-convert>]
  <folder>     the mod is written to <folder>\\example_mod (a scenario's prepare line: the run's harness_saves, which
               the scenario selects with "mod harness_saves\\example_mod")
  --install    the game folder to read (read only); default the vanilla install (EI_GAME_VANILLA), as an unpacked or
               converted copy too (tools/unpack_install.py, convert_install.py)
  --convert    ei-convert (default: EI_BUILD_DIR's, else build\\verify's, else build\\release's); it reads the mod's own
               files from the examples folder beside it, which the build copies there (tools/convert/example_mod.h)
"""
import argparse, os, subprocess, sys

import exe_path


def ei_convert(path):
    if path:
        return path
    exe = 'ei-convert.exe' if os.name == 'nt' else 'ei-convert'
    builds = [os.environ.get('EI_BUILD_DIR')] + [os.path.join(exe_path.ROOT, 'build', b) for b in ('verify', 'release')]
    for build in builds:
        if build and os.path.isfile(os.path.join(build, exe)):
            return os.path.join(build, exe)
    sys.exit('make_example_mod: no ei-convert (build.cmd release builds build\\release\\ei-convert.exe)')


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('folder')
    parser.add_argument('--install', help='the game folder to read (default: the vanilla install)')
    parser.add_argument('--convert', help='ei-convert to run')
    args = parser.parse_args()
    install = args.install or exe_path.install_file('vanilla')
    done = subprocess.run([ei_convert(args.convert), '--example-mod', install, os.path.abspath(args.folder)],
                          capture_output=True, text=True)
    print(done.stdout, end='')
    if done.returncode != 0:
        sys.exit(f'make_example_mod: ei-convert --example-mod failed (exit code {done.returncode})\n{done.stderr}')


if __name__ == '__main__':
    main()
