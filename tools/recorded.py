"""Recorded play: real play turned into a scenario with a golden (docs/notes/harness.md, "Recorded play";
tests/verify/harness_record.h).

    python tools\\recorded.py play <name> [rebirth|vanilla]
        Play a session yourself (harness.cmd record <name> builds the verify build first): the verify build on
        tests/scenarios/record/live.txt in a window shown and activated, your keyboard and mouse recorded. Close the
        window (or quit from the menu) to end it; the recording becomes tests/scenarios/rec_<name>.txt. A session that
        runs past 3000 frames (live.txt) is cut by the game into parts, each from a save of its own: rec_<name>_1.txt,
        rec_<name>_2.txt, ... (harness_record.h, "Parts").
    python tools\\recorded.py take <run folder> <name> [--force]
        A run's recording (its recorded.txt, and recorded_2.txt, ... for each cut; from a scenario with "recordinput")
        as tests/scenarios/rec_<name>.txt, or rec_<name>_<n>.txt for each part.

A recorded scenario replays like any other (python tools\\work.py test <ID> rec_<name> --golden) and gets its golden as
any other does (docs/notes/standalone-verification.md, "Recording goldens"). A later part's first lines prepare the
save of the part before (tools/recorded_save.py plays that part once per build and keeps the save).
"""
import argparse
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

LIVE_START = os.path.join(ROOT, 'tests', 'scenarios', 'record', 'live.txt')
# Live play's frames are 16 ms of game time each (live.txt's frametime): about a minute for 3750 of them. A part that
# is longer may not replay within a run's 60 s.
LONG_FRAMES = 3750


def scenario_path(name, root=ROOT):
    return os.path.join(root, 'tests', 'scenarios', f'rec_{name}.txt')


def parts_of(run):
    """The files of a run's recording: recorded.txt, then recorded_2.txt, ... for each cut."""
    source = os.path.join(run, 'recorded.txt')
    if not os.path.exists(source):
        sys.exit(f'recorded: {source} not found (the scenario needs a "recordinput" line)')
    found = [source]
    while os.path.exists(os.path.join(run, f'recorded_{len(found) + 1}.txt')):
        found.append(os.path.join(run, f'recorded_{len(found) + 1}.txt'))
    return found


def part_name(name, part, parts):
    """The name of a recording's part (without rec_): the name itself when it was not cut, else <name>_<part>."""
    return name if parts == 1 else f'{name}_{part}'


def take(run, name, force=False, root=ROOT):
    """Copies the run's recording to tests/scenarios/rec_<name>.txt, or when it was cut to rec_<name>_1.txt,
    rec_<name>_2.txt, ... (each later part prepares its save from the part before: {previous} is that scenario's name).
    Returns the first scenario's path."""
    sources = parts_of(run)
    targets = [scenario_path(part_name(name, n, len(sources)), root) for n in range(1, len(sources) + 1)]
    for target in targets:
        if os.path.exists(target) and not force:
            sys.exit(f'recorded: {target} exists (--force replaces it)')
    for n, (source, target) in enumerate(zip(sources, targets), 1):
        with open(source, encoding='utf-8') as f:
            text = f.read()
        if n > 1:
            text = text.replace('{previous}', 'rec_' + part_name(name, n - 1, len(sources)))
        with open(target, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
        lines = text.splitlines()
        events = sum(1 for line in lines if line.startswith('at '))
        last = next((line.split()[1] for line in reversed(lines) if line.startswith('at ')), '0')
        print(f'recorded: {os.path.relpath(target, root)}: {events} lines up to frame {last}')
        if int(last) > LONG_FRAMES:
            print(f'recorded: frame {last} is past about a minute of live play: its replay may not fit in a run\'s 60 s')
    return targets[0]


def play(name, data):
    """The live session through run_all's launch (the game's environment as a run's), then take."""
    import run_all
    if os.path.exists(scenario_path(name)) or os.path.exists(scenario_path(name + '_1')):
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
    first = take(out, name)
    shown = os.path.basename(first)[:-4]
    names = shown if not shown.endswith('_1') else f'{shown[:-1]}<n>'
    print(f'recorded: replay it with python tools\\work.py test <ID> {names} --golden')


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
