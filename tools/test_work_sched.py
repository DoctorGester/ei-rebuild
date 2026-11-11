"""Tests of tools/work_sched.py's scheduling without games: a fake runner sleeps instead of playing.

usage: python tools/test_work_sched.py [-v]
Each test has its own database and slot folder in a temporary directory (the machine's slots are not
touched), so it can run beside real suites.
"""
import contextlib, datetime, io, json, os, shutil, subprocess, sys, tempfile, threading, time, unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import install_watch, run_all, work, work_sched

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

    def __init__(self, durations=None, results=None, default=0.2, gates=None, after=None):
        self.durations, self.results, self.default = durations or {}, results or {}, default
        self.after = after or {}       # scenario -> seconds of after-game work (frame packing), with the slots given back
        self.after_spans = []          # (scenario, start, end) of that work
        self.gates = gates or {}       # scenario -> Event the run waits for (instead of sleeping): the test decides when it ends
        self.gate_timed_out = False    # a gate nobody opened in time: the scheduler held up what should have opened it
        self.launches = []   # (batch, scenario) in the order the scheduler started them (its thread, so no thread jitter)
        self.spans = []      # (batch, scenario, start, end)
        self.lock = threading.Lock()

    def __call__(self, batch, run, retries, on_attempt, slots=None):
        attempts = iter(range(retries + 1))

        def one(name, install, folder):
            on_attempt(next(attempts))
            if slots:
                slots.acquire()
            t0 = time.time()
            if name in self.gates:
                if not self.gates[name].wait(30):
                    self.gate_timed_out = True
            else:
                time.sleep(self.durations.get(name, self.default))
            t1 = time.time()
            if slots:
                slots.release()
            if self.after.get(name):
                time.sleep(self.after[name])
                with self.lock:
                    self.after_spans.append((name, t1, time.time()))
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
        self.building = []      # the builds an alone run waits for (build_gate.compiling() in a real scheduler)

    def tearDown(self):
        self.db.close()

    def sched(self, fake, count=4, code=None):
        s = work_sched.Scheduler(self.db, runner=fake, slots=work_sched.SlotPool(count, self.slots), code=code or [],
                                 builds=lambda: list(self.building))
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
        runs =[{'scenario': n, 'install': 'standalone', 'hooks': True, 'alone': n in alone,
                 'expected': (expected or {}).get(n), 'folder': f'{build}/runs/{n}-standalone'} for n in names]
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
            if jobs == [('a1', 'standalone')]:
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

    def test_quiet_batch_holds_the_machine(self):
        # H64: a quiet batch's games each run by themselves, after the games ahead of them, and the gate stays held
        # from the first gathering to the end of its last game (no build or later game slips in between its runs)
        fake = Fake({'a1': .4, 'a2': .4, 'a3': .4, 'a4': .4, 'q1': .3, 'q2': .3}, default=.2)
        a = self.submit(['a1', 'a2', 'a3', 'a4'])
        q = self.submit(['q1', 'q2'], alone=['q1', 'q2'], build='b2')
        c = self.submit([f'c{i}' for i in range(8)], build='b3')     # backfills until the quiet batch is at the head
        s = self.sched(fake)
        gate_since, dropped = None, False
        end = time.time() + 30
        while s.tick():
            self.assertLess(time.time(), end, 'scheduler did not finish')
            quiet_left = self.db.execute("SELECT count(*) FROM run WHERE batch = ? AND state != 'finished'", (q,)).fetchone()[0]
            if s.pool.gate is not None and gate_since is None:
                gate_since = time.time()
            if gate_since is not None and quiet_left and s.pool.gate is None:
                dropped = True
            time.sleep(0.02)
        self.assertFalse(dropped, 'the gate was let go between the quiet runs')
        self.assertIsNone(s.pool.gate)
        spans = {n: (t0, t1) for _, n, t0, t1 in fake.spans}
        self.assertGreaterEqual(min(spans['q1'][0], spans['q2'][0]), max(fake.ends(a)) - 0.01)
        for n in ('q1', 'q2'):
            self.assertEqual(fake.most_at_once(spans[n][0] + 0.01, spans[n][1] - 0.01), 1, f'{n} ran beside another game')
        quiet_end = max(spans['q1'][1], spans['q2'][1])
        self.assertEqual([t for t in fake.starts(c) if gate_since - 0.05 < t < quiet_end - 0.01], [],
                         'a later batch started while the quiet one held the gate')
        self.assertTrue(any(t >= quiet_end - 0.01 for t in fake.starts(c)), 'nothing was left to slip in')

    def test_gathering_stops_backfill(self):
        # H64: once the head batch gathers the machine for its alone run, a later batch that batch_priority puts first
        # starts nothing more (before, it took each slot the gathering had not reached yet)
        gate = threading.Event()
        fake = Fake(default=0.2, gates={'a1': gate, 'a2': gate})
        self.submit(['a1', 'a2'], expected={'a1': 1, 'a2': 1})
        self.submit(['w'], alone=['w'], expected={'w': 1000}, build='b2')
        c = self.submit(['c1', 'c2', 'c3'], expected={'c1': 1, 'c2': 1, 'c3': 1}, build='b3')
        ended = []

        def when():
            if not ended and len([x for x in fake.spans if x[1] in ('c1', 'c2')]) == 2:
                ended.append(time.time())
            return bool(ended) and time.time() - ended[0] > 0.5   # c3 had half a second to start
        self.drive_opening(self.sched(fake), fake, gate, when)
        (w0, w1), = [(t0, t1) for _, n, t0, t1 in fake.spans if n == 'w']
        self.assertGreaterEqual(fake.starts(c, ['c3'])[0], w1 - 0.01, 'c3 started while the alone run gathered')

    def test_quiet_run_waits_for_builds(self):
        # H64: an alone run holds the gate (no new build starts) and starts once the running builds have ended
        self.building = [{'checkout': 'E:/x/agent-1', 'config': 'verify'}]
        fake = Fake(default=0.1)
        self.submit(['w'], alone=['w'])
        s = self.sched(fake)
        end = time.time() + 1.0
        while time.time() < end:
            s.tick()
            time.sleep(0.02)
        self.assertEqual(fake.spans, [], 'the quiet run started beside a build')
        self.assertIsNone(run_all._lock(os.path.join(self.slots, 'alone')), 'the gate was not held while it waited')
        self.building = []
        self.drive(s)
        self.assertEqual([n for _, n, _, _ in fake.spans], ['w'])
        self.assertIsNone(s.pool.gate)

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
            def __call__(self, batch, run, retries, on_attempt, slots=None):
                held[run['scenario']] = run['slots_held']
                return call(batch, run, retries, on_attempt, slots)
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
        self.assertEqual(sorted(times), ['bad-standalone', 'flaky-standalone', 'good-standalone'])

    def test_slot_free_when_the_game_ends(self):
        # SV10h: a run's after-game work (frame packing) runs without its slot; the next game starts as the game ends
        fake = Fake(default=0.3, after={n: 1.5 for n in ('a1', 'a2', 'a3', 'a4')})
        a = self.submit(['a1', 'a2', 'a3', 'a4'])
        s = self.sched(fake, count=2)
        self.drive(s)
        self.assertEqual(len(fake.spans), 4)
        self.assertLessEqual(fake.most_at_once(), 2)
        starts = sorted(fake.starts())
        self.assertLess(starts[2] - starts[0], 1.2, "the third game waited for the first run's packing")
        self.assertEqual(s.pool.held, {})
        self.assertEqual({r['result'] for r in self.runs(a).values()}, {'PASS'})

    def test_retry_takes_a_slot_again(self):
        # the retry's game asks for a slot again and gets it before a queued run starts
        fake = Fake({'flaky': 0.2, 'a': 0.4, 'b': 0.1}, results={'flaky': ['FAIL', 'PASS']}, after={'flaky': 0.1})
        a = self.submit(['flaky', 'a', 'b'], argv=['--retries', '1'], expected={'flaky': 30, 'a': 20, 'b': 10})
        self.drive(self.sched(fake, count=1))
        self.assertLessEqual(fake.most_at_once(), 1)
        self.assertEqual([n for _, n, s, e in sorted(fake.spans, key=lambda x: x[2])], ['flaky', 'a', 'flaky', 'b'])
        self.assertEqual(self.runs(a)['flaky']['result'], 'PASS*')

    def test_runs_past_their_game_are_bounded(self):
        # no new game while AFTER_GAME_MAX runs are still packing
        fake = Fake(default=0.05, after={f'x{i}': 0.6 for i in range(8)})
        self.submit([f'x{i}' for i in range(8)])
        old = work_sched.AFTER_GAME_MAX
        work_sched.AFTER_GAME_MAX = 2
        try:
            self.drive(self.sched(fake, count=4))
        finally:
            work_sched.AFTER_GAME_MAX = old
        self.assertEqual(len(fake.after_spans), 8)
        # a game starts only while fewer than 2 runs are past theirs (one more may have ended its game unseen)
        for t0 in fake.starts():
            self.assertLessEqual(sum(s < t0 < e for _, s, e in fake.after_spans), 2)

    def test_real_runner_gives_the_slot_back_around_the_game(self):
        events = []

        class Slots:
            def acquire(self):
                events.append('acquire')

            def release(self):
                events.append('release')

        def fake_launch(scenario, out, data, build, env=None):
            events.append('game')
            return 0, ''
        scenarios = os.path.join(self.tmp, 'tests', 'scenarios')
        os.makedirs(scenarios)
        open(os.path.join(scenarios, 'x.txt'), 'w').write('waitmenu\n')
        build = os.path.join(self.tmp, 'bld')
        with mock.patch.object(run_all, 'launch', fake_launch):
            run = {'scenario': 'x', 'install': 'standalone', 'hooks': 1, 'folder': build + '/runs/x-standalone'}
            work_sched.real_runner({'worktree': self.tmp, 'build': build, 'env': None}, run, 1, lambda n: None, Slots())
        self.assertEqual(events, ['acquire', 'game', 'release'] * 2)     # NO REPORT, so played again

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

    def test_folder_stays_with_the_batch_until_its_submitter_is_done(self):
        # H54: a rerun by name of a scenario of a suite emptied the run folder between the suite's last game and its
        # --golden comparison. A later batch's run waits for the folder while the earlier batch's work.py test lives.
        fake = Fake(default=0.1)
        a = self.submit(['x'], submitter=os.getpid())      # alive: still comparing when its batch has finished
        b = self.submit(['x'])
        s = self.sched(fake, count=4)
        end = time.time() + 5
        while self.batch(a)['state'] != 'finished' and time.time() < end:
            s.tick()
            time.sleep(0.02)
        self.assertEqual(self.batch(a)['state'], 'finished')
        for _ in range(15):
            s.tick()
            time.sleep(0.02)
        self.assertEqual(self.runs(b)['x']['state'], 'queued')
        self.db.execute('UPDATE batch SET submitter = NULL WHERE id = ?', (a,))     # its comparison is done
        self.drive(s)
        self.assertEqual(self.runs(b)['x']['state'], 'finished')

    def test_reused_submitter_pid_lets_the_folder_go(self):
        # R52c1: batch #7938's work.py test ended and Windows gave its pid to the next work.py test of the same
        # scenario, whose batch then waited on its own run folder forever. A process created after the batch was
        # submitted is not its submitter.
        before = '2000-01-01T00:00:00+00:00'
        self.assertTrue(work_sched.pid_alive(os.getpid(), work.now()))
        self.assertFalse(work_sched.pid_alive(os.getpid(), before))
        fake = Fake(default=0.1)
        a = self.submit(['x'], submitter=os.getpid())
        self.db.execute('UPDATE batch SET submitted = ? WHERE id = ?', (before, a))   # this process came later
        b = self.submit(['x'])
        self.drive(self.sched(fake, count=4))
        self.assertEqual(self.runs(b)['x']['state'], 'finished')

    def test_service_with_a_finished_batchs_pid_is_not_its_submitter(self):
        # H66: batch #9446's work.py test had ended, but its pid (or an earlier holder's) went to a process this one
        # may not open (a service's: no creation time to compare), which counted as alive, and the next quiet batch of
        # the same build waited 10 minutes for the folder. Such a process is a submitter only when it is a Python.
        self.assertTrue(work_sched.image_name(os.getpid()).lower().startswith('python'))
        dead = subprocess.Popen([sys.executable, '-c', 'pass'])
        dead.wait()
        self.assertIsNone(work_sched.image_name(dead.pid))
        denied = mock.Mock()
        denied.OpenProcess.return_value = None
        program = {'name': 'SearchProtocolHost.exe'}
        with mock.patch.object(work_sched, '_kernel32', denied), \
                mock.patch.object(work_sched.ctypes, 'get_last_error', return_value=5), \
                mock.patch.object(work_sched, 'image_name', side_effect=lambda pid: program['name']):
            self.assertTrue(work_sched.pid_alive(4321))             # the id alone: it exists
            self.assertFalse(work_sched.pid_alive(4321, work.now()))
            program['name'] = 'python.exe'
            self.assertTrue(work_sched.pid_alive(4321, work.now()))
            program['name'] = 'SearchProtocolHost.exe'
            fake = Fake(default=0.1)
            self.submit(['x'], submitter=4321)
            b = self.submit(['x'], alone=['x'])
            self.drive(self.sched(fake, count=4))
        self.assertEqual(self.runs(b)['x']['state'], 'finished')

    def test_quiet_head_waiting_for_a_folder_keeps_its_place(self):
        # H66: quiet batch #9450 was the head, but its one run's folder was still an earlier batch's; it gathered
        # nothing and the plain batches behind it took every slot until the folder came free (10 minutes). Now it keeps
        # its place: nothing later starts, the wait is logged once and is in the scheduler's process row (the board).
        self.db.execute("INSERT INTO process (name, pid, started, heartbeat, info) VALUES ('scheduler', ?, ?, ?, ?)",
                        (os.getpid(), work.now(), work.now(), json.dumps({'slots': 4})))
        fake = Fake(default=0.1)
        a = self.submit(['x'], submitter=os.getpid())      # alive: still comparing when its batch has finished
        q = self.submit(['x'], alone=['x'])                 # the same build: the same run folder
        c = self.submit(['c1', 'c2'], build='b3')
        s = self.sched(fake, count=1)                       # c waits for a's game, then for the head
        lines = []
        s.log = lines.append
        end = time.time() + 5
        while self.batch(a)['state'] != 'finished' and time.time() < end:
            s.tick()
            time.sleep(0.02)
        self.assertEqual(self.batch(a)['state'], 'finished')
        for _ in range(15):
            s.tick()
            time.sleep(0.02)
        self.assertEqual(self.runs(q)['x']['state'], 'queued')
        self.assertEqual({r['state'] for r in self.runs(c).values()}, {'queued'}, 'a later batch went ahead of the quiet head')
        self.assertIsNone(s.pool.gate, 'the folder wait held the alone gate (builds would wait for a comparison)')
        waits = [t for t in lines if f"waits for batch #{a}'s folder" in t]
        self.assertEqual(len(waits), 1, lines)
        self.assertIn(f'batch #{q}: x-standalone (alone)', waits[0])
        info = json.loads(self.db.execute("SELECT info FROM process WHERE name = 'scheduler'").fetchone()['info'])
        self.assertEqual((info['waits']['batch'], info['waits']['holder'], info['waits']['folder'], info['slots']),
                         (q, a, 'x-standalone', 4))
        from work_board import server
        board = server.Board(work)
        board.build_gate = lambda: mock.Mock(builds=lambda *a, **k: [])
        quiet = board.machine(self.db)['quiet']
        self.assertEqual((quiet['id'], quiet['state'], quiet['folder_wait']['holder']), (q, 'folder', a))
        self.db.execute('UPDATE batch SET submitter = NULL WHERE id = ?', (a,))     # its comparison is done
        self.drive(s)
        (x0, x1), = [(t0, t1) for b, n, t0, t1 in fake.spans if b == q]
        self.assertGreaterEqual(min(fake.starts(c)), x1 - 0.01, 'a later batch started before the quiet run')
        info = json.loads(self.db.execute("SELECT info FROM process WHERE name = 'scheduler'").fetchone()['info'])
        self.assertNotIn('waits', info)

    def test_quiet_head_waits_for_an_earlier_batchs_game_in_its_folder(self):
        # H66: the holder is an earlier batch still playing in the folder; it never waits on the head, so both end
        fake = Fake({'x': 0.6}, default=0.1)
        a = self.submit(['x', 'y'])
        q = self.submit(['x'], alone=['x'])
        c = self.submit(['c1'], expected={'c1': 1000}, build='b3')       # behind a in batch_priority too
        s = self.sched(fake, count=2)                       # y's slot comes free while x plays
        lines = []
        s.log = lines.append
        self.drive(s)
        self.assertTrue(any(f"waits for batch #{a}'s folder (its games play)" in t for t in lines), lines)
        (x0, x1), = [(t0, t1) for b, n, t0, t1 in fake.spans if b == q]
        self.assertGreaterEqual(x0, max(fake.ends(a)) - 0.01)
        self.assertGreaterEqual(min(fake.starts(c)), x1 - 0.01)

    def test_batches_with_crossed_folders_do_not_wait_for_each_other(self):
        fake = Fake(default=0.1)
        a = self.submit(['x', 'y'])
        b = self.submit(['y', 'x'])
        self.drive(self.sched(fake, count=4))
        states = {r['state'] for batch in (a, b) for r in self.runs(batch).values()}
        self.assertEqual(states, {'finished'})

    def test_install_check(self):
        """H47a: the batch's installs are listed when its first game starts and again before the summary; the run whose
        game window holds a write into the install fails, the other passes."""
        install = os.path.join(self.tmp, 'install')
        os.makedirs(install)
        open(os.path.join(install, 'a.res'), 'w').write('a')
        build = f'{self.root}/b'
        os.makedirs(build, exist_ok=True)

        def runner(batch, run, retries, on_attempt, slots=None):
            on_attempt(0)
            folder = os.path.join(batch['build'], 'runs', os.path.basename(run['folder']))
            if run['scenario'] == 'clean':
                time.sleep(0.6)      # plays well after the write
            t0 = time.time()
            if run['scenario'] == 'writer':
                open(os.path.join(install, 'driver.cache'), 'w').write('x')
            time.sleep(0.05)
            run_all.RUN_WINDOWS[folder] = [(t0, time.time())]
            return 'PASS', 0.05, [], {}, []
        names = ['writer', 'clean']
        runs = [{'scenario': n, 'install': 'standalone', 'hooks': True, 'alone': False, 'expected': None,
                 'folder': f'{build}/runs/{n}-standalone'} for n in names]
        env = {'EI_GAME_VANILLA': install}
        a = work_sched.submit(self.db, 'T1', self.root, build, names, runs, None, env=env)
        s = work_sched.Scheduler(self.db, runner=runner, slots=work_sched.SlotPool(4, self.slots), code=[])
        s.last_check = time.time() + 3600
        with mock.patch.object(install_watch, 'AFTER_S', 0.2), mock.patch.object(install_watch, 'BEFORE_S', 0.2):
            self.drive(s)
        summary = self.batch(a)['summary']
        self.assertIn('install trees: 1 files in 1 install', summary)
        self.assertIn('1 changed', summary)
        self.assertIn('driver.cache', summary)
        self.assertIn('1 run(s) overlap a change and fail: writer-standalone', summary)
        self.assertIn('SOME RUNS FAILED', summary)
        self.assertNotIn('clean-standalone ', summary.split('not passed:')[1])
        self.assertEqual(s.watches, {})

    def test_game_environment_per_batch(self):
        """WB5: a batch's games get the submitter's EI_* variables (and only those), os.environ is untouched,
        and a batch from before (no recorded env) inherits as it did."""
        seen = []

        def fake_launch(scenario, out, data, build, env=None):
            seen.append((data, env))
            return 0, ''
        scenarios = os.path.join(self.tmp, 'tests', 'scenarios')
        os.makedirs(scenarios)
        open(os.path.join(scenarios, 'x.txt'), 'w').write('waitmenu\n')
        build = os.path.join(self.tmp, 'bld')
        old_launch, old_environ = run_all.launch, dict(os.environ)
        run_all.launch = fake_launch
        os.environ['EI_PROFILE'] = 'scheduler-own'      # the scheduler's own, not any batch's
        os.environ['EI_BUILD_DIR'] = 'whatever'
        try:
            env = {'EI_EXAMPLE': '1', 'EI_PROFILE': '1'}
            row = {'worktree': self.tmp, 'build': build, 'env': json.dumps(env)}
            run = {'scenario': 'x', 'install': 'standalone', 'hooks': 1, 'folder': build + '/runs/x-standalone'}
            work_sched.real_runner(row, run, 0, lambda n: None)
            work_sched.real_runner(dict(row, env=json.dumps({})), run, 0, lambda n: None)
            work_sched.real_runner(dict(row, env=None), run, 0, lambda n: None)
            work_sched.real_runner({'worktree': self.tmp, 'build': build}, run, 0, lambda n: None)   # no env column at all
            self.assertEqual(os.environ['EI_PROFILE'], 'scheduler-own')
        finally:
            run_all.launch = old_launch
            os.environ.clear()
            os.environ.update(old_environ)
        with_env, empty, null, no_column = (e for _, e in seen)
        self.assertEqual((with_env['EI_EXAMPLE'], with_env['EI_PROFILE']), ('1', '1'))
        self.assertEqual(run_all.game_environment(with_env), env)
        self.assertEqual(run_all.game_environment(empty), {}, "the scheduler's own EI_PROFILE leaked into a batch")
        self.assertEqual(empty['EI_BUILD_DIR'], 'whatever')        # not a game setting: left alone
        self.assertIsNone(null)                                     # inherit
        self.assertIsNone(no_column)

        # and through the database: recorded at submit, absent (NULL) for a batch submitted without
        a = self.submit(['q'])
        self.assertIsNone(work_sched.batch_env(self.batch(a)))
        runs = [{'scenario': 'q', 'install': 'standalone', 'hooks': True, 'alone': False, 'expected': None,
                 'folder': f'{self.root}/b/runs/q-standalone'}]
        b = work_sched.submit(self.db, 'T1', self.root, f'{self.root}/b', ['q'], runs, None, env=env)
        self.assertEqual(work_sched.batch_env(self.batch(b)), env)

    def test_prepare_tools_get_the_batch_environment(self):
        """H65: a "# prepare:" tool runs with the batch's game variables and EI_BUILD_DIR=<its build>, not the
        scheduler daemon's (a deleted batch folder in the daemon's EI_BUILD_DIR broke every later prepare)."""
        scenarios = os.path.join(self.tmp, 'tests', 'scenarios')
        tools = os.path.join(self.tmp, 'tools')
        os.makedirs(scenarios)
        os.makedirs(tools)
        open(os.path.join(scenarios, 'x.txt'), 'w').write('# prepare: envdump.py\nwaitmenu\n')
        open(os.path.join(tools, 'envdump.py'), 'w').write(
            'import json, os, sys\n'
            'os.makedirs(sys.argv[1], exist_ok=True)\n'
            'json.dump({k: v for k, v in os.environ.items() if k.startswith("EI_")}, open(sys.argv[1] + "/env.json", "w"))\n')
        build = os.path.join(self.tmp, 'bld')
        folder = build + '/runs/x-standalone'
        run = {'scenario': 'x', 'install': 'standalone', 'hooks': 1, 'folder': folder}
        row = {'worktree': self.tmp, 'build': build, 'env': json.dumps({'EI_EXAMPLE': '1'})}
        daemon = {'EI_BUILD_DIR': 'daemon-gone', 'EI_PROFILE': 'daemon-own'}
        with mock.patch.object(run_all, 'launch', lambda *a, **k: (0, '')), mock.patch.dict(os.environ, daemon):
            work_sched.real_runner(row, run, 0, lambda n: None)
            self.assertEqual({k: os.environ[k] for k in daemon}, daemon)       # the daemon's own, untouched
        seen = json.load(open(folder + '/harness_saves/env.json'))
        self.assertEqual(seen['EI_BUILD_DIR'], build)
        self.assertEqual(seen['EI_EXAMPLE'], '1')
        self.assertNotIn('EI_PROFILE', seen)

    def test_daemon_environment_has_no_batch_variables(self):
        got = work_sched.daemon_environment({'EI_BUILD_DIR': 'b', 'EI_PROFILE': '1', 'EI_GAME_VANILLA': 'v',
                                             'EI_HARNESS_SLOTS': '4', 'PATH': 'p', 'WORK_DB': 'd'})
        self.assertEqual(got, {'EI_HARNESS_SLOTS': '4', 'PATH': 'p', 'WORK_DB': 'd'})

    def test_clock_reaches_the_game(self):
        seen = []
        def fake_launch(scenario, out, data, build, env=None):
            seen.append(env)
            return 0, ''
        scenarios = os.path.join(self.tmp, 'tests', 'scenarios')
        os.makedirs(scenarios)
        open(os.path.join(scenarios, 'x.txt'), 'w').write('waitmenu\n')
        build = os.path.join(self.tmp, 'bld')
        with mock.patch.object(run_all, 'launch', fake_launch):
            run = {'scenario': 'x', 'install': 'standalone', 'hooks': 1, 'folder': build + '/runs/x-standalone'}
            row = {'worktree': self.tmp, 'build': build, 'env': None}
            work_sched.real_runner(dict(row, args=json.dumps(['x', '--clock', 'virtual,randtrace'])), run, 0, lambda n: None)
            work_sched.real_runner(dict(row, args=json.dumps(['x'])), run, 0, lambda n: None)
        self.assertEqual(seen[0]['EI_HARNESS_CLOCK'], 'virtual,randtrace')
        self.assertIsNone(seen[1])                                  # no --clock: the game inherits

    def test_game_environment_names(self):
        got = run_all.game_environment({'EI_EXAMPLE': '1', 'EI_PROFILE': '1', 'EI_BUILD_DIR': 'x', 'EI_HARNESS_SLOTS': '4',
                                        'EI_OUT_DIR': 'o', 'EI_HARNESS': 'h', 'PATH': 'p', 'WORK_DB': 'd'})
        self.assertEqual(got, {'EI_EXAMPLE': '1', 'EI_PROFILE': '1'})

    # ---- WB6: machine-wide expected run times

    def add_timing(self, scenario, install, rev, *seconds):
        for sec in seconds:
            self.db.execute("INSERT INTO timing (scenario, install, scenario_rev, seconds, time) VALUES (?, ?, ?, ?, '2026-10-14T00:00:00+02:00')",
                            (scenario, install, rev, sec))

    def test_expected_seconds_fallbacks(self):
        e = lambda *a, **k: work.expected_seconds(self.db, *a, **k)
        self.assertIsNone(e('s', 'standalone', 'r1'))
        self.assertEqual(e('s', 'standalone', 'r1', fallback=42), 42)                      # run_times.json
        self.add_timing('s', 'standalone', 'old', 100, 100, 100)
        self.assertEqual(e('s', 'standalone', 'r1', fallback=42), 100)                     # other rev beats run_times.json
        self.assertEqual(e('s', 'standalone-rebirth', 'r1', fallback=42), 42)  # other install does not
        self.add_timing('s', 'standalone', 'r1', 10, 12)
        self.assertEqual(e('s', 'standalone', 'r1', fallback=42), 11)                      # this rev only, median
        self.add_timing('s', 'standalone', 'r1', 50, 14, 16, 18, 20)                       # the last 5 count: 50 14 16 18 20
        self.assertEqual(e('s', 'standalone', 'r1'), 18)
        self.assertEqual(e('s', 'standalone', 'r1+dirty'), 18)                             # uncommitted: any rev, last 5 overall
        self.assertEqual(e('s', 'standalone', 'r2'), 18)                                   # a changed scenario: latest rows at any rev
        self.add_timing('s', 'standalone', 'r2', 3)
        self.assertEqual(e('s', 'standalone', 'r2'), 3)                                    # ...until it has its own
        self.assertEqual(e('s', 'standalone', 'r1'), 18)
        self.assertEqual(e('s', 'standalone', 'r2+dirty'), 16)                             # any rev: 14 16 18 20 3
        times = work.expected_times(self.db, self.tmp, [('s', 'standalone'), ('t', 'standalone')], {'t-standalone': 7})
        self.assertEqual(times[('t', 'standalone')], 7)       # not a repository: rev '' -> any rev -> run_times.json
        self.assertEqual(times[('s', 'standalone')], 16)

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
        self.assertTrue(all(r['batch'] == a and r['install'] == 'standalone' and r['scenario_rev'] == '' and r['seconds'] >= 0 for r in rows))
        self.assertEqual(work.expected_times(self.db, self.tmp, [('p', 'standalone')])[('p', 'standalone')],
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

    # ---- the image's runs (H40: game.exe is no longer run)

    def scenario(self, name, text, root=None):
        folder = os.path.join(root or self.tmp, 'tests', 'scenarios')
        os.makedirs(folder, exist_ok=True)
        path = os.path.join(folder, name + '.txt')
        with open(path, 'w') as f:
            f.write(text)
        return path

    def test_only_the_image_plays(self):
        """H40: every run is the image's, without EI_RENDER (the scenario's "render" step decides); game.exe's installs
        and a game.exe row from an older checkout (hooks off) play no game."""
        seen = []

        def fake_launch(scenario, out, data, build, env=None):
            seen.append((data, env))
            return 0, ''
        self.scenario('plain', 'waitmenu\n')
        build = os.path.join(self.tmp, 'bld')
        with mock.patch.object(run_all, 'launch', fake_launch):
            run_all.run('plain', 'standalone', root=self.tmp, build=build, take_slot=False, game_env={'EI_RENDER': 'smooth'})
            gone = [run_all.run('plain', install, root=self.tmp, build=build, take_slot=False)
                    for install in ('vanilla', 'rebirth', 'native')]
            row = {'worktree': self.tmp, 'build': build, 'env': None}
            unhooked = work_sched.real_runner(row, {'scenario': 'plain', 'install': 'vanilla', 'hooks': 0,
                                                    'folder': build + '/runs/plain-vanilla'}, 1, lambda n: None)
        self.assertEqual(len(seen), 1, 'a game was played for an unknown install or a game.exe row')
        self.assertEqual(seen[0][0], 'vanilla')
        self.assertNotIn('EI_RENDER', seen[0][1])
        for result in gone:
            self.assertEqual(result[0], 'FAIL')
            self.assertIn('unknown install', result[2][0])
        self.assertEqual(unhooked[0], 'FAIL')
        self.assertIn('no longer run since H40', unhooked[2][0])

    def test_standalone(self):
        """--standalone (the default since S21): every scenario, install "standalone", the image on the vanilla
        install's data, no game.exe (S18k1), always our renderer (never the display mode); "standalone-rebirth" on
        Rebirth's data."""
        seen = []

        def fake_launch(scenario, out, data, build, env=None):
            seen.append((data, env))
            return 0, 'ei-rebuild: the game returned 0\n'
        self.scenario('boot', 'waitmenu\nquit\n')
        self.scenario('wide', 'render smooth\nresolution 800x600\nfullscreen 1\n')
        # The vanilla data only unless the Rebirth data is asked for (SV9)
        vanilla = [('boot', 'standalone'), ('wide', 'standalone')]
        both = [('boot', 'standalone'), ('boot', 'standalone-rebirth'), ('wide', 'standalone'), ('wide', 'standalone-rebirth')]
        self.assertEqual(run_all.plan(run_all.parse_args(['--standalone']), root=self.tmp), vanilla)
        self.assertEqual(run_all.plan(run_all.parse_args([]), root=self.tmp), vanilla)     # the default since S21
        self.assertEqual(run_all.plan(run_all.parse_args([]), root=self.tmp, left_out=('wide',)), [('boot', 'standalone')])
        self.assertEqual(run_all.plan(run_all.parse_args(['wide']), root=self.tmp, left_out=('wide',)), [('wide', 'standalone')])
        self.assertEqual(run_all.plan(run_all.parse_args(['--all-rebirth']), root=self.tmp), both)
        # The screenshot scenarios (D7) run by name only
        self.scenario('screenshots_town', 'waitmenu\nquit\n')
        self.assertEqual(run_all.plan(run_all.parse_args([]), root=self.tmp), vanilla)
        self.assertEqual(run_all.plan(run_all.parse_args(['screenshots_town']), root=self.tmp),
                         [('screenshots_town', 'standalone')])
        os.remove(os.path.join(self.tmp, 'tests', 'scenarios', 'screenshots_town.txt'))
        for gone in ('--hooked', '--native', '--dgvoodoo', '--verified-candidates',     # S21c
                     '--no-hooks', '--fresh', '--record'):                              # H40
            with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
                run_all.parse_args([gone, '1'] if gone == '--verified-candidates' else [gone])
        # --rebirth without names: the smoke set, config/rebirth_scenarios.csv's marks and any scenario with a step for
        # one install (or plugin, widescreen)
        os.makedirs(os.path.join(self.tmp, 'config'), exist_ok=True)
        with open(os.path.join(self.tmp, 'config', 'rebirth_scenarios.csv'), 'w') as f:
            f.write('scenario,rebirth,why\nwide,1,test\n')
        self.assertEqual(run_all.plan(run_all.parse_args(['--rebirth']), root=self.tmp),
                         [('boot', 'standalone'), ('wide', 'standalone'), ('wide', 'standalone-rebirth')])
        os.remove(os.path.join(self.tmp, 'config', 'rebirth_scenarios.csv'))
        stepped = self.scenario('stepped', 'waitmenu\nrebirth click 1 1\n')
        self.assertEqual(run_all.plan(run_all.parse_args(['--rebirth-only']), root=self.tmp), [('stepped', 'standalone-rebirth')])
        os.remove(stepped)
        # "# installs:" keeps a scenario to one install, named or not; "# suite: named" leaves it out of the suite (H74)
        only = self.scenario('only', '# installs: rebirth\nwaitmenu\nquit\n')
        plain = self.scenario('plain', '# installs: vanilla\nwaitmenu\nrebirth click 1 1\n')
        named = self.scenario('named', '# suite: named\nwaitmenu\nquit\n')
        self.assertEqual(run_all.plan(run_all.parse_args(['--rebirth']), root=self.tmp),
                         [('boot', 'standalone'), ('only', 'standalone-rebirth'), ('plain', 'standalone'),
                          ('wide', 'standalone')])
        self.assertEqual(run_all.plan(run_all.parse_args(['--all-rebirth', 'only', 'plain']), root=self.tmp),
                         [('only', 'standalone-rebirth'), ('plain', 'standalone')])
        self.assertEqual(run_all.plan(run_all.parse_args(['named']), root=self.tmp), [('named', 'standalone')])
        for path in (only, plain, named):
            os.remove(path)
        # and every scenario with a Rebirth golden (H51a)
        os.makedirs(os.path.join(self.tmp, 'tests', 'golden', 'boot', 'rebirth'))
        with open(os.path.join(self.tmp, 'tests', 'golden', 'boot', 'rebirth', 'golden.json'), 'w') as f:
            f.write('{}')
        self.assertEqual(run_all.plan(run_all.parse_args(['--rebirth-only']), root=self.tmp), [('boot', 'standalone-rebirth')])
        self.assertEqual(run_all.rebirth_scenarios(['boot', 'wide'], root=self.tmp, goldens=False), set())
        shutil.rmtree(os.path.join(self.tmp, 'tests', 'golden'))
        self.assertEqual(run_all.plan(run_all.parse_args(['--standalone', '--vanilla-only', 'wide']), root=self.tmp),
                         [('wide', 'standalone')])
        self.assertEqual(run_all.plan(run_all.parse_args(['--standalone', '--rebirth-only', 'wide']), root=self.tmp),
                         [('wide', 'standalone-rebirth')])
        build = os.path.join(self.tmp, 'bld')
        with mock.patch.object(run_all, 'launch', fake_launch):
            notes = run_all.run('boot', 'standalone', root=self.tmp, build=build, take_slot=False,
                                game_env={'EI_RENDER': 'smooth'})[4]
            run_all.run('boot', 'standalone-rebirth', root=self.tmp, build=build, take_slot=False, game_env={})
        self.assertEqual(seen[0][0], 'vanilla')
        self.assertNotIn('EI_RENDER', seen[0][1])
        self.assertEqual(notes, ['ei-rebuild: the game returned 0'])
        self.assertEqual(seen[1][0], 'rebirth')
        self.assertEqual(run_all.GOLDEN_INSTALL['standalone-rebirth'], 'rebirth')

    def test_install_dirs(self):
        """The installs' folders: EI_GAME_VANILLA / EI_GAME_REBIRTH, else the machine's defaults."""
        with mock.patch.dict(os.environ, {'EI_GAME_VANILLA': 'V', 'EI_GAME_REBIRTH': ''}):
            self.assertEqual(run_all.install_dir('vanilla'), 'V')
            self.assertEqual(run_all.install_dir('rebirth'), run_all.DEFAULT_INSTALLS['rebirth'])
        self.assertNotIn('EI_GAME_VANILLA', run_all.game_environment({'EI_GAME_VANILLA': 'V', 'EI_GAME_ROOT': 'R'}))
        # a batch's own folders (the stub data, H41): its environment's, not the scheduler's
        with mock.patch.dict(os.environ, {'EI_GAME_VANILLA': 'V'}):
            self.assertEqual(run_all.install_dir('vanilla', {'EI_GAME_VANILLA': 'S'}), 'S')
            self.assertEqual(run_all.install_dir('rebirth', {}), run_all.DEFAULT_INSTALLS['rebirth'])
            self.assertEqual(run_all.install_environment(), {'EI_GAME_VANILLA': 'V'})

    def test_linux_vulkan_driver(self):
        """H50a: dzn the default under WSL when installed, the system's (lavapipe) elsewhere or when asked; the
        scenarios left out without names follow the driver."""
        manifest = os.path.join(self.tmp, 'dzn_icd.json')
        open(manifest, 'w').close()
        quiet = contextlib.redirect_stdout(io.StringIO())
        with mock.patch.object(sys, 'platform', 'linux'), mock.patch.object(run_all, 'DZN_ICD', manifest), quiet:
            env = {}
            self.assertEqual(run_all.vulkan_driver(None, env, wsl=True), manifest)
            self.assertEqual(env['VK_DRIVER_FILES'], manifest)
            for choice, env, wsl in ((None, {}, False), ('system', {}, True), (None, {'EI_VK_ICD': 'system'}, True)):
                self.assertIsNone(run_all.vulkan_driver(choice, env, wsl=wsl))
                self.assertNotIn('VK_DRIVER_FILES', env)
            self.assertEqual(run_all.vulkan_driver('dzn', {}, wsl=False), manifest)
            self.assertIn('net_pair_master', run_all.linux_left_out(manifest))
            self.assertNotIn('camera_smooth', run_all.linux_left_out(manifest))
            self.assertIn('camera_smooth', run_all.linux_left_out(None))
        with mock.patch.object(run_all, 'DZN_ICD', manifest), quiet:
            self.assertIsNone(run_all.vulkan_driver(None, {}, wsl=True))      # not Linux: nothing to choose
            self.assertEqual(run_all.linux_left_out(None), ())
            with mock.patch.object(sys, 'platform', 'win32'):
                self.assertFalse(run_all.under_wsl())          # Windows' side of hold_windows_slots(), H50a
        with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
            run_all.parse_args(['--dzn', '--lavapipe'])

    def test_machine_slots_up_to(self):
        """hold_slots()'s most: as many slots as are free, up to most, once count are."""
        with mock.patch.object(run_all, 'SLOT_DIR', self.slots), mock.patch.object(run_all, 'MACHINE_SLOTS', 4):
            with run_all.machine_slot(count=1) as one:
                self.assertEqual(one, 1)
                with run_all.machine_slot(count=2, most=8) as free:
                    self.assertEqual(free, 3)
            with run_all.machine_slot(count=2) as two:
                self.assertEqual(two, 2)


    def test_hold_slots_follow_commands(self):
        """H50a1: Windows' side of a WSL run's hold takes slots one at a time and gives some back on "want n"."""
        stdin = io.StringIO(''.join(line + chr(10) for line in ('want 4', 'want 1', 'bogus', 'want 3')))
        out = io.StringIO()
        with mock.patch.object(run_all, 'SLOT_DIR', self.slots), mock.patch.object(run_all, 'MACHINE_SLOTS', 4), \
                contextlib.redirect_stdout(out):
            self.assertEqual(run_all.hold_slots(2, 3, stdin), 0)
        self.assertEqual(out.getvalue().splitlines(), ['held 3', 'held 4', 'held 1', 'held 3'])
        with mock.patch.object(run_all, 'SLOT_DIR', self.slots), mock.patch.object(run_all, 'MACHINE_SLOTS', 4):
            with run_all.machine_slot(count=4) as all_back:        # the end of the hold released every slot
                self.assertEqual(all_back, 4)

    def test_adjust_hold_takes_only_free_slots(self):
        with mock.patch.object(run_all, 'SLOT_DIR', self.slots), mock.patch.object(run_all, 'MACHINE_SLOTS', 4):
            with run_all.machine_slot(count=1) as one:                # another run holds a slot
                held = []
                self.assertEqual(run_all.adjust_hold(held, 4), 3)     # no waiting for the fourth
                self.assertEqual(run_all.adjust_hold(held, 1), 1)
                self.assertEqual(run_all.adjust_hold(held, 2), 2)
                for _, f in held:
                    run_all._unlock(f)

    def test_game_slots_floor(self):
        slots = run_all.GameSlots(2)
        with slots.hold(1):
            self.assertEqual(slots.resize(0), 1)                      # not below the games running
        self.assertEqual(slots.used, 0)

    def test_game_slots_follow_capacity(self):
        slots = run_all.GameSlots(1)
        started = []
        release = threading.Event()

        def game(name, count):
            with slots.hold(count):
                started.append(name)
                release.wait(10)
        with slots.hold(1):
            pair = threading.Thread(target=game, args=('pair', 2))
            pair.start()
            time.sleep(0.1)
            single = threading.Thread(target=game, args=('single', 1))
            single.start()
            time.sleep(0.1)
            self.assertEqual(started, [], 'nothing starts on a full capacity (a single does not pass the pair)')
            slots.resize(4)
            deadline = time.time() + 5
            while len(started) < 2 and time.time() < deadline:
                time.sleep(0.02)
            self.assertEqual(started, ['pair', 'single'])
        release.set()
        pair.join(5)
        single.join(5)
        self.assertEqual(slots.used, 0)

    def test_rebalance_follows_need(self):
        """The hold grows as Windows' slots free (up to most) and goes back as the queue empties, never below the
        games running."""
        class Windows:
            def __init__(self, free):
                self.free, self.held, self.asked = free, 2, []

            def want(self, n):
                self.asked.append(n)
                self.held = min(n, self.held + self.free)
                self.free = 0
                return self.held
        windows = Windows(free=0)
        slots = run_all.GameSlots(2)
        self.assertEqual(run_all.rebalance(windows, slots, 100, 8), 2)         # Windows busy: stays at 2
        windows.free = 4
        self.assertEqual(run_all.rebalance(windows, slots, 100, 8), 6)
        self.assertEqual(slots.capacity, 6)
        self.assertEqual(run_all.rebalance(windows, slots, 100, 8), 6)
        windows.free = 9
        self.assertEqual(run_all.rebalance(windows, slots, 100, 8), 8)         # never above most
        with slots.hold(1), slots.hold(2):                                     # 3 games running
            self.assertEqual(run_all.rebalance(windows, slots, 2, 8), 3)       # 2 units left but 3 running
            self.assertEqual(windows.asked[-1], 3)
        self.assertEqual(run_all.rebalance(windows, slots, 1, 8), 1)
        self.assertEqual(windows.asked[-1], 1)

    def test_follow_need_thread(self):
        class Windows:
            held = 2

            def want(self, n):
                self.held = n
                return n
        windows, slots, stop = Windows(), run_all.GameSlots(2), threading.Event()
        left = [6]
        thread = threading.Thread(target=run_all.follow_need, args=(windows, slots, lambda: left[0], 4, stop, 0.02))
        thread.start()
        deadline = time.time() + 5
        while slots.capacity != 4 and time.time() < deadline:
            time.sleep(0.02)
        self.assertEqual((slots.capacity, windows.held), (4, 4))
        left[0] = 1
        while slots.capacity != 1 and time.time() < deadline:
            time.sleep(0.02)
        self.assertEqual((slots.capacity, windows.held), (1, 1))
        stop.set()
        thread.join(5)
        self.assertFalse(thread.is_alive())


if __name__ == '__main__':
    unittest.main()
