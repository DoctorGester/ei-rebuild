"""Where the workers' time goes: builds, golden suites, scenario reruns, the scheduler's queue, and their own reasoning.

    python tools/worker_waits.py [--days 30] [--out build/live/waits.json] [--print [--window-days 7]]

Reads the area workers' transcripts (~/.claude/projects/<this repo>/*/subagents/agent-*.jsonl) and the work database
(batch, timing). For every worker: wall time, its reasoning (assistant turns), its foreground tool calls by kind, and
its background jobs from launch to the notification that they finished. Builds and test runs are listed one by one, so
repeats show. The work board serves the result at /waits/ (work.py serve); the coordinator reads its findings after
merges and acts on the largest avoidable wait (docs/notes/work-board.md, "Waits").

The JSON holds timestamped rows over the last --days (30), not totals: waits.html picks a window [start, end) (6 h to
30 days, stepped through time) and totals it client-side. view() below is the same computation in Python (--print and
tools/test_waits.py, which checks the page's JavaScript against it). What belongs to a window: a worker, where it
finished (or is still running); a test batch, where it was submitted; game time, per hour, by the hour's start.

Per-transcript results are cached in build/live/waits_cache.json by size and mtime, so a refresh reads only new ones.
"""
import argparse
import json
import os
import re
import sqlite3
import statistics
import sys
import time
from collections import defaultdict
from datetime import datetime, timezone

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import work  # noqa: E402

CACHE_VERSION = 3
DAYS = 30
SLOW_KEPT = 100          # scenarios (per install) whose hourly game times are kept for the slowest-scenarios table


def stamp(s):
    return datetime.fromisoformat(s.replace('Z', '+00:00')).timestamp()


def transcripts_root():
    main = work.main_checkout()
    key = re.sub(r'[^A-Za-z0-9]', '-', os.path.abspath(main))
    return os.path.join(os.path.expanduser('~'), '.claude', 'projects', key)


def kind(name, inp):
    """What a tool call spends its time on."""
    if name in ('Read', 'Grep', 'Glob'):
        return 'read'
    if name in ('Edit', 'Write', 'NotebookEdit'):
        return 'edit'
    if name in ('Monitor', 'TaskStop', 'TaskOutput'):
        return 'wait'
    c = inp.get('command', '') if isinstance(inp, dict) else ''
    if re.search(r'work\.py test\b|run_all\.py|main_suites', c):
        return 'test'
    if re.search(r'build\.cmd|cmake (--build|-S)|ninja\b|ctest\b', c):
        return 'build'
    if re.search(r'\bwsl(\.exe)?\b', c):
        return 'wsl'
    if re.search(r'(^|[;&|]\s*)git\s', c):
        return 'git'
    if re.search(r'tickets\.py|work\.py (note|start|status)', c):
        return 'board'
    if re.search(r'(^|[;&|]\s*)(grep|rg|sed -n|cat|head|tail|ls|find|wc)\b', c):
        return 'read'
    if name in ('Bash', 'PowerShell'):
        return 'shell'
    return 'other'


def detail(k, inp):
    """A short label for a build or test call: which builds, which suite or scenarios."""
    c = inp.get('command', '') if isinstance(inp, dict) else ''
    if k == 'build':
        cfgs = re.findall(r'build\.cmd\W+(\w+)', c)
        return ' '.join(cfgs) or 'cmake'
    if k == 'test':
        m = re.search(r'work\.py test (\S+)(.*?)(?:2>|>|\||;|&&|$)', c)
        if not m:
            return 'run_all' if 'run_all' in c else 'suites'
        rest = m.group(2).split()
        flags, names = [], []
        for i, a in enumerate(rest):
            if a.startswith('--'):
                if a != '--jobs':
                    flags.append(a)
            elif not (i and rest[i - 1] == '--jobs'):
                names.append(a)
        if names:
            return f'{len(names)} scenario' + ('s' if len(names) > 1 else '') + (' ' + ' '.join(flags) if flags else '')
        return 'suite ' + (' '.join(flags) or '(all)')
    return ''


def read_transcript(path):
    rows = []
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            if line.strip():
                try:
                    rows.append(json.loads(line))
                except ValueError:
                    pass
    uses, bg, done = {}, {}, {}
    fg = defaultdict(float)
    calls = []          # builds and tests, foreground and background: [start, seconds, kind, detail, background]
    model = 0.0
    first = last = None
    prev_end = None
    for r in rows:
        t = r.get('timestamp')
        if not t:
            continue
        t = stamp(t)
        first = first if first is not None else t
        last = t
        m = r.get('message')
        c = m.get('content') if isinstance(m, dict) else None
        if r.get('type') == 'assistant':
            if prev_end is not None:
                model += max(0.0, t - prev_end)
                prev_end = None
            if isinstance(c, list):
                for x in c:
                    if x.get('type') == 'tool_use':
                        uses[x['id']] = (t, x.get('name', ''), x.get('input') or {})
        elif r.get('type') == 'user':
            if isinstance(c, list):
                for x in c:
                    if x.get('type') == 'tool_result' and x.get('tool_use_id') in uses:
                        t0, name, inp = uses.pop(x['tool_use_id'])
                        k = kind(name, inp)
                        body = json.dumps(x.get('content'))
                        launched = re.search(r'with ID: (\w+)', body) if isinstance(inp, dict) and inp.get('run_in_background') else None
                        if launched:
                            bg[launched.group(1)] = (t0, k, detail(k, inp))
                        else:
                            fg[k] += t - t0
                            if k in ('build', 'test'):
                                calls.append([t0, t - t0, k, detail(k, inp), False])
            prev_end = t
        if r.get('type') in ('user', 'attachment') or 'attachment' in r:
            # a background job's end: its <task-notification>, queued as an attachment while the worker is busy
            for tid in re.findall(r'<task-id>(\w+)</task-id>', json.dumps(r.get('attachment') or c or '')):
                if tid in bg and tid not in done:
                    done[tid] = t
    bgk = defaultdict(float)
    for tid, (t0, k, d) in bg.items():
        if tid in done:
            bgk[k] += done[tid] - t0
            if k in ('build', 'test'):
                calls.append([t0, done[tid] - t0, k, d, True])
    calls.sort()
    return {'v': CACHE_VERSION, 'first': first, 'last': last, 'model': model,
            'fg': dict(fg), 'bg': dict(bgk), 'calls': calls}


def agent_tickets(db):
    out = defaultdict(list)
    for a in db.execute('SELECT ticket, agent FROM assignment WHERE agent IS NOT NULL ORDER BY id'):
        if a['ticket'] not in out[a['agent']]:
            out[a['agent']].append(a['ticket'])
    return out


def median(xs):
    return round(statistics.median(xs), 1) if xs else None


def collect_workers(days, db):
    """One row per area worker whose transcript was written in the last `days`: its times and its build and test calls
    ([start, seconds, what, background] each)."""
    since = time.time() - days * 86400
    root = transcripts_root()
    cache_path = os.path.join(work.main_checkout(), 'build', 'live', 'waits_cache.json')
    try:
        with open(cache_path, encoding='utf-8') as f:
            cache = json.load(f)
    except (OSError, ValueError):
        cache = {}
    tickets = agent_tickets(db)
    workers = []
    for session in os.listdir(root) if os.path.isdir(root) else []:
        sub = os.path.join(root, session, 'subagents')
        if not os.path.isdir(sub):
            continue
        for name in os.listdir(sub):
            if not (name.startswith('agent-') and name.endswith('.jsonl')):
                continue
            path = os.path.join(sub, name)
            st = os.stat(path)
            if st.st_mtime < since:
                continue
            key = f'{st.st_size}:{int(st.st_mtime)}'
            hit = cache.get(path)
            if not hit or hit.get('key') != key or hit.get('data', {}).get('v') != CACHE_VERSION:
                hit = {'key': key, 'data': read_transcript(path)}
                cache[path] = hit
            d = hit['data']
            if d['first'] is None:
                continue
            tk = tickets.get(name[:-len('.jsonl')], [])
            if not tk:
                continue      # not an area worker of the board (reviews, explorers)
            workers.append({
                'tickets': tk, 'start': round(d['first']), 'end': round(d['last']),
                'live': time.time() - d['last'] < 600, 'model': round(d['model']),
                'fg': {k: round(v) for k, v in d['fg'].items()},
                'bg': {k: round(v) for k, v in d['bg'].items()},
                'builds': [[round(c[0]), round(c[1]), c[3], int(c[4])] for c in d['calls'] if c[2] == 'build'],
                'tests': [[round(c[0]), round(c[1]), c[3], int(c[4])] for c in d['calls'] if c[2] == 'test'],
            })
    os.makedirs(os.path.dirname(cache_path), exist_ok=True)
    tmp = cache_path + '.tmp'
    with open(tmp, 'w', encoding='utf-8') as f:
        json.dump(cache, f)
    os.replace(tmp, cache_path)
    workers.sort(key=lambda w: -w['start'])
    return workers


def collect_batches(db, since):
    """Test batches submitted since `since` (epoch) that got going: [id, ticket, submitted, queue s, run s or None, games, retries, args]."""
    iso = datetime.fromtimestamp(since - 86400, timezone.utc).strftime('%Y-%m-%dT%H:%M:%S')    # a day's margin for the string compare
    out = []
    for b in db.execute("SELECT id, ticket, args, submitted, started, finished FROM batch WHERE submitted >= ? ORDER BY id", (iso,)):
        if not b['started'] or stamp(b['submitted']) < since:
            continue
        run = stamp(b['finished']) - stamp(b['started']) if b['finished'] else None
        games = db.execute('SELECT COUNT(*), SUM(attempt) FROM run WHERE batch = ?', (b['id'],)).fetchone()
        out.append([b['id'], b['ticket'] or '', round(stamp(b['submitted'])), round(stamp(b['started']) - stamp(b['submitted'])),
                    round(run) if run else None, games[0], games[1] or 0, ' '.join(json.loads(b['args'] or '[]')[:6])])
    return out


def collect_games(db, since):
    """Game time per hour: {'hours': [[hour, games, seconds]], 'scenarios': [[name, install]], 'rows': [[hour, scenario index,
    games, seconds, longest]]}. The rows are only for the SLOW_KEPT slowest standalone scenarios (the table lists 25)."""
    epoch = "CAST(strftime('%s', time) AS INTEGER)"
    hour = f'CAST({epoch} / 3600 AS INTEGER)'
    hours = [[r[0], r[1], round(r[2] or 0)] for r in db.execute(
        f'SELECT {hour} h, COUNT(*), SUM(seconds) FROM timing WHERE {epoch} >= ? GROUP BY h ORDER BY h', (int(since),))]
    scen = [[r[0], r[1]] for r in db.execute(
        f"SELECT scenario, install FROM timing WHERE {epoch} >= ? AND install LIKE 'standalone%' "
        'GROUP BY scenario, install HAVING COUNT(*) > 2 ORDER BY AVG(seconds) DESC LIMIT ?', (int(since), SLOW_KEPT))]
    index = {tuple(s): i for i, s in enumerate(scen)}
    rows = []
    for r in db.execute(f'SELECT {hour} h, scenario, install, COUNT(*), SUM(seconds), MAX(seconds) FROM timing '
                        f"WHERE {epoch} >= ? AND install LIKE 'standalone%' GROUP BY h, scenario, install ORDER BY h", (int(since),)):
        i = index.get((r[1], r[2]))
        if i is not None:
            rows.append([r[0], i, r[3], round(r[4], 1), round(r[5], 1)])
    return {'hours': hours, 'scenarios': scen, 'rows': rows}


# ---- one window of the rows: the same computation as waits.html (tools/test_waits.py compares them)

def view(data, start, end):
    """Everything the page shows for [start, end) (epoch seconds), from the rows build() wrote."""
    ws = [dict(w) for w in data['workers'] if start <= w['end'] < end]
    for w in ws:
        w['wall'] = w['end'] - w['start']
        w['build_seconds'] = sum(c[1] for c in w['builds'])
        w['test_seconds'] = sum(c[1] for c in w['tests'])
        w['suite_count'] = sum(1 for c in w['tests'] if c[2].startswith('suite'))
    bs = [b for b in data['batches'] if start <= b[2] < end]
    g = data['games']
    games = [h for h in g['hours'] if start <= h[0] * 3600 < end]
    acc = {}
    for h, i, n, sm, mx in g['rows']:
        if start <= h * 3600 < end:
            a = acc.setdefault(i, [0, 0.0, 0.0])
            a[0] += n
            a[1] += sm
            a[2] = max(a[2], mx)
    slow = sorted(({'scenario': g['scenarios'][i][0], 'install': g['scenarios'][i][1], 'n': a[0], 'avg': round(a[1] / a[0], 1),
                    'max': round(a[2], 1)} for i, a in acc.items() if a[0] > 2), key=lambda s: -s['avg'])[:25]
    tot = defaultdict(float, reasoning=0.0, builds=0.0, tests=0.0)
    for w in ws:
        tot['reasoning'] += w['model']
        tot['builds'] += w['build_seconds']
        tot['tests'] += w['test_seconds']
        for k, v in list(w['fg'].items()) + list(w['bg'].items()):
            if k not in ('build', 'test'):
                tot[k] += v
    found, bmed, smed = findings(ws, bs, slow)
    return {'workers': ws, 'batches': bs, 'slow': slow, 'findings': found, 'median_build': bmed, 'median_suite': smed,
            'totals': {k: round(v) for k, v in sorted(tot.items(), key=lambda kv: -kv[1])},
            'game_runs': sum(h[1] for h in games), 'game_seconds': sum(h[2] for h in games)}


def findings(workers, batches, slow):
    """The waits worth acting on, largest first."""
    out = []
    done = [w for w in workers if not w['live']]
    bmed = median([b[1] for w in workers for b in w['builds'] if 'verify' in b[2] or 'release' in b[2]])
    smed = median([t[1] for w in workers for t in w['tests'] if t[2].startswith('suite') and t[1] > 20])
    for w in done:
        extra = max(0, w['suite_count'] - 2)
        if extra:
            secs = sum(sorted(t[1] for t in w['tests'] if t[2].startswith('suite'))[:extra])
            out.append({'seconds': secs, 'what': f"{'/'.join(w['tickets'])}: {w['suite_count']} golden suites "
                        f"(2 expected: image + Rebirth), {extra} extra"})
        nb = len(w['builds'])
        if nb > 3:
            secs = sum(sorted(b[1] for b in w['builds'])[:nb - 3])
            out.append({'seconds': secs, 'what': f"{'/'.join(w['tickets'])}: {nb} build calls"})
    qs = [b for b in batches if b[3] > 120]
    if qs:
        out.append({'seconds': sum(b[3] for b in qs),
                    'what': f'{len(qs)} test batches queued over 2 min for game slots (longest {max(b[3] for b in qs) // 60} min)'})
    for s in slow[:5]:
        if s['avg'] > 40:
            out.append({'seconds': round(s['avg'] * s['n']),
                        'what': f"scenario {s['scenario']} ({s['install']}) averages {s['avg']:.0f} s over {s['n']} runs"})
    out.sort(key=lambda f: -f['seconds'])
    return out, bmed, smed


def build(days):
    since = time.time() - days * 86400
    db = work.connect()
    workers = collect_workers(days, db)
    data = {'generated': time.time(), 'days': days, 'workers': workers,
            'batches': collect_batches(db, since), 'games': collect_games(db, since)}
    db.close()
    return data


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--days', type=float, default=DAYS, help='how far back the rows go')
    ap.add_argument('--out', default=os.path.join(work.main_checkout(), 'build', 'live', 'waits.json'))
    ap.add_argument('--print', action='store_true', help='print the findings and per-worker summary of the last --window-days')
    ap.add_argument('--window-days', type=float, default=7)
    a = ap.parse_args()
    data = build(a.days)
    with open(a.out, 'w', encoding='utf-8') as f:
        json.dump(data, f, separators=(',', ':'))
    if a.print:
        now = time.time()
        v = view(data, now - a.window_days * 86400, now + 1)
        print(f"{len(v['workers'])} workers, last {a.window_days:g} days; median verify/release build call "
              f"{v['median_build']} s, median golden suite {v['median_suite']} s")
        print('totals (min):', {k: round(x / 60) for k, x in v['totals'].items()})
        for f in v['findings'][:12]:
            print(f"  {f['seconds'] / 60:6.1f} min  {f['what']}")
        for w in v['workers'][:15]:
            print(f"  {'/'.join(w['tickets']):18s} wall {w['wall'] / 60:5.0f}  reason {w['model'] / 60:4.0f}  "
                  f"build {w['build_seconds'] / 60:4.0f} ({len(w['builds'])})  test {w['test_seconds'] / 60:4.0f} "
                  f"({len(w['tests'])}, {w['suite_count']} suites)")


if __name__ == '__main__':
    main()
