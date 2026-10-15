"""Generates src/game/short_functions.cpp: the game functions under five bytes that nothing ports.

A hook needs five bytes, so a function shorter than that (RET, XOR EAX,EAX; RET, RET 4,
MOV EAX,[ECX+n]; RET, ...) cannot get a jump in game.exe. The hook build leaves them to the original;
the standalone image (docs/notes/standalone.md section 2) has room for a jump at each (the next
function starts at least five bytes later) and needs a body for them. Each unported one is decoded
from its bytes into one of the kinds below and registered with EI_REPLACE_SHORT.

Unported = config/functions.csv's game functions, minus config/library.csv and config/dead_code.csv,
under five bytes (src/hook/function_sizes.inc), with no EI_REPLACE / EI_REPLACE_METHOD / UI input
handler registration in src/ (the output file itself is not read). A byte pattern that is not one
of the kinds stops the script. tools/check_short_functions.py checks the result.

usage: python tools/gen_short_functions.py [--exe game.exe]
"""
import argparse, csv, os, re, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'src', 'game', 'short_functions.cpp')
EXE = r'F:\games\Evil Islands (Rebirth)\starter\Engine\game.exe'
BASE = 0x400000
SHORT = 5

# Kind -> its bytes (`arg`: the member offset of the parameterised kinds; disp8, so under 0x80).
FIXED = {
    'Ret': b'\xc3',
    'Ret4': b'\xc2\x04\x00',
    'Ret8': b'\xc2\x08\x00',
    'Ret12': b'\xc2\x0c\x00',
    'ReturnZero': b'\x33\xc0\xc3',
    'ReturnMinusOne': b'\x83\xc8\xff\xc3',
    'ReturnTrue': b'\xb0\x01\xc3',
    'ReturnFalse': b'\x32\xc0\xc3',
    'ReturnThis': b'\x8b\xc1\xc3',
}
OPCODES = {'LoadDword': 0x8b, 'MemberAddress': 0x8d, 'LoadFloat': 0xd9}   # MOV EAX / LEA EAX / FLD DWORD, [ECX+n]


def kind_bytes(kind, arg=None):
    """The code of a kind (with its offset), or None for an unknown kind or offset."""
    if kind in FIXED:
        return FIXED[kind] if arg is None else None
    op = OPCODES.get(kind)
    if op is None or arg is None or not 0 <= arg < 0x80 or arg % 4:
        return None
    return bytes([op, 0x01, 0xc3]) if arg == 0 else bytes([op, 0x41, arg, 0xc3])


def decode(code):
    """(kind, offset or None) for a function's bytes, or None when they are not one of the kinds."""
    for kind, b in FIXED.items():
        if code == b:
            return kind, None
    for kind, op in OPCODES.items():
        if len(code) == 3 and code[0] == op and code[1:] == b'\x01\xc3':
            return kind, 0
        if len(code) == 4 and code[0] == op and code[1] == 0x41 and code[3] == 0xc3 and kind_bytes(kind, code[2]):
            return kind, code[2]
    return None


def kind_name(kind, arg):
    return kind if arg is None else f'{kind}<0x{arg:X}>'


def load_sizes():
    sizes = {}
    for line in open(os.path.join(ROOT, 'src', 'hook', 'function_sizes.inc')):
        m = re.match(r'\{0x([0-9A-F]+), (\d+)\}', line)
        if m:
            sizes[int(m.group(1), 16)] = int(m.group(2))
    return sizes


def csv_addresses(name):
    return {int(r['address'], 16) for r in csv.DictReader(open(os.path.join(ROOT, 'config', name)))}


def game_functions():
    rows = csv.DictReader(open(os.path.join(ROOT, 'config', 'functions.csv')))
    return {int(r['address'], 16) for r in rows if r['kind'] == 'game'} - csv_addresses('library.csv') - csv_addresses('dead_code.csv')


REGISTERED = re.compile(r'\b(?:EI_REPLACE(?:_METHOD)?|UI_(?:INPUT|WHEEL)_HANDLER)\(\s*0x([0-9A-Fa-f]+)')
COMMENT = re.compile(r'//[^\n]*|/\*.*?\*/', re.S)


def ported_elsewhere():
    """Addresses registered by a rewrite (not by EI_REPLACE_SHORT) anywhere in src/."""
    out = set()
    for d, _, names in os.walk(os.path.join(ROOT, 'src')):
        for n in names:
            p = os.path.join(d, n)
            if n.endswith(('.cpp', '.h')) and os.path.abspath(p) != os.path.abspath(OUT):
                out |= {int(a, 16) for a in REGISTERED.findall(COMMENT.sub('', open(p, encoding='utf-8', errors='replace').read()))}
    return out


def vtable_slots(exe):
    """Function address -> [(vtable, slot)] over config/vtables.csv."""
    out = {}
    for r in csv.DictReader(open(os.path.join(ROOT, 'config', 'vtables.csv'))):
        vt, n = int(r['vtable'], 16), int(r['slots'])
        for i in range(n):
            out.setdefault(struct.unpack_from('<I', exe, vt - BASE + 4 * i)[0], []).append((vt, i))
    return out


def unported(sizes):
    return sorted(a for a in game_functions() - ported_elsewhere() if sizes.get(a, 0) < SHORT)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--exe', default=EXE)
    args = ap.parse_args()
    exe = open(args.exe, 'rb').read()
    sizes = load_sizes()
    slots = vtable_slots(exe)
    lines, bad = [], []
    for a in unported(sizes):
        code = exe[a - BASE:a - BASE + sizes.get(a, 0)]
        k = decode(code)
        if not k:
            bad.append(f'{a:08X}: {code.hex(" ")}')
            continue
        where = slots.get(a, [])
        note = ', '.join(f'vtable {vt:08X} slot {i}' for vt, i in where[:2])
        if len(where) > 2:
            note += f' and {len(where) - 2} more'
        lines.append(f'EI_REPLACE_SHORT(0x{a:08X}, {kind_name(*k)});' + (f'  // {note}' if note else ''))
    if bad:
        print('not one of the kinds (port these by hand or add a kind):\n  ' + '\n  '.join(bad))
        return 1
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write(HEADER)
        f.write('\n'.join(lines) + '\n\n} // namespace ei::short_functions\n')
    print(f'{len(lines)} short function(s) written to {os.path.relpath(OUT, ROOT)}')
    return 0


HEADER = '''\
// Generated by tools/gen_short_functions.py from game.exe's bytes. Do not edit.
//
// The game functions under five bytes that no rewrite ports (docs/notes/standalone.md, S2). A hook
// needs five bytes, so the hook build leaves these to the original (EI_REPLACE_SHORT); the standalone
// image writes a jump here at each (the next function leaves room). Each kind compiles to the
// original's own bytes: tools/check_short_functions.py (build.cmd) compares every registration's kind
// with game.exe and with our compiled code.
#include "ei/hook.h"

#include <cstddef>
#include <cstdint>

namespace ei::short_functions {

// C3: nothing, no stack arguments (a cdecl function, or a method whose arguments are in registers).
void Ret() {}
// C2 nn 00: nothing, the callee pops nn bytes of stack arguments.
void __stdcall Ret4(std::uint32_t) {}
void __stdcall Ret8(std::uint32_t, std::uint32_t) {}
void __stdcall Ret12(std::uint32_t, std::uint32_t, std::uint32_t) {}
// 33 C0 C3, 83 C8 FF C3, B0 01 C3, 32 C0 C3: a constant.
int ReturnZero() { return 0; }
int ReturnMinusOne() { return -1; }
bool ReturnTrue() { return true; }
bool ReturnFalse() { return false; }
// 8B C1 C3: the object (ECX) itself.
const void* __fastcall ReturnThis(const void* self) { return self; }
// 8B 41 nn C3, 8D 41 nn C3, D9 41 nn C3: a dword member, a member's address, a float member, nn
// bytes into the object (8B 01 C3 for nn 0). The classes are those of the vtables named below and
// are not declared yet, so the object is read as dwords.
template <std::size_t Offset>
std::uint32_t __fastcall LoadDword(const std::uint32_t* self) { return self[Offset / 4]; }
template <std::size_t Offset>
const std::uint32_t* __fastcall MemberAddress(const std::uint32_t* self) { return self + Offset / 4; }
template <std::size_t Offset>
float __fastcall LoadFloat(const float* self) { return self[Offset / 4]; }

'''

if __name__ == '__main__':
    sys.exit(main())
