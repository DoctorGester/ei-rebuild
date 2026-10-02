"""Runs a harness scenario on the original code and on ours, and compares what the game printed.

The scenario should contain a "deterministic" line (virtual clock, fixed seeds), so both runs take
the same course; then any difference in the console output is a behaviour difference. Use it for
code whose verify wrappers can only check plausibility (functions that run once per call: AI ticks,
TakeHit, loaders).

usage: python tools/compare.py tests/scenarios/<scenario>.txt [--rebirth] [--runs N]
  --rebirth  run under Rebirth instead of the vanilla install
  --runs N   run each side N times (default 1); with N > 1 it also checks each side repeats itself
exit code: 0 same output, 1 different, 2 a run failed to produce a report
"""
import argparse, difflib, os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, 'build', 'verify')
LAUNCH = os.path.join(BUILD, 'ei_launch.exe')
REPORT = os.path.join(BUILD, 'harness_report.txt')


def run(scenario, hooks, rebirth):
    cmd = [LAUNCH, '--harness', scenario]
    if not rebirth:
        cmd += ['--game', r'F:\games\EvilIslands', '--exe', os.path.join(ROOT, 'run', 'vanilla', 'game.exe')]
    if not hooks:
        cmd.append('--no-hooks')
    subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not os.path.exists(REPORT):
        return None, None
    lines = open(REPORT, encoding='cp1251', errors='replace').read().splitlines()
    result = next((l for l in lines if l.startswith('RESULT:')), 'RESULT: ?')
    try:
        start = lines.index('console output:') + 1
    except ValueError:
        return [], result
    console = [l for l in lines[start:] if l.startswith('  ')]
    return console, result


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('scenario')
    ap.add_argument('--rebirth', action='store_true')
    ap.add_argument('--runs', type=int, default=1)
    args = ap.parse_args()
    scenario = os.path.abspath(args.scenario)
    if 'deterministic' not in open(scenario).read().split():
        print('warning: the scenario has no "deterministic" line; runs will not repeat exactly')

    outputs = {}
    for hooks in (False, True):
        side = 'ours' if hooks else 'original'
        for i in range(args.runs):
            console, result = run(scenario, hooks, args.rebirth)
            if console is None:
                print(f'{side} run {i + 1}: no report')
                return 2
            print(f'{side} run {i + 1}: {result}, {len(console)} console lines')
            if side in outputs and outputs[side] != console:
                print(f'{side} does not repeat itself between runs: the scenario is not deterministic')
                sys.stdout.writelines(l + '\n' for l in difflib.unified_diff(outputs[side], console, 'run 1', f'run {i + 1}', lineterm='', n=1))
                return 1
            outputs[side] = console
    if outputs['original'] == outputs['ours']:
        print('SAME: the console output matches the original')
        return 0
    print('DIFFERENT:')
    sys.stdout.writelines(l + '\n' for l in difflib.unified_diff(outputs['original'], outputs['ours'], 'original', 'ours', lineterm='', n=2))
    return 1


if __name__ == '__main__':
    sys.exit(main())
