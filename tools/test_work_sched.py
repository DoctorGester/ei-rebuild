"""Tests of tools/work_sched.py's scheduling without games: a fake runner sleeps instead of playing.

usage: python tools/test_work_sched.py [-v]
Each test has its own database and slot folder in a temporary directory (the machine's slots are not
touched), so it can run beside real suites.
"""
import json, os, subprocess, sys, tempfile, threading, time, unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run_all, work, work_sched

HOLD = r'''
import msvcrt, os, sys, time
folder, seconds, names = sys.argv[1], float(sys.argv[2]), sys.argv[3:]
files = []
for n in names:
    f = open(os.path.join(folder, n), 'a+b'); msvcrt.locking(f.fileno(), msvcrt.LK_NBLCK, 1); files.append(f)
open(os.path.join(folder, 'ready'), 'w').close()
time.sleep(seconds)
'''


class Fake:
    """A runner that sleeps durations[scenario] and returns results[scenario] (a list: one per attempt)."""

    def __init__(self, durations=None, results=None, default=0.2):
        self.durations, self.results, self.default = durations or {}, results or {}, default
        self.spans = []      # (batch, scenario, start, end)
        self.lock = threading.Lock()

    def __call__(self, batch, run, retries, on_attempt):
        attempts = iter(range(retries + 1))

        def one(name, install, hooks, folder):
            on_attempt(next(attempts))
            t0 = time.time()
            time.sleep(self.durations.get(name, self.default))
            t1 = time.time()
            with self.lock:
                self.spans.append((batch['id'], name, t0, t1))
                seq = self.results.get(name, ['PASS'])
                result = seq.pop(0) if len(seq) > 1 else seq[0]
            return result, t1 - t0, [] if result == 'PASS' else [f'{name} failed'], {'Fn': (3, 0)}, []
        return run_all.run_with_retries(run['scenario'], run['install'], bool(run['hooks']), retries,
                                        os.path.basename(run['folder']), runner=one)

    def starts(self, batch=None, names=None):
        return [s for b, n, s, e in self.spans if (batch is None or b == batch) and (names is None or n in names)]

    def ends(self, batch=None):
        return [e for b, n, s, e in self.spans if batch is None or b == batch]

    def most_at_once(self, t0=0, t1=1e18):
        events = sorted([(s, 1) for _, _, s, e in self.spans if s < t1 and e > t0] +
                        [(e, -1) for _, _, s, e in self.spans if s < t1 and e > t0])
        now = most = 0
        for _, d in events:
            now += d
            most = max(most, now)
        return most


class SchedTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix='wb2_')
        self.db = work.connect(os.path.join(self.tmp, 'work.db'))
        self.slots = os.path.join(self.tmp, 'slots')
        self.root = work.norm(self.tmp)

    def tearDown(self):
        self.db.close()

    def sched(self, fake, count=4, code=None):
        s = work_sched.Scheduler(self.db, runner=fake, slots=work_sched.SlotPool(count, self.slots), code=code or [])
        s.last_check = time.time() + 3600     # no submitter checks unless a test asks
        return s

    def submit(self, names, argv=(), expected=None, alone=(), build='b', submitter=None):
        build = f'{self.root}/{build}'
        os.makedirs(build, exist_ok=True)
        runs =[{'scenario': n, 'install': 'vanilla', 'hooks': True, 'alone': n in alone,
                 'expected': (expected or {}).get(n), 'folder': f'{build}/runs/{n}-vanilla'} for n in names]
        return work_sched.submit(self.db, 'T1', self.root, build, list(names) + list(argv), runs, submitter)

    def drive(self, s, timeout=30):
        end = time.time() + timeout
        while s.tick():
            self.assertLess(time.time(), end, 'scheduler did not finish')
            time.sleep(0.02)

    def batch(self, b):
        return self.db.execute('SELECT * FROM batch WHERE id = ?', (b,)).fetchone()

    def runs(self, b):
        return {r['scenario']: r for r in self.db.execute('SELECT * FROM run WHERE batch = ?', (b,))}

    def test_fifo_and_backfill(self):
        fake = Fake({'a1': .5, 'a2': .5, 'a3': .5, 'a4': .5, 'a5': .2, 'a6': .2})
        a = self.submit(['a5', 'a6', 'a1', 'a2', 'a3', 'a4'], expected={'a1': 50, 'a2': 50, 'a3': 50, 'a4': 50, 'a5': 9, 'a6': 9})
        b = self.submit(['b1', 'b2', 'b3', 'b4'], build='b2')
        self.drive(self.sched(fake))
        self.assertLessEqual(max(fake.starts(a)), min(fake.starts(b)) + 0.05, 'the later batch started before the earlier had all started')
        self.assertLess(min(fake.starts(b)), max(fake.ends(a)) - 0.1, 'no backfill while the earlier batch ran')
        self.assertLessEqual(max(fake.starts(a, ['a1', 'a2', 'a3', 'a4'])), min(fake.starts(a, ['a5', 'a6'])) + 0.05, 'not longest first')
        self.assertEqual(fake.most_at_once(), 4)
        for x in (a, b):
            self.assertEqual((self.batch(x)['state'], self.batch(x)['exit_code']), ('finished', 0))
            self.assertIn('ALL PASS', self.batch(x)['summary'])

    def test_unparseable_arguments_fail_the_batch(self):
        fake = Fake(default=0.2)
        bad = self.submit(['a1', 'a2'], argv=['--clock'])          # run_all: --clock needs a value
        good = self.submit(['b1'], build='b2')
        self.drive(self.sched(fake))
        b = self.batch(bad)
        self.assertEqual((b['state'], b['exit_code']), ('finished', 2))
        self.assertIn('FAILED', b['summary'])
        self.assertIn('--clock', b['summary'])
        self.assertIn('expected one argument', b['summary'])      # run_all's own error
        self.assertEqual({r['state'] for r in self.runs(bad).values()}, {'cancelled'})
        self.assertEqual(fake.starts(bad), [], 'a batch with unusable arguments started a game')
        self.assertEqual((self.batch(good)['state'], self.batch(good)['exit_code']), ('finished', 0))
        # the waiting `work.py test` prints the summary and returns the code
        import io, contextlib
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = work_sched.wait(self.db, bad, self.root, f'{self.root}/b')
        self.assertEqual(code, 2)
        self.assertIn('expected one argument', out.getvalue())

    def test_jobs_ignored(self):
        fake = Fake(default=0.3)
        self.submit(['a1', 'a2', 'a3', 'a4'], argv=['--jobs', '2'])
        self.drive(self.sched(fake))
        self.assertEqual(fake.most_at_once(), 4, "a batch's --jobs held it under the machine's slots")

    def test_later_alone_run_does_not_stop_backfill(self):
        fake = Fake(default=0.3)
        a = self.submit(['a1', 'a2', 'a3', 'a4', 'a5', 'a6'])
        b = self.submit(['w'], alone=['w'], build='b2')
        c = self.submit(['c1'], build='c2')
        self.drive(self.sched(fake))
        self.assertLess(min(fake.starts(c)), min(fake.starts(b)), "a later batch's display-mode run held up the backfill")
        self.assertLess(min(fake.starts(c)), max(fake.ends(a)) - 0.05, 'no backfill while the earlier batch ran')

    def test_display_mode_runs_alone(self):
        fake = Fake({'a1': .4, 'a2': .4, 'a3': .4, 'a4': .4, 'w': .3, 'b1': .2, 'b2': .2})
        a = self.submit(['a1', 'a2', 'a3', 'a4'])
        b = self.submit(['b1', 'w', 'b2'], alone=['w'], build='b2')
        self.drive(self.sched(fake))
        (w0,), (w1,) = fake.starts(b, ['w']), [e for _, n, s, e in fake.spans if n == 'w']
        self.assertGreaterEqual(w0, max(fake.ends(a)) - 0.01, 'the display-mode run started beside others')
        self.assertEqual(fake.most_at_once(w0 + 0.01, w1 - 0.01), 1)
        self.assertLessEqual(max(e for _, n, s, e in fake.spans if n in ('b1', 'b2')), w0 + 0.01)   # its other runs first
        self.assertEqual(self.runs(b)['w']['state'], 'finished')

    def test_plain_run_all_slots_and_gate(self):
        # another process (a plain run_all) holds two slots: at most two of ours run meanwhile
        os.makedirs(self.slots, exist_ok=True)
        holder = subprocess.Popen([sys.executable, '-c', HOLD, self.slots, '1.5', 'slot00', 'slot01'])
        while not os.path.exists(os.path.join(self.slots, 'ready')):
            time.sleep(0.02)
        t0 = time.time()
        fake = Fake(default=0.3)
        self.submit(['a1', 'a2', 'a3', 'a4', 'a5', 'a6'])
        self.drive(self.sched(fake))
        holder.wait()
        self.assertLessEqual(fake.most_at_once(t0, t0 + 1.3), 2)
        # it holds the alone gate (gathering for a display-mode game): none of ours starts
        os.remove(os.path.join(self.slots, 'ready'))
        holder = subprocess.Popen([sys.executable, '-c', HOLD, self.slots, '1.0', 'alone'])
        while not os.path.exists(os.path.join(self.slots, 'ready')):
            time.sleep(0.02)
        t0 = time.time()
        fake = Fake(default=0.1)
        self.submit(['c1', 'c2'], build='b2')
        self.drive(self.sched(fake))
        holder.wait()
        self.assertGreaterEqual(min(fake.starts()), t0 + 0.7)

    def test_retries_and_summary(self):
        fake = Fake(results={'flaky': ['FAIL', 'PASS'], 'bad': ['FAIL']}, default=0.05)
        a = self.submit(['flaky', 'bad', 'good'], argv=['--retries', '1'])
        self.drive(self.sched(fake))
        runs = self.runs(a)
        self.assertEqual((runs['flaky']['result'], runs['flaky']['attempt']), ('PASS*', 1))
        self.assertEqual((runs['bad']['result'], runs['bad']['attempt']), ('FAIL', 1))
        self.assertEqual((runs['good']['result'], runs['good']['attempt']), ('PASS', 0))
        self.assertEqual(json.loads(runs['good']['counts']), {'Fn': [3, 0]})
        b = self.batch(a)
        self.assertEqual(b['exit_code'], 1)
        self.assertIn('not passed:', b['summary'])
        self.assertIn('3 runs in', b['summary'])
        self.assertTrue(b['summary'].rstrip().endswith('SOME RUNS FAILED'))
        times = json.load(open(os.path.join(self.tmp, 'b', 'run_times.json')))
        self.assertEqual(sorted(times), ['bad-vanilla', 'flaky-vanilla', 'good-vanilla'])

    def test_gone_submitter_cancels_queued_runs(self):
        dead = subprocess.Popen([sys.executable, '-c', 'pass'])
        dead.wait()
        self.assertFalse(work_sched.pid_alive(dead.pid))
        self.assertTrue(work_sched.pid_alive(os.getpid()))
        fake = Fake(default=0.3)
        a = self.submit(['a1', 'a2', 'a3'], submitter=dead.pid)
        s = self.sched(fake, count=1)
        s.tick()                                  # a1 starts
        s.last_check = 0                          # the next pass checks submitters
        self.drive(s)
        states = sorted(r['state'] for r in self.runs(a).values())
        self.assertEqual(states, ['cancelled', 'cancelled', 'finished'])
        b = self.batch(a)
        self.assertEqual((b['state'], b['exit_code']), ('cancelled', 1))
        self.assertIsNotNone(b['finished'])

    def test_one_run_folder_at_a_time(self):
        fake = Fake(default=0.2)
        self.submit(['x', 'y'])
        self.submit(['x'])                         # same build: same run folder
        self.drive(self.sched(fake))
        xs = sorted((s, e) for _, n, s, e in fake.spans if n == 'x')
        self.assertEqual(len(xs), 2)
        self.assertGreaterEqual(xs[1][0], xs[0][1] - 0.01)

    def test_game_environment_per_batch(self):
        """WB5: a batch's games get the submitter's EI_* variables (and only those), os.environ is untouched,
        and a batch from before (no recorded env) inherits as it did."""
        seen = []

        def fake_subprocess_run(cmd, **kw):
            seen.append((cmd, kw.get('env')))
            return subprocess.CompletedProcess(cmd, 0, '', '')
        scenarios = os.path.join(self.tmp, 'tests', 'scenarios')
        os.makedirs(scenarios)
        open(os.path.join(scenarios, 'x.txt'), 'w').close()
        build = os.path.join(self.tmp, 'bld')
        old_run, old_environ = run_all.subprocess.run, dict(os.environ)
        run_all.subprocess.run = fake_subprocess_run
        os.environ['EI_PROFILE'] = 'scheduler-own'      # the scheduler's own, not any batch's
        os.environ['EI_BUILD_DIR'] = 'whatever'
        try:
            env = {'EI_PLAIN': '1', 'EI_PROFILE': '1'}
            row = {'worktree': self.tmp, 'build': build, 'env': json.dumps(env)}
            run = {'scenario': 'x', 'install': 'vanilla', 'hooks': 1, 'folder': build + '/runs/x-vanilla'}
            work_sched.real_runner(row, run, 0, lambda n: None)
            work_sched.real_runner(dict(row, env=json.dumps({})), run, 0, lambda n: None)
            work_sched.real_runner(dict(row, env=None), run, 0, lambda n: None)
            work_sched.real_runner({'worktree': self.tmp, 'build': build}, run, 0, lambda n: None)   # no env column at all
            self.assertEqual(os.environ['EI_PROFILE'], 'scheduler-own')
        finally:
            run_all.subprocess.run = old_run
            os.environ.clear()
            os.environ.update(old_environ)
        with_env, empty, null, no_column = (e for _, e in seen)
        self.assertEqual((with_env['EI_PLAIN'], with_env['EI_PROFILE']), ('1', '1'))
        self.assertEqual(run_all.game_environment(with_env), env)
        self.assertEqual(run_all.game_environment(empty), {}, "the scheduler's own EI_PROFILE leaked into a batch")
        self.assertEqual(empty['EI_BUILD_DIR'], 'whatever')        # not a game setting: left alone
        self.assertIsNone(null)                                     # inherit
        self.assertIsNone(no_column)

        # and through the database: recorded at submit, absent (NULL) for a batch submitted without
        a = self.submit(['q'])
        self.assertIsNone(work_sched.batch_env(self.batch(a)))
        runs = [{'scenario': 'q', 'install': 'vanilla', 'hooks': True, 'alone': False, 'expected': None,
                 'folder': f'{self.root}/b/runs/q-vanilla'}]
        b = work_sched.submit(self.db, 'T1', self.root, f'{self.root}/b', ['q'], runs, None, env=env)
        self.assertEqual(work_sched.batch_env(self.batch(b)), env)

    def test_game_environment_names(self):
        got = run_all.game_environment({'EI_PLAIN': '1', 'EI_PROFILE': '1', 'EI_BUILD_DIR': 'x', 'EI_HARNESS_SLOTS': '4',
                                        'EI_OUT_DIR': 'o', 'EI_HARNESS': 'h', 'PATH': 'p', 'WORK_DB': 'd'})
        self.assertEqual(got, {'EI_PLAIN': '1', 'EI_PROFILE': '1'})

    # ---- WB6: machine-wide expected run times

    def add_timing(self, scenario, install, rev, *seconds):
        for sec in seconds:
            self.db.execute("INSERT INTO timing (scenario, install, scenario_rev, seconds, time) VALUES (?, ?, ?, ?, '2026-10-14T00:00:00+02:00')",
                            (scenario, install, rev, sec))

    def test_expected_seconds_fallbacks(self):
        e = lambda *a, **k: work.expected_seconds(self.db, *a, **k)
        self.assertIsNone(e('s', 'vanilla', 'r1'))
        self.assertEqual(e('s', 'vanilla', 'r1', fallback=42), 42)                      # run_times.json
        self.add_timing('s', 'vanilla', 'old', 100, 100, 100)
        self.assertEqual(e('s', 'vanilla', 'r1', fallback=42), 100)                     # other rev beats run_times.json
        self.assertEqual(e('s', 'rebirth', 'r1', fallback=42), 42)                      # other install does not
        self.add_timing('s', 'vanilla', 'r1', 10, 12)
        self.assertEqual(e('s', 'vanilla', 'r1', fallback=42), 11)                      # this rev only, median
        self.add_timing('s', 'vanilla', 'r1', 50, 14, 16, 18, 20)                       # the last 5 count: 50 14 16 18 20
        self.assertEqual(e('s', 'vanilla', 'r1'), 18)
        self.assertEqual(e('s', 'vanilla', 'r1+dirty'), 18)                             # uncommitted: any rev, last 5 overall
        self.assertEqual(e('s', 'vanilla', 'r2'), 18)                                   # a changed scenario: latest rows at any rev
        self.add_timing('s', 'vanilla', 'r2', 3)
        self.assertEqual(e('s', 'vanilla', 'r2'), 3)                                    # ...until it has its own
        self.assertEqual(e('s', 'vanilla', 'r1'), 18)
        self.assertEqual(e('s', 'vanilla', 'r2+dirty'), 16)                             # any rev: 14 16 18 20 3
        times = work.expected_times(self.db, self.tmp, [('s', 'vanilla'), ('t', 'vanilla')], {'t-vanilla': 7})
        self.assertEqual(times[('t', 'vanilla')], 7)       # not a repository: rev '' -> any rev -> run_times.json
        self.assertEqual(times[('s', 'vanilla')], 16)

    def test_scenario_revs_follow_the_last_commit(self):
        repo = os.path.join(self.tmp, 'repo')
        os.makedirs(os.path.join(repo, 'tests', 'scenarios'))
        env = dict(os.environ, GIT_AUTHOR_NAME='t', GIT_AUTHOR_EMAIL='t@t', GIT_COMMITTER_NAME='t', GIT_COMMITTER_EMAIL='t@t')
        git = lambda *a: subprocess.run(['git', *a], cwd=repo, env=env, capture_output=True, text=True, check=True).stdout.strip()
        write = lambda n, text: open(os.path.join(repo, 'tests', 'scenarios', n + '.txt'), 'w').write(text)
        git('init', '-q')
        write('a', '1'), write('b', '1')
        git('add', '.'), git('commit', '-qm', 'a, b')
        write('b', '2')
        git('commit', '-qam', 'b')
        write('c', '1')
        git('add', '.'), git('commit', '-qm', 'c')
        revs = work.scenario_revs(repo, max_age=0)
        for n in 'abc':
            self.assertEqual(revs[n], git('log', '-1', '--format=%H', '--', f'tests/scenarios/{n}.txt'), n)
        self.assertEqual(len({revs[n] for n in 'abc'}), 3)
        write('a', '2')                                                        # uncommitted edit
        write('d', '1')                                                        # not even added
        revs2 = work.scenario_revs(repo, max_age=0)
        self.assertEqual((revs2['a'], revs2['b']), (revs['a'] + '+dirty', revs['b']))
        self.assertEqual(revs2['d'], '+dirty')
        self.assertEqual(work.scenario_revs(self.tmp, max_age=0), {})          # not a repository

    def test_scheduler_records_and_uses_timings(self):
        fake = Fake({'p': .05, 'f': .05, 'e': .05}, results={'f': ['FAIL'], 'e': ['ERROR']})
        a = self.submit(['p', 'f', 'e'], argv=['--retries', '0'])
        self.drive(self.sched(fake))
        rows = self.db.execute('SELECT scenario, install, scenario_rev, seconds, batch FROM timing ORDER BY id').fetchall()
        self.assertEqual(sorted(r['scenario'] for r in rows), ['f', 'p'])    # a run that errored is no time
        self.assertTrue(all(r['batch'] == a and r['install'] == 'vanilla' and r['scenario_rev'] == '' and r['seconds'] >= 0 for r in rows))
        self.assertEqual(work.expected_times(self.db, self.tmp, [('p', 'vanilla')])[('p', 'vanilla')],
                         self.db.execute("SELECT seconds FROM timing WHERE scenario = 'p'").fetchone()[0])

    # ---- WB6: leaving for new scheduler code

    def test_restart_on_new_code(self):
        code = os.path.join(self.tmp, 'sched_code.py')
        open(code, 'w').write('one')
        fake = Fake(default=0.4)
        a = self.submit(['a1', 'a2', 'a3', 'a4'])
        s = self.sched(fake, count=2, code=[code])
        s.last_check = 0
        s.tick()                                          # a1, a2 start
        self.assertEqual(len(s.games), 2)
        s.last_check = 0
        s.tick()
        self.assertFalse(s.stale, 'unchanged code made it leave')
        self.assertFalse(s.should_restart())
        open(code, 'w').write('two')
        s.last_check = 0
        s.tick()
        self.assertTrue(s.stale)
        self.assertFalse(s.should_restart(), 'left with games running')
        end = time.time() + 20
        while not s.should_restart():
            s.tick()
            self.assertLess(time.time(), end)
            time.sleep(0.02)
        self.assertEqual(len(fake.spans), 2, 'a game started after the code changed')
        states = sorted(r['state'] for r in self.runs(a).values())
        self.assertEqual(states, ['finished', 'finished', 'queued', 'queued'], 'queued runs must stay queued')
        self.assertEqual(self.batch(a)['state'], 'running')
        # the next scheduler (new code) takes the batch on
        self.drive(self.sched(fake, count=2))
        self.assertEqual((self.batch(a)['state'], self.batch(a)['exit_code']), ('finished', 0))

    def test_missing_code_file_is_not_a_change(self):
        s = self.sched(Fake(), code=[os.path.join(self.tmp, 'nope.py')])
        s.last_check = 0
        s.tick()
        self.assertFalse(s.stale)

    def test_scheduler_process_leaves_on_new_code(self):
        code = os.path.join(self.tmp, 'code.py')
        open(code, 'w').write('one')
        env = dict(os.environ, WORK_DB=os.path.join(self.tmp, 'work.db'), WORK_SCHED_CODE=code)
        cmd = [sys.executable, os.path.join(work.ROOT, 'tools', 'work.py'), 'scheduler']
        proc = subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE, text=True)
        end = time.time() + 10
        while not self.db.execute("SELECT 1 FROM process WHERE name = 'scheduler'").fetchone():
            self.assertLess(time.time(), end)
            time.sleep(0.05)
        open(code, 'w').write('two')
        self.assertEqual(proc.wait(timeout=30), 0)
        self.assertIn('new scheduler code', proc.stdout.read())
        proc.stdout.close()
        self.assertIsNone(self.db.execute("SELECT 1 FROM process WHERE name = 'scheduler'").fetchone())

    def test_one_scheduler_and_idle_exit(self):
        env = dict(os.environ, WORK_DB=os.path.join(self.tmp, 'work.db'), WORK_SCHED_IDLE='1.5')
        cmd = [sys.executable, os.path.join(work.ROOT, 'tools', 'work.py'), 'scheduler']
        first = subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE, text=True)
        end = time.time() + 10
        while not self.db.execute("SELECT 1 FROM process WHERE name = 'scheduler'").fetchone():
            self.assertLess(time.time(), end)
            time.sleep(0.05)
        second = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=20)
        self.assertIn('already running', second.stdout)
        self.assertEqual(first.wait(timeout=20), 0)
        self.assertIn('idle; exiting', first.stdout.read())
        first.stdout.close()
        self.assertIsNone(self.db.execute("SELECT 1 FROM process WHERE name = 'scheduler'").fetchone())


if __name__ == '__main__':
    unittest.main()
