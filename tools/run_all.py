"""Runs every harness scenario on the vanilla install and under Rebirth and prints a summary.

usage: python tools/run_all.py [scenario names...] [--vanilla-only | --rebirth-only | --native] [--no-hooks]
                               [--jobs N] [--retries N] [--verified-candidates N]
  scenario names   e.g. combat items (default: every tests/scenarios/*.txt)
  --native         only the install without dgVoodoo (run\\native, tools/setup_native_run.cmd), with
                   EI_RENDER=new,present: the game has no DirectDraw or Direct3D 7 device and the new
                   renderer presents through its own swap chain (docs/notes/renderer.md section 9)
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
import argparse, collections, concurrent.futures, contextlib, glob, msvcrt, os, re, shutil, subprocess, sys, tempfile, threading, time

import prepare_scenario

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, 'build', 'verify')
LAUNCH = os.path.join(BUILD, 'ei_launch.exe')
RUNS = os.path.join(BUILD, 'runs')
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


def run(name, install, hooks):
    scenario = os.path.join(ROOT, 'tests', 'scenarios', name + '.txt')
    out = os.path.join(RUNS, f'{name}-{install}')
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
    notes = [l for l in (proc.stdout + proc.stderr).splitlines() if 'terminating' in l or 'not found' in l]
    return result, seconds, failures, counts, notes


def run_with_retries(name, install, hooks, retries):
    result, seconds, failures, counts, notes = run(name, install, hooks)
    first_failures = failures
    for _ in range(retries):
        if result == 'PASS' or any(bad for _, bad in counts.values()):   # never retry away a mismatch
            break
        result, seconds, failures, counts, notes = run(name, install, hooks)
        if result == 'PASS':
            result = 'PASS*'
            failures = ['first run: ' + '; '.join(first_failures[:2])]
    return result, seconds, failures, counts, notes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('scenarios', nargs='*')
    ap.add_argument('--vanilla-only', action='store_true')
    ap.add_argument('--rebirth-only', action='store_true')
    ap.add_argument('--native', action='store_true')
    ap.add_argument('--no-hooks', action='store_true')
    ap.add_argument('--verified-candidates', type=int, default=0)
    ap.add_argument('--retries', type=int, default=1)
    ap.add_argument('--jobs', type=int, default=16)
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
    jobs = [(name, install) for name in names for install in installs]

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
        futures = {pool.submit(run_with_retries, n, i, not args.no_hooks, args.retries): (n, i) for n, i in jobs}
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

    ok = all(r[0] in ('PASS', 'PASS*') for r in results.values())
    failed = [(n, i) for n, i in jobs if results[(n, i)][0] not in ('PASS', 'PASS*')]
    if failed and len(jobs) > 1:
        print('\nnot passed:')
        for n, i in failed:
            print(line(n, i, *results[(n, i)]))
    bad = sorted((fn, c) for fn, c in totals.items() if c[1])
    if bad:
        print('\nverify mismatches:')
        for fn, (calls, mismatches) in bad:
            print(f'  {fn}: {mismatches} of {calls}')
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
