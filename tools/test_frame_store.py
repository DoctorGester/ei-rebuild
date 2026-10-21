"""Tests of tools/frame_store.py (packed frame captures) and that compare_frames reads both forms.

usage: python tools/test_frame_store.py
"""
import os, random, struct, subprocess, sys, tempfile, unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare_frames, frame_store, run_all


def bmp(width, height, bits=24, seed=1):
    """A bottom-up BMP with a flat background and some noisy blocks."""
    rnd = random.Random(seed)
    step = bits // 8
    stride = (width * step + 3) & ~3
    rows = []
    for y in range(height):
        row = bytearray(bytes((10, 20, 30)) * width if step == 3 else bytes((10, 20, 30, 0)) * width)
        if y % 7 < 3:
            for x in range(width // 3, width // 2):
                row[x * step:x * step + 3] = bytes(rnd.randrange(256) for _ in range(3))
        rows.append(bytes(row) + b'\0' * (stride - width * step))
    header = b'BM' + struct.pack('<IHHI', 54 + stride * height, 0, 0, 54)
    header += struct.pack('<IiiHHIIiiII', 40, width, height, 1, bits, 0, stride * height, 0, 0, 0, 0)
    return header + b''.join(rows)


class FrameStoreTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix='wb11_')

    def path(self, *names):
        return os.path.join(self.tmp, *names)

    def test_round_trip(self):
        for data in (bmp(101, 37), bmp(64, 20, 32), b'', b'x', b'BM' + bytes(range(256)) * 5, os.urandom(1000)):
            blob = frame_store.pack(data)
            self.assertEqual(frame_store.unpack(blob), data)
        self.assertLess(len(frame_store.pack(bmp(400, 200))), len(bmp(400, 200)) // 3)

    def test_folder_pack_and_read(self):
        os.makedirs(self.path('frames'))
        a, b = bmp(500, 300), bmp(500, 300, seed=2)
        for name, data in (('a.bmp', a), ('b.bmp', b), ('tiny.bmp', bmp(10, 10))):
            with open(self.path('frames', name), 'wb') as f:
                f.write(data)
        before, after = frame_store.pack_folder(self.path('frames'))
        self.assertGreater(before, after)
        self.assertEqual(sorted(os.listdir(self.path('frames'))), ['a.bmp.z', 'b.bmp.z', 'tiny.bmp'])
        self.assertEqual(frame_store.frame_names(self.path('frames')), ['a.bmp', 'b.bmp', 'tiny.bmp'])
        self.assertTrue(frame_store.exists(self.path('frames', 'a.bmp')))
        self.assertFalse(frame_store.exists(self.path('frames', 'c.bmp')))
        self.assertEqual(frame_store.read_frame(self.path('frames', 'a.bmp')), a)
        self.assertEqual(frame_store.pack_folder(self.path('frames')), (0, 0))      # nothing left to pack

    def test_pack_run_in_its_own_process(self):
        os.makedirs(self.path('run', 'frames'))
        self.assertEqual(frame_store.pack_run(self.path('run')), (0, 0))         # nothing to pack: no process
        a = bmp(500, 300)
        with open(self.path('run', 'frames', 'a.bmp'), 'wb') as f:
            f.write(a)
        before, after = frame_store.pack_run(self.path('run'))
        self.assertEqual(before, len(a))
        self.assertEqual(after, os.path.getsize(self.path('run', 'frames', 'a.bmp.z')))
        self.assertEqual(frame_store.read_frame(self.path('run', 'frames', 'a.bmp')), a)

    def test_compare_frames_reads_both_forms(self):
        os.makedirs(self.path('x', 'frames'))
        os.makedirs(self.path('y', 'frames'))
        for folder, data in (('x', bmp(500, 300)), ('y', bmp(500, 300))):
            with open(self.path(folder, 'frames', 'f.bmp'), 'wb') as f:
                f.write(data)
        frame_store.pack_folder(self.path('x'))          # x packed, y plain
        image = compare_frames.read_bmp(self.path('x', 'frames', 'f.bmp'))
        self.assertEqual((image.width, image.height), (500, 300))
        run = subprocess.run([sys.executable, os.path.join(os.path.dirname(__file__), 'compare_frames.py'),
                              self.path('x'), self.path('y')], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn('f.bmp: 500x300, 0 pixels differ', run.stdout)

    def test_run_packs_the_run_folder(self):
        data = bmp(500, 300)

        def fake_run_once(name, install, folder, root, build, *rest):
            os.makedirs(os.path.join(build, 'runs', folder, 'frames'))
            with open(os.path.join(build, 'runs', folder, 'frames', 'f.bmp'), 'wb') as f:
                f.write(data)
            return 'PASS', 1.0, [], {}, []
        real = run_all.run_once
        run_all.run_once = fake_run_once
        try:
            self.assertEqual(run_all.run('t', 'vanilla', build=self.tmp, root=self.tmp, fresh=True)[0], 'PASS')
        finally:
            run_all.run_once = real
        frames = os.path.join(self.tmp, 'runs', 't-vanilla', 'frames')
        self.assertEqual(os.listdir(frames), ['f.bmp.z'])
        self.assertEqual(frame_store.read_frame(os.path.join(frames, 'f.bmp')), data)


if __name__ == '__main__':
    unittest.main()
