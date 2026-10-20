"""Tests of tools/work_sched.py's scheduling without games: a fake runner sleeps instead of playing.

usage: python tools/test_work_sched.py [-v]
Each test has its own database and slot folder in a temporary directory (the machine's slots are not
touched), so it can run beside real suites.
"""
import contextlib, datetime, io, json, os, subprocess, sys, tempfile, threading, time, unittest

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


REAL_RUN = subprocess.run     # tests that fake the game replace subprocess.run (git still needs the real one)


def is_game(cmd):
    return os.path.basename(str(cmd[0])).lower() == 'ei_launch.exe'


class Fake:
    """A runner that sleeps durations[scenario] and returns results[scenario] (a list: one per attempt)."""

    def __init__(self, durations=None, results=None, default=0.2, gates=None):
        self.durations, self.results, self.default = durations or {}, results or {}, default
        self.gates = gates or {}       # scenario -> Event the run waits for (instead of sleeping): the test decides when it ends
        self.gate_timed_out = False    # a gate nobody opened in time: the scheduler held up what should have opened it
        self.launches = []   # (batch, scenario) in the order the scheduler started them (its thread, so no thread jitter)
        self.spans = []      # (batch, scenario, start, end)
        self.lock = threading.Lock()

    def __call__(self, batch, run, retries, on_attempt):
        attempts = iter(range(retries + 1))

        def one(name, install, folder):
            on_attempt(next(attempts))
            t0 = time.time()
            if name in self.gates:
                if not self.gates[name].wait(30):
                    self.gate_timed_out = True
            else:
                time.sleep(self.durations.get(name, self.default))
            t1 = time.time()
            with self.lock:
                self.spans.append((batch['id'], name, t0, t1))
                seq = self.results.get(name, ['PASS'])
                result = seq.pop(0) if len(seq) > 1 else seq[0]
            return result, t1 - t0, [] if result == 'PASS' else [f'{name} failed'], {}, []
        return run_all.run_with_retries(run['scenario'], run['install'], retries, os.path.basename(run['folder']),
                                        runner=one)

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
        launches = getattr(fake, 'launches', None)
        if launches is not None:
            start = s.start

            def recording_start(batch, run, *args):
                started = start(batch, run, *args)
                if started:
                    launches.append((batch['id'], run['scenario']))
                return started
            s.start = recording_start
        return s

    def submit(self, names, argv=(), expected=None, alone=(), build='b', submitter=None):
        build = f'{self.root}/{build}'
        os.makedirs(build, exist_ok=True)
        runs =[{'scenario': n, 'install': 'vanilla', 'hooks': False, 'alone': n in alone,
                 'expected': (expected or {}).get(n), 'folder': f'{build}/runs/{n}-vanilla'} for n in names]
        return work_sched.submit(self.db, 'T1', self.root, build, list(names) + list(argv), runs, submitter)

    def drive(self, s, timeout=30):
        end = time.time() + timeout
        while s.tick():
            self.assertLess(time.time(), end, 'scheduler did not finish')
            time.sleep(0.02)

    def drive_opening(self, s, fake, gate, when, timeout=60):
        """Drive the scheduler; the first time when() holds, open the gate (games waiting on it end)."""
        end = time.time() + timeout
        opened = False
        while s.tick():
            self.assertLess(time.time(), end, 'scheduler did not finish')
            if not opened and when():
                gate.set()
                opened = True
            time.sleep(0.02)
        self.assertTrue(opened, 'the condition for opening the gate never came')
        self.assertFalse(fake.gate_timed_out, 'the scheduler did not start what should have started while the gate was shut')

    def batch(self, b):
        return self.db.execute('SELECT * FROM batch WHERE id = ?', (b,)).fetchone()

    def runs(self, b):
        return {r['scenario']: r for r in self.db.execute('SELECT * FROM run WHERE batch = ?', (b,))}

    def test_fifo_and_backfill(self):
        # a5 and a6 (the shortest, so last) stay running until a batch-b run has started: that is the backfill
        gate = threading.Event()
        fake = Fake({'a1': .5, 'a2': .5, 'a3': .5, 'a4': .5}, gates={'a5': gate, 'a6': gate})
        a = self.submit(['a5', 'a6', 'a1', 'a2', 'a3', 'a4'], expected={'a1': 50, 'a2': 50, 'a3': 50, 'a4': 50, 'a5': 9, 'a6': 9})
        b = self.submit(['b1', 'b2', 'b3', 'b4'], build='b2')
        self.drive_opening(self.sched(fake), fake, gate, lambda: any(x[0] == b for x in fake.launches))
        order = [(x[0], x[1]) for x in fake.launches]
        a_at = [i for i, x in enumerate(order) if x[0] == a]
        b_at = [i for i, x in enumerate(order) if x[0] == b]
        self.assertLess(max(a_at), min(b_at), 'the later batch started before the earlier had all started')
        long_ones = [order.index((a, n)) for n in ('a1', 'a2', 'a3', 'a4')]
        short_ones = [order.index((a, n)) for n in ('a5', 'a6')]
        self.assertLess(max(long_ones), min(short_ones), 'not longest first')
        self.assertLessEqual(fake.most_at_once(), 4)
        for x in (a, b):
            self.assertEqual((self.batch(x)['state'], self.batch(x)['exit_code']), ('finished', 0))
            self.assertIn('ALL PASS', self.batch(x)['summary'])

    def test_small_batch_goes_ahead_of_a_big_one(self):
        fake = Fake(default=0.3)
        big = [f'a{i}' for i in range(16)]
        a = self.submit(big, expected={n: 30 for n in big})
        b = self.submit(['b1'], expected={'b1': 10}, build='b2')
        self.drive(self.sched(fake))
        self.assertLess(min(fake.starts(b)), max(fake.starts(a)) - 0.1, 'the one-run batch waited for the big one to start')

    def test_waiting_big_batch_gets_its_turn(self):
        a = self.submit([f'a{i}' for i in range(8)], expected={f'a{i}': 30 for i in range(8)})
        b = self.submit(['b1'], expected={'b1': 10}, build='b2')
        rows = {r['id']: r for r in self.db.execute('SELECT * FROM batch')}
        runs = lambda x: self.db.execute('SELECT * FROM run WHERE batch = ?', (x,)).fetchall()
        t = datetime.datetime.fromisoformat(rows[a]['submitted']).timestamp()
        self.assertLess(work_sched.batch_priority(rows[b], runs(b), 4, t), work_sched.batch_priority(rows[a], runs(a), 4, t))
        later = t + 120     # a has waited 2 min for 60 s of machine work; a new small batch comes in then
        fresh = dict(rows[b], submitted=datetime.datetime.fromtimestamp(later).astimezone().isoformat())
        self.assertLess(work_sched.batch_priority(rows[a], runs(a), 4, later), work_sched.batch_priority(fresh, runs(b), 4, later))

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

    def test_slow_summary_does_not_hold_the_loop(self):
        # a batch's post-game checks (--widescreen frame comparisons) take minutes: the next batch's
        # games start meanwhile and the scheduler keeps ticking.  The summary here waits for the test to
        # release it (not for a time), so the test depends on what the loop did, never on how fast it ran.
        fake = Fake(default=0.2)
        slow = self.submit(['a1'])
        later = self.submit([f'b{i}' for i in range(1, 10)], build='b2')   # three waves on 4 slots
        summary = run_all.summary
        entered, release, released_by_test = threading.Event(), threading.Event(), []

        def slow_summary(jobs, *a, **k):
            if jobs == [('a1', 'vanilla')]:
                entered.set()
                released_by_test.append(release.wait(60))      # False: nothing released it, the loop was held
            return summary(jobs, *a, **k)
        run_all.summary = slow_summary
        try:
            s = self.sched(fake)
            ticks, end = 0, time.time() + 60
            while s.tick():
                self.assertLess(time.time(), end, 'scheduler did not finish')
                if entered.is_set() and not release.is_set():
                    ticks += 1
                    # the whole next batch has played while the summary is still being written, and the loop
                    # ticked 30 more times meanwhile
                    if ticks > 30 and len(fake.starts(later)) == 9 and len(fake.ends(later)) == 9:
                        release.set()
                time.sleep(0.02)
        finally:
            release.set()
            run_all.summary = summary
        self.assertEqual(released_by_test, [True], 'the main loop stood still during the summary')
        self.assertEqual((self.batch(slow)['state'], self.batch(slow)['exit_code']), ('finished', 0))
        self.assertIn('ALL PASS', self.batch(slow)['summary'])
        self.assertEqual((self.batch(later)['state'], self.batch(later)['exit_code']), ('finished', 0))

    def test_jobs_ignored(self):
        fake = Fake(default=0.3)
        self.submit(['a1', 'a2', 'a3', 'a4'], argv=['--jobs', '2'])
        self.drive(self.sched(fake))
        self.assertEqual(fake.most_at_once(), 4, "a batch's --jobs held it under the machine's slots")

    def test_later_alone_run_does_not_stop_backfill(self):
        gate = threading.Event()
        fake = Fake(default=0.3, gates={'a5': gate, 'a6': gate})     # a5, a6 run until c1 has started
        a = self.submit(['a1', 'a2', 'a3', 'a4', 'a5', 'a6'])
        b = self.submit(['w'], alone=['w'], build='b2')
        c = self.submit(['c1'], build='c2')
        self.drive_opening(self.sched(fake), fake, gate, lambda: any(x[0] == c for x in fake.launches))
        order = [x[1] for x in fake.launches]
        self.assertLess(order.index('c1'), order.index('w'), "a later batch's display-mode run held up the backfill")

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

    def test_pair_takes_two_slots(self):
        # SV5d: a scenario with a "peer" line plays two games, so it takes two of the machine's slots together.
        scenarios = os.path.join(self.tmp, 'tests', 'scenarios')
        os.makedirs(scenarios)
        with open(os.path.join(scenarios, 'p.txt'), 'w') as f:
            f.write('peer p\nwaitmenu\n')
        held = {}
        fake = Fake({'p': .5, 'q1': .3, 'q2': .3, 'q3': .3})
        call = fake.__call__

        class Recording(Fake):
            def __call__(self, batch, run, retries, on_attempt):
                held[run['scenario']] = run['slots_held']
                return call(batch, run, retries, on_attempt)
        recording = Recording()
        recording.__dict__ = fake.__dict__
        self.submit(['p', 'q1', 'q2', 'q3'])
        self.drive(self.sched(recording, count=3))
        self.assertEqual(held, {'p': 2, 'q1': 1, 'q2': 1, 'q3': 1})
        (p0,), (p1,) = fake.starts(names=['p']), [e for _, n, s, e in fake.spans if n == 'p']
        self.assertEqual(fake.most_at_once(p0 + 0.01, p1 - 0.01), 2, 'the pair and more than one other game on 3 slots')

    def hold(self, *names):
        """A process holding the named slot files (or the alone gate) until the test kills it."""
        ready = os.path.join(self.slots, 'ready')
        if os.path.exists(ready):
            os.remove(ready)
        holder = subprocess.Popen([sys.executable, '-c', HOLD, self.slots, '600', *names])
        end = time.time() + 60
        while not os.path.exists(ready):
            self.assertLess(time.time(), end, 'the holder did not start')
            time.sleep(0.02)
        return holder

    def test_plain_run_all_slots_and_gate(self):
        # another process (a plain run_all) holds two slots: at most two of ours run meanwhile.  It holds them
        # until the test is done with it, not for a time, so a slow machine cannot outlast the hold.
        os.makedirs(self.slots, exist_ok=True)
        holder = self.hold('slot00', 'slot01')
        try:
            fake = Fake(default=0.3)
            self.submit(['a1', 'a2', 'a3', 'a4', 'a5', 'a6'])
            self.drive(self.sched(fake), timeout=120)
            self.assertEqual(len(fake.spans), 6)
            self.assertLessEqual(fake.most_at_once(), 2)
        finally:
            holder.kill()
            holder.wait()
        # it holds the alone gate (gathering for a display-mode game): none of ours starts until it lets go
        holder = self.hold('alone')
        try:
            fake = Fake(default=0.1)
            self.submit(['c1', 'c2'], build='b2')
            s = self.sched(fake)
            end = time.time() + 1.0
            while time.time() < end:
                s.tick()
                time.sleep(0.02)
            self.assertEqual(fake.spans, [], 'a game started while the alone gate was held')
        finally:
            released = time.time()
            holder.kill()
            holder.wait()
        self.drive(s, timeout=120)
        self.assertEqual(len(fake.spans), 2)
        self.assertGreaterEqual(min(fake.starts()), released)

    def test_retries_and_summary(self):
        fake = Fake(results={'flaky': ['FAIL', 'PASS'], 'bad': ['FAIL']}, default=0.05)
        a = self.submit(['flaky', 'bad', 'good'], argv=['--retries', '1'])
        self.drive(self.sched(fake))
        runs = self.runs(a)
        self.assertEqual((runs['flaky']['result'], runs['flaky']['attempt']), ('PASS*', 1))
        self.assertEqual((runs['bad']['result'], runs['bad']['attempt']), ('FAIL', 1))
        self.assertEqual((runs['good']['result'], runs['good']['attempt']), ('PASS', 0))
        self.assertIsNone(runs['good']['counts'])        # WB11: cleared once the batch has its summary
        b = self.batch(a)
        self.assertEqual(b['exit_code'], 1)
        self.assertIn('not passed:', b['summary'])
        self.assertIn('3 runs in', b['summary'])
        self.assertTrue(b['summary'].rstrip().endswith('SOME RUNS FAILED'))
        times = json.load(open(os.path.join(self.tmp, 'b', 'run_times.json')))
        self.assertEqual(sorted(times), ['bad-vanilla', 'flaky-vanilla', 'good-vanilla'])

    def test_prune_counts(self):
        fake = Fake(default=0.05)
        a = self.submit(['x1', 'x2'])
        self.drive(self.sched(fake))
        db = self.db
        db.execute("UPDATE run SET counts = '{\"Fn\": [3, 0]}'")       # a database from before WB11
        self.assertEqual(work.prune_counts(db, say=lambda *_: None), 2)
        self.assertEqual(db.execute('SELECT count(*) FROM run WHERE counts IS NOT NULL').fetchone()[0], 0)
        self.assertEqual(work.prune_counts(db, say=lambda *_: None), 0)

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
            if not is_game(cmd):
                return REAL_RUN(cmd, **kw)       # the reference cache's git
            seen.append((cmd, kw.get('env')))
            return subprocess.CompletedProcess(cmd, 0, '', '')
        scenarios = os.path.join(self.tmp, 'tests', 'scenarios')
        os.makedirs(scenarios)
        open(os.path.join(scenarios, 'x.txt'), 'w').write('waitmenu\n')   # the original code: no EI_RENDER of run_all's own
        build = os.path.join(self.tmp, 'bld')
        old_run, old_environ = run_all.subprocess.run, dict(os.environ)
        run_all.subprocess.run = fake_subprocess_run
        os.environ['EI_PROFILE'] = 'scheduler-own'      # the scheduler's own, not any batch's
        os.environ['EI_BUILD_DIR'] = 'whatever'
        try:
            env = {'EI_VECTORS': '1', 'EI_PROFILE': '1'}
            row = {'worktree': self.tmp, 'build': build, 'env': json.dumps(env)}
            run = {'scenario': 'x', 'install': 'vanilla', 'hooks': 0, 'folder': build + '/runs/x-vanilla'}
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
        self.assertEqual((with_env['EI_VECTORS'], with_env['EI_PROFILE']), ('1', '1'))
        self.assertEqual(run_all.game_environment(with_env), env)
        self.assertEqual(run_all.game_environment(empty), {}, "the scheduler's own EI_PROFILE leaked into a batch")
        self.assertEqual(empty['EI_BUILD_DIR'], 'whatever')        # not a game setting: left alone
        self.assertIsNone(null)                                     # inherit
        self.assertIsNone(no_column)

        # and through the database: recorded at submit, absent (NULL) for a batch submitted without
        a = self.submit(['q'])
        self.assertIsNone(work_sched.batch_env(self.batch(a)))
        runs = [{'scenario': 'q', 'install': 'vanilla', 'hooks': False, 'alone': False, 'expected': None,
                 'folder': f'{self.root}/b/runs/q-vanilla'}]
        b = work_sched.submit(self.db, 'T1', self.root, f'{self.root}/b', ['q'], runs, None, env=env)
        self.assertEqual(work_sched.batch_env(self.batch(b)), env)

    def test_clock_reaches_the_game(self):
        seen = []
        def fake_subprocess_run(cmd, **kw):
            if not is_game(cmd):
                return REAL_RUN(cmd, **kw)       # the reference cache's git
            seen.append(kw.get('env'))
            return subprocess.CompletedProcess(cmd, 0, '', '')
        scenarios = os.path.join(self.tmp, 'tests', 'scenarios')
        os.makedirs(scenarios)
        open(os.path.join(scenarios, 'x.txt'), 'w').write('waitmenu\n')   # the original code: no EI_RENDER of run_all's own
        build = os.path.join(self.tmp, 'bld')
        old_run = run_all.subprocess.run
        run_all.subprocess.run = fake_subprocess_run
        try:
            run = {'scenario': 'x', 'install': 'vanilla', 'hooks': 0, 'folder': build + '/runs/x-vanilla'}
            row = {'worktree': self.tmp, 'build': build, 'env': None}
            work_sched.real_runner(dict(row, args=json.dumps(['x', '--clock', 'virtual,randtrace'])), run, 0, lambda n: None)
            work_sched.real_runner(dict(row, args=json.dumps(['x'])), run, 0, lambda n: None)
        finally:
            run_all.subprocess.run = old_run
        self.assertEqual(seen[0]['EI_HARNESS_CLOCK'], 'virtual,randtrace')
        self.assertIsNone(seen[1])                                  # no --clock: the game inherits

    def test_game_environment_names(self):
        got = run_all.game_environment({'EI_VECTORS': '1', 'EI_PROFILE': '1', 'EI_BUILD_DIR': 'x', 'EI_HARNESS_SLOTS': '4',
                                        'EI_OUT_DIR': 'o', 'EI_HARNESS': 'h', 'PATH': 'p', 'WORK_DB': 'd'})
        self.assertEqual(got, {'EI_VECTORS': '1', 'EI_PROFILE': '1'})

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
        end = time.time() + 60
        while not self.db.execute("SELECT 1 FROM process WHERE name = 'scheduler'").fetchone():
            self.assertLess(time.time(), end)
            time.sleep(0.05)
        open(code, 'w').write('two')
        self.assertEqual(proc.wait(timeout=30), 0)
        self.assertIn('new scheduler code', proc.stdout.read())
        proc.stdout.close()
        self.assertIsNone(self.db.execute("SELECT 1 FROM process WHERE name = 'scheduler'").fetchone())

    def start_scheduler(self, idle):
        env = dict(os.environ, WORK_DB=os.path.join(self.tmp, 'work.db'), WORK_SCHED_IDLE=str(idle))
        cmd = [sys.executable, os.path.join(work.ROOT, 'tools', 'work.py'), 'scheduler']
        proc = subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE, text=True)
        end = time.time() + 60
        while not self.db.execute("SELECT 1 FROM process WHERE name = 'scheduler'").fetchone():
            self.assertLess(time.time(), end)
            time.sleep(0.05)
        return cmd, env, proc

    def test_one_scheduler(self):
        # its idle exit is far away: a loaded machine starting the second process late must not end the first
        cmd, env, first = self.start_scheduler(600)
        try:
            second = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=120)
            self.assertIn('already running', second.stdout)
            self.assertIsNone(first.poll(), 'the first scheduler left')
        finally:
            first.kill()
            first.wait()
            first.stdout.close()

    def test_idle_exit(self):
        _, _, first = self.start_scheduler(1.5)
        self.assertEqual(first.wait(timeout=120), 0)
        self.assertIn('idle; exiting', first.stdout.read())
        first.stdout.close()
        self.assertIsNone(self.db.execute("SELECT 1 FROM process WHERE name = 'scheduler'").fetchone())

    # ---- R10: which renderer, alone, the reference cache

    def scenario(self, name, text, root=None):
        folder = os.path.join(root or self.tmp, 'tests', 'scenarios')
        os.makedirs(folder, exist_ok=True)
        path = os.path.join(folder, name + '.txt')
        with open(path, 'w') as f:
            f.write(text)
        return path

    def test_renderer_choice_and_alone(self):
        """S21c: our code runs only in the image (our renderer, never alone); game.exe runs the original code on
        dgVoodoo, alone when the scenario sets the display mode."""
        plain = self.scenario('plain', 'waitmenu\n')
        wide = self.scenario('wide', 'resolution desktop\nwidescreen 1\n')
        for install in ('standalone', 'standalone-rebirth'):
            self.assertTrue(run_all.our_code(install))
            self.assertFalse(run_all.runs_alone(wide, install))          # our renderer changes no display mode
        for install in ('vanilla', 'rebirth', 'rebirth-nohooks', 'native'):
            self.assertFalse(run_all.our_code(install))
        self.assertTrue(run_all.runs_alone(wide, 'rebirth'))              # --widescreen --record's rebirth-nohooks
        self.assertFalse(run_all.runs_alone(plain, 'vanilla'))

    def test_game_exe_runs_the_original(self):
        """S21c: a game.exe run is always ei_launch --no-hooks without EI_RENDER, the image always --standalone with
        our renderer; an install that is neither (native, a hooked row from an older checkout) plays no game."""
        seen = []

        def fake_subprocess_run(cmd, **kw):
            if not is_game(cmd):
                return REAL_RUN(cmd, **kw)
            seen.append((cmd, kw.get('env')))
            return subprocess.CompletedProcess(cmd, 0, '', '')
        self.scenario('plain', 'waitmenu\n')
        build = os.path.join(self.tmp, 'bld')
        old_run = run_all.subprocess.run
        run_all.subprocess.run = fake_subprocess_run
        try:
            for install, kw in (('vanilla', {'fresh': True}), ('rebirth', {'fresh': True}),
                                ('vanilla', {'fresh': True, 'game_env': {'EI_RENDER': 'new,present', 'EI_VECTORS': '1'}}),
                                ('standalone', {'game_env': {'EI_RENDER': 'new'}})):
                run_all.run('plain', install, root=self.tmp, build=build, take_slot=False, **kw)
            native = run_all.run('plain', 'native', root=self.tmp, build=build, take_slot=False)
            row = {'worktree': self.tmp, 'build': build, 'env': None}
            hooked = work_sched.real_runner(row, {'scenario': 'plain', 'install': 'vanilla', 'hooks': 1,
                                                  'folder': build + '/runs/plain-vanilla'}, 1, lambda n: None)
        finally:
            run_all.subprocess.run = old_run
        self.assertEqual(len(seen), 4, 'a game was played for an unknown install or a hooked row')
        for cmd, env in seen[:3]:
            self.assertIn('--no-hooks', cmd)
            self.assertNotIn('--standalone', cmd)
            self.assertNotIn('EI_RENDER', os.environ if env is None else env)
        self.assertEqual(seen[2][1]['EI_VECTORS'], '1')                 # vector recording: the original's runs
        self.assertIn('--standalone', seen[3][0])
        self.assertNotIn('--no-hooks', seen[3][0])
        self.assertEqual(seen[3][1]['EI_RENDER'], 'new,present')
        self.assertEqual(native[0], 'FAIL')
        self.assertIn('unknown install', native[2][0])
        self.assertEqual(hooked[0], 'FAIL')
        self.assertIn('standalone image', hooked[2][0])

    def test_standalone(self):
        """--standalone (the default since S21): every scenario, install "standalone", ei_launch --standalone on run\\vanilla's
        game.exe, always our renderer (never the display mode), not with the original code."""
        seen = []

        def fake_subprocess_run(cmd, **kw):
            if not is_game(cmd):
                return REAL_RUN(cmd, **kw)
            seen.append((cmd, kw.get('env')))
            return subprocess.CompletedProcess(cmd, 0, 'ei_standalone: 504 imports resolved, 0 missing\n'
                                               'ei_standalone: the game returned 0\n', '')
        self.scenario('boot', 'waitmenu\nquit\n')
        wide = self.scenario('wide', 'render new\nresolution 800x600\nfullscreen 1\n')
        # The vanilla data only unless the Rebirth data is asked for (SV9)
        vanilla = [('boot', 'standalone'), ('wide', 'standalone')]
        both = [('boot', 'standalone'), ('boot', 'standalone-rebirth'), ('wide', 'standalone'), ('wide', 'standalone-rebirth')]
        self.assertEqual(run_all.plan(run_all.parse_args(['--standalone']), root=self.tmp), vanilla)
        self.assertEqual(run_all.plan(run_all.parse_args([]), root=self.tmp), vanilla)     # the default since S21
        self.assertEqual(run_all.plan(run_all.parse_args(['--all-rebirth']), root=self.tmp), both)
        self.assertEqual(run_all.plan(run_all.parse_args(['--no-hooks', 'boot']), root=self.tmp), [('boot', 'vanilla')])
        self.assertEqual(run_all.plan(run_all.parse_args(['--no-hooks', '--rebirth', 'boot']), root=self.tmp),
                         [('boot', 'vanilla'), ('boot', 'rebirth')])
        for gone in ('--hooked', '--native', '--dgvoodoo', '--verified-candidates'):    # S21c
            with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
                run_all.parse_args([gone, '1'] if gone == '--verified-candidates' else [gone])
        self.assertEqual(run_all.plan(run_all.parse_args(['--no-hooks', '--vanilla-only', 'boot']), root=self.tmp),
                         [('boot', 'vanilla')])
        # "# no original:" leaves the original's runs out on the installs it names (S21a), and only those
        flags = self.scenario('flags', '# no original: vanilla (flags the original lacks)\nwaitmenu\n')
        self.assertEqual(run_all.no_original(flags), {'vanilla'})
        self.assertEqual(run_all.plan(run_all.parse_args(['--no-hooks', '--rebirth', 'flags']), root=self.tmp),
                         [('flags', 'rebirth')])
        self.assertEqual(run_all.plan(run_all.parse_args(['--rebirth', 'flags']), root=self.tmp),
                         [('flags', 'standalone'), ('flags', 'standalone-rebirth')])
        os.remove(flags)
        # --rebirth without names: the smoke set, config/rebirth_scenarios.csv's marks and any scenario with a step for
        # one install (or rebirthflags, addonpath, widescreen)
        os.makedirs(os.path.join(self.tmp, 'config'), exist_ok=True)
        with open(os.path.join(self.tmp, 'config', 'rebirth_scenarios.csv'), 'w') as f:
            f.write('scenario,rebirth,why\nwide,1,test\n')
        self.assertEqual(run_all.plan(run_all.parse_args(['--rebirth']), root=self.tmp),
                         [('boot', 'standalone'), ('wide', 'standalone'), ('wide', 'standalone-rebirth')])
        with contextlib.redirect_stderr(io.StringIO()):   # wide: dgVoodoo's mode
            self.assertEqual(run_all.plan(run_all.parse_args(['--no-hooks', '--rebirth']), root=self.tmp), [('boot', 'vanilla')])
        os.remove(os.path.join(self.tmp, 'config', 'rebirth_scenarios.csv'))
        stepped = self.scenario('stepped', 'waitmenu\nrebirth click 1 1\n')
        self.assertEqual(run_all.plan(run_all.parse_args(['--rebirth-only']), root=self.tmp), [('stepped', 'standalone-rebirth')])
        os.remove(stepped)
        self.assertEqual(run_all.plan(run_all.parse_args(['--standalone', '--vanilla-only', 'wide']), root=self.tmp),
                         [('wide', 'standalone')])
        self.assertEqual(run_all.plan(run_all.parse_args(['--standalone', '--rebirth-only', 'wide']), root=self.tmp),
                         [('wide', 'standalone-rebirth')])
        with self.assertRaises(SystemExit):
            run_all.plan(run_all.parse_args(['--standalone', '--no-hooks']), root=self.tmp)
        self.assertFalse(run_all.runs_alone(wide, 'standalone'))
        build = os.path.join(self.tmp, 'bld')
        old_run = run_all.subprocess.run
        run_all.subprocess.run = fake_subprocess_run
        try:
            notes = run_all.run('boot', 'standalone', root=self.tmp, build=build, take_slot=False,
                                game_env={'EI_RENDER': 'new'})[4]
        finally:
            run_all.subprocess.run = old_run
        cmd, env = seen[0]
        self.assertIn('--standalone', cmd)
        self.assertEqual(cmd[cmd.index('--exe') + 1], os.path.join(run_all.run_dir(self.tmp), 'vanilla', 'game.exe'))
        self.assertEqual(env['EI_RENDER'], 'new,present')
        self.assertEqual(notes, ['ei_standalone: the game returned 0'])
        # standalone-rebirth: run\rebirth's game.exe in ei_launch's default (Rebirth) game folder, the ini's flags
        os.makedirs(os.path.join(self.tmp, 'config'), exist_ok=True)
        with open(os.path.join(self.tmp, 'config', 'ei_rebirth_standalone.ini'), 'w') as f:
            f.write('[Rebirth]\nDefault=0\nNivalRegistry=1\nWideScreen=1\nMaps=0\nNPlayers=7\n')
        run_all.subprocess.run = fake_subprocess_run
        try:
            run_all.run('boot', 'standalone-rebirth', root=self.tmp, build=build, take_slot=False, game_env={})
        finally:
            run_all.subprocess.run = old_run
        cmd, env = seen[1]
        self.assertIn('--standalone', cmd)
        self.assertNotIn('--game', cmd)
        self.assertEqual(cmd[cmd.index('--exe') + 1], os.path.join(run_all.run_dir(self.tmp), 'rebirth', 'game.exe'))
        self.assertEqual((env['EI_RENDER'], env['EI_REBIRTH']), ('new,present', 'NivalRegistry,WideScreen'))
        self.assertEqual(run_all.GOLDEN_INSTALL['standalone-rebirth'], 'rebirth')

    def test_reference_cache(self):
        repo = os.path.join(self.tmp, 'repo')
        env = dict(os.environ, GIT_AUTHOR_NAME='t', GIT_AUTHOR_EMAIL='t@t', GIT_COMMITTER_NAME='t', GIT_COMMITTER_EMAIL='t@t')
        git = lambda *a: subprocess.run(['git', *a], cwd=repo, env=env, capture_output=True, text=True, check=True).stdout.strip()
        scen = self.scenario('w', 'resolution desktop\n', repo)
        conf = os.path.join(repo, 'run', 'rebirth', 'dgVoodoo.conf')
        os.makedirs(os.path.dirname(conf))
        write = lambda path, text, mode='w': open(path, mode).write(text)
        write(conf, 'a')
        git('init', '-q'), git('add', 'tests'), git('commit', '-qm', 'w')
        build = os.path.join(self.tmp, 'bld')
        played = []

        def fake_subprocess_run(cmd, **kw):
            if not is_game(cmd):
                return REAL_RUN(cmd, **kw)
            out = cmd[cmd.index('--out') + 1]
            played.append(out)
            os.makedirs(os.path.join(out, 'frames'), exist_ok=True)
            os.makedirs(os.path.join(out, 'harness_saves'), exist_ok=True)
            write(os.path.join(out, 'frames', 'menu.bmp'), str(len(played)))
            write(os.path.join(out, 'harness_saves', 'big.sav'), 'x')
            write(os.path.join(out, 'harness_report.txt'), 'RESULT: PASS\nconsole output:\n  hello\n')
            return subprocess.CompletedProcess(cmd, 0, '', '')

        def failing(cmd, **kw):
            if not is_game(cmd):
                return REAL_RUN(cmd, **kw)
            out = cmd[cmd.index('--out') + 1]
            played.append(out)
            write(os.path.join(out, 'harness_report.txt'), 'RESULT: FAIL\nFAILED: x\n')
            return subprocess.CompletedProcess(cmd, 0, '', '')
        old_run, old_dir = run_all.subprocess.run, os.environ.get('EI_REFERENCE_DIR')
        run_all.subprocess.run = fake_subprocess_run
        os.environ['EI_REFERENCE_DIR'] = os.path.join(self.tmp, 'reference')
        frame = lambda: open(os.path.join(build, 'runs', 'w-rebirth-nohooks', 'frames', 'menu.bmp')).read()
        go = lambda **k: run_all.run('w', 'rebirth', 'w-rebirth-nohooks', root=repo, build=build, take_slot=False,
                                     game_env={}, **k)
        cached = lambda clock=None, environ=None, install='rebirth': run_all.cached_reference('w', install, repo, clock,
                                                                                              environ or {})
        try:
            self.assertFalse(cached())
            first = go()
            self.assertEqual((first[0], len(played)), ('PASS', 1))
            self.assertTrue(cached())
            self.assertFalse(cached(install='standalone-rebirth'))             # runs of our code: never
            second = go()
            self.assertEqual(len(played), 1, 'a cached reference run was played again')
            self.assertEqual((second[0], second[3]), ('PASS', {}))
            self.assertTrue(run_all.is_cached(second[4]))
            self.assertFalse(run_all.is_cached(first[4]))
            self.assertEqual(frame(), '1')
            self.assertEqual(run_all.console_of('w-rebirth-nohooks', build), ['  hello'])
            self.assertFalse(os.path.exists(os.path.join(build, 'runs', 'w-rebirth-nohooks', 'harness_saves', 'big.sav')))
            go(fresh=True)                                                     # --fresh plays it and keeps the newer one
            self.assertEqual((len(played), frame()), (2, '2'))
            go()
            self.assertEqual((len(played), frame()), (2, '2'))
            self.assertFalse(cached(environ={'EI_VECTORS': '1'}))              # other game settings
            self.assertFalse(cached(clock='real'))                             # another clock
            write(conf, 'b')                                                   # the install's dgVoodoo.conf changed
            self.assertFalse(cached())
            go()
            self.assertEqual(len(played), 3)
            write(scen, 'wait 10\n', 'a')                                      # uncommitted edits: never cached
            work._revs.clear()
            self.assertIsNone(run_all.reference_key('w', 'rebirth', repo, None, {}))
            go(), go()
            self.assertEqual(len(played), 5)
            git('commit', '-qam', 'w2')                                        # a new commit: played once
            work._revs.clear()
            go(), go()
            self.assertEqual(len(played), 6)
            self.assertLessEqual(len(os.listdir(os.path.join(self.tmp, 'reference', 'w-rebirth'))), run_all.REFERENCE_KEEP)
            run_all.subprocess.run = failing                                   # a failed run is not kept
            write(conf, 'c')
            self.assertEqual(go()[0], 'FAIL')
            self.assertFalse(cached())
        finally:
            run_all.subprocess.run = old_run
            if old_dir is None:
                os.environ.pop('EI_REFERENCE_DIR', None)
            else:
                os.environ['EI_REFERENCE_DIR'] = old_dir
            work._revs.clear()

    def test_cached_runs_are_no_times(self):
        fake = Fake(default=0.05)

        def runner(batch, run, retries, on_attempt):
            r = fake(batch, run, retries, on_attempt)
            return r[:4] + ([run_all.CACHED_NOTE + 'k'],) if run['scenario'] == 'c' else r
        self.submit(['p', 'c'])
        self.drive(self.sched(runner))
        self.assertEqual([r['scenario'] for r in self.db.execute('SELECT scenario FROM timing')], ['p'])


if __name__ == '__main__':
    unittest.main()
