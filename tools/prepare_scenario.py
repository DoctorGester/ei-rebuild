"""Runs a scenario's preparation steps before the game starts.

usage: python tools/prepare_scenario.py <scenario.txt> <saves dir>

A scenario line "# prepare: <tool> [args]" runs `python tools/<tool> <saves dir> [args]`, where
<saves dir> is the run's harness_saves folder (what the game sees as <game root>saves\\). Used by
harness.cmd (build\\verify\\harness_saves) and tools/run_all.py (each run's own folder, after it is
emptied). Example: magic_traps.txt writes traps.mob there with tools/make_trap_mob.py.
"""
import os, subprocess, sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
PREFIX = '# prepare:'


def prepare(scenario, saves):
    for line in open(scenario, encoding='utf-8', errors='replace'):
        if not line.startswith(PREFIX):
            continue
        tool, *args = line[len(PREFIX):].split()
        subprocess.run([sys.executable, os.path.join(TOOLS, tool), saves, *args], check=True,
                       capture_output=True)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    prepare(sys.argv[1], sys.argv[2])
