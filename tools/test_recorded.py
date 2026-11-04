"""Tests of tools/recorded.py and tools/recorded_save.py on made-up recordings (no games).
usage: python tools/test_recorded.py [-v]
"""
import os, shutil, sys, tempfile, unittest
from contextlib import redirect_stdout
import io

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import recorded
import recorded_save

FIRST = '# Recorded by the harness, part 1:\nrecorded\nat 4 newgame\nat 99 event key down 0x74 0x3f 0\nat 100 quit\n'
SECOND = ('# Recorded by the harness, part 2:\nrecorded\n# prepare: recorded_save.py {previous} saveq\nwaitmenu\n'
          'load saveq\nwaitwindow 30 battle\norigin\nat 0 event move 504 456 c\nat 181 quit\n')
THIRD = SECOND.replace('part 2', 'part 3')


class RecordedTest(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp()
        self.run = os.path.join(self.root, 'run')
        os.makedirs(self.run)
        os.makedirs(os.path.join(self.root, 'tests', 'scenarios'))

    def tearDown(self):
        shutil.rmtree(self.root, ignore_errors=True)

    def write(self, name, text):
        with open(os.path.join(self.run, name), 'w', encoding='utf-8') as f:
            f.write(text)

    def scenario(self, name):
        with open(os.path.join(self.root, 'tests', 'scenarios', name + '.txt'), encoding='utf-8') as f:
            return f.read()

    def take(self, name='x'):
        with redirect_stdout(io.StringIO()) as out:
            recorded.take(self.run, name, root=self.root)
        return out.getvalue()

    def test_one_part_keeps_its_name(self):
        self.write('recorded.txt', FIRST)
        self.take()
        self.assertEqual(self.scenario('rec_x'), FIRST)

    def test_parts_are_numbered_and_name_the_part_before(self):
        self.write('recorded.txt', FIRST)
        self.write('recorded_2.txt', SECOND)
        self.write('recorded_3.txt', THIRD)
        self.take()
        self.assertEqual(self.scenario('rec_x_1'), FIRST)
        self.assertIn('# prepare: recorded_save.py rec_x_1 saveq', self.scenario('rec_x_2'))
        self.assertIn('# prepare: recorded_save.py rec_x_2 saveq', self.scenario('rec_x_3'))
        self.assertFalse(os.path.exists(os.path.join(self.root, 'tests', 'scenarios', 'rec_x.txt')))

    def test_existing_part_is_kept_without_force(self):
        self.write('recorded.txt', FIRST)
        self.write('recorded_2.txt', SECOND)
        self.take()
        with self.assertRaises(SystemExit):
            self.take()

    def test_save_chain_follows_the_prepare_lines(self):
        scenarios = os.path.join(self.root, 'tests', 'scenarios')
        for name, text in (('rec_x_1', FIRST), ('rec_x_2', SECOND.replace('{previous}', 'rec_x_1')),
                           ('rec_x_3', THIRD.replace('{previous}', 'rec_x_2'))):
            with open(os.path.join(scenarios, name + '.txt'), 'w', encoding='utf-8') as f:
                f.write(text)
        self.assertEqual(recorded_save.chain('rec_x_3', self.root), ['rec_x_3', 'rec_x_2', 'rec_x_1'])
        self.assertEqual(recorded_save.chain('rec_x_1', self.root), ['rec_x_1'])


if __name__ == '__main__':
    unittest.main()
