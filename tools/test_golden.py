"""Tests of tools/golden.py on made-up digests (no games).
usage: python tools/test_golden.py [-v]
"""
import io, json, os, shutil, struct, sys, tempfile, unittest
from contextlib import redirect_stdout

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import golden

TITLE = '# ei state digest 1; hooks {hooks}; install vanilla; every 20 world updates\n'


def record(n, update, lines, before):
    """A record's header and lines; its hash covers every line so far (before: the earlier records)."""
    state = golden.crc('\n'.join(before + lines))
    return [f'@ {n} update {update} frame {update // 5} tick {update} zone gz1g hash {state} world 0 random 0 players 0 objects 0 n 3'] + lines


def digest(hooks='off', hp='20.0', countdown='141', wolf_x='10.5', later=None):
    """Three records: a hero (a unit) and a stone; the hero walks; later: extra lines in the last record."""
    lines = record(0, 20, ['+ world world', 'world tick 20', '+ random random', 'random crt.rand 0x00000001',
                           '+ obj1 80 unhuma ""', 'obj1 class 80', f'obj1 pos.x {wolf_x}', f'obj1 hp {hp}', 'obj1 player 0',
                           'obj1 ai.mode guard', f'obj1 updateCountdown {countdown}',
                           '+ obj2 84 stbr1', 'obj2 class 84', 'obj2 pos.x 3.0'], [])
    lines += record(1, 40, ['world tick 40', 'obj1 pos.x 11.0'], lines)
    lines += record(2, 60, ['world tick 60', 'random crt.rand 0x00000002'] + (later or []), lines)
    return TITLE.format(hooks=hooks) + '\n'.join(lines) + '\n'


class GoldenTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    def write(self, name, text):
        path = os.path.join(self.dir, name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'w', encoding='cp1251', newline='\n') as f:
            f.write(text)
        return path

    def run_folder(self, name, text, console=('  hello',), saves=b'gz1g'):
        folder = os.path.join(self.dir, name)
        self.write(os.path.join(name, 'digest.txt'), text)
        self.write(os.path.join(name, 'dump_loaded.txt'), text.split('@ 1 ')[0])
        self.write(os.path.join(name, 'harness_report.txt'), 'RESULT: PASS\nconsole output:\n' + '\n'.join(console) + '\n')
        os.makedirs(os.path.join(folder, golden.SAVES, 'savea'))
        with open(os.path.join(folder, golden.SAVES, 'savea', 'info.sav'), 'wb') as f:
            # An info.sav (not framed, tools/savefile.py read_info): the zone name differs by `saves`.
            f.write(struct.pack('<IIf', 0x111, 3, 6.0) + b'gipat\0' + saves + b'\0' + bytes(5))
        return folder

    def compact_of(self, text, name, ignore=None):
        src = self.write(name + '.full', text)
        dst = os.path.join(self.dir, name + '.compact')
        golden.compact(src, dst, ignore=ignore)
        return dst

    def diff_lines(self, a, b):
        out = []
        code = golden.diff(a, b, 'A', 'B', out=out.append)
        return code, out

    def test_compact_round_trip(self):
        a = self.compact_of(digest(), 'a')
        d = golden.Digest(a)
        self.assertTrue(d.compact)
        self.assertEqual(d.period(), 20)
        states = [dict(d.state) for _ in d]
        self.assertEqual(len(states), 3)
        self.assertEqual(sorted(d.state['obj1'][1]), ['#ai', '#place', '#unit', '#vitals'])
        self.assertEqual(list(d.state['obj2'][1]), ['#'])
        self.assertEqual(d.state['random'][1]['crt.rand'], '0x00000002')

    def test_countdown_compared_since_sv1b(self):
        # SV1b's repeatable heap takes the countdown from the id on both sides, so it is compared; runs with
        # `--clock virtual,lfheap` keep the game's and leave it out with --ignore.
        a, b = self.compact_of(digest(), 'a'), self.compact_of(digest(countdown='150'), 'b')
        self.assertEqual(self.diff_lines(a, b)[0], 1)
        a, b = (self.compact_of(digest(), 'a2', ignore=r' updateCountdown$'),
                self.compact_of(digest(countdown='150'), 'b2', ignore=r' updateCountdown$'))
        code, out = self.diff_lines(a, b)
        self.assertEqual(code, 0, out)

    def test_difference_names_record_object_and_group(self):
        a, b = self.compact_of(digest(), 'a'), self.compact_of(digest(later=['obj1 hp 19.0']), 'b')
        code, out = self.diff_lines(a, b)
        self.assertEqual(code, 1)
        self.assertIn('record 2 (world update 60', out[0])
        self.assertTrue(any(line.startswith('  obj1 (80 unhuma "") #vitals:') for line in out), out)
        self.assertFalse(any('#place' in line for line in out), out)

    def test_known_difference_kept_in_clear_and_left_out(self):
        old = dict(golden.KNOWN)
        golden.KNOWN.clear()
        golden.KNOWN[r'^obj1 hp$'] = 'test'
        try:
            a, b = self.compact_of(digest(), 'a'), self.compact_of(digest(hp='nan:0xFFFFFFFF'), 'b')
            d = golden.Digest(a)
            for _ in d:
                pass
            self.assertEqual(d.state['obj1'][1]['hp'], '20.0')
            out = []
            self.assertEqual(golden.diff(a, b, 'A', 'B', ignore=golden.known_pattern(), out=out.append), 0, out)
            self.assertEqual(golden.diff(a, b, 'A', 'B', out=out.append), 1)
        finally:
            golden.KNOWN.clear()
            golden.KNOWN.update(old)

    def test_full_against_compact_refused(self):
        full = self.write('full.txt', digest())
        code, out = self.diff_lines(self.compact_of(digest(), 'a'), full)
        self.assertEqual(code, 1)
        self.assertIn('compact', out[0])

    def test_full_diff_after_untouched_records(self):
        a, b = self.write('a.txt', digest()), self.write('b.txt', digest(later=['obj2 pos.x 4.0']))
        code, out = self.diff_lines(a, b)
        self.assertEqual(code, 1)
        self.assertIn('obj2 (84 stbr1) pos.x: 3.0 in A, 4.0 in B', out[1])

    def test_record_and_compare(self):
        orig1 = self.run_folder('orig1/levers-vanilla', digest())
        self.run_folder('orig2/levers-vanilla', digest())
        ours = self.run_folder('ours/levers-vanilla', digest(hooks='on'))
        old = golden.GOLDEN, golden.STORE
        golden.GOLDEN = os.path.join(self.dir, 'golden')
        golden.STORE = os.path.join(golden.GOLDEN, 'saves')
        try:
            args = type('A', (), dict(scenarios=['levers'], install='vanilla', reason='test', fields=12, image=False,
                                      runs=[os.path.dirname(orig1), os.path.dirname(ours), os.path.join(self.dir, 'orig2')]))
            with redirect_stdout(io.StringIO()) as out:
                self.assertEqual(golden.record(args), 0, out.getvalue())
            target = os.path.join(golden.GOLDEN, 'levers', 'vanilla')
            self.assertEqual(sorted(os.listdir(target)),
                             ['console.txt', 'digest.txt', 'dump_loaded.txt', 'golden.json', 'saves.txt'])
            self.assertEqual(json.load(open(os.path.join(target, 'golden.json')))['digest_period'], 20)
            golden_folder = golden.golden_folder
            golden.golden_folder = lambda name, install, root=None: os.path.join(golden.GOLDEN, name, install)
            try:
                self.assertEqual(golden.compare_one('levers', 'vanilla', ours), ('SAME', []))
                bad = self.run_folder('bad/levers-vanilla', digest(hooks='on', later=['obj1 hp 1.0']),
                                      console=('  hello', '  extra'), saves=b'other')
                result, lines = golden.compare_one('levers', 'vanilla', bad)
                self.assertEqual(result, 'DIFFERS')
                self.assertTrue(lines[0].startswith('console: differs from line 2'), lines)
                self.assertTrue(any('#vitals' in line for line in lines), lines)
                self.assertIn('  info zone: "gz1g" in the golden, "other" in the run', lines)
            finally:
                golden.golden_folder = golden_folder
            # A second run of the original that does not repeat: not recorded.
            self.run_folder('orig3/levers-vanilla', digest(later=['obj1 pos.x 12.0']))
            args.runs = [os.path.dirname(orig1), os.path.join(self.dir, 'orig3')]
            with redirect_stdout(io.StringIO()) as out:
                self.assertEqual(golden.record(args), 2)
            self.assertIn('not recorded', out.getvalue())
            self.assertIn('obj1 (80 unhuma "") pos.x: 11.0', out.getvalue())
        finally:
            golden.GOLDEN, golden.STORE = old

    def test_pair_records_and_compares_both_games(self):
        # SV5d: a pair's second game is the run folder's peer\ and the golden's peer\.
        runs = []
        for d in ('orig1', 'orig2'):
            runs.append(os.path.dirname(self.run_folder(f'{d}/pair-vanilla', digest())))
            self.run_folder(f'{d}/pair-vanilla/peer', digest(wolf_x='20.5'), console=('  peer',))
        old, golden_folder = (golden.GOLDEN, golden.STORE), golden.golden_folder
        golden.GOLDEN = os.path.join(self.dir, 'golden')
        golden.STORE = os.path.join(golden.GOLDEN, 'saves')
        golden.golden_folder = lambda name, install, root=None: os.path.join(golden.GOLDEN, name, install)
        try:
            args = type('A', (), dict(scenarios=['pair'], install='vanilla', reason='test', fields=12, image=False,
                                      runs=runs))
            with redirect_stdout(io.StringIO()) as out:
                self.assertEqual(golden.record(args), 0, out.getvalue())
            target = os.path.join(golden.GOLDEN, 'pair', 'vanilla')
            with open(os.path.join(target, 'peer', 'golden.json'), encoding='utf-8') as f:
                self.assertEqual(json.load(f)['side'], 'peer')
            self.assertEqual(golden.read_lines(os.path.join(target, 'peer', 'console.txt')), ['  peer'])
            self.assertEqual(golden.golden_store(os.path.join(target, 'peer')), golden.STORE)
            self.assertTrue(os.path.exists(os.path.join(target, 'peer', 'saves.txt')))
            self.assertEqual(golden.compare_one('pair', 'vanilla', os.path.join(runs[1], 'pair-vanilla')), ('SAME', []))
            bad = self.run_folder('bad/pair-vanilla', digest(hooks='on'))
            self.run_folder('bad/pair-vanilla/peer', digest(hooks='on', wolf_x='20.5'), console=('  other',))
            result, lines = golden.compare_one('pair', 'vanilla', bad)
            self.assertEqual(result, 'DIFFERS')
            self.assertTrue(lines and all(line.startswith('peer: ') for line in lines), lines)
            shutil.rmtree(os.path.join(bad, 'peer'))
            self.assertEqual(golden.compare_one('pair', 'vanilla', bad)[1][0][:17], 'peer: no run in ' + bad[:1])
            # The second game's runs must repeat too.
            shutil.rmtree(os.path.join(runs[1], 'pair-vanilla', 'peer'))
            self.run_folder('orig2/pair-vanilla/peer', digest(wolf_x='21.5'), console=('  peer',))
            with redirect_stdout(io.StringIO()) as out:
                self.assertEqual(golden.record(args), 2)
            self.assertIn('peer differs', out.getvalue())
        finally:
            golden.GOLDEN, golden.STORE = old
            golden.golden_folder = golden_folder

    def test_snapshot_skips_harness_screenshots(self):
        build = os.path.join(self.dir, 'build')
        folder = self.run_folder('build/runs/levers-vanilla', digest())
        saves = os.path.join(folder, golden.SAVES)
        for name in ('shot0.bmp', 'Shot12.BMP'):
            with open(os.path.join(saves, name), 'wb') as f:
                f.write(b'BM')
        with open(os.path.join(saves, 'savea', 'shot.bmp'), 'wb') as f:
            f.write(b'BM')
        with open(os.path.join(saves, 'keyboard.ini'), 'wb') as f:
            f.write(b'k')
        old, dest = golden.run_all.BUILD, os.path.join(self.dir, 'dest')
        golden.run_all.BUILD = build
        try:
            args = type('Args', (), {'scenarios': ['levers'], 'install': 'vanilla', 'image': False, 'dest': dest})()
            with redirect_stdout(io.StringIO()):
                self.assertEqual(golden.snapshot(args), 0)
        finally:
            golden.run_all.BUILD = old
        copied = os.path.join(dest, 'levers-vanilla')
        self.assertTrue(os.path.exists(os.path.join(copied, 'digest.txt')))
        self.assertEqual(sorted(os.listdir(os.path.join(copied, golden.SAVES))), ['keyboard.ini', 'savea'])
        self.assertEqual(os.listdir(os.path.join(copied, golden.SAVES, 'savea')), ['info.sav'])


if __name__ == '__main__':
    unittest.main()
