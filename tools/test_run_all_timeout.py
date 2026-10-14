"""Tests of run_all.py's time limit handling without games (SV1d): a stand-in ei_launch.

usage: python tools/test_run_all_timeout.py [-v]
The real checks with a game are tests/scenarios/watchdog_*.txt ("# expect: TIMEOUT").
"""
import os, subprocess, sys, tempfile, unittest
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

    def test_expected_result(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertIsNone(run_all.expected_result(scenario_file(d, 'quit\n')))
            self.assertEqual(run_all.expected_result(scenario_file(d, '# x\n# expect: TIMEOUT\nquit\n')), 'TIMEOUT')


class Run(unittest.TestCase):
    """run_once() with ei_launch replaced: its exit code and the report decide the result."""

    def run_scenario(self, scenario_text, returncode, report, stdout='', stderr=''):
        with tempfile.TemporaryDirectory() as root:
            os.makedirs(os.path.join(root, 'tests', 'scenarios'))
            build = os.path.join(root, 'build')
            with open(os.path.join(root, 'tests', 'scenarios', 'x.txt'), 'w') as f:
                f.write(scenario_text)

            def fake(cmd, **kwargs):
                out = cmd[cmd.index('--out') + 1]
                if report is not None:
                    with open(os.path.join(out, 'harness_report.txt'), 'w') as f:
                        f.write(report)
                self.timeout = kwargs.get('timeout')
                return subprocess.CompletedProcess(cmd, returncode, stdout, stderr)

            with mock.patch.object(subprocess, 'run', fake):
                return run_all.run_once('x', 'vanilla', True, None, root, build, False, None, None)

    def test_ei_launch_ended_the_game(self):
        result, _, failures, _, notes = self.run_scenario(
            'timeout 3\nhang all\n', run_all.TIMEOUT_EXIT, 'RESULT: RUNNING\n',
            stderr='ei_launch: TIMEOUT: the game is still running 23 s after its start; terminating it\n')
        self.assertEqual(result, 'TIMEOUT')
        self.assertIn('3 s time limit', failures[0])
        self.assertEqual(self.timeout, 3 + run_all.LIMIT_MARGIN_S + 90)
        self.assertTrue(any('terminating' in n for n in notes))

    def test_watchdog_ended_the_game(self):
        result, _, failures, _, _ = self.run_scenario(
            'quit\n', 1, 'FAILED: scenario exceeded its 60 s time limit at step 3 of 9\nRESULT: FAIL\n')
        self.assertEqual(result, 'TIMEOUT')
        self.assertTrue(failures[0].startswith('scenario exceeded'))

    def test_unkillable_game(self):
        result, _, failures, _, _ = self.run_scenario(
            'quit\n', run_all.UNKILLABLE_EXIT, 'RESULT: RUNNING\n', stderr='ei_launch: the game (pid 1) is still alive\n')
        self.assertEqual(result, 'TIMEOUT')
        self.assertIn('still alive', failures[-1])

    def test_expected_timeout_passes_but_not_an_unkillable_game(self):
        text = '# expect: TIMEOUT\ntimeout 3\nhang all\n'
        result, _, failures, _, _ = self.run_scenario(text, run_all.TIMEOUT_EXIT, 'RESULT: RUNNING\n')
        self.assertEqual((result, failures), ('PASS', []))
        result, _, _, _, _ = self.run_scenario(text, run_all.UNKILLABLE_EXIT, 'RESULT: RUNNING\n')
        self.assertEqual(result, 'TIMEOUT')

    def test_ordinary_results_are_unchanged(self):
        self.assertEqual(self.run_scenario('quit\n', 0, 'RESULT: PASS\n')[0], 'PASS')
        self.assertEqual(self.run_scenario('quit\n', 1, 'FAILED: a check\nRESULT: FAIL\n')[0], 'FAIL')
        self.assertEqual(self.run_scenario('quit\n', 2, None)[0], 'NO REPORT')


if __name__ == '__main__':
    unittest.main()
