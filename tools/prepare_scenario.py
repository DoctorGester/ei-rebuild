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


def tools_of(scenario):
    """The tools folder of the scenario's own checkout (<root>/tests/scenarios/x.txt -> <root>/tools): a
    worker's batch runs from the main checkout's scheduler, with the worktree's scenarios and tools."""
    tools = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(scenario)))), 'tools')
    return tools if os.path.isdir(tools) else TOOLS


IN_SLOT = 'EI_PREPARE_IN_SLOT'


def prepare(scenario, saves, in_slot=False, environ=None):
    """Runs the scenario's prepare tools. in_slot: the caller holds a machine game slot for the run (its game has not
    started, so the slot is idle): a tool that plays a game (recorded_save.py) plays it in that slot, sets EI_PREPARE_IN_SLOT.
    environ: the tools' environment (default: this process's); a scheduler running a batch passes the batch's (H65)."""
    base = os.environ if environ is None else environ
    env = dict(base, **{IN_SLOT: '1'}) if in_slot else {k: v for k, v in base.items() if k != IN_SLOT}
    for line in open(scenario, encoding='utf-8', errors='replace'):
        if not line.startswith(PREFIX):
            continue
        tool, *args = line[len(PREFIX):].split()
        subprocess.run([sys.executable, os.path.join(tools_of(scenario), tool), saves, *args], check=True,
                       capture_output=True, env=env)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    prepare(sys.argv[1], sys.argv[2])
