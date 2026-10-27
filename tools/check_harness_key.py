"""Checks that game.exe's side of the harness names no class of ours (ticket H30b).

The reference cache keeps runs of the original code by the files game.exe's side of the harness builds from
(tools/run_all.py, harness_files): the harness reads game.exe's objects through its own views
(tests/verify/exe_objects.h) and reaches our code only through tests/verify/image (image/image_side.h), so our
classes may change without playing the original's runs again. This fails when one of those files includes a file of
src/ other than the hook framework (src/hook) and the few below, e.g. a game header in a shared harness file.

usage: python tools/check_harness_key.py     (exits 1 on any problem)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run_all  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# What of src/ game.exe's side may build from, besides src/hook.
ALLOWED = {
    'src/game/game_data.h',              # GameData / GameAddress (the shift is 0 in game.exe)
    'src/lib/gameapi.h',                 # game.exe's import slots (hook/paths.cpp)
    'src/lib/import_table.h',            # ... their table (gameapi.h)
    'src/lib/import_prototypes.h',       # ... its members' types where the SDK has none (import_table.h)
    'src/lib/rebirth.cpp',               # the Rebirth flags (HARNESS_SHARED)
    'src/lib/stlp_algo.h',               # STLport's sort, as game.exe's (harness_address_order.cpp)
    'src/standalone/image_data.cpp',     # GameData's shift (HARNESS_SHARED)
    'src/standalone/image_cells.inc',    # ... its cells
}


def main():
    files = run_all.harness_files(ROOT)
    bad = sorted(name for name in files
                 if name.startswith('src/') and not name.startswith('src/hook/') and name not in ALLOWED)
    for name in bad:
        print(f'check_harness_key: {name} is in the reference key: a file of game.exe\'s side of the harness includes it '
              '(read game.exe\'s objects through tests/verify/exe_objects.h, ours through tests/verify/image)')
    print(f'check_harness_key: {len(files)} files in the reference key, {len(bad)} problem(s)')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
