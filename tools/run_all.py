"""Runs every harness scenario on the vanilla install and under Rebirth and prints a summary.

usage: python tools/run_all.py [scenario names...] [--vanilla-only | --rebirth-only] [--no-hooks]
                               [--jobs N] [--retries N] [--verified-candidates N]
  scenario names   e.g. combat items (default: every tests/scenarios/*.txt)
  --jobs N         games running at once (default 10). Each run writes to its own folder,
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
import argparse, collections, concurrent.futures, glob, os, re, shutil, subprocess, sys, threading, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, 'build', 'verify')
LAUNCH = os.path.join(BUILD, 'ei_launch.exe')
RUNS = os.path.join(BUILD, 'runs')


def run(name, install, hooks):
    scenario = os.path.join(ROOT, 'tests', 'scenarios', name + '.txt')
    out = os.path.join(RUNS, f'{name}-{install}')
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out, exist_ok=True)
    cmd = [LAUNCH, '--harness', scenario, '--out', out]
    if install == 'vanilla':
        cmd += ['--game', r'F:\games\EvilIslands', '--exe', os.path.join(ROOT, 'run', 'vanilla', 'game.exe')]
    if not hooks:
        cmd.append('--no-hooks')
    start = time.time()
    proc = subprocess.run(cmd, capture_output=True, text=True, encoding='cp1251', errors='replace')
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
    ap.add_argument('--no-hooks', action='store_true')
    ap.add_argument('--verified-candidates', type=int, default=0)
    ap.add_argument('--retries', type=int, default=1)
    ap.add_argument('--jobs', type=int, default=10)
    args = ap.parse_args()
    names = args.scenarios or sorted(os.path.splitext(os.path.basename(p))[0]
                                     for p in glob.glob(os.path.join(ROOT, 'tests', 'scenarios', '*.txt')))
    installs = ['vanilla', 'rebirth']
    if args.vanilla_only: installs = ['vanilla']
    if args.rebirth_only: installs = ['rebirth']
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
