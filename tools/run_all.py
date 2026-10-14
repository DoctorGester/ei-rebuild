"""Runs every harness scenario on the vanilla install and under Rebirth and prints a summary.

usage: python tools/run_all.py [scenario names...] [--vanilla-only | --rebirth-only | --all-rebirth | --native] [--no-hooks]
                               [--jobs N] [--retries N] [--verified-candidates N]
       python tools/run_all.py --widescreen [scenario names...]
  scenario names   e.g. combat items (default: every tests/scenarios/*.txt)
                   Every scenario runs on vanilla; under Rebirth only those config/rebirth_scenarios.csv
                   marks (tools/rebirth_coverage.py: the fewest that reach every function Rebirth patches
                   twice) and any it does not list yet. Named scenarios and --all-rebirth run under both.
  --native         only the install without dgVoodoo (run\\native, tools/setup_native_run.cmd), with
                   EI_RENDER=new,present: the game has no DirectDraw or Direct3D 7 device and the new
                   renderer presents through its own swap chain (docs/notes/renderer.md section 9)
  --widescreen     Rebirth's WideScreen against our rewrite of it (docs/notes/rebirth.md, "WideScreen"):
                   the scenarios (default ui_widescreen and ui_widescreen_more; they set the wide mode with the harness's
                   "resolution" / "widescreen" steps) under Rebirth twice, with our hooks
                   (<scenario>-rebirth) and without (<scenario>-rebirth-nohooks: addon's code only),
                   then tools/compare_frames.py on the two runs' frames
  --jobs N         games running at once (default 16); all runs on the machine together never
                   exceed EI_HARNESS_SLOTS (default 16) games, whatever --jobs says. Each run writes to its own folder,
                   build\\verify\\runs\\<scenario>-<install> (ei_hook.log, harness_report.txt and
                   harness_saves, emptied first, so every run starts without saves or texture cache).
  --retries N      re-run a failed scenario up to N times (default 1); fights against spawned
                   enemies are random, and the hero occasionally loses one on the original too.
                   A pass after a retry is reported as "PASS*".
  --verified-candidates N
                   afterwards, list rewritten functions with at least N verify calls and no
                   mismatch in any run that are not yet in tests/verify/verified.txt
exit code: 0 when every run passed
"""
import argparse, collections, concurrent.futures, contextlib, glob, json, msvcrt, os, re, shutil, subprocess, sys, tempfile, threading, time

import prepare_scenario

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.environ.get('EI_BUILD_DIR') or os.path.join(ROOT, 'build', 'verify')   # EI_BUILD_DIR: another verify build (timing runs)
# Runs go to <BUILD>/runs. <BUILD>/run_times.json keeps each run's seconds from the last suite: the
# longest start first, so the last games to finish are short ones (alphabetical order left a 50 s run
# for the end). run(), plan() and summary() take root/build so tools/work_sched.py can point them at
# another checkout.

# Games running at once on this machine, over every checkout and every run_all (2026-10-14: three
# workers' suites, 24 games of ~640 MB video memory each rendering without a frame limit, hung the
# NVIDIA driver until Windows crashed twice). A slot is a byte lock on a file in the temp folder;
# Windows drops it when the process ends, so a killed run never leaves a slot taken.
MACHINE_SLOTS = int(os.environ.get('EI_HARNESS_SLOTS', '16'))
SLOT_DIR = os.path.join(tempfile.gettempdir(), 'ei_harness_slots')


def _lock(path):
    """Takes the one-byte lock on path without waiting; the open file, or None if another holds it."""
    f = open(path, 'a+b')
    try:
        msvcrt.locking(f.fileno(), msvcrt.LK_NBLCK, 1)
        return f
    except OSError:
        f.close()
        return None


def _unlock(f):
    f.seek(0)
    msvcrt.locking(f.fileno(), msvcrt.LK_UNLCK, 1)
    f.close()


@contextlib.contextmanager
def machine_slot(alone=False):
    """One of the machine's MACHINE_SLOTS game slots (shared by every checkout's run_all), or all of
    them (alone): for a game that sets the display mode, which takes the surfaces of every other
    game running then (they fail with DDERR_SURFACELOST: "Can't begin scene", untextured models).
    Games that want all the slots queue on one more lock, so two of them never split the slots; while
    one holds it, no other game takes a slot (a suite's next game would take each slot as it frees,
    and the one that wants them all waited for the end of every other suite on the machine)."""
    os.makedirs(SLOT_DIR, exist_ok=True)
    held = []
    gate = None
    try:
        if alone:
            while not (gate := _lock(os.path.join(SLOT_DIR, 'alone'))):
                time.sleep(0.5)
        while True:
            if not alone:
                # A game waiting for all the slots holds the gate; others take no new slot meanwhile,
                # or freed slots go to them first and it starves (35 minutes once with four suites).
                waiting = _lock(os.path.join(SLOT_DIR, 'alone'))
                if not waiting:
                    time.sleep(0.5)
                    continue
                _unlock(waiting)
            for i in range(MACHINE_SLOTS):
                if alone and i in [h[0] for h in held]:
                    continue
                f = _lock(os.path.join(SLOT_DIR, f'slot{i:02}'))
                if f:
                    held.append((i, f))
                    if not alone:
                        break
            if len(held) == (MACHINE_SLOTS if alone else 1):
                break
            time.sleep(0.5)
        yield
    finally:
        for _, f in held:
            _unlock(f)
        if gate:
            _unlock(gate)


def sets_display_mode(scenario):
    """Whether a scenario sets the display mode (a `resolution` step): it runs with no other game."""
    with open(scenario, encoding='cp1251', errors='replace') as f:
        return any(re.match(r'\s*resolution\b', line) for line in f)


def run(name, install, hooks, folder=None, root=ROOT, build=BUILD, take_slot=True):
    """One game of a scenario on an install. root: the checkout whose tests/scenarios and run/ it uses;
    build: the verify build folder (ei_launch.exe; the run folder is <build>/runs/<folder>).
    take_slot False: the caller already holds a machine slot for it (tools/work_sched.py)."""
    scenario = os.path.join(root, 'tests', 'scenarios', name + '.txt')
    out = os.path.join(build, 'runs', folder or f'{name}-{install}')
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out, exist_ok=True)
    prepare_scenario.prepare(scenario, os.path.join(out, 'harness_saves'))   # "# prepare:" lines
    cmd = [os.path.join(build, 'ei_launch.exe'), '--harness', scenario, '--out', out]
    env = None
    if install == 'vanilla':
        cmd += ['--game', r'F:\games\EvilIslands', '--exe', os.path.join(root, 'run', 'vanilla', 'game.exe')]
    elif install == 'native':
        cmd += ['--game', r'F:\games\EvilIslands', '--exe', os.path.join(root, 'run', 'native', 'game.exe')]
        env = dict(os.environ, EI_RENDER='new,present')
    elif os.path.exists(os.path.join(root, 'run', 'rebirth', 'game.exe')):
        # Rebirth's engine copied with our own dgVoodoo.conf (tools/setup_rebirth_run.cmd).
        cmd += ['--exe', os.path.join(root, 'run', 'rebirth', 'game.exe')]
    if not hooks:
        cmd.append('--no-hooks')
    with machine_slot(alone=sets_display_mode(scenario)) if take_slot else contextlib.nullcontext():
        start = time.time()
        proc = subprocess.run(cmd, capture_output=True, text=True, encoding='cp1251', errors='replace', env=env)
        seconds = time.time() - start
    path = os.path.join(out, 'harness_report.txt')
    report = open(path, encoding='cp1251', errors='replace').read() if os.path.exists(path) else ''
    result = next((l[8:] for l in report.splitlines() if l.startswith('RESULT: ')), 'NO REPORT')
    failures = [l[8:] for l in report.splitlines() if l.startswith('FAILED: ')]
    counts = {}
    for line in report.splitlines():
        m = re.match(r'(.+?)\s+(\d+) calls\s+(\d+) mismatches$', line)
        if m:
            counts[m.group(1).strip()] = (int(m.group(2)), int(m.group(3)))
    notes = [l for l in (proc.stdout + proc.stderr).splitlines() if 'terminating' in l or 'not found' in l or 'open window' in l]
    notes += [l for l in report.splitlines() if l.startswith('EXIT: ')]   # a message box after quit
    return result, seconds, failures, counts, notes


def run_with_retries(name, install, hooks, retries, folder=None, runner=run):
    """run() again after a failure, up to retries times. runner: run() or a stand-in with its arguments."""
    result, seconds, failures, counts, notes = runner(name, install, hooks, folder)
    first_failures = failures
    for _ in range(retries):
        if result == 'PASS' or any(bad for _, bad in counts.values()):   # never retry away a mismatch
            break
        result, seconds, failures, counts, notes = runner(name, install, hooks, folder)
        if result == 'PASS':
            result = 'PASS*'
            failures = ['first run: ' + '; '.join(first_failures[:2])]
    return result, seconds, failures, counts, notes


def compare_widescreen(name, out=None, root=ROOT, build=BUILD):
    """--widescreen: each frame of <name>-rebirth against <name>-rebirth-nohooks (tools/compare_frames.py).
    A frame <f> with a <f>_later (captured after the clocks ran on) is compared without what moved
    between the two in either run (animations whose phase differs between runs: the hero, his
    portrait, the global map's clouds), so that what is compared is the interface.
    out: where the comparisons print (default stdout)."""
    out = out or sys.stdout
    runs = os.path.join(build, 'runs')
    ours, addons = (os.path.join(runs, f'{name}-{i}', 'frames') for i in ('rebirth', 'rebirth-nohooks'))
    diffs = os.path.join(runs, f'{name}-rebirth', 'frames_diff')
    os.makedirs(diffs, exist_ok=True)
    frames = sorted(f[:-4] for f in os.listdir(ours) if f.endswith('.bmp')) if os.path.isdir(ours) else []
    if not frames:
        print('no frames to compare', file=out)
        return False
    ok = True
    for frame in frames:
        if frame.endswith('_later'):
            continue
        a, b = os.path.join(ours, frame + '.bmp'), os.path.join(addons, frame + '.bmp')
        cmd = [sys.executable, os.path.join(root, 'tools', 'compare_frames.py'), a, b, '--worst', '4',
               '--diff', os.path.join(diffs, frame + '.bmp')]
        later_a, later_b = os.path.join(ours, frame + '_later.bmp'), os.path.join(addons, frame + '_later.bmp')
        excludes = []
        if os.path.exists(later_a) and os.path.exists(later_b):
            excludes += ['--exclude', a, later_a, '--exclude', b, later_b]
        # A window that pauses the world (the Esc menu's): what moved in <f>_moving / <f>_moving_later,
        # captured before it opened, is left out of <f> too.
        moving = [os.path.join(d, frame + s) for d in (ours, addons) for s in ('_moving.bmp', '_moving_later.bmp')]
        if all(os.path.exists(m) for m in moving):
            excludes += ['--exclude', moving[0], moving[1], '--exclude', moving[2], moving[3]]
        if excludes:
            cmd += excludes + ['--exclude-grow', '8']
        out.flush()
        if out is sys.stdout:
            ok = subprocess.run(cmd).returncode == 0 and ok
        else:
            p = subprocess.run(cmd, capture_output=True, text=True, errors='replace')
            out.write(p.stdout + p.stderr)
            ok = p.returncode == 0 and ok
    return ok


def rebirth_scenarios(names, root=ROOT):
    """The scenarios that also run under Rebirth: marked 1 in config/rebirth_scenarios.csv, or not in it."""
    path = os.path.join(root, 'config', 'rebirth_scenarios.csv')
    listed = {}
    if os.path.exists(path):
        for line in open(path):
            parts = line.strip().split(',')
            if len(parts) >= 2 and parts[1] in ('0', '1'):
                listed[parts[0]] = parts[1] == '1'
    return {n for n in names if listed.get(n, True)}


def parse_args(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('scenarios', nargs='*')
    ap.add_argument('--vanilla-only', action='store_true')
    ap.add_argument('--rebirth-only', action='store_true')
    ap.add_argument('--all-rebirth', action='store_true')
    ap.add_argument('--native', action='store_true')
    ap.add_argument('--no-hooks', action='store_true')
    ap.add_argument('--widescreen', action='store_true')
    ap.add_argument('--verified-candidates', type=int, default=0)
    ap.add_argument('--retries', type=int, default=1)
    ap.add_argument('--jobs', type=int, default=16)
    return ap.parse_args(argv)


def plan(args, root=ROOT):
    """The runs args ask for: (scenario, install) pairs in the summary's order. install is vanilla,
    rebirth, native or rebirth-nohooks (--widescreen); the run folder is <scenario>-<install>.
    Also used by tools/work_sched.py (`work.py test`), so a batch holds the runs run_all would make."""
    names = args.scenarios or sorted(os.path.splitext(os.path.basename(p))[0]
                                     for p in glob.glob(os.path.join(root, 'tests', 'scenarios', '*.txt')))
    installs = ['vanilla', 'rebirth']
    if args.vanilla_only: installs = ['vanilla']
    if args.rebirth_only: installs = ['rebirth']
    if args.native:
        installs = ['native']
        native = os.path.join(root, 'run', 'native', 'game.exe')
        if not os.path.exists(native):
            sys.exit(f'{native} is missing: run tools\\setup_native_run.cmd first')
    rebirth = set(names) if args.scenarios or args.all_rebirth or args.rebirth_only else rebirth_scenarios(names, root)
    jobs = [(name, install) for name in names for install in installs if install != 'rebirth' or name in rebirth]
    if args.widescreen:
        names = args.scenarios or ['ui_widescreen', 'ui_widescreen_more']
        jobs = [(name, install) for name in names for install in ('rebirth', 'rebirth-nohooks')]
    return jobs


def uses_hooks(install, args):
    """Whether a run of plan()'s install loads our hooks ("rebirth-nohooks": Rebirth without them)."""
    return not args.no_hooks and not install.endswith('-nohooks')


def load_times(build=BUILD):
    try:
        return json.load(open(os.path.join(build, 'run_times.json')))
    except (OSError, ValueError):
        return {}


def save_times(seconds, build=BUILD):
    """seconds: {'<scenario>-<install>': seconds}, merged into the build's run_times.json."""
    times = load_times(build)
    times.update({k: round(v, 1) for k, v in seconds.items()})
    with contextlib.suppress(OSError):
        json.dump(times, open(os.path.join(build, 'run_times.json'), 'w'), indent=0, sort_keys=True)


HEADER = f'{"scenario":<18} {"install":<8} {"result":<10} {"time":>6}  failures'


def line(name, install, result, seconds, failures, notes, root=ROOT, build=BUILD):
    """A run's line in the table (and under "not passed:")."""
    detail = '; '.join(failures[:3]) + (f' (+{len(failures) - 3} more)' if len(failures) > 3 else '')
    if result not in ('PASS', 'PASS*'):
        detail += f'  [{os.path.relpath(os.path.join(build, "runs", f"{name}-{install}"), root)}]'
    text = f'{name:<18} {install:<8} {result:<10} {seconds:5.0f}s  {detail}'
    for n in notes:
        text += f'\n{"":<38}{n}'
    return text


def summary(jobs, results, totals, args, elapsed, out=None, root=ROOT, build=BUILD):
    """Prints what follows the table: runs not passed, message boxes after quit, verify mismatches,
    --widescreen's frame comparisons, --verified-candidates and the totals line. results:
    {(scenario, install): (result, seconds, failures, notes)} for every job; totals: {function:
    [calls, mismatches]}. True when every run passed (and the frames matched)."""
    out = out or sys.stdout
    ok = all(r[0] in ('PASS', 'PASS*') for r in results.values())
    failed = [(n, i) for n, i in jobs if results[(n, i)][0] not in ('PASS', 'PASS*')]
    if failed and len(jobs) > 1:
        print('\nnot passed:', file=out)
        for n, i in failed:
            print(line(n, i, *results[(n, i)], root=root, build=build), file=out)
    exits = [(n, i) for n, i in jobs if any(x.startswith('EXIT: ') for x in results[(n, i)][3])]
    if exits:
        print(f'\n{len(exits)} run(s) showed a message box after the scenario quit (EXIT: lines; stack in ei_hook.log): '
              + ', '.join(f'{n}-{i}' for n, i in exits), file=out)
    bad = sorted((fn, c) for fn, c in totals.items() if c[1])
    if bad:
        print('\nverify mismatches:', file=out)
        for fn, (calls, mismatches) in bad:
            print(f'  {fn}: {mismatches} of {calls}', file=out)
    if args.widescreen:
        for name in args.scenarios or ['ui_widescreen']:
            print(f'\nframes of {name}: ours (rebirth) against addon\'s alone (rebirth-nohooks)', file=out)
            out.flush()
            ok = compare_widescreen(name, out, root, build) and ok
    if args.verified_candidates:
        print(f'\nfunctions with >= {args.verified_candidates} calls and no mismatch (names; map them to addresses '
              'in tests/verify/*.cpp before adding to verified.txt):', file=out)
        for fn, (calls, mismatches) in sorted(totals.items(), key=lambda kv: -kv[1][0]):
            if calls >= args.verified_candidates and not mismatches:
                print(f'  {calls:10d}  {fn}', file=out)
    print(f'\n{len(jobs)} runs in {elapsed:.0f}s with {args.jobs} at once', file=out)
    print('ALL PASS' if ok else 'SOME RUNS FAILED', file=out)
    return ok


def main():
    args = parse_args()
    jobs = plan(args)
    times = load_times()
    order = sorted(jobs, key=lambda j: -times.get(f'{j[0]}-{j[1]}', 1000))   # new scenarios first

    totals = collections.defaultdict(lambda: [0, 0])
    results = {}
    lock = threading.Lock()
    started = time.time()
    print(HEADER)
    sys.stdout.flush()

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        # (--widescreen's "rebirth-nohooks": Rebirth without our hooks, in its own folder)
        futures = {pool.submit(run_with_retries, n, i.split('-')[0], uses_hooks(i, args), args.retries, f'{n}-{i}'): (n, i)
                   for n, i in order}
        for future in concurrent.futures.as_completed(futures):
            name, install = futures[future]
            result, seconds, failures, counts, notes = future.result()
            with lock:
                results[(name, install)] = (result, seconds, failures, notes)
                for fn, (calls, bad) in counts.items():
                    totals[fn][0] += calls
                    totals[fn][1] += bad
                print(line(name, install, result, seconds, failures, notes))
                sys.stdout.flush()

    save_times({f'{n}-{i}': r[1] for (n, i), r in results.items()})
    ok = summary(jobs, results, totals, args, time.time() - started)
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
