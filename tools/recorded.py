"""Recorded play: real play turned into a scenario with a golden (docs/notes/harness.md, "Recorded play";
tests/verify/harness_record.h).

    python tools\\recorded.py play <name> [rebirth|vanilla]
        Play a session yourself (play.cmd record <name> builds the verify build first): the verify build on
        tests/scenarios/record/live.txt in a window shown and activated, your keyboard and mouse recorded. Close the
        window (or quit from the menu) to end it; the recording becomes tests/scenarios/rec_<name>.txt.
    python tools\\recorded.py take <run folder> <name> [--force]
        A run's recording (its recorded.txt, from a scenario with "recordinput") as tests/scenarios/rec_<name>.txt.

A recorded scenario replays like any other (python tools\\work.py test <ID> rec_<name> --golden) and gets its golden as
any other does (docs/notes/standalone-verification.md, "Recording goldens").
"""
import argparse
import os
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

LIVE_START = os.path.join(ROOT, 'tests', 'scenarios', 'record', 'live.txt')
# Live play's frames are 16 ms of game time each (live.txt's frametime): about a minute.
LONG_FRAMES = 3750


def scenario_path(name, root=ROOT):
    return os.path.join(root, 'tests', 'scenarios', f'rec_{name}.txt')


def take(run, name, force=False, root=ROOT):
    """Copies <run>/recorded.txt to tests/scenarios/rec_<name>.txt; returns the scenario's path."""
    source = os.path.join(run, 'recorded.txt')
    if not os.path.exists(source):
        sys.exit(f'recorded: {source} not found (the scenario needs a "recordinput" line)')
    target = scenario_path(name, root)
    if os.path.exists(target) and not force:
        sys.exit(f'recorded: {target} exists (--force replaces it)')
    with open(source, encoding='utf-8') as f:
        lines = f.read().splitlines()
    events = sum(1 for line in lines if line.startswith('at '))
    last = next((line.split()[1] for line in reversed(lines) if line.startswith('at ')), '0')
    shutil.copyfile(source, target)
    print(f'recorded: {os.path.relpath(target, root)}: {events} lines up to frame {last}')
    if int(last) > LONG_FRAMES:
        print(f'recorded: frame {last} is past about a minute of live play: its replay may not fit in a run\'s 60 s')
    return target


def play(name, data):
    """The live session through run_all's launch (the game's environment as a run's), then take."""
    import run_all
    if os.path.exists(scenario_path(name)):
        sys.exit(f'recorded: {scenario_path(name)} exists: choose another name')
    exe = os.path.join(run_all.BUILD, run_all.EXE)
    if not os.path.exists(exe):
        sys.exit(f'recorded: {exe} not found (build.cmd verify)')
    out = os.path.join(run_all.BUILD, 'runs', f'record_{name}-{data}')
    if run_all.empty_folder(out):
        sys.exit(f'recorded: {out} cannot be emptied (a game still open?)')
    env = dict(os.environ, EI_HARNESS_SHOW_WINDOW='1')
    print(f'recorded: playing on the {data} data; close the window to end the recording')
    code, _ = run_all.launch(LIVE_START, out, data, run_all.BUILD, env)
    print(f'recorded: the game ended (run_all code {code}; {os.path.relpath(out, ROOT)})')
    take(out, name)
    print(f'recorded: replay it with python tools\\work.py test <ID> rec_{name} --golden')


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='command', required=True)
    p = sub.add_parser('play')
    p.add_argument('name')
    p.add_argument('data', nargs='?', default='vanilla', choices=['vanilla', 'rebirth'])
    t = sub.add_parser('take')
    t.add_argument('run')
    t.add_argument('name')
    t.add_argument('--force', action='store_true')
    args = ap.parse_args(argv)
    if args.command == 'play':
        play(args.name, args.data)
    else:
        take(args.run, args.name, args.force)


if __name__ == '__main__':
    main()
