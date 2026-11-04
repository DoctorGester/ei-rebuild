"""Tests of tools/worker_waits.py's windowed rows and of waits.html's model (the same totals in JavaScript).

usage: python tools/test_waits.py [-v]
Needs no transcripts and no running board: a temporary work database and synthetic worker rows. The JavaScript
comparison runs when node is on PATH.
"""
import json, os, shutil, subprocess, sys, tempfile, unittest
from datetime import datetime, timezone

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import work, worker_waits as ww

NOW = 1_793_296_800        # a fixed 'now', on a whole hour
H = 3600


def iso(t):
    return datetime.fromtimestamp(t, timezone.utc).strftime('%Y-%m-%dT%H:%M:%S+00:00')


def worker(end, tickets, model, builds=(), tests=(), live=False, fg=None):
    return {'tickets': tickets, 'start': end - 1800, 'end': end, 'live': live, 'model': model,
            'fg': fg or {'read': 50, 'shell': 20}, 'bg': {'wsl': 10},
            'builds': [[end - 1000 + 100 * i, s, w, 1] for i, (s, w) in enumerate(builds)],
            'tests': [[end - 900 + 100 * i, s, w, 0] for i, (s, w) in enumerate(tests)]}


def model_js():
    """The JavaScript between the model markers of waits.html (no DOM)."""
    with open(os.path.join(HERE, 'work_board', 'waits.html'), encoding='utf-8') as f:
        block = f.read().split('// ---- the model', 1)[1].split('// ---- end of the model', 1)[0]
    return block.split(chr(10), 1)[1]


class Case(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()
        self.db = work.connect(os.path.join(self.dir, 'work.db'))
        self.addCleanup(shutil.rmtree, self.dir, True)
        self.addCleanup(self.db.close)

    def add_game(self, scenario, install, seconds, t):
        self.db.execute('INSERT INTO timing(scenario, install, seconds, time) VALUES (?,?,?,?)', (scenario, install, seconds, iso(t)))

    def add_batch(self, submitted, queue, run, args=('a', 'b')):
        c = self.db.execute("INSERT INTO batch(ticket, worktree, build, args, submitted, started, finished, state) "
                            "VALUES ('T1', 'w', 'b', ?, ?, ?, ?, 'finished')",
                            (json.dumps(list(args)), iso(submitted), iso(submitted + queue), iso(submitted + queue + run)))
        self.db.execute("INSERT INTO run(batch, scenario, install, attempt) VALUES (?, 's', 'standalone', 1)", (c.lastrowid,))

    def data(self):
        for i in range(60):                                     # 60 hours, three standalone scenarios, one vanilla
            t = NOW - (60 - i) * H
            for _ in range(4):
                self.add_game('slow', 'standalone', 50 + i % 7, t + 5)
            for _ in range(3):
                self.add_game('quick', 'standalone-rebirth', 5, t + 7)
            self.add_game('old', 'vanilla', 9, t + 9)
        self.add_game('rare', 'standalone', 90, NOW - 10 * H)    # fewer than three runs: not in the slowest table
        for i in range(30):
            self.add_batch(NOW - (60 - 2 * i) * H, 30 + 20 * i, 60)
        ws = [worker(NOW - (50 - 3 * i) * H, [f'T{i}'], 300 + i, [(30, 'verify')] * (i % 6), [(200, 'suite image'), (120, 'suite rebirth'),
              (150, 'suite x'), (60, '2 scenarios')][:i % 5]) for i in range(16)]
        ws.append(worker(NOW - 100, ['LIVE'], 40, live=True))
        return {'generated': NOW, 'days': 30, 'workers': ws, 'batches': ww.collect_batches(self.db, NOW - 30 * 86400),
                'games': ww.collect_games(self.db, NOW - 30 * 86400)}

    def test_rows(self):
        d = self.data()
        self.assertEqual(len(d['batches']), 30)
        b = d['batches'][3]
        self.assertEqual([b[3], b[4], b[5], b[6], b[7]], [90, 60, 1, 1, 'a b'])
        self.assertEqual(sum(h[1] for h in d['games']['hours']), 60 * 8 + 1)
        names = {tuple(s) for s in d['games']['scenarios']}
        self.assertEqual(names, {('slow', 'standalone'), ('quick', 'standalone-rebirth')})   # no vanilla, no 'rare'
        self.assertLess(len(json.dumps(d)), 200_000)

    def test_windows_partition(self):
        """Adjacent windows hold every worker, batch and game once; the whole span equals their sum."""
        d = self.data()
        edges = [NOW - 72 * H + i * 12 * H for i in range(7)]       # six 12 h windows, whole hours
        parts = [ww.view(d, a, b) for a, b in zip(edges, edges[1:])]
        whole = ww.view(d, edges[0], edges[-1])
        self.assertEqual(sum(len(p['workers']) for p in parts), len(whole['workers']))
        self.assertEqual(len(whole['workers']), 17)
        self.assertEqual(sum(len(p['batches']) for p in parts), len(whole['batches']))
        self.assertEqual(sum(p['game_runs'] for p in parts), whole['game_runs'])
        self.assertEqual(sum(p['game_seconds'] for p in parts), whole['game_seconds'])
        for k in whole['totals']:
            self.assertEqual(sum(p['totals'].get(k, 0) for p in parts), whole['totals'][k], k)
        self.assertEqual(whole['game_runs'], 60 * 8 + 1)

    def test_slow(self):
        d = self.data()
        v = ww.view(d, NOW - 72 * H, NOW + H)
        self.assertEqual([(s['scenario'], s['n']) for s in v['slow']], [('slow', 240), ('quick', 180)])
        self.assertEqual(v['slow'][0]['max'], 56)
        narrow = ww.view(d, NOW - 3 * H, NOW)            # three hours: slow has 12 runs
        self.assertEqual(narrow['slow'][0]['n'], 12)

    @unittest.skipUnless(shutil.which('node'), 'node is not installed')
    def test_javascript_matches(self):
        """waits.html's model (compute) returns what worker_waits.view returns for the same rows and window."""
        d = self.data()
        model = model_js()
        js = os.path.join(self.dir, 'model.js')
        with open(js, 'w', encoding='utf-8') as f:
            f.write(model + "\nconst [data, start, end] = JSON.parse(require('fs').readFileSync(0, 'utf8'));\n"
                    "process.stdout.write(JSON.stringify(compute(data, start, end)));\n")
        for start, end in [(NOW - 72 * H, NOW + H), (NOW - 24 * H, NOW), (NOW - 30 * H, NOW - 12 * H), (NOW - 5 * H, NOW - 4 * H), (NOW + H, NOW + 2 * H)]:
            out = subprocess.run(['node', js], input=json.dumps([d, start, end]), capture_output=True, text=True, check=True).stdout
            got, want = json.loads(out), ww.view(d, start, end)
            for key in ('totals', 'game_runs', 'game_seconds', 'median_build', 'median_suite', 'batches'):
                self.assertEqual(got[key], want[key], (key, start, end))
            self.assertEqual([(w['tickets'], w['wall'], w['build_seconds'], w['test_seconds'], w['suite_count']) for w in got['workers']],
                             [(w['tickets'], w['wall'], w['build_seconds'], w['test_seconds'], w['suite_count']) for w in want['workers']])
            self.assertEqual(got['slow'], want['slow'])
            self.assertEqual(got['findings'], want['findings'])

    @unittest.skipUnless(shutil.which('node'), 'node is not installed')
    def test_window_arithmetic(self):
        """windowOf steps back by whole lengths from the next whole hour; maxBack stops where half the window is outside the data."""
        model = model_js()
        js = os.path.join(self.dir, 'win.js')
        now, span = NOW, NOW - 30 * 86400
        with open(js, 'w', encoding='utf-8') as f:
            f.write(model + f"\nconst out = {{w: windowOf('1d', 2, {now}), b: Object.keys(LENGTHS).map(k => maxBack(k, {now}, {span}))}};\n"
                    "process.stdout.write(JSON.stringify(out));\n")
        out = json.loads(subprocess.run(['node', js], capture_output=True, text=True, check=True).stdout)
        end = -(-NOW // H) * H - 2 * 86400
        self.assertEqual(out['w'], [end - 86400, end])
        self.assertTrue(out['b'][0] > out['b'][1] > out['b'][2] > out['b'][3] > out['b'][4] == 0)


if __name__ == '__main__':
    unittest.main(verbosity=2 if '-v' in sys.argv else 1, argv=[a for a in sys.argv if a != '-v'])
