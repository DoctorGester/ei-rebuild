"""Tests of tools/direct_calls.py on made-up sources and map declarations (no build).
usage: python tools/test_direct_calls.py [-v]
"""
import os, sys, unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import direct_calls as dc

HEADER = '''#pragma once
struct Foo {
    int Bar(int x);
    // 00401000. The old way in.
    int BarOld(int x);
    void Baz() const;
};
'''
DEFINITION = '''#include "foo.h"
int Foo::Bar(int x) { return x; }
EI_REPLACE_METHOD(0x00401000, Foo::Bar);
void Foo::Baz() const {}
EI_REPLACE_METHOD(0x00401020, Foo::Baz);
int Foo::Qux(int x) { return BarOld(x); }
__declspec(naked) int Foo::BarOld(int) EI_ORIGINAL(0x00401000)
'''
USER = '''#include "ei/hook.h"

namespace {
// 00401000 through its address, so the verify wrapper sees the calls.
__declspec(naked) int __fastcall BarAt(Foo*, void*, int) EI_ORIGINAL(0x00401000)
__declspec(naked) void __fastcall BazAt(const Foo*) EI_ORIGINAL(0x00401020)
}

int User::Run(Foo* p, Foo* q, Foo& foo) {
    BazAt(p);
    int a = BarAt(&foo, nullptr, 1) + BarAt(p, nullptr, BarAt(q, nullptr, 2));
    return a + BarAt(Pick(p, q), nullptr,
                     3);
}
'''
MAP = {
    'Foo::Bar': [dc.Sig('public: int __thiscall Foo::Bar(int)', 'foo.cpp.obj')],
    'Foo::BarOld': [dc.Sig('public: int __thiscall Foo::BarOld(int)', 'foo.cpp.obj')],
    'Foo::Baz': [dc.Sig('public: void __thiscall Foo::Baz(void)const ', 'foo.cpp.obj')],
    'BarAt': [dc.Sig("int __fastcall `anonymous namespace'::BarAt(struct Foo *,void *,int)", 'user.cpp.obj')],
    'BazAt': [dc.Sig("void __fastcall `anonymous namespace'::BazAt(struct Foo const *)", 'user.cpp.obj')],
}


def run(sources, decls=MAP, tiers=None):
    dc._FREE_DECLS['all'] = sources
    dc._HEADERS.clear()
    dc._SEES.clear()
    fwds, classes = dc.analyse(sources, decls, tiers=tiers)
    edits, includes, done = dc.plan(fwds, classes)
    deleted = {}
    for f in done:
        deleted.setdefault(f.src.path, set()).add(f.address)
        for s, _, _ in f.extra:
            deleted.setdefault(s.path, set()).add(f.address)
    return fwds, dc.rewritten(edits, includes, deleted)


class DirectCallsTest(unittest.TestCase):
    def test_signature(self):
        s = dc.Sig('public: bool __thiscall CAIMap::ApplyBridgeDecks(struct FootprintCellSet *,bool)', 'x')
        self.assertTrue(s.member)
        self.assertEqual((s.scope, s.simple, s.params, s.ret), ('CAIMap', 'ApplyBridgeDecks', ['FootprintCellSet*', 'bool'], 'bool'))
        c = dc.Sig('public: void __thiscall Foo::Baz(void)const ', 'x')
        self.assertTrue(c.ok and c.const and c.params == [])
        t = dc.Sig('class A<struct B> * __fastcall ns::Make(class A<struct B> *,void *,char const *)', 'x')
        self.assertEqual((t.qname, t.params), ('ns::Make', ['A<B>*', 'void*', 'char const*']))

    def test_match(self):
        member = dc.Sig('public: int __thiscall Foo::Bar(int)', 'x')
        self.assertEqual(dc.match(dc.Sig('int __fastcall F(struct Foo *,void *,int)', 'x'), member), 'this')
        self.assertEqual(dc.match(dc.Sig('void __fastcall F(struct Foo *,void *,int)', 'x'), member), 'this')
        self.assertEqual(dc.match(dc.Sig('int __fastcall F(void *,void *,int)', 'x'), member), 'cast')
        self.assertIsNone(dc.match(dc.Sig('int __fastcall F(class Other *,void *,int)', 'x'), member))
        self.assertIsNone(dc.match(dc.Sig('int __fastcall F(struct Foo *,void *,unsigned int)', 'x'), member))
        self.assertIsNone(dc.match(dc.Sig('char __fastcall F(struct Foo *,void *,int)', 'x'), member))
        free = dc.Sig('void __stdcall G(struct Foo const *,int)', 'x')
        self.assertEqual(dc.match(dc.Sig('void __stdcall H(struct Foo *,int)', 'x'), free), 'rename')
        self.assertIsNone(dc.match(dc.Sig('void __cdecl H(struct Foo *,int)', 'x'), free))

    def test_postfix(self):
        for e in ('p', 'a.b', 'a->b[i]', 'Database().units', 'static_cast<Foo*>(p)', 'x.y->z(1, 2)'):
            self.assertTrue(dc.is_postfix(e), e)
        for e in ('*p', 'a + b', 'flag ? a : b', '(Foo*)p', 'p++'):
            self.assertFalse(dc.is_postfix(e), e)

    def test_rewrite(self):
        sources = [dc.Source('src/game/foo.h', HEADER), dc.Source('src/game/foo.cpp', DEFINITION),
                   dc.Source('src/game/user.cpp', USER)]
        fwds, out = run(sources)
        self.assertEqual({f.name: f.reason for f in fwds}, {'BarAt': '', 'BazAt': '', 'Foo::BarOld': ''})
        user = out['src/game/user.cpp']
        self.assertIn('#include "ei/hook.h"\n#include "foo.h"\n', user)
        self.assertNotIn('EI_ORIGINAL', user)
        self.assertNotIn('verify wrapper', user)
        self.assertIn('    p->Baz();\n', user)
        self.assertIn('int a = foo.Bar(1) + p->Bar(q->Bar(2));', user)
        self.assertIn('return a + Pick(p, q)->Bar(3);', user)
        self.assertNotIn('BarOld', out['src/game/foo.h'])
        self.assertNotIn('The old way in', out['src/game/foo.h'])
        self.assertIn('return Bar(x);', out['src/game/foo.cpp'])

    def test_cast(self):
        # An object passed as void*: through static_cast where a call needs it (the compiler's errors),
        # plainly where Foo is the only class with a Bar.
        user = USER.replace('Foo*, void*, int) EI_ORIGINAL', 'void*, void*, int) EI_ORIGINAL')
        decls = dict(MAP, BarAt=[dc.Sig("int __fastcall `anonymous namespace'::BarAt(void *,void *,int)", 'user.cpp.obj')])
        sources = [dc.Source('src/game/foo.h', HEADER), dc.Source('src/game/foo.cpp', DEFINITION),
                   dc.Source('src/game/user.cpp', user)]
        fwds, _ = run(sources, decls)
        self.assertEqual({f.name: f.reason for f in fwds}['BarAt'], 'object passed as void* (--cast)')
        dc.CAST_SITES.add(('src/game/user.cpp', 12))
        try:
            fwds, out = run(sources, decls, {'cast': True})
        finally:
            dc.CAST_SITES.clear()
        self.assertIn('int a = foo.Bar(1) + p->Bar(q->Bar(2));', out['src/game/user.cpp'])
        self.assertIn('return a + static_cast<Foo*>(Pick(p, q))->Bar(3);', out['src/game/user.cpp'])

    def test_after_includes(self):
        # A forwarder right after the #include lines: only its own line goes.
        user = ('#include "ei/hook.h"\n#include "other.h"\n'
                'static __declspec(naked) int __fastcall BarAt(Foo*, void*, int) EI_ORIGINAL(0x00401000)\n\n'
                'int User::Run(Foo* p) {\n    return BarAt(p, nullptr, 1);\n}\n')
        decls = dict(MAP, BarAt=[dc.Sig('int __fastcall BarAt(struct Foo *,void *,int)', 'user.cpp.obj')])
        sources = [dc.Source('src/game/foo.h', HEADER), dc.Source('src/game/foo.cpp', DEFINITION),
                   dc.Source('src/game/user.cpp', user)]
        _, out = run(sources, decls)
        self.assertEqual(out['src/game/user.cpp'], '#include "ei/hook.h"\n#include "foo.h"\n#include "other.h"\n\n'
                         'int User::Run(Foo* p) {\n    return p->Bar(1);\n}\n')

    def test_other_scope(self):
        # std::BarAt is another function: only the forwarder's own calls change.
        user = USER.replace('BazAt(p);\n', 'BazAt(p);\n    std::BarAt(p, nullptr, 0);\n')
        sources = [dc.Source('src/game/foo.h', HEADER), dc.Source('src/game/foo.cpp', DEFINITION),
                   dc.Source('src/game/user.cpp', user)]
        _, out = run(sources)
        self.assertIn('    std::BarAt(p, nullptr, 0);\n', out['src/game/user.cpp'])
        self.assertIn('int a = foo.Bar(1) + p->Bar(q->Bar(2));', out['src/game/user.cpp'])

    def test_namespace(self):
        # crt::LenAt's own calls change; an unqualified LenAt outside namespace crt is another function.
        lib = '#include "ei/hook.h"\nint __cdecl Len(const char* s) { return 0; }\nEI_REPLACE(0x00401040, Len);\n'
        user = ('#include "ei/hook.h"\nnamespace crt {\n'
                '__declspec(naked) int __cdecl LenAt(const char*) EI_ORIGINAL(0x00401040)\n}\n'
                'int User::Run(const char* p) {\n    return crt::LenAt(p) + LenAt(p);\n}\n')
        decls = {'Len': [dc.Sig('int __cdecl Len(char const *)', 'lib.cpp.obj')],
                 'crt::LenAt': [dc.Sig('int __cdecl crt::LenAt(char const *)', 'user.cpp.obj')]}
        sources = [dc.Source('src/game/lib.h', '#pragma once\nint __cdecl Len(const char* s);\n'),
                   dc.Source('src/game/lib.cpp', '#include "lib.h"\n' + lib), dc.Source('src/game/user.cpp', user)]
        _, out = run(sources, decls)
        self.assertIn('return Len(p) + LenAt(p);', out['src/game/user.cpp'])

    def test_defined_later(self):
        # A free target defined after the call in the same file: its declaration takes the forwarder's place.
        src = ('#include "ei/hook.h"\nnamespace {\n'
               '__declspec(naked) int __cdecl LenAt(const char*) EI_ORIGINAL(0x00401040)\n}\n'
               'int Run(const char* p) {\n    return LenAt(p);\n}\n'
               'namespace {\nint __cdecl Len(const char* s) { return 0; }\n}\nEI_REPLACE(0x00401040, Len);\n')
        decls = {'Len': [dc.Sig("int __cdecl `anonymous namespace'::Len(char const *)", 'lib.cpp.obj')],
                 'LenAt': [dc.Sig("int __cdecl `anonymous namespace'::LenAt(char const *)",
                                                         'lib.cpp.obj')]}
        _, out = run([dc.Source('src/game/lib.cpp', src)], decls)
        self.assertIn('namespace {\nint __cdecl Len(const char*);\n}\n', out['src/game/lib.cpp'])
        self.assertIn('return Len(p);', out['src/game/lib.cpp'])

    def test_reindent(self):
        self.assertEqual(dc.reindent('a,\n       b,\n   c)', 7, -3), 'a,\n    b,\n   c)')

    def test_loose_match(self):
        classes = dc.Classes([dc.Source('src/game/c.h', 'struct Base {};\nstruct Derived : Base {};\n')])
        member = dc.Sig('public: int __thiscall Derived::Bar(unsigned int,struct Base *,char)', 'x')
        kind, conv = dc.match_loose(dc.Sig('int __fastcall F(void *,void *,int,void *,int)', 'x'), member, classes)
        self.assertEqual(kind, 'loose')
        self.assertEqual(conv['self'], 'cast')
        self.assertEqual(conv['args'], [None, ('cast', 'Base*'), ('cast', 'char')])
        _, conv = dc.match_loose(dc.Sig('int __fastcall F(struct Base *,void *,int,struct Derived *,char)', 'x'),
                                 member, classes)
        self.assertEqual((conv['self'], conv['args']), ('cast', [None, None, None]))
        # A float's bits, a bool, unrelated classes and a result read as another class stay forwarders.
        for fwd in ('int __fastcall F(void *,void *,float,void *,char)', 'bool __fastcall F(void *,void *,int,void *,char)',
                    'int __fastcall F(struct Other *,void *,int,void *,char)',
                    'int __fastcall F(void *,void *,int,struct Other *,char)'):
            self.assertIsNone(dc.match_loose(dc.Sig(fwd, 'x'), member, classes)[0], fwd)
        free = dc.Sig('struct Derived * __stdcall G(struct Vector3 const &)', 'x')
        kind, conv = dc.match_loose(dc.Sig('struct Base * __fastcall H(struct Vector3 const *)', 'x'), free, classes)
        self.assertEqual((kind, conv['args'], conv['ret']), ('loose', [('deref',)], None))
        _, conv = dc.match_loose(dc.Sig('struct Derived * __fastcall H(struct Vector3 const *)', 'x'),
                                 dc.Sig('struct Base * __stdcall G(struct Vector3 const &)', 'x'), classes)
        self.assertEqual(conv['ret'], ('cast', 'Derived*'))

    def test_loose_rewrite(self):
        header = '#pragma once\nstruct Foo {\n    int Bar(unsigned int n, char c);\n};\n'
        definition = ('#include "foo.h"\nint Foo::Bar(unsigned int n, char c) { return n + c; }\n'
                      'EI_REPLACE_METHOD(0x00401000, Foo::Bar);\n')
        user = ('#include "ei/hook.h"\nnamespace {\n'
                '__declspec(naked) int __fastcall BarAt(void*, void*, int, int) EI_ORIGINAL(0x00401000)\n}\n'
                'int User::Run(Foo* p, int n) {\n    BarAt((void*)p, nullptr, n, n);\n    return BarAt(p, nullptr, 1, n);\n}\n')
        decls = {'Foo::Bar': [dc.Sig('public: int __thiscall Foo::Bar(unsigned int,char)', 'foo.cpp.obj')],
                 'BarAt': [dc.Sig("int __fastcall `anonymous namespace'::BarAt(void *,void *,int,int)", 'user.cpp.obj')]}
        sources = [dc.Source('src/game/foo.h', header), dc.Source('src/game/foo.cpp', definition),
                   dc.Source('src/game/user.cpp', user)]
        fwds, _ = run(sources, decls, {'cast': True})
        self.assertEqual({f.name: f.reason for f in fwds}['BarAt'], 'types differ')
        fwds, out = run(sources, decls, {'cast': True, 'loose': True})
        self.assertIn('    p->Bar(n, static_cast<char>(n));\n    return p->Bar(1, static_cast<char>(n));\n',
                      out['src/game/user.cpp'])

    def test_kept(self):
        # A name used other than in a call, or a recorder's address, keeps the forwarder.
        sources = [dc.Source('src/game/foo.h', HEADER), dc.Source('src/game/foo.cpp', DEFINITION),
                   dc.Source('src/game/user.cpp', USER + 'auto* fn = &BarAt;\n'),
                   dc.Source('tests/vectors/record_foo.cpp', 'EI_RECORD(0x00401020, Rec, 6, orig);\n')]
        fwds, out = run(sources)
        reasons = {f.name: f.reason for f in fwds}
        self.assertIn('BarAt', reasons['BarAt'])
        self.assertEqual(reasons['BazAt'], 'vector recorder (EI_RECORD)')
        self.assertIn('BarAt(&foo, nullptr, 1)', out.get('src/game/user.cpp', USER))

    ABI_HEADER = '''#pragma once
namespace stlp {
template <int N> struct Opaque { char bytes[N]; };
template <typename T> struct Vec {
    void Grow(int n, const T& x);
    template <typename E> void Fill(int n);
};
template <typename T, typename E = int>
struct VecAbi {
    using Vector = Vec<T>;
    static void __fastcall Grow(Vector* self, void*, int n, const T* x) { self->Grow(n, *x); }
    static void __fastcall Fill(Vector* self, void*, int n) { self->template Fill<E>(n); }
    static int __fastcall Count(Vector* self) { return self->size + 0; }
};
} // namespace stlp
'''
    ABI_REGISTRATIONS = ('#include "abi.h"\nnamespace {\nusing Dwords = stlp::VecAbi<unsigned int>;\n'
                         'using Erased = stlp::VecAbi<stlp::Opaque<4>>;\n}\n'
                         'EI_REPLACE(0x00401100, Dwords::Grow);\nEI_REPLACE(0x00401140, Dwords::Fill);\n'
                         'EI_REPLACE(0x00401180, Erased::Grow);\nEI_REPLACE(0x004011C0, Dwords::Count);\n')
    ABI_MAP = {
        'stlp::VecAbi<unsigned int,int>::Grow': [dc.Sig(
            'public: static void __fastcall stlp::VecAbi<unsigned int,int>::Grow(struct stlp::Vec<unsigned int> *,void *,'
            'int,unsigned int const *)', 'abi.cpp.obj')],
        'stlp::VecAbi<unsigned int,int>::Fill': [dc.Sig(
            'public: static void __fastcall stlp::VecAbi<unsigned int,int>::Fill(struct stlp::Vec<unsigned int> *,void *,'
            'int)', 'abi.cpp.obj')],
        'stlp::VecAbi<stlp::Opaque<4>,int>::Grow': [dc.Sig(
            'public: static void __fastcall stlp::VecAbi<struct stlp::Opaque<4>,int>::Grow(struct stlp::Vec<struct '
            'stlp::Opaque<4> > *,void *,int,struct stlp::Opaque<4> const *)', 'abi.cpp.obj')],
        'stlp::VecAbi<unsigned int,int>::Count': [dc.Sig(
            'public: static int __fastcall stlp::VecAbi<unsigned int,int>::Count(struct stlp::Vec<unsigned int> *)',
            'abi.cpp.obj')],
        'GrowAt': [dc.Sig("void __fastcall `anonymous namespace'::GrowAt(struct stlp::Vec<unsigned int> *,void *,int,"
                          "unsigned int const *)", 'user.cpp.obj')],
        'FillAt': [dc.Sig("void __fastcall `anonymous namespace'::FillAt(struct stlp::Vec<unsigned int> *,void *,int)",
                          'user.cpp.obj')],
        'GrowFooAt': [dc.Sig("void __fastcall `anonymous namespace'::GrowFooAt(struct stlp::Vec<struct Foo> *,void *,"
                             "int,struct Foo const *)", 'user.cpp.obj')],
        'CountAt': [dc.Sig("int __fastcall `anonymous namespace'::CountAt(struct stlp::Vec<unsigned int> *)",
                           'user.cpp.obj')],
        'GrowIntAt': [dc.Sig("void __fastcall `anonymous namespace'::GrowIntAt(struct stlp::Vec<int> *,void *,int,"
                             "int const *)", 'user.cpp.obj')],
        'GrowFloatAt': [dc.Sig("void __fastcall `anonymous namespace'::GrowFloatAt(struct stlp::Vec<float> *,void *,"
                               "int,float const *)", 'user.cpp.obj')],
    }

    def test_wrappers(self):
        # Registrations through a file's using-alias (with a default template argument left out) are found in the
        # map; a forwarder of a wrapper whose body is one member call becomes that call on the container, also
        # for a container of the real element where the wrapper erases it (Opaque<4>). Count's body is not one
        # member call: it stays a call of the wrapper. A same-size integer element is retargeted (int for
        # unsigned int); a float is not.
        user = ('#include "lib/abi.h"\nstruct Foo { int x; };\nnamespace {\n'
                '__declspec(naked) void __fastcall GrowAt(stlp::Vec<unsigned int>*, void*, int, const unsigned int*) '
                'EI_ORIGINAL(0x00401100)\n'
                '__declspec(naked) void __fastcall FillAt(stlp::Vec<unsigned int>*, void*, int) EI_ORIGINAL(0x00401140)\n'
                '__declspec(naked) void __fastcall GrowFooAt(stlp::Vec<Foo>*, void*, int, const Foo*) '
                'EI_ORIGINAL(0x00401180)\n'
                '__declspec(naked) int __fastcall CountAt(stlp::Vec<unsigned int>*) EI_ORIGINAL(0x004011C0)\n'
                '__declspec(naked) void __fastcall GrowIntAt(stlp::Vec<int>*, void*, int, const int*) '
                'EI_ORIGINAL(0x00401100)\n'
                '__declspec(naked) void __fastcall GrowFloatAt(stlp::Vec<float>*, void*, int, const float*) '
                'EI_ORIGINAL(0x00401100)\n}\n'
                'int Run(stlp::Vec<unsigned int>* v, stlp::Vec<Foo>& foos, unsigned int x, const Foo* foo) {\n'
                '    GrowAt(v, nullptr, 3, &x);\n    FillAt(v, nullptr, 2);\n    GrowFooAt(&foos, nullptr, 1, foo);\n'
                '    return CountAt(v);\n}\n'
                'void Ints(stlp::Vec<int>& ints, int y, stlp::Vec<float>& floats, float z) {\n'
                '    GrowIntAt(&ints, nullptr, 1, &y);\n    GrowFloatAt(&floats, nullptr, 1, &z);\n}\n')
        sources = [dc.Source('src/lib/abi.h', self.ABI_HEADER), dc.Source('src/lib/abi.cpp', self.ABI_REGISTRATIONS),
                   dc.Source('src/game/user.cpp', user)]
        fwds, out = run(sources, self.ABI_MAP)
        self.assertEqual({f.name: (f.kind, f.reason) for f in fwds},
                         {'GrowAt': ('unwrap', ''), 'FillAt': ('unwrap', ''), 'GrowFooAt': ('unwrap', ''),
                          'CountAt': ('rename', ''), 'GrowIntAt': ('unwrap', ''), 'GrowFloatAt': (None, 'types differ')})
        self.assertIn('    v->Grow(3, x);\n    v->Fill<int>(2);\n    foos.Grow(1, *foo);\n'
                      '    return stlp::VecAbi<unsigned int, int>::Count(v);\n', out['src/game/user.cpp'])
        # vector<unsigned int>'s wrapper serves vector<int> (the same code), never vector<float>.
        self.assertIn('    ints.Grow(1, y);\n', out['src/game/user.cpp'])

    def test_abbreviated(self):
        # A template-id the site has an alias for is written through it (default arguments left out).
        src = dc.Source('src/game/user.cpp', 'using AttachedMap = stlp::map<int, char>;\nvoid f() {}\n')
        dc._FREE_DECLS['all'] = [src]
        dc._SEES.clear()
        dc.collect_aliases([src])
        self.assertEqual(dc.abbreviated('stlp::TreeElement<stlp::map<int,char,stlp::Less<int>>::Value>', src, 60),
                         'stlp::TreeElement<AttachedMap::Value>')
        self.assertEqual(dc.abbreviated('stlp::map<int,int>', src, 60), 'stlp::map<int,int>')

    def test_alias_templates(self):
        # C13b1. An alias template's arguments go in for its parameters, a class's member alias (`Second::Type`)
        # is written out, and names in an alias's expansion are looked up in its own namespace.
        header = ('#pragma once\nnamespace net::trees {\nstruct Counted { using Type = RefPtr; };\n'
                  'template <typename K, typename Second>\n'
                  'using PairTree = stlp::TreeAbi<stlp::map<K, typename Second::Type>,\n'
                  '                               stlp::PairElement<typename stlp::map<K, typename Second::Type>::Value, Second>>;\n'
                  'using Objects = PairTree<std::int32_t, Counted>;\n}\n')
        reg = '#include "trees.h"\nusing namespace net::trees;\nEI_REPLACE(0x00401000, Objects::Find);\n'
        hdr, cpp = dc.Source('src/game/trees.h', header), dc.Source('src/game/trees.cpp', reg)
        dc._FREE_DECLS['all'] = [hdr, cpp]
        dc._SEES.clear()
        dc.collect_aliases([hdr, cpp])
        self.assertEqual(dc.expand_aliases('Objects::Find', cpp, ['net', 'trees']),
                         'stlp::TreeAbi<stlp::map<std::int32_t, RefPtr>,\n                               '
                         'stlp::PairElement<stlp::map<std::int32_t, RefPtr>::Value, Counted>>::Find')
        und = ('public: static struct RefPtr * __fastcall stlp::TreeAbi<class stlp::map<int,struct RefPtr,struct '
               'stlp::TreeLess<int> >,struct stlp::PairElement<struct stlp::map<int,struct RefPtr,struct stlp::TreeLess'
               '<int> >::Value,struct net::trees::Counted> >::Find(int const *)')
        decls = {s.qname: [s] for s in [dc.Sig(und, 'trees.cpp.obj')]}
        t, why, _ = dc.resolve_registered('Objects::Find', cpp, decls, dc.by_simple(decls))
        self.assertEqual((why, t and t.simple), ('', 'Find'))

    def test_function_arguments(self):
        # Function pointer template arguments as undname prints them become their names; the source's spelling
        # (hex numbers, nullptr, no `&`) has the same loose key.
        und = ('public: static void __fastcall mfc::impl::ListAbi<struct mfc::impl::Bytes<12>,struct mfc::impl::'
               'EachElements<struct mfc::impl::Bytes<12>,0,&void __cdecl gamelists::ReleaseHolder(struct mfc::impl::'
               'Bytes<12> *)> >::FreeNode(struct mfc::impl::Bytes<12> *,void *)')
        s = dc.Sig(und, 'x')
        self.assertTrue(s.ok)
        self.assertEqual(s.qname, 'mfc::impl::ListAbi<mfc::impl::Bytes<12>,mfc::impl::EachElements<mfc::impl::Bytes<12>,'
                                  '0,&gamelists::ReleaseHolder>>::FreeNode')
        self.assertEqual(s.params, ['mfc::impl::Bytes<12>*', 'void*'])
        member = dc.collapse_function_arguments(
            'void __cdecl f<struct R,&public: void __thiscall ns::R::Assign(struct R const *),7>(void)')
        self.assertEqual(member, 'void __cdecl f<struct R,&ns::R::Assign,7>(void)')
        dc._NAMESPACES.update({'mfc', 'impl', 'gamelists'})
        self.assertEqual(dc.loose_key('ListAbi<Bytes<0xC>, EachElements<Bytes<0xC>, nullptr, ReleaseHolder>>'),
                         dc.loose_key(s.qname.rsplit('::', 1)[0]))
        self.assertEqual(dc.loose_key('PlainArray<unsigned, X>'), 'PlainArray<unsigned int,X>')


if __name__ == '__main__':
    unittest.main()
