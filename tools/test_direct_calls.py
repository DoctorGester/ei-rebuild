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


def run(sources, decls=MAP):
    dc._FREE_DECLS['all'] = sources
    fwds, classes = dc.analyse(sources, decls)
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
        self.assertIsNone(dc.match(dc.Sig('int __fastcall F(void *,void *,int)', 'x'), member))
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

    def test_reindent(self):
        self.assertEqual(dc.reindent('a,\n       b,\n   c)', 7, -3), 'a,\n    b,\n   c)')

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


if __name__ == '__main__':
    unittest.main()
