"""Generates src/standalone/static_init.cpp: game.exe's 748 static initialisers as C++.

The C runtime runs the functions listed in .data at 00783004-00783BB0 (the C++ initialiser list,
__xc_a + 4 to __xc_z - 4) before WinMain. In game.exe the original runs them; the standalone
image (docs/notes/standalone.md section 2) has no code of game.exe and runs these instead, in list
order, before WinMain.

Each initialiser is decoded from its instructions by a small symbolic interpreter into operations:
dword stores (constants, copies, fills), float folds of .rdata constants (x87 sequences: + - * /,
ln), calls (cdecl, stdcall, thiscall, through an import slot) with their arguments, atexit
registrations, and the once-only guard bit (`if (flag & bit) skip; flag |= bit`). A call goes to the
target's address in the image (our rewrite through its jump, or a library port) unless the target is
not registered anywhere, is referenced from one place only and decodes itself: then it is inlined
(the compiler's helper halves, `call A; jmp B`). An initialiser registered by a rewrite (0042FAE0) is
one call to its own address. Anything the interpreter does not know stops the script.

The result is one template, StaticInitialisers(Do&), that the standalone runs (Run) and the verify
build's start-up check evaluates against what the original's initialisers left in memory (Check).
tools/check_static_init.py (build.cmd) re-decodes game.exe and compares.

usage: python tools/gen_static_init.py [--exe game.exe]
"""
import argparse, bisect, collections, os, re, struct, sys

import capstone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import exe_path  # noqa: E402
import gen_short_functions as short  # noqa: E402
import rdata_literals as rdata  # noqa: E402

ROOT = short.ROOT
OUT = os.path.join(ROOT, 'src', 'standalone', 'static_init.cpp')
OUT_EXIT = os.path.join(ROOT, 'src', 'standalone', 'static_exit.cpp')
SIZES = os.path.join(ROOT, 'src', 'hook', 'function_sizes.inc')
EXTRA_SIZES = os.path.join(ROOT, 'config', 'extra_function_sizes.csv')
EXTRA_NOTE = 'exit function of a static initialiser (tools/gen_static_init.py; no Ghidra function)'
BASE = 0x400000
LIST_BEGIN, LIST_END = 0x00783004, 0x00783BB0   # inclusive
ATEXIT = 0x006E727B
TEXT = (0x00401000, 0x0073A670)
LOCAL_MAX = 0x400   # src/standalone/static_init.h, Run::frame_


class Image:
    def __init__(self, path):
        self.data = open(path, 'rb').read()
        d = self.data
        pe = struct.unpack_from('<I', d, 0x3c)[0]
        nsec, optsz = struct.unpack_from('<H', d, pe + 6)[0], struct.unpack_from('<H', d, pe + 20)[0]
        self.sections = []
        for i in range(nsec):
            o = pe + 24 + optsz + 40 * i
            vsz, va, rsz, rp = struct.unpack_from('<IIII', d, o + 8)
            self.sections.append((d[o:o + 8].rstrip(b'\0').decode(), BASE + va, vsz, rp, rsz))

    def iat(self):
        """[begin, end) of the import address table (data directory 12)."""
        d = self.data
        pe = struct.unpack_from('<I', d, 0x3c)[0]
        va, size = struct.unpack_from('<II', d, pe + 24 + 96 + 8 * 12)
        return BASE + va, BASE + va + size

    def section(self, a):
        for name, va, vsz, _, _ in self.sections:
            if va <= a < va + vsz:
                return name
        return None

    def read(self, a, n):
        for _, va, vsz, rp, rsz in self.sections:
            if va <= a < va + vsz:
                off = a - va
                raw = self.data[rp + off:rp + min(off + n, rsz)] if off < rsz else b''
                return raw + b'\0' * (n - len(raw))
        raise ValueError(f'{a:08X} is not in the image')

    def dword(self, a):
        return struct.unpack('<I', self.read(a, 4))[0]


class RdataView:
    """The image as tools/rdata_literals.py reads it (S18e1): a float fold's operand in constant .rdata is
    its tagged literal; one in .data (an earlier fold's result) stays a read. The folds are exact at
    the original's 53 bits and in float alike (check_static_init), so the compiler may fold them."""

    def __init__(self, img):
        self.bytes, self.written = img.read, [(a, a + n) for a, n in rdata.addon_sites()]

    readable = rdata.Exe.readable


class Unknown(Exception):
    pass


def references(img):
    """Target -> number of E8 / E9 rel32 instructions in .text that reach it (a byte scan)."""
    _, va, vsz, rp, _ = next(s for s in img.sections if s[0] == '.text')
    text = img.data[rp:rp + vsz]
    count = collections.Counter()
    for m in re.finditer(b'[\xe8\xe9]', text, re.S):
        i = m.start()
        if i + 5 <= len(text):
            t = va + i + 5 + struct.unpack_from('<i', text, i + 1)[0]
            if TEXT[0] <= t < TEXT[1]:
                count[t] += 1
    return count


def hexa(v):
    return f'0x{v:08X}'


def lit(v):
    v &= 0xFFFFFFFF
    return str(v) if v < 10 else f'0x{v:X}'


# game.exe's data (its import address table, .rdata, .data and .bss, .rsrc): an argument there is a
# data address, which the visitor resolves through the data seam (d.At, static_init.h; S18).
DATA = (0x0073B000, 0x007D7000)


def arg_lit(v):
    v &= 0xFFFFFFFF
    return f'd.At({hexa(v)})' if DATA[0] <= v < DATA[1] else lit(v)


MEM = re.compile(r'^(byte|dword|qword) ptr \[(0x[0-9a-f]+)\]$')
REGMEM = re.compile(r'^(byte|dword|qword) ptr \[(e[a-d]x|e[sd]i)(?: ([+-]) (0x[0-9a-f]+|\d+))?\]$')
STACK = re.compile(r'^\[esp(?: \+ (0x[0-9a-f]+|\d+))?\]$')
REG32 = {'al': 'eax', 'eax': 'eax', 'cl': 'ecx', 'ecx': 'ecx', 'dl': 'edx', 'edx': 'edx', 'ebx': 'ebx', 'esi': 'esi',
         'edi': 'edi', 'ebp': 'ebp'}


def imm(s):
    return int(s, 0) & 0xFFFFFFFF


class Interpreter:
    """Decodes one initialiser into C++ statements (strings) and a summary of what it does."""

    def __init__(self, img, registered, refs):
        self.img, self.registered, self.refs = img, registered, refs
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

    def decode(self, address):
        self.lines, self.kinds, self.targets, self.inlined = [], [], [], []
        self.spans = set()   # (address, size) of every instruction interpreted (inlined helpers included)
        self.regs = {}
        self.stack = []      # ('frame',) markers and argument expressions
        self.fpu = []
        self.flag = None     # (address, bit) of the last test
        self.local = 0
        if address in self.registered:
            self.emit(f'd.Cdecl({hexa(address)});', '')
            self.kinds.append('cdecl')
            self.targets.append(('call', address))
            return
        self.run(address, None, '')

    # Values: ('imm', v), ('dword', addr) a load, ('result', expr) a call's EAX, ('byte', addr),
    # ('bit', addr, bit), ('setbit', addr, bit), ('local', offset), ('frame',).
    def arg(self, v):
        kind = v[0]
        if kind == 'imm':
            return arg_lit(v[1])
        if kind == 'dword':
            return f'd.Dword({hexa(v[1])})'
        if kind == 'local':
            return f'd.Local({lit(v[1])})'
        raise Unknown(f'argument {v}')

    def emit(self, text, indent):
        self.lines.append(indent + text)

    def instructions(self, at):
        while True:
            x = next(self.md.disasm(self.img.read(at, 16), at), None)
            if x is None:
                raise Unknown(f'{at:08X}: cannot disassemble')
            self.spans.add((x.address, x.size))
            yield x
            at = x.address + x.size

    def run(self, at, stop, indent):
        """Interprets from `at` until RET (or `stop`); returns at RET or at `stop`."""
        it = self.instructions(at)
        x = next(it)
        while True:
            if stop is not None and x.address == stop:
                return
            m, ops = x.mnemonic, [o.strip() for o in x.op_str.split(',')] if x.op_str else []
            nxt = x.address + x.size
            if m == 'ret':
                if ops:
                    raise Unknown(f'{x.address:08X}: ret {x.op_str}')
                if stop is not None:
                    raise Unknown(f'{x.address:08X}: ret inside a guarded block')
                return
            if m == 'push':
                self.stack.append(self.value(ops[0], x))
            elif m == 'pop':
                if not self.stack:
                    raise Unknown(f'{x.address:08X}: pop of an empty stack')
                self.regs[REG32[ops[0]]] = self.stack.pop()
            elif m == 'sub' and ops[0] == 'esp':
                if self.local:
                    raise Unknown(f'{x.address:08X}: second local frame')
                self.local = imm(ops[1])
                if self.local > LOCAL_MAX:
                    raise Unknown(f'{x.address:08X}: a stack frame over {LOCAL_MAX:#x} bytes (Run::frame_)')
                self.stack.append(('frame',))
            elif m == 'add' and ops[0] == 'esp':
                n = imm(ops[1])
                while n:
                    if not self.stack:
                        raise Unknown(f'{x.address:08X}: add esp past the frame')
                    top = self.stack.pop()
                    n -= self.local if top == ('frame',) else 4
                    if n < 0:
                        raise Unknown(f'{x.address:08X}: add esp splits a slot')
            elif m == 'lea':
                s = STACK.match(ops[1])
                if not s or not self.stack or self.stack[-1] != ('frame',):
                    raise Unknown(f'{x.address:08X}: lea {x.op_str}')
                self.regs[REG32[ops[0]]] = ('local', int(s.group(1) or '0', 0))
            elif m == 'mov' and ops[0] == 'ebp' and ops[1] == 'esp':
                pass   # a frame pointer (debug-built code), popped again before RET
            elif m == 'mov':
                dst = self.memory(ops[0])
                if dst:
                    self.store(dst[0], dst[1], self.value(ops[1], x), indent, x)
                elif ops[0] in REG32:
                    self.regs[REG32[ops[0]]] = self.value(ops[1], x)
                else:
                    raise Unknown(f'{x.address:08X}: mov {x.op_str}')
            elif m == 'xor' and ops[0] == ops[1] and ops[0] in REG32:
                self.regs[REG32[ops[0]]] = ('imm', 0)
            elif m in ('and', 'or') and ops[0] in REG32:
                r = REG32[ops[0]]
                v, o = self.regs.get(r), self.value(ops[1], x)
                if m == 'and' and v and v[0] == 'byte' and o[0] == 'imm':
                    self.regs[r] = ('bit', v[1], o[1])
                elif m == 'or' and v and v[0] == 'byte' and o[0] == 'imm':
                    self.regs[r] = ('setbit', v[1], o[1])
                else:
                    raise Unknown(f'{x.address:08X}: {m} {x.op_str}')
            elif m == 'test':
                a, b = self.value(ops[0], x), self.value(ops[1], x)
                if a[0] == 'imm' and b[0] == 'byte':
                    self.flag = (b[1], a[1])
                elif a[0] == 'bit' and a == b:
                    self.flag = (a[1], a[2])
                else:
                    raise Unknown(f'{x.address:08X}: test {x.op_str}')
            elif m == 'jne':
                if not self.flag:
                    raise Unknown(f'{x.address:08X}: jne without a guard test')
                target = imm(ops[0])
                flag, bit = self.flag
                self.flag = None
                depth = len(self.stack)
                self.emit(f'if (d.Once({hexa(flag)}, {lit(bit)})) {{', indent)
                self.kinds.append('once')
                self.run(nxt, target, indent + '    ')
                if len(self.stack) != depth:
                    raise Unknown(f'{x.address:08X}: the guarded block leaves the stack changed')
                self.emit('}', indent)
                it = self.instructions(target)
            elif m == 'call' or m == 'jmp':
                tail = m == 'jmp'
                mem = MEM.match(ops[0])
                if mem and mem.group(1) == 'dword' and self.img.section(int(mem.group(2), 16)) == '.rdata':
                    target, through = int(mem.group(2), 16), 'import'
                elif re.fullmatch(r'0x[0-9a-f]+', ops[0]):
                    target, through = int(ops[0], 16), None
                else:
                    raise Unknown(f'{x.address:08X}: {m} {x.op_str}')
                if through is None and target != ATEXIT and self.inlinable(target):
                    # A helper referenced from here only: interpret its body (a call with a return
                    # address on the stack; a tail jump returns for us). Back to a call if it does
                    # something the interpreter does not know.
                    saved = self.snapshot()
                    try:
                        self.inlined.append(target)
                        if not tail:
                            self.stack.append(('return',))
                        self.run(target, None, indent)
                        if not tail and (not self.stack or self.stack.pop() != ('return',)):
                            raise Unknown(f'{target:08X}: the inlined helper leaves the stack changed')
                        if tail:
                            return
                        x = next(it)
                        continue
                    except Unknown:
                        self.restore(saved)
                cleanup = 0
                if not tail:
                    follow = next(self.md.disasm(self.img.read(nxt, 16), nxt))
                    if follow.mnemonic == 'pop' and follow.op_str == 'ecx':
                        cleanup = 4
                    elif follow.mnemonic == 'add' and follow.op_str.startswith('esp, '):
                        cleanup = imm(follow.op_str[5:])
                self.call_site(target, through, cleanup, tail, indent, x)
                if tail:
                    self.check_balanced(x)
                    return
            elif m == 'rep stosd':
                count, dst, value = self.regs.get('ecx'), self.regs.get('edi'), self.regs.get('eax')
                if not (count and dst and value and count[0] == dst[0] == value[0] == 'imm'):
                    raise Unknown(f'{x.address:08X}: rep stosd with unknown registers')
                self.emit(f'd.Fill({hexa(dst[1])}, {lit(value[1])}, {lit(count[1])});', indent)
                self.kinds.append('fill')
                self.regs['edi'] = ('imm', dst[1] + 4 * count[1])
                self.regs['ecx'] = ('imm', 0)
            elif m in ('fld', 'fldln2', 'fadd', 'fsub', 'fmul', 'fdiv', 'fstp', 'fyl2x'):
                self.x87(m, ops, indent, x)
            else:
                raise Unknown(f'{x.address:08X}: {m} {x.op_str}')
            x = next(it)

    def check_balanced(self, x):
        rest = [v for v in self.stack if v not in (('return',),)]
        if rest:
            raise Unknown(f'{x.address:08X}: stack left with {rest}')

    def inlinable(self, target):
        return target not in self.registered and self.refs.get(target, 0) == 1

    def snapshot(self):
        return (len(self.lines), len(self.kinds), len(self.targets), len(self.inlined), dict(self.regs), list(self.stack),
                list(self.fpu), self.flag, self.local)

    def restore(self, saved):
        lines, kinds, targets, inlined, self.regs, self.stack, self.fpu, self.flag, self.local = saved
        del self.lines[lines:], self.kinds[kinds:], self.targets[targets:], self.inlined[inlined:]

    def memory(self, op):
        """(size, address) of an absolute operand or one through a register holding a known address."""
        m = MEM.match(op)
        if m:
            return m.group(1), int(m.group(2), 16)
        m = REGMEM.match(op)
        if m and REG32.get(m.group(2)) and m.group(2) in ('eax', 'ecx', 'edx', 'esi', 'edi'):
            base = self.regs.get(m.group(2))
            if base and base[0] == 'imm':
                return m.group(1), (base[1] + (int(m.group(4), 0) if m.group(4) else 0) * (-1 if m.group(3) == '-' else 1)) & 0xFFFFFFFF
        return None

    def value(self, op, x):
        if op in REG32:
            v = self.regs.get(REG32[op])
            if v is None and op in ('ebx', 'esi', 'edi', 'ebp'):
                return ('saved', op)   # a callee-saved register (or the frame pointer) pushed on entry
            if v is None:
                raise Unknown(f'{x.address:08X}: {op} is not known')
            return v
        if re.fullmatch(r'-?(0x[0-9a-f]+|\d+)', op):
            return ('imm', imm(op))
        mem = self.memory(op)
        if mem:
            return ('byte', mem[1]) if mem[0] == 'byte' else ('dword', mem[1])
        raise Unknown(f'{x.address:08X}: operand {op}')

    def store(self, size, address, v, indent, x):
        if size == 'byte':
            if v[0] == 'setbit' and v[1] == address:
                return   # the guard's own bit, set by d.Once
            raise Unknown(f'{x.address:08X}: byte store')
        if size != 'dword':
            raise Unknown(f'{x.address:08X}: {size} store')
        if v[0] == 'imm':
            self.emit(f'd.Store({hexa(address)}, {arg_lit(v[1])});', indent)
            self.kinds.append('store')
        elif v[0] == 'dword':
            self.emit(f'd.Store({hexa(address)}, d.Dword({hexa(v[1])}));', indent)
            self.kinds.append('copy')
        elif v[0] == 'result':
            if not self.lines or not self.lines[-1].endswith(';') or not self.lines[-1].lstrip().startswith(v[1]):
                raise Unknown(f'{x.address:08X}: a call result stored after other code')
            call = self.lines.pop().strip()[:-1]
            self.emit(f'd.Store({hexa(address)}, {call});', indent)
            self.kinds.append('store result')
        else:
            raise Unknown(f'{x.address:08X}: store of {v}')

    def call_site(self, target, through, cleanup, tail, indent, x):
        # Arguments: everything pushed since the last frame marker (or return address), last first.
        args = []
        for v in reversed(self.stack):
            if v[0] in ('frame', 'return', 'saved'):
                break
            args.append(v)
        if cleanup and cleanup != 4 * len(args):
            below = self.stack[len(self.stack) - len(args) - 1] if len(self.stack) > len(args) else None
            if cleanup > 4 * len(args) and not (below == ('frame',) and cleanup == 4 * len(args) + self.local):
                raise Unknown(f'{x.address:08X}: cleanup {cleanup} for {len(args)} argument(s)')
            args = args[:cleanup // 4]
        ecx = self.regs.get('ecx')
        if target == ATEXIT and through is None:
            if not cleanup or len(args) != 1 or args[0][0] != 'imm':
                raise Unknown(f'{x.address:08X}: atexit call of another shape')
            self.emit(f'd.AtExit({hexa(args[0][1])});', indent)
            self.kinds.append('atexit')
            self.targets.append(('exit', args[0][1]))
        else:
            if through == 'import':
                conv, head = 'import', f'd.Import({hexa(target)}'
            elif cleanup or (tail and not ecx):
                conv, head = 'cdecl', f'd.Cdecl({hexa(target)}'
            elif ecx and ecx[0] == 'imm':
                conv, head = 'thiscall', f'd.Thiscall({hexa(target)}, {hexa(ecx[1])}'
            else:
                conv, head = 'stdcall', f'd.Stdcall({hexa(target)}'
            text = head + ''.join(', ' + self.arg(a) for a in args) + ');'
            self.emit(text, indent)
            self.kinds.append(conv)
            self.targets.append(('call', target if through is None else None))
        # The callee pops its own arguments (stdcall, thiscall); the caller's pop / add follows.
        if not cleanup:
            del self.stack[len(self.stack) - len(args):]
        for r in ('ecx', 'edx'):
            self.regs.pop(r, None)
        prefix = self.lines[-1].strip()
        self.regs['eax'] = ('result', prefix[:prefix.index('(')])

    def x87(self, m, ops, indent, x):
        f = self.fpu

        def operand(op):
            mem = MEM.match(op)
            if not mem or mem.group(1) not in ('dword', 'qword'):
                raise Unknown(f'{x.address:08X}: x87 operand {op}')
            a = int(mem.group(2), 16)
            literal = rdata.literal_of(RdataView(self.img), a, 'float' if mem.group(1) == 'dword' else 'double')
            return literal or ('d.Float' if mem.group(1) == 'dword' else 'd.Double') + f'({hexa(a)})'

        if m == 'fldln2':
            f.append('ln2')
        elif m == 'fld':
            f.append(operand(ops[0]))
        elif m in ('fadd', 'fsub', 'fmul', 'fdiv'):
            sign = {'fadd': '+', 'fsub': '-', 'fmul': '*', 'fdiv': '/'}[m]
            if ops == ['st(0)', 'st(0)']:
                rhs = f[-1]
            elif len(ops) == 1:
                rhs = operand(ops[0])
            else:
                raise Unknown(f'{x.address:08X}: {m} {x.op_str}')
            f[-1] = f'{f[-1]} {sign} {rhs}'
        elif m == 'fyl2x':
            if len(f) < 2 or f[-2] != 'ln2':
                raise Unknown(f'{x.address:08X}: fyl2x of something else than ln 2')
            v = f.pop()
            f[-1] = f'd.Log({v})'
        elif m == 'fstp':
            mem = MEM.match(ops[0])
            if not mem or mem.group(1) != 'dword' or len(f) != 1:
                raise Unknown(f'{x.address:08X}: fstp {x.op_str}')
            self.emit(f'd.StoreFloat({hexa(int(mem.group(2), 16))}, {f.pop()});', indent)
            self.kinds.append('float')


KIND_NAMES = [   # what the summary calls an initialiser, by its operations (first match)
    ('registered', lambda k, n: k == ['cdecl'] and n['self']),
    ('guarded atexit', lambda k, n: k == ['once', 'atexit']),
    ('stores', lambda k, n: set(k) == {'store'}),
    ('float fold', lambda k, n: set(k) <= {'float', 'store'} and 'float' in k),
    ('constructor + atexit', lambda k, n: len(k) == 2 and k[0] in ('thiscall', 'stdcall', 'cdecl') and k[1] == 'atexit'),
    ('atexit', lambda k, n: k == ['atexit']),
    ('stores + atexit', lambda k, n: set(k[:-1]) <= {'store', 'copy'} and k[-1:] == ['atexit']),
    ('call', lambda k, n: len(k) == 1 and k[0] in ('thiscall', 'stdcall', 'cdecl', 'import')),
    ('copy', lambda k, n: set(k) == {'copy'}),
    ('empty', lambda k, n: not k),
]


def kind_of(kinds, registered_self):
    for name, test in KIND_NAMES:
        if test(kinds, {'self': registered_self}):
            return name
    return 'other'


def registered_addresses():
    """Addresses registered anywhere in src/ but in the exit functions this tool writes (static_exit.cpp)."""
    regs = short.ported_elsewhere(skip=(OUT_EXIT,))
    pat = re.compile(r'\bEI_REPLACE_SHORT\(\s*0x([0-9A-Fa-f]+)')
    text = open(os.path.join(ROOT, 'src', 'game', 'short_functions.cpp'), encoding='utf-8').read()
    return regs | {int(a, 16) for a in pat.findall(text)}


def decode_all(img):
    """[(address, kind, statements, targets, inlined, instruction spans)] in list order."""
    registered = registered_addresses()
    refs = references(img)
    interp = Interpreter(img, registered, refs)
    out, bad = [], []
    for slot in range(LIST_BEGIN, LIST_END + 4, 4):
        a = img.dword(slot)
        try:
            interp.decode(a)
        except Unknown as e:
            bad.append(f'{a:08X} (slot {slot:08X}): {e}')
            continue
        out.append((a, kind_of(interp.kinds, a in registered), list(interp.lines), list(interp.targets),
                    list(interp.inlined), sorted(interp.spans)))
    return out, bad


# ---- game.exe's data our code holds or nothing reads (C18j; docs/notes/standalone.md "Named data (C18)") -----------
#
# A store, float fold or fill into a range of config/named_data.csv (a named global of ours, tools/named_data.py) is
# left out: the definition's initialiser takes its place (named_data.py compares it with what game.exe's bytes and
# these stores leave, stored_values). A store into an object nothing reads (unread_objects, config/unread_data.csv) is
# left out too. A call, an atexit or an exit function that names such an object is not left out: named_data.py's
# check refuses its address literal, so the initialiser (or exit function) is ported by hand on the name and runs
# in list order through its registration.

NAMED_DATA = os.path.join(ROOT, 'config', 'named_data.csv')
UNREAD_DATA = os.path.join(ROOT, 'config', 'unread_data.csv')
UNREAD_HEADER = 'address,size,initialiser,dwords'
STORE_LINE = re.compile(r'^\s*d\.(Store|StoreFloat)\((0x[0-9A-F]{8}), (.*)\);$')
FILL_LINE = re.compile(r'^\s*d\.Fill\((0x[0-9A-F]{8}), (\w+), (\w+)\);$')
CALL = re.compile(r'^d\.(?:Cdecl|Stdcall|Thiscall|Import)\(')
READ = re.compile(r'd\.(?:Dword|Float|Double|NonZero)\((0x[0-9A-F]{8})\)')
DATA_SECTIONS = (0x00783000, 0x007CD000)   # .data and its zero-filled tail
UNREAD_KINDS = ('stores', 'float fold', 'copy')   # initialisers of plain constant objects
UNREAD_NEAR = 0x100   # an object referenced right below may be the enclosing one


def written_cells(line):
    """The dword addresses a store, float fold or fill statement writes (None for another statement)."""
    m = STORE_LINE.match(line)
    if m:
        return [int(m.group(2), 16)]
    m = FILL_LINE.match(line)
    if m:
        base = int(m.group(1), 16)
        return [base + 4 * i for i in range(int(m.group(3), 0))]
    return None


def named_ranges():
    """[(begin, end, name)] of config/named_data.csv."""
    if not os.path.exists(NAMED_DATA):
        return []
    rows = open(NAMED_DATA, encoding='utf-8').read().splitlines()[1:]
    return [(int(r.split(',')[0], 16), int(r.split(',')[0], 16) + int(r.split(',')[1]), r.split(',')[2]) for r in rows if r]


def stored_values(img, decoded):
    """{address: dword or None} of what the initialisers' stores, float folds and fills leave in game.exe's data, in
    list order (calls are not run: a call's stored result, a fold of ln or of an unknown value is None). A float fold
    is computed as the original does: x87 order at 53 bits (check_static_init), rounded to a float."""
    values = {}

    def dword(a):
        return values[a] if a in values else img.dword(a)

    def operand(text):
        m = rdata.TAG.fullmatch(text.strip())
        if m:
            single = m.group(3) or m.group(1).rstrip(')').endswith(('f', 'F'))
            return struct.unpack('<f' if single else '<d', img.read(int(m.group(2), 16), 4 if single else 8))[0]
        m = re.fullmatch(r'd\.Float\((0x[0-9A-F]{8})\)', text.strip())
        if m:
            v = dword(int(m.group(1), 16))
            return None if v is None else struct.unpack('<f', struct.pack('<I', v))[0]
        return None

    for _, _, lines, _, _, _ in decoded:
        for line in lines:
            m = STORE_LINE.match(line)
            if m and m.group(1) == 'Store':
                v = m.group(3)
                at = re.fullmatch(r'd\.(At|Dword)\((0x[0-9A-F]{8})\)', v)
                if at:
                    values[int(m.group(2), 16)] = int(at.group(2), 16) if at.group(1) == 'At' else dword(int(at.group(2), 16))
                elif CALL.match(v):
                    values[int(m.group(2), 16)] = None
                else:
                    values[int(m.group(2), 16)] = int(v, 0) & 0xFFFFFFFF
            elif m:
                parts = re.split(r' ([-+*/]) ', m.group(3))
                result = operand(parts[0])
                for op, text in zip(parts[1::2], parts[2::2]):
                    v = operand(text)
                    result = None if result is None or v is None else {'+': result + v, '-': result - v,
                                                                        '*': result * v, '/': result / v}[op]
                values[int(m.group(2), 16)] = None if result is None else struct.unpack('<I', struct.pack('<f', result))[0]
            elif FILL_LINE.match(line):
                f = FILL_LINE.match(line)
                for a in written_cells(line):
                    values[a] = int(f.group(2), 0) & 0xFFFFFFFF
    return values


def hand_literals():
    """The game data addresses src/ and tests/ name (comments blanked), but in the files this tool writes."""
    import named_data
    found = set()
    for rel, full in named_data.other_files():
        if os.path.normpath(os.path.join(ROOT, rel)) in (OUT, OUT_EXIT) or rel.endswith(('image_cells.inc', 'image_code_cells.inc')):
            continue
        with open(full, encoding='utf-8', errors='replace') as f:
            code = named_data.srcscan.blank_comments(f.read())
        found.update(int(m.group(1), 16) for m in named_data.LITERAL.finditer(code))
    return found


def unread_objects(img, decoded, exits):
    """[(begin, size, initialiser)] of the objects nothing but their initialiser reaches: each the contiguous stores
    of one initialiser that stores constants only (UNREAD_KINDS), into .data, whose bytes
      - no instruction of game.exe's code reaches but the initialisers' and their exit functions' own (an absolute
        operand inside the range; the same object read elsewhere in the original is not unread),
      - no address literal of src/ or tests/ names, no initialiser call or exit function takes, no initialiser reads,
      - no named range (config/named_data.csv) holds,
    and right below which (UNREAD_NEAR) no address referenced elsewhere starts an object the stores may lie inside.
    These are the per-file copies of header constants (zero vectors, identity rotations, 1 / 1, pi's folds) that
    the original's code never reads: nothing of ours needs them."""
    sizes = load_sizes()
    own = []
    for a, kind, _, _, _, spans in decoded:
        own += [(x, x + n) for x, n in spans] or [(a, a + sizes.get(a, 1))]
    own += [(fn, fn + size) for fn, _, _, size, _ in exits]
    own.sort()
    starts = [b for b, _ in own]

    def owned(at):
        i = bisect.bisect_right(starts, at) - 1
        return i >= 0 and own[i][0] <= at < own[i][1]

    _, va, vsz, rp, _ = next(s for s in img.sections if s[0] == '.text')
    text = img.data[rp:rp + vsz]
    referenced, foreign = set(), set()
    for k in range(4):
        n = (len(text) - k) // 4
        for i, (v,) in enumerate(struct.iter_unpack('<I', text[k:k + 4 * n])):
            if DATA_SECTIONS[0] <= v < DATA_SECTIONS[1]:
                referenced.add(v)
                if not owned(va + k + 4 * i):
                    foreign.add(v)
    touched = hand_literals()
    stored, groups = set(), []
    for a, kind, lines, _, _, _ in decoded:
        cells = []
        for line in lines:
            touched.update(int(x, 16) for x in READ.findall(line))
            if re.search(r'd\.(?:Cdecl|Stdcall|Thiscall|Import|AtExit)\(', line):
                touched.update(int(x, 16) for x in re.findall(r'0x([0-9A-F]{8})', line))
            written = written_cells(line)
            if written:
                m = STORE_LINE.match(line)
                cells += [(c, bool(m and CALL.match(m.group(3)))) for c in written]
        stored.update(c for c, _ in cells)
        run = []
        for c, call in sorted(cells) + [(None, False)]:
            if run and c == run[-1][0] + 4:
                run.append((c, call))
                continue
            if run:
                groups.append((a, kind, run))
            run = [(c, call)]
    for _, _, body, _, _ in exits:
        touched.update(int(x, 16) for x in re.findall(r'0x([0-9A-F]{8})', body))
    named = named_ranges()
    points = sorted(referenced | stored)
    out = []
    for a, kind, run in groups:
        lo, hi = run[0][0], run[-1][0] + 4
        if (kind not in UNREAD_KINDS or any(call for _, call in run) or not DATA_SECTIONS[0] <= lo < hi <= DATA_SECTIONS[1]
                or any(lo <= v < hi for v in foreign) or any(lo <= v < hi for v in touched)
                or any(b < hi and lo < e for b, e, _ in named)):
            continue
        i = bisect.bisect_left(points, lo) - 1
        below = points[i] if i >= 0 else 0
        if below in foreign and below not in stored and lo - below < UNREAD_NEAR:
            continue
        out.append((lo, hi - lo, a))
    return out


def unread_csv(img, decoded, unread):
    values = stored_values(img, decoded)
    rows = [UNREAD_HEADER]
    for lo, size, a in sorted(unread):
        rows.append(f'{lo:08X},{size},{a:08X},' + ' '.join(f'{values[c]:08X}' for c in range(lo, lo + size, 4)))
    return '\n'.join(rows) + '\n'


LEFT_OUT = {}   # initialiser -> number of statements left out (render's note)


def leave_out(decoded, ranges):
    """The initialisers without the stores, float folds and fills into `ranges` ([(begin, end)]); problems for a
    statement that writes across a range's end or stores a call's result there (port the initialiser by hand)."""
    LEFT_OUT.clear()
    out, problems = [], []
    for a, kind, lines, targets, inlined, spans in decoded:
        kept = []
        for line in lines:
            written = written_cells(line)
            inside = [c for c in written or () if any(b <= c < e for b, e in ranges)]
            if not inside:
                kept.append(line)
                continue
            m = STORE_LINE.match(line)
            if len(inside) != len(written):
                problems.append(f'{a:08X}: {line.strip()} writes across the end of a named or unread range')
            elif m and CALL.match(m.group(3)):
                problems.append(f'{a:08X}: {line.strip()} stores a call\'s result into a named range: port the '
                                'initialiser by hand on the name (EI_REPLACE at its address)')
            LEFT_OUT[a] = LEFT_OUT.get(a, 0) + 1
        out.append((a, kind, kept, targets, inlined, spans))
    return out, problems


def generate(img):
    """(initialisers with their named and unread stores left out, exit functions, unread objects, problems)."""
    decoded, bad = decode_all(img)
    if bad:
        return None, None, None, [f'not decoded (port by hand or teach the interpreter): {b}' for b in bad]
    exits, bad = decode_exits(img, decoded)
    if bad:
        return None, None, None, [f'not decoded (port by hand or add a pattern): {b}' for b in bad]
    unread = unread_objects(img, decoded, exits)
    ranges = [(b, e) for b, e, _ in named_ranges()] + [(lo, lo + n) for lo, n, _ in unread]
    kept, problems = leave_out(decoded, ranges)
    return kept, exits, unread_csv(img, decoded, unread), problems


# The exit functions the initialisers register with atexit, each a whole-function pattern of
# instructions as capstone prints them. {NAME} matches a number, the same wherever the name repeats;
# {@n} is the address of the pattern's n-th instruction (a branch inside the function), {@E} of its
# last. The C++ is the body of ExitFunction's case over static_init.h's operations (a guard is d.Once,
# as in the initialisers). A function that matches none stops the script: add a pattern or port it
# by hand (src/game/atexit_400.cpp).
GUARD_TEST = ['test byte ptr [{F}], {BIT}', 'jne {@E}', 'or byte ptr [{F}], {BIT}']
GUARD_MOV = ['mov cl, byte ptr [{F}]', 'mov al, {BIT}', 'test al, cl', 'jne {@E}', 'or cl, al', 'mov byte ptr [{F}], cl']
FREE = ['mov eax, dword ptr [eax - 4]', 'push eax', 'call {D}', 'pop ecx', 'ret']
EXIT_PATTERNS = [
    # MOV ECX, object; JMP destructor (a tail call); once only behind a guard bit.
    ('destructor', ['mov ecx, {OBJ}', 'jmp {T}'], 'd.Thiscall({T}, {OBJ});'),
    ('guarded destructor', GUARD_TEST + ['mov ecx, {OBJ}', 'jmp {T}', 'ret'], 'if (d.Once({F}, {BIT})) d.Thiscall({T}, {OBJ});'),
    ('guarded destructor', GUARD_MOV + ['mov ecx, {OBJ}', 'jmp {T}', 'ret'], 'if (d.Once({F}, {BIT})) d.Thiscall({T}, {OBJ});'),
    # Two calls on the object (a member's destructor, then the object's).
    ('two destructors', ['mov ecx, {OBJ}', 'call {A}', 'mov ecx, {OBJ}', 'jmp {T}'],
     'd.Thiscall({A}, {OBJ});\n        d.Thiscall({T}, {OBJ});'),
    # A global with nothing to destroy: RET.
    ('nothing', ['ret'], ''),
    # A buffer whose block address is kept in the dword before it: operator delete of that block, when set.
    ('free', ['mov eax, dword ptr [{P}]', 'test eax, eax', 'je {@E}'] + FREE,
     'if (d.NonZero({P})) d.Cdecl({D}, d.Dword(d.Dword({P}) - 4));'),
    ('guarded free', ['mov cl, byte ptr [{F}]', 'mov al, {BIT}', 'test al, cl', 'jne {@E}', 'mov dl, cl', 'or dl, al',
                      'mov eax, dword ptr [{P}]', 'test eax, eax', 'mov byte ptr [{F}], dl', 'je {@E}'] + FREE,
     'if (d.Once({F}, {BIT}) && d.NonZero({P})) d.Cdecl({D}, d.Dword(d.Dword({P}) - 4));'),
    # A counted pointer: the count at +4 goes down, at zero the deleting destructor (slot 0) with 1.
    ('release', ['mov ecx, dword ptr [{P}]', 'test ecx, ecx', 'je {@E}', 'mov eax, dword ptr [ecx + 4]', 'dec eax',
                 'mov dword ptr [ecx + 4], eax', 'jne {@E}', 'test ecx, ecx', 'je {@E}', 'mov eax, dword ptr [ecx]',
                 'push 1', 'call dword ptr [eax]', 'ret'],
     'd.Release({P});'),
    ('guarded release', ['mov cl, byte ptr [{F}]', 'mov eax, 1', 'test al, cl', 'jne {@E}', 'or cl, al',
                         'mov byte ptr [{F}], cl', 'mov ecx, dword ptr [{P}]', 'test ecx, ecx', 'je {@E}',
                         'mov edx, dword ptr [ecx + 4]', 'dec edx', 'mov dword ptr [ecx + 4], edx', 'jne {@E}',
                         'test ecx, ecx', 'je {@E}', 'mov edx, dword ptr [ecx]', 'push eax', 'call dword ptr [edx]', 'ret'],
     'if (d.Once({F}, 1)) d.Release({P});'),
    # A Win32 function with one argument, through the import address table.
    ('import', ['push {A}', 'call dword ptr [{S}]', 'ret'], 'd.Import({S}, {A});'),
    # A stdcall function with four arguments (the CRT's eh vector destructor iterator: array, element
    # size, count, the element's destructor).
    ('stdcall', ['push {A4}', 'push {A3}', 'push {A2}', 'push {A1}', 'call {T}', 'ret'],
     'd.Stdcall({T}, {A1}, {A2}, {A3}, {A4});'),
    # Waits while a global is set (a Win32 function through the import table, Sleep), then a tail call
    # with no arguments.
    ('wait, then call', ['mov eax, dword ptr [{W}]', 'test eax, eax', 'je {@E}', 'push esi', 'mov esi, dword ptr [{S}]',
                         'push {MS}', 'call esi', 'mov eax, dword ptr [{W}]', 'test eax, eax', 'jne {@5}', 'pop esi',
                         'jmp {T}'],
     'while (d.Dword({W})) d.Import({S}, {MS});\n        d.Cdecl({T});'),
]
PLACEHOLDER = re.compile(r'\\\{(@?\w+)\\\}')


def match_exit(img, address, md):
    """(pattern name, C++ body, size in bytes) of the exit function at `address`, or None."""
    for name, pattern, body in EXIT_PATTERNS:
        found, starts, at = {}, [], address
        for template in pattern:
            x = next(md.disasm(img.read(at, 16), at), None)
            names = PLACEHOLDER.findall(re.escape(template))
            m = x and re.fullmatch(PLACEHOLDER.sub(r'(0x[0-9a-f]+|\\d+)', re.escape(template)), f'{x.mnemonic} {x.op_str}'.strip())
            if not m or any(found.setdefault(n, int(v, 0)) != int(v, 0) for n, v in zip(names, m.groups())):
                break
            starts.append(at)
            at = x.address + x.size
        else:
            label = lambda n: starts[-1] if n == '@E' else starts[int(n[1:])]
            if all(v == label(n) for n, v in found.items() if n.startswith('@')):
                def value(m):
                    v = found[m.group(1)]
                    if m.group(1).startswith('A') and DATA[0] <= v < DATA[1]:
                        return arg_lit(v)   # an argument: a data address goes through the seam
                    return hexa(v) if v >= 0x10000 else lit(v)
                return name, re.sub(r'\{(\w+)\}', value, body), at - address
    return None


# The call sites of runtime_atexits' registrations (render_exits says "atexit at" for them).
RUNTIME_SITES = set()


def runtime_atexits(img, md):
    """{exit function: [call sites]} of the atexit registrations outside the initialisers (a function's
    local static: PUSH exit function, then CALL atexit within a few instructions, nothing pushed, called
    or branched to in between), by address."""
    text = img.read(TEXT[0], TEXT[1] - TEXT[0])
    found = {}
    for i in range(len(text) - 5):
        if text[i] != 0x68:
            continue
        fn = struct.unpack_from('<I', text, i + 1)[0]
        if not TEXT[0] <= fn < TEXT[1]:
            continue
        for n, x in enumerate(md.disasm(text[i + 5:i + 80], TEXT[0] + i + 5)):
            if n > 14 or x.mnemonic in ('push', 'ret') or x.mnemonic.startswith('j'):
                break
            if x.mnemonic == 'call':
                if x.op_str == hex(ATEXIT):
                    found.setdefault(fn, []).append(x.address)
                break
    return dict(sorted(found.items()))


def decode_exits(img, decoded):
    """[(address, pattern name, C++ body, size, [initialisers registering it])] of the exit functions the
    initialisers register that no rewrite registers, in the order of their first registration, then
    those registered at run time (function-local statics; S16a) by address, with their call sites; and
    the ones no pattern matches."""
    registered = registered_addresses()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    by = collections.OrderedDict()
    for a, _, _, targets, _, _ in decoded:
        for kind, fn in targets:
            if kind == 'exit' and fn not in registered and a not in by.setdefault(fn, []):
                by[fn].append(a)
    RUNTIME_SITES.clear()
    for fn, sites in runtime_atexits(img, md).items():
        if fn not in registered and fn not in by:
            by[fn] = sites
            RUNTIME_SITES.update(sites)
    out, bad = [], []
    for fn, initialisers in by.items():
        m = match_exit(img, fn, md)
        if m:
            out.append((fn, *m, initialisers))
        else:
            bad.append(f'exit function {fn:08X} (atexit by {initialisers[0]:08X}): no pattern matches')
    return out, bad


def render_exits(exits):
    kinds = collections.Counter(name for _, name, _, _, _ in exits)
    cases, wrappers, regs = [], [], []
    for fn, name, body, size, initialisers in exits:
        by = ', '.join(f'{a:08X}' for a in initialisers[:3]) + (f' and {len(initialisers) - 3} more' if len(initialisers) > 3 else '')
        how = 'atexit at' if initialisers[0] in RUNTIME_SITES else 'atexit by'
        cases.append(f'    case {hexa(fn)}:   // {name}, {size} byte(s); {how} {by}')
        if body:
            cases.append(f'        {body}')
        cases.append('        return true;')
        if name == 'nothing':   # under five bytes: registered as a short function (EI_REPLACE_SHORT)
            regs.append(f'EI_REPLACE_SHORT({hexa(fn)}, Ret);')
        else:
            wrappers.append(f'void __cdecl Exit{fn:08X}() {{ RunExitFunction({hexa(fn)}); }}')
            regs.append(f'EI_REPLACE({hexa(fn)}, ei::static_init::Exit{fn:08X});')
    summary = ', '.join(f'{n} {k}' for k, n in kinds.most_common())
    return (EXIT_HEADER.replace('@COUNT@', str(len(exits))).replace('@SUMMARY@', summary) + '\n'.join(cases) +
            EXIT_MIDDLE + '\n'.join(wrappers) + '\n\n} // namespace ei::static_init\n\nusing ei::short_functions::Ret;\n\n' +
            '\n'.join(regs) + '\n')


def load_sizes():
    """src/hook/function_sizes.inc as {address: size}."""
    return {int(a, 16): int(n) for a, n in re.findall(r'\{0x([0-9A-F]+), (\d+)\}', open(SIZES).read())}


def write_sizes(exits):
    """Adds the exit functions' sizes to config/extra_function_sizes.csv and src/hook/function_sizes.inc
    (InstallHooks compares a body with game.exe on disk, and the image needs room for the jump)."""
    sizes = load_sizes()
    rows = open(EXTRA_SIZES, encoding='utf-8').read().splitlines()
    listed = {int(r.split(',')[0], 16) for r in rows[1:] if r}
    for fn, _, _, size, _ in exits:
        if fn not in listed:
            rows.append(f'{fn:08X},{size},{EXTRA_NOTE}')
        sizes[fn] = size
    with open(EXTRA_SIZES, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(rows) + '\n')
    head = open(SIZES).readline()
    with open(SIZES, 'w', newline='\n') as f:
        f.write(head + ''.join(f'{{0x{a:08X}, {n}}},\n' for a, n in sorted(sizes.items())))


def render(decoded):
    kinds = collections.Counter(k for _, k, _, _, _, _ in decoded)
    body = []
    for a, kind, lines, _, inlined, _ in decoded:
        note = f'{a:08X}: {kind}' + (f' (inlined {", ".join(f"{t:08X}" for t in inlined)})' if inlined else '')
        if LEFT_OUT.get(a):
            note += f'; {LEFT_OUT[a]} left out'
        body.append(f'    // {note}')
        body.append(f'    d.Begin({hexa(a)});')
        body.extend('    ' + line for line in lines)
    summary = ', '.join(f'{n} {k}' for k, n in kinds.most_common())
    return HEADER.replace('@SUMMARY@', summary).replace('@COUNT@', str(len(decoded))) + '\n'.join(body) + FOOTER


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--exe')   # default: tools/exe_path.py
    args = ap.parse_args()
    args.exe = exe_path.resolve(args.exe)
    decoded, exits, unread, problems = generate(Image(args.exe))
    if problems:
        print('\n'.join(problems))
        return 1
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write(render(decoded))
    with open(OUT_EXIT, 'w', encoding='utf-8', newline='\n') as f:
        f.write(render_exits(exits))
    with open(UNREAD_DATA, 'w', encoding='utf-8', newline='\n') as f:
        f.write(unread)
    write_sizes(exits)
    print(f'{len(decoded)} static initialiser(s) written to {os.path.relpath(OUT, ROOT)} ({sum(LEFT_OUT.values())} '
          f'statement(s) left out), {len(exits)} exit function(s) to {os.path.relpath(OUT_EXIT, ROOT)}, '
          f'{len(unread.splitlines()) - 1} unread object(s) to {os.path.relpath(UNREAD_DATA, ROOT)}')
    return 0


HEADER = '''\
// Generated by tools/gen_static_init.py from game.exe's code. Do not edit.
//
// game.exe's @COUNT@ static initialisers (the C++ initialiser list in .data, 00783004-00783BB0), in list
// order (docs/notes/standalone.md, S3): @SUMMARY@.
// Stores, float folds and fills into game.exe's data that a named global of ours holds (config/named_data.csv: its
// definition's initialiser takes their place) or that nothing reads (config/unread_data.csv) are left out ("n left
// out"; docs/notes/standalone.md, "Named data (C18)").
// The standalone image runs them before WinMain (Run in static_init.h); the verify build compares the
// stores and float folds with what the original's initialisers left in memory (Check). Calls go to the
// callee's address in the image: our rewrite through its jump, or a library port.
// tools/check_static_init.py (build.cmd) checks this file against game.exe.
#include "static_init.h"

namespace ei::static_init {

template <class Do>
void StaticInitialisers(Do& d) {
'''

FOOTER = '''
}

template void StaticInitialisers<Run>(Run&);
template void StaticInitialisers<Check>(Check&);
template void StaticInitialisers<Targets>(Targets&);

} // namespace ei::static_init
'''

EXIT_HEADER = '''\
// Generated by tools/gen_static_init.py from game.exe's code. Do not edit.
//
// The @COUNT@ exit functions game.exe registers with atexit that no rewrite ports, the static
// initialisers' ("atexit by" the initialiser; docs/notes/standalone.md, S3a; by hand:
// src/game/atexit_400.cpp) and the function-local statics' ("atexit at" the call; S16a): @SUMMARY@.
// Each is decoded from its instructions as a whole-function pattern; ExitFunction states its body over
// static_init.h's operations. The replacements, registered with EI_REPLACE, run it through Code at exit.
// A library type's destructor is called at its address in the image (a library
// port, S4-S12; the standalone check logs the callees with no code yet).
// tools/check_static_init.py (build.cmd) checks this file and the functions' sizes against game.exe.
#include "static_init.h"

#include "ei/hook.h"

namespace ei::short_functions {
void Ret();   // src/game/short_functions.cpp
} // namespace ei::short_functions

namespace ei::static_init {

template <class Do>
bool ExitFunction(Do& d, std::uintptr_t fn) {
    switch (fn) {
'''

EXIT_MIDDLE = '''
    }
    return false;
}

template bool ExitFunction<Code>(Code&, std::uintptr_t);
template bool ExitFunction<Targets>(Targets&, std::uintptr_t);

namespace {
void RunExitFunction(std::uintptr_t fn) {
    Code d;
    ExitFunction(d, fn);
}
} // namespace

'''

if __name__ == '__main__':
    sys.exit(main())
