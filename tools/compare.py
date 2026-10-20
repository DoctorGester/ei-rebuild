r"""Runs harness scenarios on the original code and on ours, and compares what the game printed.

The original side is game.exe with the original code only (run_all --no-hooks: install vanilla or rebirth), ours
the standalone image on the same data (install standalone or standalone-rebirth; since S21c our code runs nowhere
else). On the virtual clock (tests/verify/harness_clock.h) a scenario takes the same course every time, so any
difference in the console output is a behaviour difference; the goldens (run_all --golden) check the console, the
digest and the saves of the scenarios that have one, this any scenario's console, a side's repeats too.

usage: python tools/compare.py [scenario names or paths...] [--rebirth | --both] [--runs N] [--jobs N]
                               [--clock virtual|real[,clocktrace][,randtrace]] [--fresh]
  scenarios  names in tests/scenarios (default: all); with --rebirth and no names, run_all's Rebirth
             smoke set (run_all.rebirth_scenarios)
  --rebirth  run under Rebirth instead of the vanilla install; --both: vanilla, then Rebirth
  --runs N   run each side N times (default 1); with N > 1 it also checks each side repeats itself
  --jobs N   games at once (default 4; run_all.py's machine-wide slots apply)
  --clock    override the scenarios' clock (default: as each scenario says); randtrace writes every
             random draw to randtrace.txt in each run's folder (find where two runs part)
  --fresh    play the original side again instead of taking it from run_all's reference cache (with
             --runs N > 1 it is always played: it has to repeat itself)
Each run writes to build\verify\runs\<scenario>-<install>-<original|ours><n> (install: vanilla or rebirth, the
data both sides run on). A scenario's verdict
is printed as soon as its runs are done.
exit code: 0 every scenario repeats and matches, 1 otherwise, 2 a run produced no report
"""
import argparse, concurrent.futures, difflib, glob, os, sys, time

import run_all

ROOT = run_all.ROOT
OURS = {'vanilla': 'standalone', 'rebirth': 'standalone-rebirth'}     # the image on the same install's data


def diff(a, b, name_a, name_b, limit=12):
    lines = list(difflib.unified_diff(a, b, name_a, name_b, lineterm='', n=1))
    shown = lines[:limit]
    if len(lines) > limit:
        shown.append(f'... ({len(lines) - limit} more diff lines)')
    return ['    ' + l for l in shown]


def verdict(n, i, results, runs):
    """(verdict, detail lines, exit code) of one scenario in one install."""
    lines, verdicts, consoles, code = [], [], {}, 0
    for side in ('original', 'ours'):
        outs = [results[(n, i, side, k)] for k in range(1, runs + 1)]
        if any(r[0] == 'NO REPORT' for r in outs):
            verdicts.append(f'{side}: no report')
            code = 2
            continue
        consoles[side] = outs[0][3]
        bad = [k for k in range(2, runs + 1) if outs[k - 1][3] != outs[0][3]]
        lines.append(f'  {side}: {" ".join(r[0] for r in outs)}; {len(outs[0][3])} console lines; '
                     f'{max(r[1] for r in outs):.0f}s at most')
        for r in outs:
            lines += [f'    {r[0]}: {f}' for f in r[2][:2]]
        if bad:
            verdicts.append(f'{side} does not repeat')
            lines += diff(outs[0][3], outs[bad[0] - 1][3], f'{side} run 1', f'{side} run {bad[0]}')
    if len(consoles) == 2 and consoles['original'] != consoles['ours']:
        verdicts.append('ours differs from the original')
        lines += diff(consoles['original'], consoles['ours'], 'original', 'ours')
    text = '; '.join(verdicts) or ('REPEATS, SAME' if runs > 1 else 'SAME')
    return text, lines, max(code, 1 if verdicts else 0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('scenarios', nargs='*')
    ap.add_argument('--rebirth', action='store_true')
    ap.add_argument('--both', action='store_true')
    ap.add_argument('--runs', type=int, default=1)
    ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('--clock')
    ap.add_argument('--fresh', action='store_true')
    args = ap.parse_args()
    names = [os.path.splitext(os.path.basename(s))[0] for s in args.scenarios] or sorted(
        os.path.splitext(os.path.basename(p))[0] for p in glob.glob(os.path.join(ROOT, 'tests', 'scenarios', '*.txt')))
    installs = ['vanilla', 'rebirth'] if args.both else ['rebirth'] if args.rebirth else ['vanilla']
    rebirth = set(names) if args.scenarios else run_all.rebirth_scenarios(names)
    cases = [(n, i) for i in installs for n in names if i != 'rebirth' or n in rebirth]
    runs = [(n, i, side, k) for n, i in cases for side in ('original', 'ours') for k in range(1, args.runs + 1)]

    started = time.time()
    results = {}
    pending = {case: 2 * args.runs for case in cases}
    code = 0
    good = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futures = {}
        for n, i, side, k in runs:
            folder = f'{n}-{i}-{side}{k}'
            futures[pool.submit(run_all.run, n, OURS[i] if side == 'ours' else i, folder, clock=args.clock,
                                fresh=args.fresh or args.runs > 1)] = (n, i, side, k, folder)
        for future in concurrent.futures.as_completed(futures):
            n, i, side, k, folder = futures[future]
            result, seconds, failures, counts, notes = future.result()
            results[(n, i, side, k)] = (result, seconds, failures, run_all.console_of(folder))
            pending[(n, i)] -= 1
            if pending[(n, i)]:
                continue
            text, lines, case_code = verdict(n, i, results, args.runs)
            code = max(code, case_code)
            good += case_code == 0
            print(f'{n:<26} {i:<8} {text}')
            if case_code:
                for l in lines:
                    print(l)
            sys.stdout.flush()
    print(f'\n{good} of {len(cases)} scenario runs repeat and match ({len(runs)} games in {time.time() - started:.0f}s)')
    return code


if __name__ == '__main__':
    sys.exit(main())
