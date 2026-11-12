"""Each scenario's time on the Linux CI runner against its time limit there (H75), from the workflow's logs.

usage: python tools/ci_times.py [--runs N] [--over FRACTION] [--all] [--branch NAME]
  --runs N        the last N completed runs of the Linux workflow (.github/workflows/suites-linux.yml; default 10)
  --over F        list the runs whose slowest pass took more than F of their limit there, and every run that did not
                  pass (default 0.5)
  --all           list every scenario
  --branch NAME   the runs of that branch (default main)

The runner draws on llvmpipe, 10 to 25 times a desktop's time, so a scenario's `timeout` is sized by its time there
(docs/notes/linux.md, "The CI job"): the limit there is the timeout times the workflow's EI_TIME_SCALE (run_all.
scenario_limit), and a run passes it comfortably at half of it or less (the runner's speed varies 40 to 90% from one
run to the next). Each suites job's log is read once through the gh CLI (on PATH or in its Windows install folder;
`gh auth login` first) and its run_all result lines kept in build/ci_times/<job id>.txt; nothing of the game's data
is in them. Scenarios with "# expect: TIMEOUT" (watchdog_*) are left out.
"""
import argparse, json, os, re, shutil, statistics, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run_all

REPO = 'DoctorGester/ei-rebuild'
WORKFLOW = 'Linux'
WORKFLOW_FILE = os.path.join(run_all.ROOT, '.github', 'workflows', 'suites-linux.yml')
CACHE = os.path.join(run_all.ROOT, 'build', 'ci_times')
# A run_all table line in a job's log: "<timestamp>Z <scenario> <install> <result> <seconds>s".
RESULT_LINE = re.compile(r'Z (\S+)\s+(standalone(?:-rebirth)?)\s+(PASS\*?|FAIL|TIMEOUT|NO REPORT|ERROR)\s+(\d+)s')


def gh_exe():
    found = shutil.which('gh')
    if found:
        return found
    windows = os.path.join(os.environ.get('ProgramFiles', r'C:\Program Files'), 'GitHub CLI', 'gh.exe')
    if os.path.exists(windows):
        return windows
    sys.exit('no gh CLI (on PATH or in Program Files\\GitHub CLI)')


def gh(*args):
    out = subprocess.run([gh_exe(), *args, '--repo', REPO], capture_output=True, text=True, encoding='utf-8',
                         errors='replace')
    if out.returncode:
        sys.exit(f'gh {" ".join(args)}: {out.stderr.strip()}')
    return out.stdout


def workflow_scale(path=WORKFLOW_FILE):
    """The workflow's EI_TIME_SCALE (1 when it sets none)."""
    with open(path, encoding='utf-8') as f:
        m = re.search(r'^\s*EI_TIME_SCALE:\s*([\d.]+)', f.read(), re.M)
    return m.group(1) if m else '1'


def result_lines(log):
    """[(scenario, install, result, seconds)] of a suites job's log: each run's line of run_all's table (the first one
    in each run_all call, between its header and its "not passed:" list, which repeats it)."""
    found, in_table = [], False
    for text in log.splitlines():
        text = text.rstrip()
        if text.endswith(run_all.HEADER) or text.endswith('not passed:'):
            in_table = text.endswith(run_all.HEADER)
            continue
        m = RESULT_LINE.search(text)
        if m and in_table:
            found.append((m.group(1), m.group(2), m.group(3), int(m.group(4))))
    return found


def job_results(job_id):
    """A suites job's result lines, read from the cache or its log."""
    path = os.path.join(CACHE, f'{job_id}.txt')
    if os.path.exists(path):
        with open(path, encoding='utf-8') as f:
            return [tuple(int(v) if i == 3 else v for i, v in enumerate(line.rstrip('\n').split('\t'))) for line in f]
    lines = result_lines(gh('run', 'view', '--job', str(job_id), '--log'))
    os.makedirs(CACHE, exist_ok=True)
    with open(path, 'w', encoding='utf-8') as f:
        f.writelines('\t'.join(map(str, line)) + '\n' for line in lines)
    return lines


def suites_jobs(runs, branch):
    """[(run id, job id)] of the suites jobs of the branch's last `runs` completed runs of the workflow."""
    listed = json.loads(gh('run', 'list', '--workflow', WORKFLOW, '--branch', branch, '--status', 'completed',
                           '--limit', str(runs), '--json', 'databaseId'))
    jobs = []
    for run in listed:
        view = json.loads(gh('run', 'view', str(run['databaseId']), '--json', 'jobs'))
        jobs += [(run['databaseId'], j['databaseId']) for j in view['jobs']
                 if j['name'].startswith('suites') and j['conclusion'] in ('success', 'failure')]
    return jobs


def table(results, scale):
    """[(fraction, scenario, install, limit, passed times, not passed [(result, seconds)])], the slowest first."""
    rows = []
    for (name, install), runs in results.items():
        scenario = os.path.join(run_all.ROOT, 'tests', 'scenarios', name + '.txt')
        if not os.path.exists(scenario) or run_all.expected_result(scenario):
            continue
        limit = run_all.scenario_limit(scenario, {'EI_TIME_SCALE': scale})
        times = sorted(s for r, s in runs if r.startswith('PASS'))
        failed = [(r, s) for r, s in runs if not r.startswith('PASS')]
        fraction = max(max(times, default=0), max((s for _, s in failed), default=0)) / limit
        rows.append((fraction, name, install, limit, times, failed))
    return sorted(rows, reverse=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--runs', type=int, default=10)
    ap.add_argument('--over', type=float, default=0.5)
    ap.add_argument('--all', action='store_true')
    ap.add_argument('--branch', default='main')
    args = ap.parse_args()
    results = {}
    jobs = suites_jobs(args.runs, args.branch)
    for _, job in jobs:
        for name, install, result, seconds in job_results(job):
            results.setdefault((name, install), []).append((result, seconds))
    scale = workflow_scale()
    rows = table(results, scale)
    print(f'{len(jobs)} suites jobs of {args.runs} runs; limits are the timeout times EI_TIME_SCALE {scale}')
    print(f'{"scenario":<26} {"install":<18} {"limit":>5} {"runs":>4} {"median":>6} {"max":>5}  of limit  not passed')
    for fraction, name, install, limit, times, failed in rows:
        if not args.all and fraction <= args.over and not failed:
            continue
        median = statistics.median(times) if times else 0
        print(f'{name:<26} {install:<18} {limit:5d} {len(times):4d} {median:6.0f} {max(times, default=0):5d}  '
              f'{fraction:7.0%}  {", ".join(f"{r} {s}s" for r, s in failed)}')


if __name__ == '__main__':
    main()
