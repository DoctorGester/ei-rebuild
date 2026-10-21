r"""Prints the folder the image runs from on an install ('vanilla' or 'rebirth'): run\<install>-clean, made or brought up to
date first (run_all.clean_run_dir: run\<install>'s files but dgVoodoo's, hard links). The launch scripts call it so a
played session takes the DirectX check's native path as the harness's image runs do (SV10f, SV10g).
usage: python tools/clean_run.py vanilla|rebirth"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run_all


def main(argv):
    if len(argv) != 2 or argv[1] not in ('vanilla', 'rebirth'):
        print(__doc__, file=sys.stderr)
        return 2
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    clean = run_all.clean_run_dir(root, argv[1])
    if not os.path.isfile(os.path.join(clean, 'game.exe')):
        print(f'clean_run: game.exe is missing in {clean} (run tools/setup_{argv[1]}_run.cmd)', file=sys.stderr)
        return 1
    print(os.path.normpath(clean))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
