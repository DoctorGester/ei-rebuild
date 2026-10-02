"""Runs every harness scenario on the vanilla install and under Rebirth and prints a summary.

usage: python tools/run_all.py [scenario names...] [--vanilla-only | --rebirth-only] [--no-hooks]
                               [--verified-candidates N]
  scenario names   e.g. combat items (default: every tests/scenarios/*.txt)
  --verified-candidates N
                   afterwards, list rewritten functions with at least N verify calls and no
                   mismatch in any run that are not yet in tests/verify/verified.txt
exit code: 0 when every run passed
"""
import argparse, collections, glob, os, re, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, 'build', 'verify')
LAUNCH = os.path.join(BUILD, 'ei_launch.exe')
REPORT = os.path.join(BUILD, 'harness_report.txt')
LOG = os.path.join(BUILD, 'ei_hook.log')


def run(scenario, install, hooks):
    cmd = [LAUNCH, '--harness', scenario]
    if install == 'vanilla':
        cmd += ['--game', r'F:\games\EvilIslands', '--exe', os.path.join(ROOT, 'run', 'vanilla', 'game.exe')]
    if not hooks:
        cmd.append('--no-hooks')
    start = time.time()
    proc = subprocess.run(cmd, capture_output=True, text=True, encoding='cp1251', errors='replace')
    seconds = time.time() - start
    report = open(REPORT, encoding='cp1251', errors='replace').read() if os.path.exists(REPORT) else ''
    result = next((l[8:] for l in report.splitlines() if l.startswith('RESULT: ')), 'NO REPORT')
    failures = [l[8:] for l in report.splitlines() if l.startswith('FAILED: ')]
    counts = {}
    for line in report.splitlines():
        m = re.match(r'(.+?)\s+(\d+) calls\s+(\d+) mismatches$', line)
        if m:
            counts[m.group(1).strip()] = (int(m.group(2)), int(m.group(3)))
    notes = [l for l in (proc.stdout + proc.stderr).splitlines() if 'terminating' in l or 'not found' in l]
    return result, seconds, failures, counts, notes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('scenarios', nargs='*')
    ap.add_argument('--vanilla-only', action='store_true')
    ap.add_argument('--rebirth-only', action='store_true')
    ap.add_argument('--no-hooks', action='store_true')
    ap.add_argument('--verified-candidates', type=int, default=0)
    args = ap.parse_args()
    names = args.scenarios or sorted(os.path.splitext(os.path.basename(p))[0]
                                     for p in glob.glob(os.path.join(ROOT, 'tests', 'scenarios', '*.txt')))
    installs = ['vanilla', 'rebirth']
    if args.vanilla_only: installs = ['vanilla']
    if args.rebirth_only: installs = ['rebirth']

    totals = collections.defaultdict(lambda: [0, 0])
    ok = True
    print(f'{"scenario":<18} {"install":<8} {"result":<10} {"time":>6}  failures')
    for name in names:
        scenario = os.path.join(ROOT, 'tests', 'scenarios', name + '.txt')
        for install in installs:
            result, seconds, failures, counts, notes = run(scenario, install, not args.no_hooks)
            ok = ok and result == 'PASS'
            for fn, (calls, bad) in counts.items():
                totals[fn][0] += calls
                totals[fn][1] += bad
            detail = '; '.join(failures[:3]) + (f' (+{len(failures) - 3} more)' if len(failures) > 3 else '')
            print(f'{name:<18} {install:<8} {result:<10} {seconds:5.0f}s  {detail}')
            for n in notes:
                print(f'{"":<38}{n}')
            sys.stdout.flush()

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
    print('\nALL PASS' if ok else '\nSOME RUNS FAILED')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
