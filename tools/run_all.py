"""Runs every harness scenario on the vanilla install and under Rebirth and prints a summary.

usage: python tools/run_all.py [scenario names...] [--vanilla-only | --rebirth-only | --all-rebirth | --native] [--no-hooks]
                               [--jobs N] [--retries N] [--clock virtual|real[,...]] [--verified-candidates N]
       python tools/run_all.py --widescreen [scenario names...]
  scenario names   e.g. combat items (default: every tests/scenarios/*.txt)
                   Every scenario runs on vanilla; under Rebirth only those config/rebirth_scenarios.csv
                   marks (tools/rebirth_coverage.py: the fewest that reach every function Rebirth patches
                   twice) and any it does not list yet. Named scenarios and --all-rebirth run under both.
  --native         only the install without dgVoodoo (run\\native, tools/setup_native_run.cmd), with
                   EI_RENDER=new,present: the game has no DirectDraw or Direct3D 7 device and the new
                   renderer presents through its own swap chain (docs/notes/renderer.md section 9)
  --widescreen     Rebirth's WideScreen against our rewrite of it (docs/notes/rebirth.md, "WideScreen"):
                   the scenarios (default ui_widescreen; they set the wide mode with the harness's
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
  --clock virtual|real[,clocktrace][,randtrace]
                   run every scenario on the virtual clock or on real time, whatever its own lines say
                   (EI_HARNESS_CLOCK; tests/verify/harness_clock.h), with the clock or random trace
  --verified-candidates N
                   afterwards, list rewritten functions with at least N verify calls and no
                   mismatch in any run that are not yet in tests/verify/verified.txt
exit code: 0 when every run passed
"""
import argparse, collections, concurrent.futures, contextlib, glob, json, msvcrt, os, re, shutil, subprocess, sys, tempfile, threading, time

import prepare_scenario

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, 'build', 'verify')
LAUNCH = os.path.join(BUILD, 'ei_launch.exe')
RUNS = os.path.join(BUILD, 'runs')
# Each run's seconds from the last suite: the longest start first, so the last games to finish are
# short ones (alphabetical order left a 50 s run for the end).
TIMES = os.path.join(BUILD, 'run_times.json')
NATIVE_EXE = os.path.join(ROOT, 'run', 'native', 'game.exe')

# Games running at once on this machine, over every checkout and every run_all (2026-10-14: three
# workers' suites, 24 games of ~640 MB video memory each rendering without a frame limit, hung the
# NVIDIA driver until Windows crashed twice). A slot is a byte lock on a file in the temp folder;
# Windows drops it when the process ends, so a killed run never leaves a slot taken.
MACHINE_SLOTS = int(os.environ.get('EI_HARNESS_SLOTS', '16'))
SLOT_DIR = os.path.join(tempfile.gettempdir(), 'ei_harness_slots')


@contextlib.contextmanager
def machine_slot():
    os.makedirs(SLOT_DIR, exist_ok=True)
    while True:
        for i in range(MACHINE_SLOTS):
            f = open(os.path.join(SLOT_DIR, f'slot{i:02}'), 'a+b')
            try:
                msvcrt.locking(f.fileno(), msvcrt.LK_NBLCK, 1)
            except OSError:
                f.close()
                continue
            try:
                yield
            finally:
                f.seek(0)
                msvcrt.locking(f.fileno(), msvcrt.LK_UNLCK, 1)
                f.close()
            return
        time.sleep(0.5)


# A game window that had the foreground (Windows hands it to another game when the one in front
# closes) and lost it to a starting game is minimised by dgVoodoo, which takes its surfaces and,
# when that comes twice, its exclusive mode: the run ends on the game's "surface memory is gone"
# box (harness.cpp, RestoreAfterMinimise). That is the machine, not the code: such a run is run again.
MINIMISED = 'harness: the game window was minimised'
MINIMISED_RERUNS = 2


def run(name, install, hooks, folder=None, clock=None):
    """One run of a scenario in build\\verify\\runs\\<folder> (default <name>-<install>); `clock`
    overrides the scenario's clock (EI_HARNESS_CLOCK: 'virtual' or 'real', ',clocktrace', ',randtrace').
    A run that fails after dgVoodoo minimised its window is run again (MINIMISED)."""
    for attempt in range(MINIMISED_RERUNS + 1):
        result, seconds, failures, counts, notes = run_once(name, install, hooks, folder, clock)
        if result == 'PASS' or attempt == MINIMISED_RERUNS or not minimised(name, install, folder):
            break
        print(f'  {folder or name + "-" + install}: {result} after dgVoodoo minimised the window; running it again',
              flush=True)
    return result, seconds, failures, counts, notes


def minimised(name, install, folder):
    path = os.path.join(RUNS, folder or f'{name}-{install}', 'ei_hook.log')
    with contextlib.suppress(OSError):
        with open(path, encoding='cp1251', errors='replace') as f:
            return any(MINIMISED in line for line in f)
    return False


def run_once(name, install, hooks, folder=None, clock=None):
    scenario = os.path.join(ROOT, 'tests', 'scenarios', name + '.txt')
    out = os.path.join(RUNS, folder or f'{name}-{install}')
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out, exist_ok=True)
    prepare_scenario.prepare(scenario, os.path.join(out, 'harness_saves'))   # "# prepare:" lines
    cmd = [LAUNCH, '--harness', scenario, '--out', out]
    env = None
    if install == 'vanilla':
        cmd += ['--game', r'F:\games\EvilIslands', '--exe', os.path.join(ROOT, 'run', 'vanilla', 'game.exe')]
    elif install == 'native':
        cmd += ['--game', r'F:\games\EvilIslands', '--exe', NATIVE_EXE]
        env = dict(os.environ, EI_RENDER='new,present')
    elif os.path.exists(os.path.join(ROOT, 'run', 'rebirth', 'game.exe')):
        # Rebirth's engine copied with our own dgVoodoo.conf (tools/setup_rebirth_run.cmd).
        cmd += ['--exe', os.path.join(ROOT, 'run', 'rebirth', 'game.exe')]
    if not hooks:
        cmd.append('--no-hooks')
    if clock:
        env = dict(env or os.environ, EI_HARNESS_CLOCK=clock)
    with machine_slot():
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


def console_of(folder):
    """The console output a run's report holds (its lines after "console output:")."""
    path = os.path.join(RUNS, folder, 'harness_report.txt')
    lines = open(path, encoding='cp1251', errors='replace').read().splitlines() if os.path.exists(path) else []
    lines = lines[lines.index('console output:') + 1:] if 'console output:' in lines else []
    return [l for l in lines if l.startswith('  ')]


def run_with_retries(name, install, hooks, retries, folder=None, clock=None):
    result, seconds, failures, counts, notes = run(name, install, hooks, folder, clock)
    first_failures = failures
    for _ in range(retries):
        if result == 'PASS' or any(bad for _, bad in counts.values()):   # never retry away a mismatch
            break
        result, seconds, failures, counts, notes = run(name, install, hooks, folder, clock)
        if result == 'PASS':
            result = 'PASS*'
            failures = ['first run: ' + '; '.join(first_failures[:2])]
    return result, seconds, failures, counts, notes


def compare_widescreen(name):
    """--widescreen: each frame of <name>-rebirth against <name>-rebirth-nohooks (tools/compare_frames.py).
    A frame <f> with a <f>_later (captured after the clocks ran on) is compared without what moved
    between the two in either run (animations whose phase differs between runs: the hero, his
    portrait, the global map's clouds), so that what is compared is the interface."""
    ours, addons = (os.path.join(RUNS, f'{name}-{i}', 'frames') for i in ('rebirth', 'rebirth-nohooks'))
    diffs = os.path.join(RUNS, f'{name}-rebirth', 'frames_diff')
    os.makedirs(diffs, exist_ok=True)
    frames = sorted(f[:-4] for f in os.listdir(ours) if f.endswith('.bmp')) if os.path.isdir(ours) else []
    if not frames:
        print('no frames to compare')
        return False
    ok = True
    for frame in frames:
        if frame.endswith('_later'):
            continue
        a, b = os.path.join(ours, frame + '.bmp'), os.path.join(addons, frame + '.bmp')
        cmd = [sys.executable, os.path.join(ROOT, 'tools', 'compare_frames.py'), a, b, '--worst', '4',
               '--diff', os.path.join(diffs, frame + '.bmp')]
        later_a, later_b = os.path.join(ours, frame + '_later.bmp'), os.path.join(addons, frame + '_later.bmp')
        if os.path.exists(later_a) and os.path.exists(later_b):
            cmd += ['--exclude', a, later_a, '--exclude', b, later_b, '--exclude-grow', '8']
        sys.stdout.flush()
        ok = subprocess.run(cmd).returncode == 0 and ok
    return ok


def rebirth_scenarios(names):
    """The scenarios that also run under Rebirth: marked 1 in config/rebirth_scenarios.csv, or not in it."""
    path = os.path.join(ROOT, 'config', 'rebirth_scenarios.csv')
    listed = {}
    if os.path.exists(path):
        for line in open(path):
            parts = line.strip().split(',')
            if len(parts) >= 2 and parts[1] in ('0', '1'):
                listed[parts[0]] = parts[1] == '1'
    return {n for n in names if listed.get(n, True)}


def main():
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
    ap.add_argument('--clock')
    args = ap.parse_args()
    names = args.scenarios or sorted(os.path.splitext(os.path.basename(p))[0]
                                     for p in glob.glob(os.path.join(ROOT, 'tests', 'scenarios', '*.txt')))
    installs = ['vanilla', 'rebirth']
    if args.vanilla_only: installs = ['vanilla']
    if args.rebirth_only: installs = ['rebirth']
    if args.native:
        installs = ['native']
        if not os.path.exists(NATIVE_EXE):
            sys.exit(f'{NATIVE_EXE} is missing: run tools\\setup_native_run.cmd first')
    rebirth = set(names) if args.scenarios or args.all_rebirth or args.rebirth_only else rebirth_scenarios(names)
    jobs = [(name, install) for name in names for install in installs if install != 'rebirth' or name in rebirth]
    if args.widescreen:
        names = args.scenarios or ['ui_widescreen']
        jobs = [(name, install) for name in names for install in ('rebirth', 'rebirth-nohooks')]
    try:
        times = json.load(open(TIMES))
    except (OSError, ValueError):
        times = {}
    order = sorted(jobs, key=lambda j: -times.get(f'{j[0]}-{j[1]}', 1000))   # new scenarios first

    totals = collections.defaultdict(lambda: [0, 0])
    results = {}
    lock = threading.Lock()
    started = time.time()
    print(f'{"scenario":<18} {"install":<8} {"result":<10} {"time":>6}  failures')
    sys.stdout.flush()

    def line(name, install, result, seconds, failures, notes):
        detail = '; '.join(failures[:3]) + (f' (+{len(failures) - 3} more)' if len(failures) > 3 else '')
        if result not in ('PASS', 'PASS*'):
            detail += f'  [{os.path.relpath(os.path.join(RUNS, f"{name}-{install}"), ROOT)}]'
        text = f'{name:<18} {install:<8} {result:<10} {seconds:5.0f}s  {detail}'
        for n in notes:
            text += f'\n{"":<38}{n}'
        return text

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        # (--widescreen's "rebirth-nohooks": Rebirth without our hooks, in its own folder)
        futures = {pool.submit(run_with_retries, n, i.split('-')[0], not args.no_hooks and not i.endswith('-nohooks'),
                               args.retries, f'{n}-{i}', args.clock): (n, i) for n, i in order}
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

    times.update({f'{n}-{i}': round(r[1], 1) for (n, i), r in results.items()})
    with contextlib.suppress(OSError):
        json.dump(times, open(TIMES, 'w'), indent=0, sort_keys=True)
    ok = all(r[0] in ('PASS', 'PASS*') for r in results.values())
    failed = [(n, i) for n, i in jobs if results[(n, i)][0] not in ('PASS', 'PASS*')]
    if failed and len(jobs) > 1:
        print('\nnot passed:')
        for n, i in failed:
            print(line(n, i, *results[(n, i)]))
    exits = [(n, i) for n, i in jobs if any(x.startswith('EXIT: ') for x in results[(n, i)][3])]
    if exits:
        print(f'\n{len(exits)} run(s) showed a message box after the scenario quit (EXIT: lines; stack in ei_hook.log): '
              + ', '.join(f'{n}-{i}' for n, i in exits))
    bad = sorted((fn, c) for fn, c in totals.items() if c[1])
    if bad:
        print('\nverify mismatches:')
        for fn, (calls, mismatches) in bad:
            print(f'  {fn}: {mismatches} of {calls}')
    if args.widescreen:
        for name in names:
            print(f'\nframes of {name}: ours (rebirth) against addon\'s alone (rebirth-nohooks)')
            sys.stdout.flush()
            ok = compare_widescreen(name) and ok
    if args.verified_candidates:
        print(f'\nfunctions with >= {args.verified_candidates} calls and no mismatch (names; map them to addresses '
              'in tests/verify/*.cpp before adding to verified.txt):')
        for fn, (calls, mismatches) in sorted(totals.items(), key=lambda kv: -kv[1][0]):
            if calls >= args.verified_candidates and not mismatches:
                print(f'  {calls:10d}  {fn}')
    print(f'\n{len(jobs)} runs in {time.time() - started:.0f}s with {args.jobs} at once')
    print('ALL PASS' if ok else 'SOME RUNS FAILED')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
