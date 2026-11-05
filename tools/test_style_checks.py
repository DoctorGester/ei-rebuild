"""Tests of the style checks of tools/comments.py and tools/check_cleanup.py (no game files needed):
python tools/test_style_checks.py"""
import os, sys, unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_cleanup, comments


def pattern_count(name, code):
    return len(check_cleanup.PATTERNS[name].findall(code))


class CleanupPatternTest(unittest.TestCase):
    def test_define_number(self):
        name = '#define EI_ number'
        self.assertEqual(pattern_count(name, '#define EI_ASSET_CODEPAGE 1251\n'), 1)
        self.assertEqual(pattern_count(name, '#define EI_VERSION_TEXT "1.6"\n#define EI_LIMIT (3)\n'), 2)
        self.assertEqual(pattern_count(name, '#define EI_RETURN_ADDRESS() _ReturnAddress()\n'), 0)
        self.assertEqual(pattern_count(name, '#define EI_REBIRTH_PATCH(name) name,\n#define EI_GUARD\n'), 0)

    def test_value_named_constant(self):
        name = 'value-named constant'
        self.assertEqual(pattern_count(name, 'constexpr float kOne = 1.0f;\nconst volatile float kZero = 0.0f;\n'), 2)
        self.assertEqual(pattern_count(name, 'constexpr float kRateBase{1.0f};\nx = kOne;\n'), 0)
        self.assertEqual(pattern_count(name, 'constexpr int kOnesCount = 4;\nconstexpr int kPlayerOne = 1;\n'), 0)


class CommentTest(unittest.TestCase):
    def test_addon_address(self):
        count = lambda text: comments.count(text)['addon addresses']
        self.assertEqual(count("// addon's 10025C9D: a '\\' added to a path\n"), 1)
        self.assertEqual(count('// held while it calls the loader (100BD5F8)\n'), 1)
        self.assertEqual(count('// addon.dll v0.12.4.2 (image base 10000000)\n'), 0)   # digits only: a number
        self.assertEqual(count('// chunk BBAC0007 of a map\n'), 0)                       # a format's own id stays
        self.assertEqual(count('int x = 0x10025C9D;\n'), 0)                              # code is not counted

    def test_unnamed_rebirth_patch(self):
        count = lambda text: comments.count(text)['unnamed Rebirth patches']
        self.assertEqual(count('// Rebirth-patched. Builds the page.\n'), 1)
        self.assertEqual(count('// Rebirth-patched (WideScreen). Builds the page.\n'), 1)
        self.assertEqual(count('// Rebirth-patched (Patch::CameraPatch). Turns the view.\n'), 0)
        self.assertEqual(count('// Rebirth-patched (CameraPatch).\n'), 0)
        self.assertEqual(count('// Rebirth-patched; always on.\n'), 0)

    def test_shaders_are_scanned(self):
        paths = [os.path.basename(p) for p in comments.sources([])]
        self.assertIn('blit.hlsl', paths)
        self.assertIn('bindings.hlsli', paths)


if __name__ == '__main__':
    unittest.main()
