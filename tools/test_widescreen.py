"""Tests of --widescreen's references (tools/run_all.py record_widescreen / compare_widescreen, S21b) and the
compare_frames.py options they use (--shift, --exclude-mask, --exclude-rect, --write-skip).

usage: python tools/test_widescreen.py
"""
import io, json, os, shutil, struct, subprocess, sys, tempfile, unittest, unittest.mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare_frames, frame_store, run_all

TOOL = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'compare_frames.py')


def picture(width=200, height=120, panel=(40, 30, 100, 80), dot=None, seed=0):
    """A 24-bit BMP: a gradient background, a textured panel with sharp edges at `panel`, and a 3x3 dot at `dot`."""
    rows = []
    for y in range(height):
        row = bytearray()
        for x in range(width):
            if panel[0] <= x < panel[2] and panel[1] <= y < panel[3]:
                c = (200, 40 + (x + 2 * y + seed) // 4, 30)
            else:
                c = (20 + x // 8, 10 + y // 8, 60)
            if dot and abs(x - dot[0]) <= 1 and abs(y - dot[1]) <= 1:
                c = (250, 250, 250)
            row += bytes(c)
        rows.append(row)
    stride = (width * 3 + 3) & ~3
    data = b'BM' + struct.pack('<IHHI', 54 + stride * height, 0, 0, 54)
    data += struct.pack('<IiiHHIIiiII', 40, width, height, 1, 24, 0, stride * height, 0, 0, 0, 0)
    for y in range(height - 1, -1, -1):
        data += bytes(rows[y]) + b'\0' * (stride - width * 3)
    return data


class CompareFramesOptions(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix='s21b_')
        self.addCleanup(shutil.rmtree, self.tmp, True)

    def write(self, name, data):
        path = os.path.join(self.tmp, name)
        with open(path, 'wb') as f:
            f.write(data)
        return path

    def compare(self, *args):
        p = subprocess.run([sys.executable, TOOL, *args], capture_output=True, text=True)
        return p.returncode, p.stdout + p.stderr

    def test_shift_allows_a_pixel_not_more(self):
        a = self.write('a.bmp', picture())
        one = self.write('one.bmp', picture(panel=(41, 30, 101, 80)))      # the panel a pixel to the right
        four = self.write('four.bmp', picture(panel=(44, 30, 104, 80)))     # four pixels: a wrong offset
        self.assertEqual(self.compare(a, one, '--max-fraction', '0')[0], 1)
        self.assertEqual(self.compare(a, one, '--max-fraction', '0', '--shift', '1')[0], 0)
        code, text = self.compare(a, four, '--shift', '1')
        self.assertEqual(code, 1, text)

    def test_shift_sees_a_missing_dot_in_either_frame(self):
        a = self.write('a.bmp', picture())
        b = self.write('b.bmp', picture(dot=(150, 20)))
        for x, y in ((a, b), (b, a)):
            code, text = self.compare(x, y, '--max-fraction', '0', '--shift', '1')
            self.assertEqual(code, 1, text)
            self.assertIn('9 pixels differ', text)

    def test_excluded_rectangle_and_mask(self):
        a = self.write('a.bmp', picture())
        b = self.write('b.bmp', picture(dot=(150, 20)))
        self.assertEqual(self.compare(a, b, '--max-fraction', '0', '--exclude-rect', '140', '10', '160', '30')[0], 0)
        self.assertEqual(self.compare(a, b, '--max-fraction', '0', '--exclude-rect', '151', '10', '160', '30')[0], 1)
        # --write-skip keeps what moved between a frame and its twin; --exclude-mask leaves it out later
        skip = os.path.join(self.tmp, 'skip.bmp')
        code, text = self.compare(b, b, '--exclude', b, a, '--write-skip', skip)
        self.assertEqual(code, 0, text)
        self.assertIn('9 pixels left out', text)
        self.assertEqual(self.compare(a, b, '--max-fraction', '0', '--exclude-mask', skip)[0], 0)
        frame_store.pack_file(skip)                                         # read packed as well
        self.assertEqual(self.compare(a, b, '--max-fraction', '0', '--exclude-mask', skip)[0], 0)


class WidescreenReferences(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix='s21b_')
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.root = os.path.join(self.tmp, 'repo')
        self.build = os.path.join(self.tmp, 'build')
        os.makedirs(os.path.join(self.root, 'tests', 'scenarios'))
        os.makedirs(os.path.join(self.root, 'tools'))
        shutil.copy(TOOL, os.path.join(self.root, 'tools'))
        shutil.copy(frame_store.__file__, os.path.join(self.root, 'tools'))
        self.scenario('# a widescreen scenario\nresolution desktop\nwidescreen 1\ncapture menu\n')
        env = dict(os.environ, EI_WIDESCREEN_DIR=os.path.join(self.tmp, 'widescreen'))
        patcher = unittest.mock.patch.dict(os.environ, env)
        patcher.start()
        self.addCleanup(patcher.stop)

    def scenario(self, text):
        with open(os.path.join(self.root, 'tests', 'scenarios', 'w.txt'), 'w') as f:
            f.write(text)

    def run_folder(self, install, frames, result='PASS'):
        folder = os.path.join(self.build, 'runs', f'w-{install}')
        shutil.rmtree(folder, ignore_errors=True)
        os.makedirs(os.path.join(folder, 'frames'))
        with open(os.path.join(folder, 'harness_report.txt'), 'w') as f:
            f.write(f'RESULT: {result}\n')
        for name, data in frames.items():
            with open(os.path.join(folder, 'frames', name + '.bmp'), 'wb') as f:
                f.write(data)
        frame_store.pack_folder(folder)

    def record(self):
        out = io.StringIO()
        return run_all.widescreen_check([('w', 'rebirth-nohooks')], True, out, self.root, self.build), out.getvalue()

    def check(self):
        out = io.StringIO()
        return run_all.widescreen_check([('w', 'standalone-rebirth')], False, out, self.root, self.build), out.getvalue()

    def test_steps_key_ignores_comments(self):
        path = os.path.join(self.root, 'tests', 'scenarios', 'w.txt')
        key = run_all.steps_key(path)
        self.scenario('# reworded\n\nresolution desktop\n  widescreen 1\ncapture menu\n# more\n')
        self.assertEqual(run_all.steps_key(path), key)
        self.scenario('resolution desktop\nwidescreen 1\nwait 10\ncapture menu\n')
        self.assertNotEqual(run_all.steps_key(path), key)

    def test_record_then_compare(self):
        # addon's run: the menu, its creature (the dot) moved between menu and menu_later
        self.run_folder('rebirth-nohooks', {'menu': picture(dot=(150, 20)), 'menu_later': picture(dot=(160, 20))})
        ok, text = self.record()
        self.assertTrue(ok, text)
        self.assertIn('WIDESCREEN RECORDED', text)
        reference = run_all.widescreen_reference('w', self.root)
        with open(os.path.join(reference, "recorded.json")) as f:
            info = json.load(f)
        self.assertEqual((info['frames'], info['size']), (['menu'], [200, 120]))
        self.assertTrue(frame_store.exists(os.path.join(reference, 'skip', 'menu.bmp')))

        # the image: the creature elsewhere (in its own twin's moving area) and the panel a pixel off: the same
        self.run_folder('standalone-rebirth', {'menu': picture(panel=(41, 30, 101, 80), dot=(100, 100)),
                                               'menu_later': picture(panel=(41, 30, 101, 80), dot=(105, 100))})
        ok, text = self.check()
        self.assertTrue(ok, text)
        self.assertIn('WIDESCREEN FRAMES MATCH', text)

        # the panel at a wrong offset
        self.run_folder('standalone-rebirth', {'menu': picture(panel=(46, 30, 106, 80))})
        ok, text = self.check()
        self.assertFalse(ok, text)
        self.assertIn('FAIL', text)
        self.assertTrue(frame_store.exists(os.path.join(self.build, 'runs', 'w-standalone-rebirth', 'frames_diff',
                                                        'menu.bmp')))

        # a changed step needs a new recording; a comment does not
        self.scenario('# reworded\nresolution desktop\nwidescreen 1\ncapture menu\n')
        self.assertEqual(run_all.widescreen_reference('w', self.root), reference)
        self.scenario('resolution desktop\nwidescreen 1\nwait 10\ncapture menu\n')
        ok, text = self.check()
        self.assertFalse(ok)
        self.assertIn('--widescreen --record w', text)

    def test_another_mode_needs_a_new_recording(self):
        self.run_folder('rebirth-nohooks', {'menu': picture()})
        self.assertTrue(self.record()[0])
        self.run_folder('standalone-rebirth', {'menu': picture(width=100, height=60, panel=(20, 15, 50, 40))})
        ok, text = self.check()
        self.assertFalse(ok)
        self.assertIn('the references are 200x120, the run\'s frames 100x60', text)

    def test_failed_run_is_not_recorded(self):
        self.run_folder('rebirth-nohooks', {'menu': picture()}, result='FAIL')
        ok, text = self.record()
        self.assertFalse(ok)
        self.assertIn('not recorded (the run did not pass)', text)
        self.assertIsNone(run_all.widescreen_reference('w', self.root))

    def test_areas_scale_with_the_frame(self):
        areas = {('w', 'menu'): [((0, 1000, 3840, 1600), 'the sea')], ('w', '*'): [((10, 0, 20, 14), 'a band')]}
        with unittest.mock.patch.object(run_all, 'WIDESCREEN_AREAS', areas):
            self.assertEqual(run_all.widescreen_areas('w', 'menu', [1920, 1080]),
                             ['--exclude-rect', '0', '500', '1920', '800', '--exclude-rect', '5', '0', '10', '7'])
            self.assertEqual(run_all.widescreen_areas('w', 'load', None), ['--exclude-rect', '10', '0', '20', '14'])

    def test_plan(self):
        args = run_all.parse_args(['--widescreen'])
        self.assertEqual(run_all.plan(args), [(n, 'standalone-rebirth') for n in run_all.WIDESCREEN_SCENARIOS])
        self.assertTrue(run_all.standalone_mode(args))
        args = run_all.parse_args(['--widescreen', '--record', 'ui_widescreen'])
        self.assertEqual(run_all.plan(args), [('ui_widescreen', 'rebirth-nohooks')])
        self.assertFalse(run_all.uses_hooks('rebirth-nohooks', args))
        self.assertFalse(run_all.wants_dgvoodoo(args))
        for bad in (['--record'], ['--widescreen', '--vanilla-only'], ['--widescreen', '--no-hooks']):
            with self.assertRaises(SystemExit):
                run_all.plan(run_all.parse_args(bad))


if __name__ == '__main__':
    unittest.main()
