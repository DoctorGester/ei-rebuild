"""Finds double constants in our x87 code that the original function never loads (SV7d1, SV7d1a).

A product of constants that the original multiplies (or divides) at run time is rounded to 24 bits
on the game's main thread; the compiler folds the same product at double precision, so our code
multiplies by a constant with 29 more bits (terrain's 256 / pi, the briefing's and the sun's pi / 180:
the original's FLD; FDIV / FMUL of two doubles). Before SV7d1a the build was /fp:fast, which also
distributed sums ((30x + d) * (1/31) into x * (30/31) + d * (1/31), SV7d1); /fp:precise does not, so
what is left to find are the folds written in the source.

What it does, on the built ei_hook.dll and its map:
  1. The __real@ constants of the map that our .text loads with an x87 instruction, per function.
  2. Each function registered with EI_REPLACE (_METHOD, _DISPATCH, _STANDALONE) is matched to its
     symbol (check_signatures' registrations / candidates_for); the original's body in game.exe gives
     its known values: x87 memory operands and every dword it reads from a fixed address, as float
     and double, and its 32-bit immediates as float bits and as integers. A function without a
     registration (a helper the compiler did not inline) is compared with the originals of the
     registered functions that call it, through any chain of unregistered helpers (CALL / JMP rel32
     in our .text); one that no registered function calls directly (reached through a table or a
     function pointer) is compared with every value the exe's x87 instructions load, and says so.
  3. A constant whose value (rounded to float, up to a power of two: such folds are exact) the
     original does not have is new. A new double that is not a float is a problem: write the
     constant as the float the original's run-time product gives (static_cast<float> of the product;
     check the rounding) or keep the two operations. New floats (literals, float products, which
     round like the 24-bit FPU) are listed with -v.
A value listed in tools/check_fp_fold_allow.txt ("<value> <function or *>  # reason") is reviewed.

usage: python tools/check_fp_fold.py [--dll build/release/ei_hook.dll] [-v]
exit code 1 on a problem
"""
import argparse, bisect, glob, os, re, struct, sys, time

from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from capstone.x86 import X86_OP_IMM, X86_OP_MEM

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import exe_path
from check_signatures import ROOT, candidates_for, load_sizes, registrations, undecorate
from check_registrations import Image

ALLOW = os.path.join(ROOT, 'tools', 'check_fp_fold_allow.txt')
MAP_LINE = re.compile(r'^\s*000\d:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{8})\s+(f\s+)?(?:i\s+)?(\S+)\s*$', re.M)
FPU_MEM = re.compile(rb'[\xd8-\xdf][\x05\x0d\x15\x1d\x25\x2d\x35\x3d]', re.S)
IMM_FLOAT = re.compile(rb'(?:\x68|\xc7\x44\x24.|\xc7\x45.|\xc7\x05....)(....)', re.S)
QUALIFIED = re.compile(r'([\w:~<>]+)\(')
SKIP_OBJ = re.compile(r'^(?:libucrt|libcmt|libvcruntime|libcpmt|msvcprt|ei_render)|third_party')


def harness_objects():
    """Object names of the harness-only sources (tests/verify, tests/vectors)."""
    names = set()
    for pattern in ('tests/verify/*.cpp', 'tests/vectors/*.cpp'):
        for path in glob.glob(os.path.join(ROOT, pattern)):
            names.add(os.path.basename(path).lower() + '.obj')
    return names


def f32(v):
    try:
        return struct.unpack('<f', struct.pack('<f', v))[0]
    except OverflowError:
        return v


def scaled(v, have):
    """v is a value of `have` times a power of two (a fold with 2^k is exact)."""
    if v == 0 or v != v or v in (float('inf'), float('-inf')):
        return True
    for k in range(-40, 41):
        if f32(v * 2.0 ** k) in have or f32(-v * 2.0 ** k) in have:
            return True
    return False


def fmt(v):
    return f'{v:.9g}'


class Exe:
    def __init__(self):
        self.img = Image(exe_path.resolve())

    def read(self, va, size):
        rva = va - self.img.base
        for _, sva, vsz, rptr, rsz in self.img.sections:
            if sva <= rva and rva + size <= sva + rsz:
                return self.img.data[rptr + rva - sva:rptr + rva - sva + size]
        return None

    def values(self, raw):
        out = set()
        if raw is None:
            return out
        if len(raw) >= 4:
            out.add(f32(struct.unpack_from('<f', raw)[0]))
        if len(raw) >= 8:
            out.add(f32(struct.unpack_from('<d', raw)[0]))
        return out

    def global_values(self):
        """Every float / double the exe's x87 instructions load from a fixed address."""
        out = set()
        for name, sva, vsz, rptr, rsz in self.img.sections:
            if not name.startswith('.text'):
                continue
            code = self.img.data[rptr:rptr + rsz]
            for m in FPU_MEM.finditer(code):
                i = m.start()
                op, reg = code[i], (code[i + 1] >> 3) & 7
                va = struct.unpack_from('<I', code, i + 2)[0]
                if op == 0xD8 or (op == 0xD9 and reg in (0, 2, 3)):
                    raw = self.read(va, 4)
                elif op == 0xDC or (op == 0xDD and reg in (0, 2, 3)):
                    raw = self.read(va, 8)
                else:
                    continue
                if raw:
                    out.add(f32(struct.unpack('<f' if len(raw) == 4 else '<d', raw)[0]))
            # Floats passed or stored as immediates: PUSH imm32, MOV [ESP/EBP+d8], imm32, MOV [abs], imm32.
            for m in IMM_FLOAT.finditer(code):
                v = struct.unpack_from('<f', m.group(1))[0]
                if v == v:
                    out.add(f32(v))
        return out

    def function_values(self, md, address, size):
        """Values the original function has: fixed-address reads (as float and double), immediates."""
        out = set()
        off = address - self.img.base
        rva_text = next(s for s in self.img.sections if s[0] == '.text')
        code = self.read(address, size)
        if code is None:
            return out
        for insn in md.disasm(code, address):
            for op in insn.operands:
                if op.type == X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0 and op.mem.disp:
                    out |= self.values(self.read(op.mem.disp & 0xFFFFFFFF, 8) or self.read(op.mem.disp & 0xFFFFFFFF, 4))
                elif op.type == X86_OP_IMM:
                    imm = op.imm & 0xFFFFFFFF
                    out.add(f32(struct.unpack('<f', struct.pack('<I', imm))[0]))
                    out.add(float(op.imm))
        return out


def call_graph(code, text_va, starts, funcs, skip, md):
    """function index -> indices of the functions with a CALL / JMP rel32 to its start (on an
    instruction boundary of the caller; our own code only)."""
    index = {va: k for k, va in enumerate(starts)}
    boundary = {}
    callers = {}
    for m in re.finditer(rb'(?=[\xe8\xe9](....))', code, re.S):
        i = m.start()
        target = (text_va + i + 5 + struct.unpack_from('<i', m.group(1))[0]) & 0xFFFFFFFF
        t = index.get(target)
        if t is None:
            continue
        site = text_va + i
        k = bisect.bisect_right(starts, site) - 1
        if k < 0 or k == t or skip(funcs[k][2]):
            continue
        ok = boundary.get(k)
        if ok is None:
            start = starts[k]
            lim = starts[k + 1] if k + 1 < len(starts) else text_va + len(code)
            ok = boundary[k] = {x.address for x in md.disasm(code[start - text_va:lim - text_va], start)}
        if site in ok:
            callers.setdefault(t, set()).add(k)
    return callers


def load_allow():
    allow = set()
    if os.path.exists(ALLOW):
        for line in open(ALLOW):
            parts = line.split('#')[0].split(None, 1)
            if len(parts) == 2:
                allow.add((f32(float(parts[0])), parts[1].strip()))
    return allow


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dll', default=os.path.join(ROOT, 'build', 'release', 'ei_hook.dll'))
    ap.add_argument('-v', '--verbose', action='store_true', help='list new floats and reviewed doubles too')
    args = ap.parse_args(argv)
    started = time.time()
    img = Image(args.dll)
    _, text_rva, text_vsz, text_ptr, text_rsz = next(s for s in img.sections if s[0] == '.text')
    code = img.data[text_ptr:text_ptr + min(text_vsz, text_rsz)]
    text_va = img.base + text_rva

    consts = {}                                      # VA -> value
    funcs = []
    for dec, va, flag, obj in MAP_LINE.findall(open(os.path.splitext(args.dll)[0] + '.map', errors='replace').read()):
        m = re.match(r'__real@([0-9a-f]{8}|[0-9a-f]{16})$', dec)
        if m:
            h = m.group(1)
            consts[int(va, 16)] = struct.unpack('<f' if len(h) == 8 else '<d', bytes.fromhex(h)[::-1])[0]
        elif flag:
            funcs.append((int(va, 16), dec, obj.lower()))
    funcs.sort()
    starts = [f[0] for f in funcs]

    # Our x87 loads of each constant, on instruction boundaries, per function (not the libraries, the
    # renderer or the harness-only sources: the harness and the recorders are not game code).
    tests = harness_objects()

    def skip(obj):
        return bool(SKIP_OBJ.search(obj)) or obj in tests

    md = Cs(CS_ARCH_X86, CS_MODE_32)
    boundary = {}
    uses = {}                                        # function index -> {value}
    wide = set()                                     # values of double constants that are not floats
    for va, value in consts.items():
        pat = struct.pack('<I', va)
        i = code.find(pat)
        while i != -1:
            site = text_va + i - 2                   # opcode, modrm, disp32
            k = bisect.bisect_right(starts, site) - 1
            # modrm 00 xxx 101: an absolute [disp32] operand (not, say, "fld [ebp+58h]" whose next
            # bytes, a call's, happen to spell the constant's address)
            if k >= 0 and i >= 2 and 0xD8 <= code[i - 2] <= 0xDF and code[i - 1] & 0xC7 == 0x05 \
                    and not skip(funcs[k][2]):
                start = starts[k]
                ok = boundary.get(start)
                if ok is None:
                    lim = starts[k + 1] if k + 1 < len(starts) else text_va + len(code)
                    ok = boundary[start] = {x.address for x in md.disasm(code[start - text_va:lim - text_va], start)}
                if site in ok:
                    uses.setdefault(k, set()).add(f32(value))
                    if f32(value) != value:
                        wide.add((k, f32(value)))
            i = code.find(pat, i + 1)

    # Our functions' names, and the original address of each registered replacement.
    und = undecorate({f[1] for f in funcs if not skip(f[2])})
    declared = {}
    index_of = {}
    for k in range(len(funcs)):
        if skip(funcs[k][2]):
            continue
        u = und.get(funcs[k][1])
        m = QUALIFIED.search(u) if u else None
        if m:
            declared.setdefault(m.group(1), []).append((u, funcs[k][2]))
            index_of[(u, funcs[k][2])] = k
    original = {}                                    # function index -> original address
    for address, name, kind, rel, line in registrations():
        if kind != 'replace':
            continue
        obj = os.path.splitext(os.path.basename(rel))[0].lower() + '.cpp.obj'
        for c in candidates_for(declared, name, kind, rel, lambda u: u):
            # A plain name matched by suffix only counts in its own file (console's Execute is not AIMode's).
            if c[1].endswith(obj) or QUALIFIED.search(c[0]).group(1) == name:
                original.setdefault(index_of[c], address)

    callers = call_graph(code, text_va, starts, funcs, skip, md)

    # Every 4 bytes of the image that spell a function's start (at any alignment: x86 immediates), by
    # the function: ([.text sites], in data).
    start_set = set(starts)
    refs = {}
    top = re.escape(bytes([img.base >> 24]))
    for name, sva, vsz, rptr, rsz in img.sections:
        raw = img.data[rptr:rptr + min(vsz, rsz)]
        for m in re.finditer(rb'(?=(...' + top + rb'))', raw, re.S):
            v = struct.unpack('<I', m.group(1))[0]
            if v in start_set:
                entry = refs.setdefault(bisect.bisect_right(starts, v) - 1, ([], [False]))
                if name == '.text':
                    entry[0].append(text_va + m.start())
                else:
                    entry[1][0] = True

    def address_taken(k):
        """(functions of our .text that hold the address of function k, whether data holds it)."""
        sites, data = refs.get(k, ([], [False]))
        inside = set()
        for site in set(sites):
            j = bisect.bisect_right(starts, site) - 1
            if j >= 0 and j != k and not skip(funcs[j][2]):
                inside.add(j)
        return inside, data[0]

    def registered_callers(k):
        """(registered functions that reach function k through unregistered helpers, whether some
        other path reaches it from data: a table, a function pointer, which names no owner,
        whether some path is live at all)."""
        found, seen, todo, table, live = set(), {k}, [k], False, False
        while todo:
            n = todo.pop()
            above = set(callers.get(n, ()))
            if not above:                        # no CALL to it: a pointer to it, or nothing (dead)
                inside, data = address_taken(n)
                above |= inside
                table |= data
            live |= bool(above) or table
            for c in above:
                if c in original:
                    found.add(c)
                elif c not in seen:
                    seen.add(c)
                    todo.append(c)
        return found, table, live

    exe = Exe()
    known = exe.global_values()
    sizes = load_sizes()
    mdd = Cs(CS_ARCH_X86, CS_MODE_32)
    mdd.detail = True
    allow = load_allow()
    problems = allowed = helpers = unattached = dead = 0
    for k in sorted(uses, key=lambda k: (funcs[k][2], funcs[k][0])):
        address = original.get(k)
        size = sizes.get(address) if address else None
        have, via = None, ''
        if size:
            have = exe.function_values(mdd, address, size)
        else:
            owners, table, live = registered_callers(k)
            origins = [(original[c], sizes.get(original[c]), c) for c in sorted(owners)]
            origins = [o for o in origins if o[1]]
            if not live:
                dead += 1
                continue                         # an out-of-line copy nothing calls (inlined everywhere)
            if origins and not table:
                have = set()
                for a, sz, c in origins:
                    have |= exe.function_values(mdd, a, sz)
                via = ' via ' + ', '.join(f'{a:08X}' for a, _, _ in origins)
                helpers += 1
            else:
                unattached += 1
                if args.verbose:
                    print(f'  unattached: {funcs[k][2]} {und.get(funcs[k][1], funcs[k][1])}')
        if have is None:
            have = known
        new = sorted(v for v in uses[k] if not scaled(v, have | {1.0}))
        if not new:
            continue
        name = und.get(funcs[k][1], funcs[k][1])
        short = QUALIFIED.search(name).group(1) if QUALIFIED.search(name) else name
        wide_new = [v for v in new if (k, v) in wide]
        fresh = [v for v in wide_new if (v, short) not in allow and (v, '*') not in allow]
        allowed += len(wide_new) - len(fresh)
        if fresh or args.verbose:
            where = f'{address:08X}' if size else (via.strip() if via else 'no registered caller: every exe constant')
            shown = new if args.verbose else fresh
            print(f'{funcs[k][2]:32} {short} ({where}): ' +
                  ', '.join(fmt(v) + (' (double)' if (k, v) in wide else '') for v in shown))
        problems += len(fresh)
    print(f'check_fp_fold: {len(uses)} function(s) with x87 constants, {len(original)} matched to the original, '
          f'{helpers} helper(s) against their callers, {unattached} against every exe constant, {dead} uncalled, {allowed} reviewed constant(s), {problems} problem(s); {time.time() - started:.1f} s')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
