"""Tests of tools/build_gate.py (H64): a build's ticket, its wait on the alone gate, its status, and a quiet run's wait
on a build. Each test has its own builds and slot folders in a temporary directory (the machine's are not touched).

usage: python tools/test_build_gate.py [-v]
"""
import json, os, subprocess, sys, tempfile, threading, time, unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_gate, work, work_sched

HOLD = r'''
import msvcrt, os, sys, time
path = sys.argv[1]
f = open(path, 'a+b'); msvcrt.locking(f.fileno(), msvcrt.LK_NBLCK, 1)
open(path + '.ready', 'w').close()
time.sleep(600)
'''

# A build that dies without cleaning up: its ticket's files stay, its locks go with the process.
CRASH = r'''
import os, sys
sys.path.insert(0, sys.argv[3])
import build_gate
t = build_gate.Ticket('E:/x/agent-crash', 'verify', sys.argv[1], sys.argv[2])
t.wait_turn()
os._exit(3)
'''


class GateTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix='h64_')
        self.builds = os.path.join(self.tmp, 'builds')
        self.slots = os.path.join(self.tmp, 'slots')
        os.makedirs(self.slots)
        self.holders = []

    def tearDown(self):
        for p in self.holders:
            p.kill()
            p.wait()

    def hold(self, path):
        p = subprocess.Popen([sys.executable, '-c', HOLD, path])
        self.holders.append(p)
        end = time.time() + 60
        while not os.path.exists(path + '.ready'):
            self.assertLess(time.time(), end, 'the holder did not start')
            time.sleep(0.02)
        return p

    def ticket(self, name='agent-1', config='verify'):
        return build_gate.Ticket(f'E:/x/{name}', config, self.builds, self.slots)

    def test_ticket_lists_while_held(self):
        t = self.ticket()
        t.wait_turn()
        (b,) = build_gate.builds(self.builds)
        self.assertEqual((b['checkout'], b['config'], b['stage'], b['compiling']), ('E:/x/agent-1', 'verify', 'configure', True))
        self.assertTrue(b['started'])
        self.assertEqual(len(build_gate.compiling(self.builds)), 1)
        t.close()
        self.assertEqual(build_gate.builds(self.builds), [])
        self.assertEqual(os.listdir(self.builds), [])

    def test_build_waits_for_the_gate(self):
        holder = self.hold(os.path.join(self.slots, 'alone'))
        t = self.ticket()
        said = []
        thread = threading.Thread(target=t.wait_turn, kwargs={'say': lambda *a, **k: said.append(a[0]), 'poll': 0.05})
        thread.start()
        time.sleep(0.5)
        self.assertTrue(thread.is_alive(), 'the build started while a quiet run held the gate')
        (b,) = build_gate.builds(self.builds)
        self.assertEqual((b['stage'], b['compiling']), ('waiting', False))   # a quiet run does not wait for it
        self.assertEqual(len(said), 1)
        holder.kill()
        holder.wait()
        thread.join(10)
        self.assertFalse(thread.is_alive())
        self.assertEqual(build_gate.compiling(self.builds)[0]['stage'], 'configure')
        t.close()

    def test_crashed_build_leaves_nothing(self):
        p = subprocess.run([sys.executable, '-c', CRASH, self.builds, self.slots, os.path.dirname(os.path.abspath(__file__))])
        self.assertEqual(p.returncode, 3)
        self.assertTrue(os.listdir(self.builds))                 # its files are there...
        self.assertEqual(build_gate.builds(self.builds), [])     # ...but it is not running, and the lister clears them
        self.assertEqual(os.listdir(self.builds), [])

    def test_progress(self):
        t = self.ticket()
        t.wait_turn()
        os.environ[build_gate.STATUS_ENV] = t.status
        try:
            build_gate._last_progress[0] = 0.0
            build_gate.stage('compile')
            build_gate.progress(b'[3/10] Building CXX object a.obj\r\n')
            build_gate.progress(b'[4/10] Building CXX object b.obj\r\n')   # within PROGRESS_EVERY: not written
            status = build_gate.read_status(t.status)
            self.assertEqual((status['stage'], status['done'], status['total']), ('compile', 3, 10))
            build_gate.progress(b'[10/10] Linking CXX executable ei-rebuild.exe\r\n')   # the last step always
            build_gate.progress(b'ninja: no work to do.\r\n')
            status = build_gate.read_status(t.status)
            self.assertEqual((status['done'], status['total']), (10, 10))
            build_gate.stage('checks')
            status = build_gate.read_status(t.status)
            self.assertEqual((status['stage'], status['done']), ('checks', None))
        finally:
            del os.environ[build_gate.STATUS_ENV]
            t.close()

    def test_run_passes_the_status_and_exit_code(self):
        bat = os.path.join(self.tmp, 'fake_build.cmd')
        seen = os.path.join(self.tmp, 'seen.txt')
        with open(bat, 'w') as f:
            f.write(f'@echo off\r\necho %EI_BUILD_STATUS% %1> "{seen}"\r\nexit /b 3\r\n')
        self.assertEqual(build_gate.run(bat, ['verify'], self.builds, self.slots), 3)
        with open(seen) as f:
            status, config = f.read().split()
        self.assertTrue(status.endswith('.json') and os.path.dirname(status) == self.builds, status)
        self.assertEqual(config, 'verify')
        self.assertEqual(os.listdir(self.builds), [])

    def test_quiet_run_waits_for_a_build(self):
        # the scheduler side: an alone run gathers the gate and every slot but starts only once the build has ended;
        # a build starting while it is held waits
        db = work.connect(os.path.join(self.tmp, 'work.db'))
        try:
            build = work.norm(os.path.join(self.tmp, 'b'))
            work_sched.submit(db, 'T1', work.norm(self.tmp), build, ['w'],
                              [{'scenario': 'w', 'install': 'standalone', 'hooks': True, 'alone': True, 'expected': None,
                                'folder': build + '/runs/w-standalone'}])
            started = []

            def runner(batch, run, retries, on_attempt, slots=None):
                started.append(time.time())
                return 'PASS', 0.1, [], {}, []
            t = self.ticket()
            t.wait_turn()
            s = work_sched.Scheduler(db, runner=runner, slots=work_sched.SlotPool(4, self.slots), code=[],
                                     builds=lambda: build_gate.compiling(self.builds))
            s.last_check = time.time() + 3600
            end = time.time() + 1.0
            while time.time() < end:
                s.tick()
                time.sleep(0.02)
            self.assertEqual(started, [], 'the quiet run started beside a build')
            self.assertIsNotNone(s.pool.gate)
            later = self.ticket('agent-2')
            self.assertTrue(build_gate.gate_held(self.slots))
            done = []
            thread = threading.Thread(target=lambda: (later.wait_turn(poll=0.05), done.append(time.time())))
            thread.start()
            ended = time.time()
            t.close()
            end = time.time() + 30
            while s.tick() or not started:
                self.assertLess(time.time(), end)
                time.sleep(0.02)
            self.assertGreaterEqual(started[0], ended)
            thread.join(10)
            self.assertTrue(done, 'the later build did not start after the quiet run')
            self.assertGreaterEqual(done[0], started[0], 'the later build started before the quiet run')
            later.close()
        finally:
            db.close()


if __name__ == '__main__':
    unittest.main()
