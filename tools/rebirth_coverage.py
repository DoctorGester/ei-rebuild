"""Chooses which scenarios also run under Rebirth (config/rebirth_scenarios.csv, read by run_all.py).

Rebirth changes the game only in the functions it patches (docs/rebirth-patched-functions.txt). This
runs every scenario on vanilla with EI_COVER naming those functions (src/hook/cover.cpp logs the first
call of each), then keeps:
  - scenarios that check something per install (a `rebirth` / `vanilla` step) or are about Rebirth
    (rebirth_*), and
  - greedily, the fewest more scenarios that reach every patched function any scenario reaches at
    least twice (once where only one scenario reaches it).
Rerun it after adding scenarios or when the patched list changes; a scenario missing from the file
runs under Rebirth until then.

usage: python tools/rebirth_coverage.py [--from-runs]   (--from-runs: reuse build/verify/runs/*-vanilla
                                                         logs of a run made with EI_COVER set)
"""
import collections, glob, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATCHED = os.path.join(ROOT, 'docs', 'rebirth-patched-functions.txt')
RUNS = os.path.join(ROOT, 'build', 'verify', 'runs')
OUT = os.path.join(ROOT, 'config', 'rebirth_scenarios.csv')
COPIES = 2


def main():
    names = sorted(os.path.splitext(os.path.basename(p))[0]
                   for p in glob.glob(os.path.join(ROOT, 'tests', 'scenarios', '*.txt')))
    if '--from-runs' not in sys.argv:
        env = dict(os.environ, EI_COVER=PATCHED)
        subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'run_all.py'), '--vanilla-only', '--jobs', '14'],
                       env=env, check=False)
    cover = {}
    for name in names:
        log = os.path.join(RUNS, f'{name}-vanilla', 'ei_hook.log')
        if os.path.exists(log):
            text = open(log, encoding='cp1251', errors='replace').read()
            if 'cover: ' in text:
                cover[name] = set(re.findall(r'\] cover ([0-9A-F]{8})', text))
    missing = [n for n in names if n not in cover]
    if missing:
        sys.exit(f'no coverage for {", ".join(missing)} (run without --from-runs)')

    step = re.compile(r'^\s*(?:rebirth|vanilla)\b', re.M)
    chosen = [n for n in names if n.startswith('rebirth')
              or step.search(open(os.path.join(ROOT, 'tests', 'scenarios', n + '.txt'), encoding='cp1251', errors='replace').read())]
    reached = collections.Counter(a for n in names for a in cover[n])
    need = {a: min(COPIES, c) for a, c in reached.items()}
    have = collections.Counter(a for n in chosen for a in cover[n])
    gain = lambda n: sum(1 for a in cover[n] if have[a] < need[a])
    while any(have[a] < need[a] for a in need):
        best = max((n for n in names if n not in chosen), key=gain)
        chosen.append(best)
        have.update(cover[best])

    with open(OUT, 'w', newline='') as f:
        f.write('# Written by tools/rebirth_coverage.py: whether run_all.py also runs the scenario under Rebirth, and how\n'
                f'# many of the {len(reached)} patched functions the scenarios reach it reaches on vanilla.\n')
        f.write('scenario,rebirth,patched_reached\n')
        for n in names:
            f.write(f'{n},{int(n in chosen)},{len(cover[n])}\n')
    print(f'{len(chosen)} of {len(names)} scenarios run under Rebirth; {len(reached)} patched functions reached, '
          f'each by {COPIES} of them where possible; wrote {os.path.relpath(OUT, ROOT)}')


if __name__ == '__main__':
    main()
