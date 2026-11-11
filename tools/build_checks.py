"""The checks build.cmd runs after the build, all at once: each reads the sources or the build and writes
nothing, so they run side by side; their output comes in the order below. Run alone they took about 9 s one after
another, together about as long as the slowest.

usage: python tools/build_checks.py BUILD_DIR [--times]   (exit code 1 when any check fails; --times: each one's
seconds)
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
        # Game data by address only as the harness's ExeTableId (tools/data_seam.py):
        # no raw use.
        py + [tool('data_seam.py'), '--check'],
        # Generated and listed files against what they come from, without game.exe: the named ranges' csv and their
        # rules, the retired vtables.
        py + [tool('named_data.py'), '--check'],
        py + [tool('retired_vtables.py'), '--check'],
        # The game's code calls Windows only through src/platform (tools/check_platform.py).
        py + [tool('check_platform.py')],
        # The engine (ei_engine) includes no game header beyond tools/engine_layers.txt's (tools/engine_layers.py).
        py + [tool('engine_layers.py')],
        # docs/settings.md against the settings catalog, and the catalog against the settings the code reads (C69).
        py + [tool('gen_settings_doc.py'), '--check'],
        # Checks of our code without the game: the settings file, lib::HashMap's order (tests/unit).
        [os.path.join(out, 'ei_unit_check.exe')],
        # The scripts of the samples and the scenarios' console lines: each text's Lua loaded and checked
        # (tools/convert/convert_scripts.cpp; docs/notes/lua-scripting.md, "Survey").
        [os.path.join(out, 'ei-convert.exe'), '--scripts', '--scenarios', os.path.join(ROOT, 'tests', 'scenarios'), '--quiet'],
        # The scripts' Lua bindings on a small world of their own (tools/convert/lua_check.cpp; docs/notes/
        # lua-scripting.md, "Bindings").
        [os.path.join(out, 'ei-convert.exe'), '--lua-check'],
        # The original particle types' sources against the runs recorded from their C++ emitters, tick by tick
        # (tools/convert/effect_check.cpp; docs/notes/effects.md, "Checks").
        [os.path.join(out, 'ei-convert.exe'), '--effect-check'],
        # The interface canvases (src/render/render_ui) on the GPU (Direct3D 12) against CPU rules and GDI (tools/ui_check).
        [os.path.join(out, 'ei_ui_check.exe')],
        # The SDL backend's keys against Windows' layouts (tools/input_check).
        [os.path.join(out, 'ei_input_check.exe')],
        # The performance overlay (src/render/render_perf) on the GPU (Direct3D 12): text, pixels, the frame's GPU time
        # (tools/perf_check).
        [os.path.join(out, 'ei_perf_check.exe')],
        # The HLSL programs made with their pipelines on Direct3D 12 and Vulkan (tools/shader_check; a driver without
        # a device is skipped).
        [os.path.join(out, 'ei_shader_check.exe')],
        # The renderer's SDL_GPU layer (src/render/gpu.cpp) on Direct3D 12 and Vulkan: resources, writes and downloads,
        # draws in order, the swap chain on a small window off the screen (tools/gpu_check).
        [os.path.join(out, 'ei_gpu_check.exe')],
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
