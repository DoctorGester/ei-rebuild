"""Tests of resolve_crash.py (X40a) without games: a made-up ei_hook.dll and game.exe (a few functions with
calls between them), their map and function lists, and a crash log with the stack words of a throw plus stale
words around it.

usage: python tools/test_resolve_crash.py [-v]
The real check with a game is tests/scenarios/harness_disk_full.txt (run_all prints its stack when it crashes).
"""
import os, struct, sys, tempfile, unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import resolve_crash as rc
import run_all

GAME_BASE = 0x400000


class FakeImage:
    """rc.Image's interface over bytes: base and read(rva, n)."""

    def __init__(self, base, size=0x1000):
        self.base, self.code, self.data = base, bytearray([0xCC] * size), bytearray()

    def call(self, at, target):
        """E8 at address `at` to `target` (addresses in the image's own units); returns the return address."""
        i = at - self.base if self.base else at
        self.code[i] = 0xE8
        struct.pack_into('<i', self.code, i + 1, target - (at + 5))
        return at + 5

    def put(self, at, raw):
        i = at - self.base if self.base else at
        self.code[i:i + len(raw)] = raw
        return at + len(raw)

    def read(self, rva, n):
        if rva < 0 or rva + n > len(self.code):
            return None
        return bytes(self.code[rva:rva + n])


MAP = """ ei_hook

 Timestamp is 6ad15282 (Fri Oct 16 00:24:02 2026)

 Preferred load address is 10000000

 Start         Length     Name                   Class
 0001:00000000 00000900H .text$mn                CODE
 0002:00000000 00000100H .rdata                  DATA

  Address         Publics by Value              Rva+Base       Lib:Object

 0001:00000100       ?CxxThrowException@libcrt@@YGXPAXPBX@Z 10000100 f   lib.obj
 0001:00000200       ?Write@CFile@impl@mfc@@QAEXPBXI@Z 10000200 f   lib.obj
 0001:00000300       ?ComposeTexture@@YIHXZ     10000300 f   tex.obj
 0001:00000320       __ehhandler$?ComposeTexture@@YIHXZ 10000320 f   tex.obj
 0001:00000400       ?StaleHelper@@YAXXZ        10000400 f   tex.obj
 0001:00000500       ?Stale2@@YAXXZ             10000500 f   tex.obj
 0002:00000010       ??_C@_04ABCD@text@         10000810     tex.obj
"""

FUNCTIONS = """address,size_est,name,kind,status,source
00401000,256,FUN_00401000,game,original,
00401100,256,FUN_00401100,game,original,
00401200,256,entry,lib,original,
"""

COVERAGE = """# comment
address,name,file,size,wrapper,status,risk,scenarios,golden,vector,reached_by
00401000,CWorldClient::Dress,src/x.cpp,256,ours,golden,0,1,a,,
00401100,ComposeTexture,src/game/texture_compose.cpp,256,ours,golden,0,1,a,,
"""


def write(path, text):
    with open(path, 'w') as f:
        f.write(text)


def make_log(words):
    lines = ['[00:00:01.000] harness: the game crashed: exception E06D7363 at 759BB702: CFileException* (cause 13)',
             '[00:00:01.000] harness: 5 MB free on the run folder\'s disk',
             '[00:00:01.000]   in C:\\Windows\\System32\\KERNELBASE.dll+13B702 (thread 1)']
    for kind, a in words:
        lines.append('[00:00:01.000]   stack ' + (f'ei_hook+{a:X}' if kind == 'ei' else f'{a:08X} (game.exe)'))
    return '\n'.join(lines) + '\n'


class Fixture(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = self.tmp.name
        os.makedirs(os.path.join(self.root, 'config'))
        os.makedirs(os.path.join(self.root, 'tests', 'coverage'))
        write(os.path.join(self.root, 'config', 'functions.csv'), FUNCTIONS)
        write(os.path.join(self.root, 'tests', 'coverage', 'coverage.csv'), COVERAGE)
        self.map = os.path.join(self.root, 'ei_hook.map')
        write(self.map, MAP)

        ei = FakeImage(0)
        game = FakeImage(GAME_BASE, 0x1400)
        # ei: ComposeTexture (0x300) calls CFile::Write (0x200) which calls CxxThrowException (0x100)
        self.ra_throw = 0x100 + 0x3C
        ei.put(0x13C - 6, b'\xff\x15\x00\x00\x00\x10')                       # call [RaiseException's slot], inside the throw
        self.ra_write = ei.call(0x210, 0x100)
        self.ra_compose = ei.call(0x310, 0x200)
        # StaleHelper (0x400) calls Stale2 (0x500): an old chain, not part of the crash
        self.ra_stale = ei.call(0x410, 0x500)
        self.ra_stale2 = ei.call(0x510, 0x400)
        # game: Dress (0x401000) calls the hooked ComposeTexture address 0x401100; entry (0x401200) calls Dress
        self.ra_dress = game.call(0x401010, 0x401100)
        self.ra_entry = game.call(0x4012E0, 0x401000)
        self.images = {'ei': ei, 'game': game}
        self.words = [('ei', 0x300 + 0x20),                     # not a return address: the code before it is no call
                      ('ei', self.ra_write),                    # an unwinding leftover before the throw's own frame
                      ('ei', self.ra_throw), ('ei', self.ra_write), ('ei', self.ra_compose),
                      ('ei', 0x300),                            # a function pointer
                      ('game', self.ra_dress),
                      ('ei', self.ra_stale), ('ei', self.ra_stale2),    # a stale call chain between the frames and the root
                      ('game', self.ra_entry),
                      ('ei', self.ra_stale)]                    # beyond the thread's first function

    def tearDown(self):
        self.tmp.cleanup()

    def stack(self):
        return rc.Stack(self.map, self.images, self.root)

    def frames(self, words=None):
        lines = make_log(words or self.words).splitlines()
        return [(k, a) for _, k, a in rc.frames(rc.stack_words(lines, 0), self.stack())]


class Pieces(unittest.TestCase):
    def test_call_of(self):
        self.assertEqual(rc.call_of(bytes([0xCC] * 3) + bytes([0xE8, 1, 0, 0, 0]))[0], 'rel')
        self.assertEqual(rc.call_of(bytes([0xCC] * 2) + bytes([0xFF, 0x15, 0, 0, 0, 0]))[0], 'indirect')   # call [abs]
        self.assertEqual(rc.call_of(bytes([0xCC] * 6) + bytes([0xFF, 0xD0]))[0], 'indirect')               # call eax
        self.assertEqual(rc.call_of(bytes([0xCC] * 5) + bytes([0xFF, 0x50, 8, 0xCC][:3]))[0], 'indirect')  # call [eax+8]
        self.assertEqual(rc.call_of(bytes([0xFF, 0x54, 0x24, 8]) + bytes([0xCC] * 4))[0], None)            # ends before the return
        self.assertEqual(rc.call_of(bytes([0xCC] * 4) + bytes([0xFF, 0x54, 0x24, 8]))[0], 'indirect')      # call [esp+8]
        self.assertEqual(rc.call_of(bytes(8))[0], None)

    def test_short_name(self):
        self.assertEqual(rc.short_name('mfc::impl::CFile::Write'), 'CFile::Write')
        self.assertEqual(rc.short_name("verify::`anonymous namespace'::HeldClientSegment"), 'verify::HeldClientSegment')

    @unittest.skipUnless(sys.platform == 'win32', 'dbghelp')
    def test_undecorate(self):
        self.assertEqual(rc.short_name(rc.undecorate(['?Write@CFile@impl@mfc@@QAEXPBXI@Z'], True)[0]), 'CFile::Write')
        self.assertEqual(rc.undecorate(['FUN_00401000']), ['FUN_00401000'])

    def test_norm_name(self):
        self.assertEqual(rc.norm_name('CWorldClient::Segment'), rc.norm_name('CWorldClient::SegmentVerify'))
        self.assertNotEqual(rc.norm_name('A::B'), rc.norm_name('A::C'))


class Lists(Fixture):
    def test_map_code_only(self):
        names = [n for _, n in rc.load_map(self.map, code_only=True)]
        self.assertIn('?Write@CFile@impl@mfc@@QAEXPBXI@Z', names)
        self.assertFalse([n for n in names if n.startswith(('__ehhandler$', '??_C@'))])
        everything = [n for _, n in rc.load_map(self.map)]
        self.assertIn('??_C@_04ABCD@text@', everything)

    def test_game_names(self):
        starts, names = rc.load_game_functions(self.root)
        self.assertEqual(starts, [0x401000, 0x401100, 0x401200])
        self.assertEqual(names, ['CWorldClient::Dress', 'ComposeTexture', 'entry'])   # coverage.csv names beat FUN_


class Chain(Fixture):
    def test_chain_keeps_the_throws_frames_and_drops_stale_words(self):
        self.assertEqual(self.frames(), [('ei', self.ra_throw), ('ei', self.ra_write), ('ei', self.ra_compose),
                                         ('game', self.ra_dress), ('game', self.ra_entry)])

    def test_without_images_every_word_stays(self):
        self.images = {}
        self.assertEqual(len(self.frames()), 8)      # from the throw to the root; nothing can be judged

    def test_a_throw_free_crash_starts_at_its_address(self):
        words = [('ei', self.ra_write), ('ei', self.ra_compose), ('game', self.ra_dress)]   # the first is stale: Write called the throw
        lines = make_log(words).splitlines()
        lines.insert(3, '[00:00:01.000]   at ei_hook+210')
        got = [(k, a) for _, k, a in rc.frames(rc.stack_words(lines, 0), self.stack())]
        self.assertEqual(got, [('ei', 0x210), ('ei', self.ra_compose), ('game', self.ra_dress)])

    def test_named_stack_text(self):
        d = os.path.join(self.root, 'run-vanilla')
        os.makedirs(d)
        write(os.path.join(d, 'ei_hook.log'), make_log(self.words))
        # resolve() reads the dll and exe from files; with none beside, the words are named but not judged
        # (the throw rule still applies)
        text = rc.named_stack(d, build=self.root, game_exe=os.path.join(self.root, 'none.exe'), root=self.root)
        self.assertTrue(text.endswith('CxxThrowException'), text)
        self.assertIn('CFile::Write', text)
        self.assertEqual(rc.named_stack(d, build=self.root, game_exe=os.path.join(self.root, 'none.exe'), root=self.root,
                                        max_frames=2).count(' -> '), 2)

    def test_no_crash(self):
        d = os.path.join(self.root, 'run-vanilla')
        os.makedirs(d)
        write(os.path.join(d, 'ei_hook.log'), '[00:00:01.000] harness: fine\n')
        self.assertEqual(rc.resolve(d, build=self.root, root=self.root), ['no crash logged'])


class RunAll(Fixture):
    def test_note_under_a_crashed_run(self):
        d = os.path.join(self.root, 'runs', 'x-vanilla')
        os.makedirs(d)
        write(os.path.join(d, 'ei_hook.log'), make_log(self.words))
        note = run_all.crash_stack_note(d, self.root, ['ei_launch', '--exe', os.path.join(self.root, 'none.exe')], self.root)
        self.assertTrue(note.startswith('stack: ') and 'CFile::Write' in note, note)

    def test_note_when_nothing_resolves(self):
        d = os.path.join(self.root, 'runs', 'y-vanilla')
        os.makedirs(d)
        note = run_all.crash_stack_note(d, self.root, ['ei_launch'], self.root)
        self.assertTrue(note.startswith('stack: not resolved'), note)


class RealRun(unittest.TestCase):
    """The crash of tests/scenarios/harness_disk_full.txt, when a run of it is in build/verify/runs: the image's
    (-standalone; since S21c a -vanilla run is the original code, named from the function list)."""

    def test_disk_full_stack(self):
        d = os.path.join(rc.ROOT, 'build', 'verify', 'runs', 'harness_disk_full-standalone')
        if not all(os.path.exists(os.path.join(rc.ROOT, 'build', 'verify', f)) for f in ('ei_hook.map', 'ei_hook.dll'))                 or not os.path.exists(os.path.join(d, 'ei_hook.log')):
            self.skipTest('no verify build or run of harness_disk_full here')
        text = rc.named_stack(d)
        names = text.split(' -> ')
        self.assertEqual(names[-1], 'CxxThrowException', text)
        self.assertLess(names.index('ComposeTexture'), names.index('CFile::Write'), text)
        self.assertNotIn('__lseeki64', text)          # the stale words after the game's first function


if __name__ == '__main__':
    unittest.main(argv=[a for a in sys.argv if a != '-v'] + (['-v'] if '-v' in sys.argv else []))
