"""Tests of tools/layout.py's mover on a small tree of its own (a git repository in a temporary folder; no game files
needed): python tools/test_layout.py"""
import contextlib
import io
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import layout

MAP = """batch B1: a
batch B2: e
a_x.h       a/a_x.h
a_x.cpp     a/a_x.cpp
b_y.h       a/b_y.h
c_fold.h    a/a_x.h      fold  # one constant
d_old.cpp   a/d_new.cpp        # named for what it holds
e_z.cpp     e/e_z.cpp
"""
FILES = {
    'src/game/a_x.h': '#pragma once\n#include "c_fold.h"\nint X();\n',
    'src/game/a_x.cpp': '#include "a_x.h"\n#include "b_y.h"\n#include "../platform/p.h"\nint X() { return kOne; }\n',
    'src/game/b_y.h': '#pragma once\n#include "a_x.h"\n',
    'src/game/c_fold.h': '#pragma once\nconstexpr int kOne = 1;\n',
    'src/game/d_old.cpp': '// d_old.cpp: see a_x.h\n#include "a_x.h"\n',
    'src/game/e_z.cpp': '#include "a_x.h"\n#include <vector>\n#include "miniz.h"\n',
    'src/platform/p.h': '#pragma once\n',
    'tests/unit/t.cpp': '#include "../../src/game/a_x.h"\n#include "local.h"\n',
    'tests/unit/local.h': '#pragma once\n',
    'docs/notes/n.md': 'X is in src/game/a_x.cpp; d_old.cpp and c_fold.h are gone.\n',
    'config/cleanup_baseline.csv': 'file,one,two\nsrc/game/a_x.h,1,0\nsrc/game/c_fold.h,2,3\nsrc/game/e_z.cpp,0,1\n',
    'tools/check.py': "A = ('src/game/d_old.cpp', 'd_old.cpp') + layout.game_paths('d_old.cpp', 'c_fold',\n"
                      "                                                       'e_z.cpp')\n",
    'CMakeLists.txt': 'add_executable(t src/game/a_x.cpp src/game/d_old.cpp\n    src/game/a_x.cpp)\n',
}


class MoverTest(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp()
        for rel, text in dict(FILES, **{'tools/layout_map.txt': MAP}).items():
            path = os.path.join(self.root, rel)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, 'w', encoding='utf-8', newline='\n') as f:
                f.write(text)
        for args in (['init', '-q'], ['add', '-A'], ['-c', 'user.name=t', '-c', 'user.email=t@t', 'commit', '-qm', 'x']):
            subprocess.run(['git', *args], cwd=self.root, check=True)
        self.saved = layout.ROOT, layout.GAME, layout.ENGINE, layout.MAP, layout.DOC
        layout.ROOT = self.root
        layout.GAME = os.path.join(self.root, 'src', 'game')
        layout.ENGINE = os.path.join(self.root, 'src', 'engine')
        layout.MAP = os.path.join(self.root, 'tools', 'layout_map.txt')
        layout.DOC = os.path.join(self.root, 'docs', 'notes', 'file-layout.md')

    def tearDown(self):
        layout.ROOT, layout.GAME, layout.ENGINE, layout.MAP, layout.DOC = self.saved
        shutil.rmtree(self.root, onerror=lambda f, p, e: (os.chmod(p, 0o700), f(p)))

    def read(self, rel):
        with open(os.path.join(self.root, rel), encoding='utf-8') as f:
            return f.read()

    def run_tool(self, *args):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = layout.main(['layout.py', *args])
        return code, out.getvalue()

    def test_root_then_move_then_idempotent(self):
        self.assertEqual(self.run_tool('rewrite')[0], 0)
        self.assertEqual(self.read('src/game/a_x.cpp'), '#include "game/a_x.h"\n#include "game/b_y.h"\n'
                         '#include "platform/p.h"\nint X() { return kOne; }\n')
        self.assertIn('#include "miniz.h"', self.read('src/game/e_z.cpp'))       # not a game file: left
        self.assertEqual(self.read('tests/unit/t.cpp'), '#include "game/a_x.h"\n#include "local.h"\n')

        code, out = self.run_tool('apply', 'B1', '--dry-run')
        self.assertEqual(code, 0, out)
        self.assertIn('4 moves, 1 folds', out)
        self.assertTrue(os.path.exists(os.path.join(self.root, 'src/game/a_x.h')))   # a dry run changes nothing

        code, out = self.run_tool('apply', 'B1')
        self.assertEqual(code, 0, out)
        self.assertFalse(os.path.exists(os.path.join(self.root, 'src/game/c_fold.h')))
        # The fold took the owner's include of it.
        self.assertEqual(self.read('src/game/a/a_x.h'), '#pragma once\nconstexpr int kOne = 1;\nint X();\n')
        self.assertEqual(self.read('src/game/a/d_new.cpp'), '// a/d_new.cpp: see a_x.h\n#include "game/a/a_x.h"\n')
        self.assertEqual(self.read('src/game/e_z.cpp').splitlines()[0], '#include "game/a/a_x.h"')
        self.assertEqual(self.read('tests/unit/t.cpp').splitlines()[0], '#include "game/a/a_x.h"')
        self.assertEqual(self.read('docs/notes/n.md'),
                         'X is in src/game/a/a_x.cpp; a/d_new.cpp and a/a_x.h are gone.\n')
        self.assertEqual(self.read('config/cleanup_baseline.csv'),
                         'file,one,two\nsrc/game/a/a_x.h,3,3\nsrc/game/e_z.cpp,0,1\n')
        self.assertEqual(self.read('CMakeLists.txt'), 'add_executable(t src/game/a/a_x.cpp src/game/a/d_new.cpp\n)\n')
        self.assertEqual(self.run_tool('check')[0], 0)

        before = {rel: self.read(rel) for rel in ('src/game/a/a_x.cpp', 'docs/notes/n.md', 'CMakeLists.txt',
                                                  'config/cleanup_baseline.csv')}
        code, out = self.run_tool('rewrite')
        self.assertIn('nothing in 0 files', out)
        self.assertEqual(before, {rel: self.read(rel) for rel in before})
        code, out = self.run_tool('apply', 'B1')
        self.assertIn('0 moves, 0 folds, 5 done before', out)

    def test_rewrite_leaves_game_paths_arguments(self):
        """layout.game_paths('d_old.cpp') names a flat file the map resolves: rewrite changes the names around the
        call and not the call's arguments."""
        self.run_tool('apply', 'B1')
        expected = ("A = ('src/game/a/d_new.cpp', 'a/d_new.cpp') + layout.game_paths('d_old.cpp', 'c_fold',\n"
                    "                                                       'e_z.cpp')\n")
        self.assertEqual(self.read('tools/check.py'), expected)
        self.run_tool('rewrite')
        self.assertEqual(self.read('tools/check.py'), expected)

    def test_stale_branch_rewrite(self):
        """A branch made before the moves (bare and flat rooted includes) merged after them: rewrite moves its
        includes to the files' paths now."""
        self.run_tool('apply', 'B1')
        with open(os.path.join(self.root, 'src/game/e_z.cpp'), 'w', encoding='utf-8', newline='\n') as f:
            f.write('#include "b_y.h"\n#include "game/c_fold.h"\n// src/game/d_old.cpp\n')
        self.run_tool('rewrite')
        self.assertEqual(self.read('src/game/e_z.cpp'),
                         '#include "game/a/b_y.h"\n#include "game/a/a_x.h"\n// src/game/a/d_new.cpp\n')

    def test_check_finds_half_moves(self):
        os.makedirs(os.path.join(self.root, 'src/game/a'))
        shutil.copy(os.path.join(self.root, 'src/game/b_y.h'), os.path.join(self.root, 'src/game/a/b_y.h'))
        os.remove(os.path.join(self.root, 'src/game/d_old.cpp'))
        code, out = self.run_tool('check')
        self.assertEqual(code, 1)
        self.assertIn('b_y.h is both flat and at a/b_y.h', out)
        self.assertIn('d_old.cpp is neither in src/game nor at a/d_new.cpp', out)
        self.assertEqual(self.run_tool('apply', 'B1')[0], 1)                   # refuses on a bad map

    def test_fold_made_early(self):
        """A split batch folds a file into its owner while both are flat: check accepts it, the rewrite names the
        owner's flat file, the owner's move batch counts the fold done."""
        with open(os.path.join(self.root, 'src/game/a_x.h'), 'a', encoding='utf-8', newline='\n') as f:
            f.write('constexpr int kOne = 1;\n')
        os.remove(os.path.join(self.root, 'src/game/c_fold.h'))
        code, out = self.run_tool('check')
        self.assertEqual(code, 0, out)
        self.assertEqual(layout.moved_map()['c_fold.h'], 'a_x.h')
        code, out = self.run_tool('apply', 'B1')
        self.assertEqual(code, 0, out)
        self.assertIn('4 moves, 0 folds, 1 done before', out)
        self.assertEqual(self.read('docs/notes/n.md'), 'X is in src/game/a/a_x.cpp; a/d_new.cpp and a/a_x.h are gone.\n')

    def test_game_paths_and_flat_name(self):
        self.run_tool('apply', 'B1')
        layout._now = layout._flat = None
        try:
            self.assertEqual(layout.game_paths('d_old', 'e_'), ('src/game/a/d_new.cpp', 'src/game/e_z.cpp'))
            self.assertEqual(layout.game_paths('c_fold'), ('src/game/a/a_x.h',))
            self.assertEqual(layout.flat_name('src/game/a/d_new.cpp'), 'd_old.cpp')
            self.assertEqual(layout.flat_name('src/game/e_z.cpp'), 'e_z.cpp')
        finally:
            layout._now = layout._flat = None


if __name__ == '__main__':
    unittest.main()
