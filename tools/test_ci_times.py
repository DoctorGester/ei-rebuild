"""Tests of ci_times.py's reading of a suites job's log (H75), without gh.

usage: python tools/test_ci_times.py [-v]
"""
import os, sys, unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ci_times, run_all

STAMP = 'suites (2)\tSuites\t2026-11-12T12:11:00.5433900Z '
LOG = '\n'.join(STAMP + line for line in [
    'shard 2 of 8: app_cursor_confine batch_spells',
    run_all.HEADER,
    'app_cursor_confine standalone PASS          27s  ',
    'batch_spells       standalone TIMEOUT      213s  scenario exceeded its 213 s time limit at step 251 of 255',
    'effect_viewer      standalone NO REPORT      0s    [build/linux-harness/runs/effect_viewer-standalone]',
    '',
    'not passed:',
    'batch_spells       standalone TIMEOUT      213s  scenario exceeded its 213 s time limit at step 251 of 255',
    'goldens (tests/golden):',
    '  batch_spells       standalone DIFFERS',
    run_all.HEADER,
    'ui_trade_hold      standalone-rebirth PASS*         13s  first run: ...',
    '1 runs in 13s with 1 at once',
    '### shard 2 suite_image',
    'not passed:',
    'batch_spells       standalone TIMEOUT      213s  scenario exceeded its 213 s time limit at step 251 of 255',
])


class ResultLines(unittest.TestCase):
    def test_each_run_once(self):
        self.assertEqual(ci_times.result_lines(LOG), [
            ('app_cursor_confine', 'standalone', 'PASS', 27),
            ('batch_spells', 'standalone', 'TIMEOUT', 213),
            ('effect_viewer', 'standalone', 'NO REPORT', 0),
            ('ui_trade_hold', 'standalone-rebirth', 'PASS*', 13),
        ])

    def test_table(self):
        results = {('batch_spells', 'standalone'): [('PASS', 100), ('TIMEOUT', 213)],
                   ('watchdog_main', 'standalone'): [('PASS', 30)]}
        rows = ci_times.table(results, '3')
        self.assertEqual([r[1] for r in rows], ['batch_spells'])     # "# expect: TIMEOUT" left out
        fraction, _, _, limit, times, failed = rows[0]
        self.assertEqual(times, [100])
        self.assertEqual(failed, [('TIMEOUT', 213)])
        self.assertAlmostEqual(fraction, 213 / limit)


if __name__ == '__main__':
    unittest.main()
