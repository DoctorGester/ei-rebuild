"""Tests of tools/compiler_cache.py's server port choice (no build needed).

usage: python tools/test_compiler_cache.py [-v]
"""
import os, sys, unittest, zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compiler_cache as cc

KEY = r'e:\workspace\ei\.claude\worktrees\agent-x|release'
FIRST = cc.FIRST_PORT + zlib.crc32(KEY.encode()) % cc.PORTS   # the port with nothing excluded
LAST = cc.FIRST_PORT + cc.PORTS - 1


def never(port):
    return False


class ChoosePort(unittest.TestCase):
    def test_deterministic_and_below_the_dynamic_range(self):
        self.assertEqual(cc.choose_port(KEY, [], never), FIRST)
        self.assertEqual(cc.choose_port(KEY, [], never), cc.choose_port(KEY, [], never))
        self.assertLess(LAST, 49152)

    def test_excluded_range_is_stepped_past(self):
        # a range like the ones that bit two worktrees (49808-49907 held 49841)
        self.assertEqual(cc.choose_port(KEY, [(FIRST - 10, FIRST + 99)], never), FIRST + 100)

    def test_ranges_in_a_row(self):
        self.assertEqual(cc.choose_port(KEY, [(FIRST, FIRST + 99), (FIRST + 100, FIRST + 199)], never), FIRST + 200)

    def test_range_elsewhere_changes_nothing(self):
        self.assertEqual(cc.choose_port(KEY, [(5357, 5357), (49808, 49907)], never), FIRST)

    def test_denied_bind_is_stepped_past(self):
        self.assertEqual(cc.choose_port(KEY, [], lambda p: FIRST <= p < FIRST + 3), FIRST + 3)

    def test_wraps_at_the_end_of_the_range(self):
        port = cc.choose_port(KEY, [(FIRST, LAST)], never)
        self.assertEqual(port, cc.FIRST_PORT)   # the whole rest of the range is excluded

    def test_everything_refused_falls_back_to_the_first_choice(self):
        self.assertEqual(cc.choose_port(KEY, [(cc.FIRST_PORT, LAST)], never), FIRST)

    def test_checkouts_and_configurations_get_different_ports(self):
        ports = {cc.choose_port(f'{k}|{c}', [], never) for k in 'abcdef' for c in ('release', 'verify')}
        self.assertGreater(len(ports), 6)


class Netsh(unittest.TestCase):
    def test_real_ranges_parse(self):
        for first, last in cc.excluded_ranges():
            self.assertLessEqual(first, last)

    def test_bind_denied_answers(self):
        self.assertIsInstance(cc.bind_denied(cc.FIRST_PORT), bool)


if __name__ == '__main__':
    unittest.main()
