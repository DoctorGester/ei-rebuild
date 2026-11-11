"""Tests of run_all.py's game runs without games (SV1d): launch() replaced, or a Python stand-in for the game.

usage: python tools/test_run_all_timeout.py [-v]
The real checks with a game are tests/scenarios/watchdog_*.txt ("# expect: TIMEOUT").
"""
import os, shutil, sys, tempfile, time, unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run_all


def scenario_file(directory, text):
    path = os.path.join(directory, 'x.txt')
    with open(path, 'w') as f:
        f.write(text)
    return path


class Limits(unittest.TestCase):
    def test_scenario_limit(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(run_all.scenario_limit(scenario_file(d, 'waitmenu\nquit\n')), 60)
            self.assertEqual(run_all.scenario_limit(scenario_file(d, '# timeout 5\n  timeout 80\nquit\n')), 80)
            self.assertEqual(run_all.scenario_limit(scenario_file(d, 'timeout 5\ntimeout 7\n')), 7)
            self.assertEqual(run_all.scenario_limit(os.path.join(d, 'missing.txt')), 60)

    def test_time_scale(self):
        with tempfile.TemporaryDirectory() as d:
            path = scenario_file(d, 'timeout 7\n')
            for value, limit in (('3', 21), ('1.5', 10), ('', 7), ('0.5', 7), ('x', 7)):
                with mock.patch.dict(os.environ, {'EI_TIME_SCALE': value}):
                    self.assertEqual(run_all.scenario_limit(path), limit, value)

    def test_expected_result(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertIsNone(run_all.expected_result(scenario_file(d, 'quit\n')))
            self.assertEqual(run_all.expected_result(scenario_file(d, '# x\n# expect: TIMEOUT\nquit\n')), 'TIMEOUT')

    def test_scenario_arguments(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(run_all.scenario_arguments(scenario_file(d, 'quit\n')), [])
            self.assertEqual(run_all.scenario_arguments(scenario_file(d, '# x\n# arguments: --effects Portal\nquit\n')),
                             ['--effects', 'Portal'])


class Run(unittest.TestCase):
    """run_once() with launch() replaced: its result code and the report decide the result."""

    def run_scenario(self, scenario_text, returncode, report, text='', game_env=None, digest=False):
        with tempfile.TemporaryDirectory() as root:
            os.makedirs(os.path.join(root, 'tests', 'scenarios'))
            build = os.path.join(root, 'build')
            with open(os.path.join(root, 'tests', 'scenarios', 'x.txt'), 'w') as f:
                f.write(scenario_text)

            def fake(scenario, out, data, build, env=None):
                if report is not None:
                    with open(os.path.join(out, 'harness_report.txt'), 'w') as f:
                        f.write(report)
                if digest:
                    with open(os.path.join(out, 'digest.txt'), 'w') as f:
                        f.write('# ei state digest 2\n')
                return returncode, text

            with mock.patch.object(run_all, 'launch', fake):
                return run_all.run_once('x', 'standalone', None, root, build, False, None, game_env)

    def test_run_all_ended_the_game(self):
        result, _, failures, _, notes = self.run_scenario(
            'timeout 3\nhang all\n', run_all.TIMEOUT_EXIT, 'RESULT: RUNNING\n',
            'run_all: TIMEOUT: the game is still running 23 s after its start; terminating it\n')
        self.assertEqual(result, 'TIMEOUT')
        self.assertIn('3 s time limit', failures[0])
        self.assertTrue(any('terminating' in n for n in notes))

    def test_silent_end_names_the_result_code(self):
        # C36b: the game died with no line in ei-rebuild.log and the report still said RUNNING.
        result, _, failures, _, _ = self.run_scenario('quit\n', 1, 'RESULT: RUNNING\n', 'run_all: started\n')
        self.assertEqual(result, 'RUNNING')
        self.assertEqual(len(failures), 1)
        self.assertIn('result code 1', failures[0])

    def test_watchdog_ended_the_game(self):
        result, _, failures, _, _ = self.run_scenario(
            'quit\n', 1, 'FAILED: scenario exceeded its 60 s time limit at step 3 of 9\nRESULT: FAIL\n')
        self.assertEqual(result, 'TIMEOUT')
        self.assertTrue(failures[0].startswith('scenario exceeded'))

    def test_unkillable_game(self):
        result, _, failures, _, _ = self.run_scenario(
            'quit\n', run_all.UNKILLABLE_EXIT, 'RESULT: RUNNING\n', 'run_all: the game (pid 1) is still alive\n')
        self.assertEqual(result, 'TIMEOUT')
        self.assertIn('still alive', failures[-1])

    def test_expected_timeout_passes_but_not_an_unkillable_game(self):
        text = '# expect: TIMEOUT\ntimeout 3\nhang all\n'
        result, _, failures, _, _ = self.run_scenario(text, run_all.TIMEOUT_EXIT, 'RESULT: RUNNING\n')
        self.assertEqual((result, failures), ('PASS', []))
        result, _, _, _, _ = self.run_scenario(text, run_all.UNKILLABLE_EXIT, 'RESULT: RUNNING\n')
        self.assertEqual(result, 'TIMEOUT')

    def test_golden_run_without_a_digest_fails(self):
        # H54: a run that passed in digest mode without digest.txt is a failure, not a PASS the compare counts as differing.
        env = {'EI_DIGEST': '20'}
        result, _, failures, _, _ = self.run_scenario('quit\n', 0, 'RESULT: PASS\n', game_env=env)
        self.assertEqual(result, 'FAIL')
        self.assertIn('no digest.txt', failures[0])
        self.assertEqual(self.run_scenario('quit\n', 0, 'RESULT: PASS\n', game_env=env, digest=True)[0], 'PASS')
        self.assertEqual(self.run_scenario('quit\n', 0, 'RESULT: PASS\n')[0], 'PASS')   # no digest asked for
        self.assertEqual(self.run_scenario('# expect: TIMEOUT\nquit\n', run_all.TIMEOUT_EXIT, 'RESULT: RUNNING\n',
                                           game_env=env)[0], 'PASS')

    def test_run_folder_that_cannot_be_emptied(self):
        with tempfile.TemporaryDirectory() as d:
            out = os.path.join(d, 'run')
            os.makedirs(os.path.join(out, 'harness_saves'))
            self.assertEqual(run_all.empty_folder(out), [])
            self.assertFalse(os.path.exists(out))
            os.makedirs(out)
            with mock.patch.object(shutil, 'rmtree'), mock.patch.object(time, 'sleep'):
                self.assertEqual(run_all.empty_folder(out, tries=2), [])   # an empty folder is fine
                open(os.path.join(out, 'digest.txt'), 'w').close()
                self.assertEqual(run_all.empty_folder(out, tries=2), ['digest.txt'])

    def test_failing_prepare_tool_fails_the_run(self):
        # H47b: a "# prepare:" tool that fails ends that run, not the whole suite; no report is read.
        result, seconds, failures, _, _ = self.run_scenario('# prepare: no_such_tool.py\nquit\n', 0, 'RESULT: PASS\n')
        self.assertEqual((result, seconds), ('FAIL', 0.0))
        self.assertIn('"# prepare:" tool failed', failures[0])
        self.assertIn('no_such_tool.py', failures[0])

    def test_ordinary_results_are_unchanged(self):
        self.assertEqual(self.run_scenario('quit\n', 0, 'RESULT: PASS\n')[0], 'PASS')
        self.assertEqual(self.run_scenario('quit\n', 1, 'FAILED: a check\nRESULT: FAIL\n')[0], 'FAIL')
        self.assertEqual(self.run_scenario('quit\n', 2, None)[0], 'NO REPORT')


# A stand-in game for launch(): FAKE_GAME says what it does; it sees EI_OUT_DIR, EI_HARNESS and EI_PAIR as the image does.
FAKE_GAME = r'''
import os, sys, time
out, mode = os.environ['EI_OUT_DIR'], os.environ['FAKE_GAME']
peer = os.environ.get('EI_PAIR', ',0').endswith(',1')
report = lambda text: open(os.path.join(out, 'harness_report.txt'), 'w').write(text)
print('ei-rebuild: started', os.environ['EI_HARNESS'], os.environ.get('EI_PAIR', ''),
      os.path.samefile(os.getcwd(), os.environ['EI_GAME_ROOT']))
sys.stdout.flush()
if mode == 'nostart':
    time.sleep(60)
report('RESULT: RUNNING\n')
if mode == 'die':
    sys.exit(3)
if mode == 'hang':
    time.sleep(60)
if mode == 'settings':
    open(os.path.join(out, 'ei_settings.ini'), 'w').write('[a]\nx = 2\n')
report('console output:\n  hello\nRESULT: ' + ('FAIL' if peer and mode == 'peerfails' else 'PASS') + '\n')
open(os.path.join(out, 'harness_quit'), 'w').close()
if mode == 'exithang':
    time.sleep(60)
if mode == 'slowexit':   # the game's own exit stages over the limit
    open(os.path.join(out, 'ei-rebuild.log'), 'w').write(
        '[10:00:01.000] exit: the quit request\n[10:00:01.500] exit: the game stopped\n[10:00:04.200] exit: the exit functions done\n')
if mode == 'abortexit':   # an abort at exit (std::terminate): the fast-fail exit code
    sys.exit(0xC0000409)
if mode == 'quickexit':
    open(os.path.join(out, 'ei-rebuild.log'), 'w').write(
        '[23:59:59.900] exit: the quit request\n[00:00:00.050] exit: the exit functions done\n')
'''


class Launch(unittest.TestCase):
    """launch() with a Python stand-in for the game: the environment, the time limits, the report's completion."""

    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.root = self.dir.name
        self.game = os.path.join(self.root, 'game.py')
        with open(self.game, 'w') as f:
            f.write(FAKE_GAME)
        self.install = os.path.join(self.root, 'install')
        os.makedirs(self.install)
        self.out = os.path.join(self.root, 'out')
        os.makedirs(self.out)
        for p in (mock.patch.dict(os.environ, {'EI_GAME_VANILLA': self.install, 'EI_GAME_REBIRTH': self.install}),
                  mock.patch.object(run_all, 'LIMIT_MARGIN_S', 1), mock.patch.object(run_all, 'HARNESS_START_S', 2),
                  mock.patch.object(run_all, 'EXIT_HANG_S', 1)):
            p.start()
            self.addCleanup(p.stop)

    def scenario(self, text, name='x'):
        path = os.path.join(self.root, 'scenarios', name + '.txt')
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'w') as f:
            f.write(text)
        return path

    def launch(self, mode, text='quit\n'):
        """(result code, launch()'s text, the report) of a run of the stand-in in `mode` on a scenario of `text`."""
        env = dict(os.environ, FAKE_GAME=mode)
        start = time.time()
        code, output = run_all.launch(self.scenario(text), self.out, 'vanilla', self.root, env, [sys.executable, self.game])
        self.seconds = time.time() - start
        path = os.path.join(self.out, 'harness_report.txt')
        report = ''
        if os.path.exists(path):
            with open(path) as f:
                report = f.read()
        return code, output, report

    def test_pass(self):
        code, output, _ = self.launch('pass')
        self.assertEqual(code, 0)
        self.assertIn('ei-rebuild: started', output)
        self.assertIn('True', output)                      # the working directory is the install
        self.assertIn(os.path.join(self.root, 'scenarios', 'x.txt'), output)
        self.assertFalse(os.path.exists(os.path.join(self.out, 'harness_quit')))
        self.assertIn('result code 0', output)

    def test_silent_end(self):
        code, _, report = self.launch('die')
        self.assertEqual(code, 1)
        self.assertIn('FAILED: the game ended (exit code 3) before the harness wrote its result', report)
        self.assertTrue(report.endswith('RESULT: FAIL\n'))

    def test_no_harness(self):
        code, output, _ = self.launch('nostart')
        self.assertEqual(code, 2)
        self.assertIn('the harness did not start', output)
        self.assertLess(self.seconds, 20)

    def test_timeout(self):
        code, output, _ = self.launch('hang', 'timeout 1\nquit\n')
        self.assertEqual(code, run_all.TIMEOUT_EXIT)
        self.assertIn('TIMEOUT: the game is still running', output)
        self.assertLess(self.seconds, 20)

    def test_exit_hang(self):
        code, output, _ = self.launch('exithang')
        self.assertEqual(code, 0)
        self.assertIn('still running 1 s after the scenario quit', output)
        self.assertTrue(os.path.exists(os.path.join(self.out, 'exit_hang.txt')))

    def test_slow_exit_and_abort_at_exit_fail_the_run(self):
        # PL22: a game that ended by itself fails when its own exit stages took over EXIT_SLOW_S or it left by an abort;
        # a quick quit (across midnight too) passes.
        code, _, report = self.launch('slowexit')
        self.assertEqual(code, 1)
        self.assertIn('FAILED: the game took 3.2 s from the quit request to the end of its exit functions (limit 2 s)', report)
        code, _, report = self.launch('abortexit')
        self.assertEqual(code, 1)
        self.assertIn('FAILED: the game ended with exit code C0000409 after its scenario quit', report)
        self.assertEqual(self.launch('quickexit')[0], 0)
        self.assertEqual(self.launch('pass')[0], 0)

    def test_settings_file(self):
        os.makedirs(os.path.join(self.root, 'scenarios', 'settings'))
        with open(os.path.join(self.root, 'scenarios', 'settings', 'x.ini'), 'w') as f:
            f.write('[a]\nx = 1\n')
        code, _, report = self.launch('settings', '# settings-file: settings/x.ini\nquit\n')
        self.assertEqual(code, 1)
        self.assertIn('FAILED: settings file line 2: expected "x = 1", got "x = 2"', report)

    def test_pair(self):
        self.scenario('quit\n', os.path.join('peer', 'p'))
        code, output, report = self.launch('peerfails', 'peer p\nquit\n')
        self.assertEqual(code, 1)
        self.assertIn('FAILED: peer: did not pass', report)
        with open(os.path.join(self.out, 'peer', 'launch.txt')) as f:
            self.assertIn(',1 ', f.read())                 # EI_PAIR's side
        self.assertIn(',0 ', output)
        code, _, report = self.launch('pass', 'peer p\nquit\n')
        self.assertEqual(code, 0, report)


class Retries(unittest.TestCase):
    """A retry empties the run folder; the failed attempt's log and report are kept beside it."""

    def test_failed_attempt_is_kept(self):
        with tempfile.TemporaryDirectory() as build:
            folder = os.path.join(build, 'runs', 'x-standalone')
            os.makedirs(folder)
            calls = []

            def runner(name, install, folder_name):
                calls.append(1)
                if len(calls) == 1:
                    with open(os.path.join(folder, 'ei-rebuild.log'), 'w') as f:
                        f.write('first attempt\n')
                    return 'FAIL', 1.0, ['a check'], {}, []
                shutil.rmtree(folder, ignore_errors=True)   # what run_once does
                os.makedirs(folder)
                return 'PASS', 1.0, [], {}, []

            keep = lambda name, attempt: run_all.keep_attempt(name, attempt, build=build)
            result, _, failures, _, _ = run_all.run_with_retries('x', 'standalone', 1, 'x-standalone', runner, keep)
            self.assertEqual(result, 'PASS*')
            self.assertIn('x-standalone-attempt1', failures[0])
            with open(os.path.join(build, 'runs', 'x-standalone-attempt1', 'ei-rebuild.log')) as f:
                self.assertEqual(f.read(), 'first attempt\n')
            self.assertFalse(os.path.exists(os.path.join(folder, 'ei-rebuild.log')))


    def test_whole_attempt_is_kept(self):
        with tempfile.TemporaryDirectory() as build:
            folder = os.path.join(build, 'runs', 'x-standalone')
            os.makedirs(os.path.join(folder, 'harness_saves'))
            for name in ('ei-rebuild.log', 'digest.txt', os.path.join('harness_saves', 'a.mmp')):
                with open(os.path.join(folder, name), 'w') as f:
                    f.write(name)
            self.assertEqual(run_all.keep_attempt('x-standalone', 1, build=build), 'x-standalone-attempt1')
            kept = os.path.join(build, 'runs', 'x-standalone-attempt1')
            self.assertTrue(os.path.exists(os.path.join(kept, 'digest.txt')))
            self.assertTrue(os.path.exists(os.path.join(kept, 'harness_saves', 'a.mmp')))
            self.assertFalse(os.path.exists(folder))
            self.assertIsNone(run_all.keep_attempt('x-standalone', 2, build=build))


class LowDisk(unittest.TestCase):
    """A game is not started on a nearly full disk (X40: file writes fail there and the game throws)."""

    def test_wait_for_disk(self):
        with tempfile.TemporaryDirectory() as build:
            self.assertIsNone(run_all.wait_for_disk(build, low=1, wait=0))
            failure = run_all.wait_for_disk(build, low=1 << 40, wait=0)
            self.assertIn('MB free on', failure)
            self.assertIn('not started', failure)


if __name__ == '__main__':
    unittest.main()
