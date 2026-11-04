"""Tests of tools/build_retry.py's reading of a failed build's log (no build needed).

usage: python tools/test_build_retry.py [-v]
"""
import os, sys, unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_retry

STATUS = b'[3/9] Building CXX object a.obj'
SILENT_COMPILE = [STATUS, b'FAILED: a.obj ', b'cl.exe /c a.cpp', b'ninja: build stopped: subcommand failed.']
LAUNCHER_LINE = (b'compiler_cache_launcher: cl.exe /c a.cpp exited with 0xC0000142')


def lines(*text):
    return [t + b'\r\n' for t in text]


class Causeless(unittest.TestCase):
    def test_step_with_no_output(self):
        self.assertTrue(build_retry.causeless(lines(*SILENT_COMPILE)))

    def test_launcher_line_only(self):
        log = [STATUS, b'FAILED: a.obj ', b'launcher -- cl.exe /c a.cpp', LAUNCHER_LINE, b'ninja: build stopped: x']
        self.assertTrue(build_retry.causeless(lines(*log)))

    def test_lnk1000_with_detail_lines(self):
        log = [STATUS, b'FAILED: ei_perf_check.exe ', b'cmd /C "link.exe"',
               b'LINK : fatal error LNK1000: Internal error during IncrBuildImage', b'  Version 14.43.34808.0',
               b'  ExceptionCode            = C0000005', b'ninja: build stopped: subcommand failed.']
        self.assertTrue(build_retry.causeless(lines(*log)))

    def test_compile_error(self):
        log = [STATUS, b'FAILED: a.obj ', b'cl.exe /c a.cpp', b'a.cpp(3): error C2065: x: undeclared identifier',
               b'ninja: build stopped: subcommand failed.']
        self.assertFalse(build_retry.causeless(lines(*log)))

    def test_one_failed_step_with_an_error_among_silent_ones(self):
        log = [b'FAILED: a.obj ', b'cl.exe /c a.cpp', b'FAILED: b.obj ', b'cl.exe /c b.cpp', b'b.cpp(1): error C2143: x',
               b'ninja: build stopped: subcommand failed.']
        self.assertFalse(build_retry.causeless(lines(*log)))

    def test_error_and_launcher_line(self):
        log = [STATUS, b'FAILED: a.obj ', b'launcher -- cl.exe /c a.cpp', b'a.cpp(3): error C2065: x', LAUNCHER_LINE]
        self.assertFalse(build_retry.causeless(lines(*log)))

    def test_no_failed_step(self):
        self.assertFalse(build_retry.causeless(lines(STATUS, b'ninja: error: loading build.ninja')))

    def test_warnings_of_a_later_successful_step_do_not_count(self):
        log = [b'FAILED: a.obj ', b'cl.exe /c a.cpp', b'[4/9] Building CXX object b.obj', b'b.cpp(1): warning C4100: x',
               b'ninja: build stopped: subcommand failed.']
        self.assertTrue(build_retry.causeless(lines(*log)))


if __name__ == '__main__':
    unittest.main()
