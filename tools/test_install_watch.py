"""Tests of install_watch.py (H47a): the install trees listed before and after a suite, the runs that overlap a change.

usage: python tools/test_install_watch.py [-v]
"""
import io, os, sys, tempfile, time, unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import install_watch, run_all


def write(root, rel, text='x', when=None):
    path = os.path.join(root, *rel.split('/'))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w') as f:
        f.write(text)
    if when is not None:
        os.utime(path, (when, when))
    return path


class Listing(unittest.TestCase):
    def test_names_sizes_and_folders(self):
        with tempfile.TemporaryDirectory() as d:
            write(d, 'a.txt', 'abc')
            write(d, 'sub/deeper/b.bin', 'hello')
            os.makedirs(os.path.join(d, 'empty'))
            tree = install_watch.list_installs([d])[os.path.normpath(d)]
            self.assertEqual(tree['a.txt'][0], 3)
            self.assertEqual(tree['sub/deeper/b.bin'][0], 5)
            self.assertIsNone(tree['empty/'])
            self.assertIn('sub/', tree)
            self.assertIn('sub/deeper/', tree)
            self.assertEqual(len(tree), 5)

    def test_missing_root_is_empty(self):
        with tempfile.TemporaryDirectory() as d:
            gone = os.path.normpath(os.path.join(d, 'none'))
            self.assertEqual(install_watch.list_installs([gone]), {gone: {}})

    def test_same_root_once(self):
        with tempfile.TemporaryDirectory() as d:
            write(d, 'a', '1')
            self.assertEqual(len(install_watch.list_installs([d, d, d + os.sep])), 1)


class Changes(unittest.TestCase):
    def test_kinds(self):
        before = {'r': {'keep': (1, 10), 'size': (1, 10), 'time': (1, 10), 'gone': (1, 10)}}
        after = {'r': {'keep': (1, 10), 'size': (2, 10), 'time': (1, 2_000_000_000), 'new': (5, 3_000_000_000)}}
        found = install_watch.changes(before, after)
        self.assertEqual([(k, p) for k, _, p, _ in found],
                         [('removed', 'gone'), ('added', 'new'), ('changed', 'size'), ('changed', 'time')])
        self.assertEqual({p: t for _, _, p, t in found}, {'new': 3.0, 'gone': None, 'size': 1e-8, 'time': 2.0})

    def test_no_change(self):
        tree = {'r': {'a': (1, 5), 'd/': None}}
        self.assertEqual(install_watch.changes(tree, dict(tree)), [])


class Owners(unittest.TestCase):
    windows = {'run-a': [(100.0, 110.0)], 'run-b': [(105.0, 120.0)], 'run-c': [(130.0, 140.0)]}

    def test_time_decides(self):
        found = [('added', 'r', 'one', 135.0), ('changed', 'r', 'two', 107.0)]
        hit, loose = install_watch.owners(found, self.windows)
        self.assertEqual(sorted(hit), ['run-a', 'run-b', 'run-c'])
        self.assertEqual([c[2] for c in hit['run-c']], ['one'])
        self.assertEqual([c[2] for c in hit['run-a']], ['two'])
        self.assertEqual(loose, [])

    def test_between_runs_is_loose(self):
        hit, loose = install_watch.owners([('added', 'r', 'x', 125.0)], self.windows)
        self.assertEqual(hit, {})
        self.assertEqual(len(loose), 1)

    def test_slack(self):
        hit, _ = install_watch.owners([('added', 'r', 'x', 140.0 + install_watch.AFTER_S - 0.1)], self.windows)
        self.assertEqual(list(hit), ['run-c'])
        hit, _ = install_watch.owners([('added', 'r', 'x', 100.0 - install_watch.BEFORE_S + 0.1)], self.windows)
        self.assertEqual(list(hit), ['run-a'])

    def test_no_time_or_older_than_the_list_hits_every_run(self):
        for seconds in (None, 5.0):
            hit, loose = install_watch.owners([('removed', 'r', 'x', seconds)], self.windows, since=90.0)
            self.assertEqual(sorted(hit), ['run-a', 'run-b', 'run-c'])
            self.assertEqual(loose, [])

    def test_no_runs(self):
        hit, loose = install_watch.owners([('removed', 'r', 'x', None)], {})
        self.assertEqual((hit, len(loose)), ({}, 1))


class Apply(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.root = self.dir.name
        write(self.root, 'data/a.res', 'aaa', when=1_000_000)
        write(self.root, 'game.ini', 'ini', when=1_000_000)
        self.jobs = {('s1', 'standalone'): 'f1', ('s2', 'standalone'): 'f2'}
        self.results = {j: ('PASS', 1.0, [], []) for j in self.jobs}

    def tearDown(self):
        self.dir.cleanup()

    def apply(self, watch, windows):
        out = io.StringIO()
        watch.apply(self.results, self.jobs, windows, out)
        return out.getvalue()

    def test_untouched(self):
        watch = install_watch.Watch([self.root])
        text = self.apply(watch, {'f1': [(1.0, 2.0)], 'f2': [(1.0, 2.0)]})
        self.assertIn('no change', text)
        self.assertIn('3 files in 1 install(s)', text)
        self.assertTrue(all(r[0] == 'PASS' for r in self.results.values()))

    def test_write_during_one_run_fails_that_run_only(self):
        watch = install_watch.Watch([self.root])
        now = time.time()
        write(self.root, 'data/cache.bin', 'new')           # now: inside f2's window, outside f1's
        text = self.apply(watch, {'f1': [(now - 60, now - 30)], 'f2': [(now - 5, now + 5)]})
        self.assertEqual(self.results[('s1', 'standalone')][0], 'PASS')
        result, _, failures, _ = self.results[('s2', 'standalone')]
        self.assertEqual(result, 'FAIL')
        self.assertIn('cache.bin', failures[-1])
        self.assertIn('1 changed', text)
        self.assertIn('1 run(s) overlap', text)

    def test_removed_file_fails_all(self):
        watch = install_watch.Watch([self.root])
        os.remove(os.path.join(self.root, 'game.ini'))
        now = time.time()
        self.apply(watch, {'f1': [(now - 5, now)], 'f2': [(now - 4, now)]})
        self.assertEqual([r[0] for r in self.results.values()], ['FAIL', 'FAIL'])

    def test_change_between_runs_only_reported(self):
        watch = install_watch.Watch([self.root])
        now = time.time()
        write(self.root, 'late.txt', 'x')
        text = self.apply(watch, {'f1': [(now - 100, now - 90)], 'f2': [(now - 80, now - 70)]})
        self.assertTrue(all(r[0] == 'PASS' for r in self.results.values()))
        self.assertIn('late.txt', text)

    def test_many_changes_are_counted(self):
        watch = install_watch.Watch([self.root])
        now = time.time()
        for i in range(10):
            write(self.root, f'n{i}.txt')
        self.apply(watch, {'f1': [(now - 5, now + 5)], 'f2': [(now - 100, now - 90)]})
        self.assertIn('and 6 more', self.results[('s1', 'standalone')][2][-1])


class RunAll(unittest.TestCase):
    """run_all.check_installs: the runs' windows (RUN_WINDOWS) decide which run fails."""

    def test_check_installs(self):
        with tempfile.TemporaryDirectory() as d:
            install = os.path.join(d, 'install')
            write(install, 'a.res', 'a', when=1_000_000)
            watch = run_all.watch_installs(['standalone'], {'EI_GAME_VANILLA': install})
            self.assertEqual(watch.roots, [os.path.normpath(install)])
            now = time.time()
            jobs = [('one', 'standalone'), ('two', 'standalone')]
            folders = {j: os.path.join(d, 'runs', f'{j[0]}-{j[1]}') for j in jobs}
            run_all.RUN_WINDOWS.update({folders[jobs[0]]: [(now - 30, now - 20)], folders[jobs[1]]: [(now - 2, now + 2)]})
            write(install, 'driver.cache', 'x')
            results = {j: ('PASS', 1.0, [], []) for j in jobs}
            out = io.StringIO()
            run_all.check_installs(watch, jobs, results, out, build=d)
            self.assertEqual([results[j][0] for j in jobs], ['PASS', 'FAIL'])
            self.assertIn('driver.cache', results[jobs[1]][2][0])
            self.assertIn('install trees: 1 files', out.getvalue())


if __name__ == '__main__':
    unittest.main()
