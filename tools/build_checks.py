"""The checks build.cmd runs after the build, all at once: each reads the sources or the build and writes
nothing, so they run side by side; their output comes in the order below. Run alone they took about 9 s one after
another, together about as long as the slowest.

usage: python tools/build_checks.py BUILD_DIR [--times]   (exit code 1 when any check fails; --times: each one's seconds)
"""
import os, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def checks(out):
    py = [sys.executable]
    tool = lambda name: os.path.join(ROOT, 'tools', name)
    return [
        # Cleanup-plan patterns (offsets, VCall, __asm, raw addresses) may only go down per file.
        py + [tool('check_cleanup.py')],
        # Addresses and ticket ids in comments may only go down per file (docs/comment-guide.md).
        py + [tool('comments.py'), 'check'],
        # Game data by address only through the data seam (src/game/game_data.h, the harness's OriginalGameData):
        # no raw use.
        py + [tool('data_seam.py'), '--check'],
        # game.exe's side of the harness reads game.exe's objects through its own views, ours only through
        # tests/verify/image: no class of ours in the reference cache's key (tools/run_all.py, harness_files).
        py + [tool('check_harness_key.py')],
        # Generated and listed files against what they come from, without game.exe: the import files (gameapi.h by
        # hand, a harness route on a slot), the named ranges' csv and their rules, the retired vtables.
        py + [tool('gen_iat.py'), '--check'],
        py + [tool('named_data.py'), '--check'],
        py + [tool('retired_vtables.py'), '--check'],
        # Every row of config/static_init_ports.csv names a definition in its file (tools/gen_static_init.py).
        py + [tool('gen_static_init.py'), '--check-ports'],
        # The game's code calls Windows only through src/platform: the files that still do may only leave
        # config/platform_files.txt (tools/check_platform.py).
        py + [tool('check_platform.py')],
        # Function vectors recorded from the original, replayed against our code without the game (tests/vectors).
        [os.path.join(out, 'ei_vectors.exe')],
        # The interface canvases (src/render/render_ui) on a WARP device against CPU rules and GDI (tools/ui_check).
        [os.path.join(out, 'ei_ui_check.exe')],
        # The performance overlay (src/render/render_perf) on a WARP device: text, pixels, GPU timestamps
        # (tools/perf_check).
        [os.path.join(out, 'ei_perf_check.exe')],
    ]


def run(cmd):
    start = time.perf_counter()
    try:
        r = subprocess.run(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    except OSError as e:
        return 1, f'{cmd[0]}: {e}\n'.encode(), time.perf_counter() - start
    return r.returncode, r.stdout, time.perf_counter() - start


def main(argv):
    args = [a for a in argv[1:] if a != '--times']
    if len(args) != 1:
        print(__doc__)
        return 2
    out = os.path.abspath(args[0])
    cmds = checks(out)
    with ThreadPoolExecutor(len(cmds)) as pool:
        results = list(pool.map(run, cmds))
    failed = []
    for cmd, (code, text, seconds) in zip(cmds, results):
        sys.stdout.buffer.write(text)
        sys.stdout.flush()
        name = os.path.basename(cmd[1] if cmd[0] == sys.executable else cmd[0])
        if '--times' in argv:
            print(f'build_checks: {name} {seconds:.1f} s')
        if code:
            failed.append(name)
    if failed:
        print(f'build_checks: failed: {", ".join(failed)}')
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
