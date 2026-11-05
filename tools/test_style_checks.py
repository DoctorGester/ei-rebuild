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


class RoundThreePatternTest(unittest.TestCase):
    def test_hex_named_message_method(self):
        name = 'hex-named message method'
        self.assertEqual(pattern_count(name, 'void SendState02(); void SendObject0A(); void OnMessage37();'), 3)
        self.assertEqual(pattern_count(name, 'void SendWeather(); void OnMessage(); void OnDeath();'), 0)

    def test_copied_small_helper(self):
        name = 'copied small helper (IsZero, 1/255, server running)'
        self.assertEqual(pattern_count(name, 'bool IsZero(float value) { return value == 0.0f; }'), 1)
        self.assertEqual(pattern_count(name, 'float s = width * (1.0f / 255.0f); constexpr float k = 0.003921569f;'), 2)
        self.assertEqual(pattern_count(name, 'bool ServerRuns() { return IsLive(net::g_gameServer); }'), 1)
        self.assertEqual(pattern_count(name, 'float s = 1.0f / 2550.0f; bool ServerRuns(int x) { return x; }'), 0)

    def test_literal_function(self):
        name = 'function returning only a string literal'
        self.assertEqual(pattern_count(name, 'const char* Name() const { return "Zone"; }'), 1)
        self.assertEqual(pattern_count(name, 'const char* Name() const { return name.c_str(); }'), 0)

    def test_cursor_after_memcpy(self):
        name = 'cursor moved by hand after memcpy'
        self.assertEqual(pattern_count(name, 'std::memcpy(p, &v, sizeof v); p += sizeof v;'), 1)
        self.assertEqual(pattern_count(name, 'std::memcpy(&v, p, 4); other = 1;'), 0)

    def test_pointer_to_member(self):
        name = 'pointer to member function'
        self.assertEqual(pattern_count(name, 'using Handler = bool (Connection::*)(Message*);'), 1)
        self.assertEqual(pattern_count(name, 'bool (*callback)(Message*);'), 0)

    def test_scope_block_around_temporary(self):
        block = '    open();\n    {\n        std::string path = folder + "a.res";\n        opened = resource.Open(path);\n    }\n'
        self.assertEqual(check_cleanup.count_temporary_blocks(block), 1)
        raii = '    open();\n    {\n        Lock lock;\n        opened = resource.Open(path);\n    }\n'
        self.assertEqual(check_cleanup.count_temporary_blocks(raii), 0)
        branch = '    if (ok)\n    {\n        std::string path = folder;\n        Use(path);\n    }\n'
        self.assertEqual(check_cleanup.count_temporary_blocks(branch), 0)

    def test_enum_never_used_as_a_type(self):
        self.assertEqual(check_cleanup.count_unused_enums('enum class ZzNoSuchKind { A, B };'), 1)
        self.assertEqual(check_cleanup.count_unused_enums('enum class ClassId : std::uint8_t { A, B };'), 0)


class CommentTest(unittest.TestCase):
    def test_banned_round_three(self):
        banned = lambda text: comments.count(text, 'src/game/x.cpp')['banned words']
        for phrase in ('installs in the vanilla launch only', 'is called through its address', 'their verify wrappers',
                       'the scalar deleting destructor', 'the result in EAX', 'SHR by CL', 'the new renderer draws'):
            self.assertEqual(banned(f'// {phrase}\n'), 1, phrase)
        self.assertEqual(banned('// the renderer draws; EAXAMPLE is no register\n'), 0)

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
