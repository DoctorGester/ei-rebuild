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


class RoundFourPatternTest(unittest.TestCase):
    def test_random_word_scaled_by_hand(self):
        name = 'random word scaled by hand (Range copy)'
        self.assertEqual(pattern_count(name, 'x = Next() * 0x1p-32f + low; y = r * 2.3283064e-10f; z = w * kTwoToMinus32;'), 3)
        self.assertEqual(pattern_count(name, 'x = g_random.Range(low, high); float tiny = 1e-10f; auto a = 0x1p-3f;'), 0)

    def test_float_helper_over_addresses(self):
        name = 'float helper over addresses (ConstPtr, out pointer)'
        self.assertEqual(pattern_count(name, 'void HeightPlus(const float* base, const float* add, float* out);'), 1)
        self.assertEqual(pattern_count(name, 'auto p = ConstPtr(2.0f); x = kFloatConstant<1>;'), 2)
        self.assertEqual(pattern_count(name, 'float Lerp(float a, float b, float t); void Copy(const float* src, int n);'), 0)

    def test_x87_store_residue(self):
        name = 'x87 store residue (volatile float, store and reload)'
        self.assertEqual(pattern_count(name, 'volatile float zero = 0.0f;'), 1)
        self.assertEqual(pattern_count(name, 'float stored = x; x = stored;'), 1)
        self.assertEqual(pattern_count(name, 'float copy = x; y = copy;'), 0)

    def test_pointer_parameters_and_namespaces(self):
        name = 'const string or vector pointer parameter'
        self.assertEqual(pattern_count(name, 'void Load(const std::string* path, int mode); void Add(int a, const std::vector<int>* list);'), 2)
        self.assertEqual(pattern_count(name, 'void Fill(std::vector<int>* out); void Name(const std::string& path);'), 0)
        self.assertEqual(pattern_count('empty namespace', 'namespace { }\nnamespace a {\n}\nnamespace { int x; }'), 2)

    def test_windows_names(self):
        count = lambda code: len(check_cleanup._WINDOWS_NAME.findall(code))
        self.assertEqual(count('HWND window; case WM_KEYUP: GetTickCount(); #include <windows.h>'), 4)
        self.assertEqual(count('int DispatchMessages(); void TranslateKey(); std::uint32_t TickCount();'), 0)

    def test_number_function_in_header(self):
        count = lambda code, rel: check_cleanup.count_number_functions_in_headers(code, rel)
        self.assertEqual(count('float MaxReach() { return 12.5f; }\nint Count() const { return 3; }\n', 'src/game/x.h'), 2)
        self.assertEqual(count('virtual int Stage() { return 0; }\nint Kind() const override { return 3; }\n', 'src/game/x.h'), 0)
        self.assertEqual(count('float MaxReach() { return 12.5f; }\n', 'src/game/x.cpp'), 0)

    def test_printf_like_function(self):
        rx = check_cleanup._PRINTF_LIKE
        self.assertEqual([m.group(1) for m in rx.finditer('int Error(Script* s, const char* format, ...);')], ['Error'])
        self.assertEqual([m.group(1) for m in rx.finditer('void FormatV(const char* fmt, va_list args);')], ['FormatV'])
        self.assertEqual(len(rx.findall('void Say(const std::string& text); void Many(int n, ...);')), 0)

    def test_conflicting_constants(self):
        saved = check_cleanup._constants
        try:
            code = 'constexpr int kNoWeapon = 0x10;\nstatic constexpr float kNoRange{1e38f};\n  constexpr int kNoLocal = 4;\nconstexpr float kMargin = 2.0f;\n'
            found = {m.group(1): check_cleanup._value(m.group(2) if m.group(2) is not None else m.group(3))
                     for m in check_cleanup._K_DEFINITION.finditer(code)}
            self.assertEqual(found, {'kNoWeapon': 16.0, 'kNoRange': 1e38, 'kMargin': 2.0})   # column 0 only
            check_cleanup._constants = {'kNoWeapon': {'a.cpp': 16.0, 'b.cpp': 7.0}, 'kMargin': {'a.cpp': 2.0, 'b.cpp': 1.0},
                                        'kNoRange': {'a.cpp': 1e38, 'b.cpp': 1e38}}
            self.assertEqual(check_cleanup.count_conflicting_constants('', 'a.cpp'), 1)    # kMargin is a tuning number
            self.assertEqual(check_cleanup.count_conflicting_constants('', 'c.cpp'), 0)
            self.assertEqual(check_cleanup._value('0x10u'), check_cleanup._value('16'))
            self.assertEqual(check_cleanup._value('1.0f'), check_cleanup._value('1'))
        finally:
            check_cleanup._constants = saved

    def test_renamed_flag_bit(self):
        saved = check_cleanup._flag_owners
        try:
            check_cleanup._flag_owners = {0x80000: {'src/game/map_object.h'}}
            code = 'constexpr std::uint32_t kFlagNearbyListed = 0x80000;\nconstexpr int kFlagSmall = 0x10;\n'
            self.assertEqual(check_cleanup.count_renamed_flag_bits(code, 'src/game/unit_ai.cpp'), 1)
            self.assertEqual(check_cleanup.count_renamed_flag_bits(code, 'src/game/map_object.h'), 0)   # the owner's own file
        finally:
            check_cleanup._flag_owners = saved

    def test_virtual_index(self):
        tree = {'a.h': 'struct B { virtual B* AsKid() { return nullptr; } virtual void Unused() {} virtual void Idle() {}\n'
                       '  virtual bool IsLonely() { return false; } virtual int Never() = 0; };\n'
                       'struct K : B { K* AsKid() override; bool IsLonely() override { return true; } };\n',
                'a.cpp': 'K* K::AsKid() { return this; }\nvoid Run(B* b) { b->Unused(); if (b->AsKid()) Idle(); }\n'}
        index = check_cleanup._index_virtuals(tree)
        self.assertEqual((index['AsKid']['overrides'], index['AsKid']['calls']), (1, 1))
        self.assertEqual((index['Unused']['empty'], index['Unused']['definitions'], index['Unused']['calls']), (1, 1, 1))
        self.assertEqual(index['Idle']['calls'], 1)             # a call from a member, no object in front
        self.assertEqual((index['IsLonely']['overrides'], index['IsLonely']['calls']), (1, 0))
        self.assertEqual(index['Never']['calls'], 0)

    def test_two_start_states(self):
        inits = {'Zz': {'count': 0.0, 'scale': 1.0, 'owner': 'nullptr'}}
        code = ('struct Zz { int count = 0; float scale = 1.0f; Zz* owner = nullptr; Zz(Zz* from) : owner(from) { count = 5; }\n'
                '  void Reset() { scale = 1.0f; count = 2; } };\n')
        self.assertEqual(check_cleanup.count_double_start_states(code, inits), 2)     # count in Zz() and in Reset()
        outside = 'Zz::Zz() : count(7) { scale = 3.0f; }\nvoid Zz::Reset() { scale = 1.0f; }\n'
        self.assertEqual(check_cleanup.count_double_start_states(outside, inits), 2)

    def test_member_through_its_global(self):
        singletons = {'WorldServer': {'g_serverWorld'}}
        code = 'void WorldServer::Loot() { g_serverWorld.items.Add(); auto x = g_clientWorld.terrain; }\nvoid Free() { g_serverWorld.Loot(); }\n'
        self.assertEqual(check_cleanup.count_members_through_global(code, singletons), 1)

    def test_copied_member_bodies(self):
        body = ('    unit->action.target = nullptr;\n    unit->action.timer = kStandTime * speed;\n'
                '    unit->PlaySteps(kStandSteps, speed, false);\n')
        tree = {'src/game/spell_effect.cpp': 'void A() {\n' + body + '}\n',
                'src/game/spell_effect_update.cpp': 'void B() {\n    int x = 1;\n' + body + '}\nvoid C() {\n' + body + '}\n',
                'src/game/spell_other.cpp': 'void D() {\n' + body + '}\n',
                'src/game/spell_effect.h': 'void E() {\n' + body + '}\n'}
        runs = check_cleanup._copied_runs(tree)
        self.assertEqual((runs['src/game/spell_effect.cpp'], runs['src/game/spell_effect_update.cpp']), (0, 2))
        self.assertEqual(runs['src/game/spell_other.cpp'], 0)      # another family


class CommentTest(unittest.TestCase):
    def test_banned_round_three(self):
        banned = lambda text: comments.count(text, 'src/game/x.cpp')['banned words']
        for phrase in ('installs in the vanilla launch only', 'is called through its address', 'their verify wrappers',
                       'the scalar deleting destructor', 'the result in EAX', 'SHR by CL', 'the new renderer draws'):
            self.assertEqual(banned(f'// {phrase}\n'), 1, phrase)
        self.assertEqual(banned('// the renderer draws; EAXAMPLE is no register\n'), 0)

    def test_banned_round_four(self):
        banned = lambda text: comments.count(text, 'src/game/x.cpp')['banned words']
        for phrase in ('hands the frame to the new renderer', 'the loop is inlined', 'the helper inlines RemoveAt', 'Names provisional',
                       'the first 0x20 bytes of the record', 'the Direct3D device draws', 'the old path projects', 'as game.exe does',
                       'patched, so it is called through its address', 'the sum is stored as a float'):
            self.assertEqual(banned(f'// {phrase}\n'), 1, phrase)
        self.assertEqual(banned('// The renderer draws; inline functions are fine; 0x20 is a size.\n'), 0)

    def test_never_read(self):
        count = lambda text: comments.count(text)['never-read comments']
        self.assertEqual(count('int zeroed = 0;   // set by each pass, never read\n'), 1)
        self.assertEqual(count('// nothing reads it, so it goes\n'), 1)
        self.assertEqual(count('std::uint32_t unreadValue = 0;   // saved, never read\n'), 0)    # a format record's field
        self.assertEqual(count('// Reads the header, then the rest.\n'), 0)

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
