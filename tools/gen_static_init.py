"""Generates src/standalone/static_init.cpp: game.exe's 748 static initialisers as C++.

The C runtime runs the functions listed in .data at 00783004-00783BB0 (the C++ initialiser list,
__xc_a + 4 to __xc_z - 4) before WinMain. In game.exe the original runs them; the standalone
image (docs/notes/standalone.md section 2) has no code of game.exe and runs these instead, in list
order, before WinMain.

Each initialiser is decoded from its instructions by a small symbolic interpreter into operations:
dword stores (constants, copies, fills), float folds of .rdata constants (x87 sequences: + - * /,
ln), calls (cdecl, stdcall, thiscall, through an import slot) with their arguments, atexit
registrations, and the once-only guard bit (`if (flag & bit) skip; flag |= bit`). A call names the
target's port (C20 batch 3: nothing is called by address), unless the target is not ported anywhere, is
referenced from one place only and decodes itself: then it is inlined (the compiler's helper halves,
`call A; jmp B`). An initialiser registered by a rewrite (0042FAE0) is one call of its own name. Anything the interpreter does not know stops the script.

The result is one function, RunStaticInitialisers, that the standalone runs before WinMain: plain calls, atexit
registrations and the guard bit (static_init.h). Statements are written `d.Operation(...)` inside this script (the
analysis below matches them); `plain` turns them into the code that is written.

usage: python tools/gen_static_init.py [--exe game.exe] [--check-ports]
"""
import argparse, bisect, collections, csv, os, re, struct, sys

import capstone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import exe_path  # noqa: E402
import rdata_literals as rdata  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'src', 'standalone', 'static_init.cpp')
OUT_EXIT = os.path.join(ROOT, 'src', 'standalone', 'static_exit.cpp')
SIZES = os.path.join(ROOT, 'src', 'hook', 'function_sizes.inc')
EXTRA_SIZES = os.path.join(ROOT, 'config', 'extra_function_sizes.csv')
EXTRA_NOTE = 'exit function of a static initialiser (tools/gen_static_init.py; no Ghidra function)'
BASE = 0x400000
LIST_BEGIN, LIST_END = 0x00783004, 0x00783BB0   # inclusive
ATEXIT = 0x006E727B
TEXT = (0x00401000, 0x0073A670)
LOCAL_MAX = 0x400   # src/standalone/static_init.h, Run::frame


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
    the original's 53 bits and in float alike, so the compiler may fold them."""

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
# data address (written d.At here; only a left-out statement has one: static_init.h has no such function, S18).
DATA = (0x0073B000, 0x007D7000)


_IMPORTS = {}


def import_ref(slot):
    """An import slot as the generated code names it: its member of lib/import_table.h's ImportTable."""
    if not _IMPORTS:
        import gen_iat
        _IMPORTS.update({a: gen_iat.member(dll, fn, o) for a, dll, fn, o in gen_iat.imports(exe_path.resolve())})
    return f'&ei::ImportTable::{_IMPORTS[slot]}'


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
        self.shadow = None
        if address in self.registered:
            # Ported by hand (C18k): one call to its own address. What its instructions do is still decoded, for
            # unread_objects only (SHADOW: the objects it stores into and the exit function it registers).
            try:
                self.run(address, None, '')
                self.shadow = (list(self.lines), sorted(self.spans), list(self.targets))
            except Unknown:
                pass
            self.lines, self.kinds, self.targets, self.inlined = [], [], [], []
            self.spans = set()
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
                    raise Unknown(f'{x.address:08X}: a stack frame over {LOCAL_MAX:#x} bytes (Run::frame)')
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
                conv, head = 'import', f'd.Import({import_ref(target)}'
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
    ('ported by hand', lambda k, n: k == ['cdecl'] and n['self']),
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
    """The functions ported by hand that the initialisers reach (config/static_init_ports.csv): never inlined or
    decoded, called by name."""
    return set(ports())


SHADOW = {}   # initialiser ported by hand -> (statements, instruction spans, targets) its instructions decode to


def decode_all(img):
    """[(address, kind, statements, targets, inlined, instruction spans)] in list order."""
    registered = registered_addresses()
    refs = references(img)
    interp = Interpreter(img, registered, refs)
    out, bad = [], []
    SHADOW.clear()
    for slot in range(LIST_BEGIN, LIST_END + 4, 4):
        a = img.dword(slot)
        try:
            interp.decode(a)
        except Unknown as e:
            bad.append(f'{a:08X} (slot {slot:08X}): {e}')
            continue
        out.append((a, kind_of(interp.kinds, a in registered), list(interp.lines), list(interp.targets),
                    list(interp.inlined), sorted(interp.spans)))
        if interp.shadow:
            SHADOW[a] = interp.shadow
    return out, bad


# ---- game.exe's data our code holds or nothing reads (C18j; docs/notes/standalone.md "Named data (C18)") -----------
#
# A store, float fold or fill into a range of config/named_data.csv (a named global of ours, tools/named_data.py) is
# left out: the definition's initialiser takes its place (named_data.py compares it with what game.exe's bytes and
# these stores leave, stored_values). A store into an object nothing reads (unread_objects, config/unread_data.csv) is
# left out too. A call, an atexit or an exit function that names such an object is not left out: named_data.py's
# check refuses its address literal, so the initialiser (or exit function) is ported by hand on the name and runs
# in list order through its registration, or the generated code names the object (GENERATED_NAMES, below).

NAMED_DATA = os.path.join(ROOT, 'config', 'named_data.csv')

# The named globals this script writes by name (C18u): address -> (C++ name, the header that declares it, None for
# static_init.h, which the generated files include anyway). Each is
# the start of a range of config/named_data.csv that only the generated code reaches. The name takes the address's
# place where the original's code has it: a guard's flag byte (`d.Once(name, bit)`), the object a call takes as
# `this` (`d.Thiscall(fn, &name, ...)`) and where a call's result goes (`d.Store(name, d.Stdcall(...))`, kept in list
# order: named_data.py compares that definition at its zero start). The generated files include the headers.
GENERATED_NAMES = {
    0x007CAEE4: ('g_emptyStaticGuard', None),   # static_init.h's, in the generated code's namespace
}
NAMED_ONCE = re.compile(r'\bd\.Once\((0x[0-9A-F]{8}),')
NAMED_THIS = re.compile(r'(\bd\.Thiscall\(0x[0-9A-F]{8}, )(0x[0-9A-F]{8})\b')
NAMED_RESULT = re.compile(r'\bd\.Store\((0x[0-9A-F]{8})(?=, d\.(?:Cdecl|Stdcall|Thiscall|Import)\()')


def name_operands(text, headers):
    """`text` with GENERATED_NAMES' addresses written as their names; adds the headers it needs to `headers`."""
    def named(m, group, form):
        entry = GENERATED_NAMES.get(int(m.group(group), 16))
        if entry is None:
            return m.group(0)
        if entry[1]:
            headers.add(entry[1])
        start, end = m.span(group)
        return m.group(0)[:start - m.start()] + form.format(entry[0]) + m.group(0)[end - m.start():]
    text = NAMED_ONCE.sub(lambda m: named(m, 1, '{}'), text)
    text = NAMED_THIS.sub(lambda m: named(m, 2, '&{}'), text)
    return NAMED_RESULT.sub(lambda m: named(m, 1, '{}'), text)


def includes(headers):
    return ''.join(f'#include "{h}"\n' for h in sorted(headers))
UNREAD_DATA = os.path.join(ROOT, 'config', 'unread_data.csv')
UNREAD_HEADER = 'address,size,initialiser,dwords'
STORE_LINE = re.compile(r'^\s*d\.(Store|StoreFloat)\((0x[0-9A-F]{8}), (.*)\);$')
FILL_LINE = re.compile(r'^\s*d\.Fill\((0x[0-9A-F]{8}), (\w+), (\w+)\);$')
CALL = re.compile(r'^d\.(?:Cdecl|Stdcall|Thiscall|Import)\(')
READ = re.compile(r'd\.(?:Dword|Float|Double|NonZero)\((0x[0-9A-F]{8})\)')
DATA_SECTIONS = (0x00783000, 0x007CD000)   # .data and its zero-filled tail
UNREAD_KINDS = ('stores', 'float fold', 'copy')   # initialisers of plain constant objects
UNREAD_NEAR = 0x100   # an object referenced right below may be the enclosing one

# The groups UNREAD_NEAR would keep out that were read against the object right below (C18m): the object ends before
# the group (its extent from the ported class's size, the ctor / dtor or the code's accesses; named_data.py --refs
# lists them), so the stores cannot be one of its fields. address of the group -> (the object below, why).
REVIEWED_NEAR = {
    0x0079B1A8: ('0079B1A0', '"sqrt", the CRT math function name (8 bytes)'),
    0x0079B2A0: ('0079B298', 'the empty string "" (its 178 readers take its address: a char)'),
    0x0079B610: ('0079B60C', 'the player serial counter (dword read and written directly)'),
    0x0079B680: ('0079B66C', 'the clip work buffer: CWordArray 0079B668, 0x14 bytes, its data pointer'),
    0x007C7E80: ('007C7E78', 'the window surfaces\' byte total (a lone dword, ui.h\'s g_uiSurfaceBytes)'),
    0x0079B690: ('0079B68C', 'the clip buffer count (dword)'),
    0x0079B978: ('0079B930', 'the debug panel (CDebugPanel, 0x44 bytes; deleted in C55)'),
    0x0079BC30: ('0079BC2C', 'the protection check list (an STLport list: one head pointer)'),
    0x0079BEE0: ('0079BED8', 'the options 0079BC70 (GameOptions, 0x26C bytes) end at 0079BEDC; 0079BED8 is its master server CString'),
    0x0079C1C0: ('0079C1AC', 'the lever table 0079C1A0 (0x20 bytes); 0079C1AC is a member'),
    0x007A9698: ('007A9694', 'the lightning noise loaded flag (dword)'),
    0x007A9750: ('007A9748', 'the sort scratch guard byte (the scratch array is below it)'),
    0x007A9788: ('007A9784', 'the select timeout 007A9778 (8 bytes) and the snapshot flags 007A9780 / 007A9784 (dwords)'),
    0x007A9BA8: ('007A9BA4', 'a dword read directly'),
    0x007A9C2C: ('007A9BFC', "the bag's ball sizes (12 floats, particle_emitters.cpp's g_bagSizes) end at 007A9C2C"),
    0x007AA5E8: ('007AA5D8', 'the party manager 007AA5A0 (PartyManager, 0x44 bytes); 007AA5D8 is its class template vector'),
    0x007AA624: ('007AA620', 'the saving script pointer (dword; the CString 007AA61C below it is 4 bytes)'),
    0x007AA880: ('007AA868', 'a decal vertex (DecalVertex, 0x14 bytes)'),
    0x007AB8F0: ('007AB8E8', "the hash_set 007AB8D8 (0x14 bytes; 007AB8E8 its element count) ends at 007AB8EC"),
    0x007ABB68: ('007ABB64', 'the fog dead cells vector 007ABB58 (12 bytes) and its guard byte'),
    0x007AEE68: ('007AEE64', 'the DirectDraw driver list 007AEE64 (gone; its unused initialiser 004ED520 still reads the cell)'),
    0x007AEE98: ('007AEE94', 'the device lock 007AEE78 (CRITICAL_SECTION, 0x18 bytes) and the holder count (dword)'),
    0x007AF3D8: ('007AF3BC', 'the unit database record tables 007AF358 / 007AF378 / 007AF398 / 007AF3B8 (RecordTable, 0x20 bytes each)'),
    0x007AF410: ('007AF40C', 'the squared radius float (read directly)'),
    0x007AFD9C: ('007AFCAC', 'the client world 007AF5B8 (CWorldClient, 0x7E4 bytes) ends at 007AFD9C; 007AFCAC is its rain / snow layer'),
    0x007AFEA8: ('007AFEA6', 'three empty one-byte objects 007AFEA4-007AFEA6 (their `this` is never read)'),
    0x007C2DDC: ('007C2D60', 'the received memory stream (CMemoryStream, 0x28 bytes)'),
    0x007C2E78: ('007C2E70', 'the part draw guard byte (the moved positions array 007C2E60 is below it)'),
    0x007C2F28: ('007C2F24', 'the flood queue guard byte (the queue 007C2F10 is below it)'),
    0x007C54E8: ('007C54E4', "the guard byte of 005D2390's function-local static 007C54C8 (0x1C bytes, below it)"),
    0x007C5530: ('007C552C', 'the loading movie screen RECT 007C5520 (16 bytes)'),
    0x007C5568: ('007C5560', 'a flag byte (read directly)'),
    0x007C7B44: ('007C7B38', 'the cheat command map (an STLport map, 0xC bytes)'),
    0x007C7B70: ('007C7B68', 'the console message filter mask (dword)'),
    0x007C7E00: ('007C7DD8', 'the interface manager 007C7D50 (CInterface3D, 0xB0 bytes) ends at 007C7E00; 007C7DD8 is a flag byte of it'),
    0x007C7E28: ('007C7E24', 'the screenshot counter (dword)'),
    0x007C80F8: ('007C80E8', 'the menus rectangles 007C8048 (11 RECTs, 0xB0 bytes: the LoadMenuRects offsets 0 to 0xA0)'),
    0x007C8120: ('007C811C', 'the next lightning tick (dword)'),
    0x007C8328: ('007C8318', 'a CString (4 bytes) after the party variables 007C8304 (NamedFloatTable, 0x14 bytes)'),
    0x007C8348: ('007C8344', 'the party being loaded (pointer)'),
    0x007C8384: ('007C8370', 'the party id set (a hash_set: 0x14 bytes)'),
    0x007C83F8: ('007C83D5', 'the received stream 007C83D0 (CMemoryStream, 0x28 bytes)'),
    0x007C84CC: ('007C8478', 'the spell footprint set (FootprintCellSet, 0x54 bytes)'),
    0x007C84F0: ('007C84EC', 'the projectile world being loaded (pointer)'),
    0x007C8578: ('007C8568', 'the spell database 007C8510 (SpellDatabase, 0x64 bytes) ends at 007C8574; 007C8568 is its recipe vector'),
    0x007C8798: ('007C877C', 'the item database 007C86D8 (ItemDatabase, 0xC0 bytes) ends at 007C8798; 007C877C is in its loot table'),
    0x007C87E8: ('007C87D4', 'the quest database 007C87A8 (QuestDatabase, 0x40 bytes) ends at 007C87E8; 007C87D4 is its briefing map'),
    0x007C8A28: ('007C8A0C', 'the prints database 007C89C8 (PrintsDatabase, 0x60 bytes) ends at 007C8A28'),
    0x007C8AC0: ('007C8AA4', 'the speech database 007C8A60 (AckDatabase, 0x60 bytes) ends at 007C8AC0'),
}


# The groups the original's code reads that are unread objects all the same (C18m): each read is in code of ours that is
# ported with a literal (src/ names no address of the group: `touched` still checks), is no operand, or is a loop bound
# that ends at the group. address of the group -> why. (Reads inside config/dead_code.csv do not count at all.)
REVIEWED_READ = {
    0x007A9678: 'LoadLightningNoise (ported with a row count): 00468BB0 compares a pointer with the noise table end',
    0x007A9C2C: 'EmitBag (ported, 00493560): 00493591 compares a pointer with the bag sizes end',
    0x007C53E0: '00683868 is the middle of a call displacement (coincidence); 005C8AE2.. are the initialiser own stores',
    0x007C7E80: '0070B443 is the middle of a CMP [reg + 0x7C], 0 (coincidence); 00600AF2.. are the initialiser own stores',
    0x007C8430: 'CEffectMoshka::Update (ported, 0067CEF0): pi / 2 is its kHalfPi literal',
    0x007AA890: 'CSky::Draw (ported): 004D9841 / 004D9846 push the identity rotation and the origin as the figure '
                'draw arguments nothing read; ours passes neither (R27c)',
    0x007AA8A0: 'CSky::Draw (ported): see 007AA890',
    # C50d1: the console log pointers; ours write to the streams by name (console::Stream& constants).
    0x007AF284: "the hit log pointer (the rpg stream): unit_hits.cpp's kHitLog",
    0x007AF3E8: "the combat log pointer (the AI stream): combat_internal.h's kCombatLog",
    0x007C2EB8: "the alarm log pointer (the unitacks stream): ai_alarm.cpp's kAlarmLog",
    0x007C53C4: "the AI's perception log pointer (the AI stream): unit_ai.h's kAiLog",
    0x007C54E8: "the modes' log pointer (the AI stream): unit_ai.cpp's kModeLog",
}


# Where one initialiser's contiguous stores are two objects (C18): the run is split before the address, so the cells
# from it on are a group of their own (the cells below stay a group, read or not). address -> (the object below, why).
REVIEWED_SPLIT = {
    0x007AF288: ('007AF284', 'the hit log pointer (dword; 005229C0 stores the console stream 007C5708 in it) is read alone; '
                 'ln 1.61 / ln 1.1 above it are copies of the logic settings defaults nothing reads'),
}


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
    is computed as the original does: x87 order at 53 bits, rounded to a float."""
    values = {}

    def dword(a):
        return values[a] if a in values else img.dword(a)

    def operand(text):
        text = text.strip()
        if text.startswith('(') and text.endswith(')') and text.count('(') == 1:   # a negative literal: (-0.3f /*addr*/)
            text = text[1:-1]
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


def dead_code():
    """[(begin, end)] of config/dead_code.csv, sorted and merged: functions nothing reaches (C18m: the reads of the original's dead
    code do not keep an object from being unread; docs/notes/standalone.md)."""
    rows = []
    with open(os.path.join(ROOT, 'config', 'dead_code.csv'), newline='', encoding='utf-8') as f:
        for r in csv.DictReader(f):
            a = int(r['address'], 16)
            rows.append((a, a + int(r['size'])))
    merged = []
    for b, e in sorted(rows):
        if merged and b <= merged[-1][1]:
            merged[-1] = (merged[-1][0], max(e, merged[-1][1]))
        else:
            merged.append((b, e))
    return merged


def hand_literals():
    """The game data addresses src/ and tests/ name (comments blanked), but in the files this tool writes."""
    import named_data
    found = set()
    for rel, full in named_data.other_files():
        if os.path.normpath(os.path.join(ROOT, rel)) in (OUT, OUT_EXIT) or rel.endswith('image_cells.inc'):
            continue
        with open(full, encoding='utf-8', errors='replace') as f:
            code = named_data.srcscan.blank_comments(f.read())
        found.update(int(m.group(1), 16) for m in named_data.LITERAL.finditer(code))
    return found


STALE_REVIEWED = []   # REVIEWED_NEAR entries that no longer fit (unread_objects, then generate)
UNREAD_EXITS = {}   # initialiser -> the exit function left out with its unread object (C18k)

# Initialisers left out whole, with their exit function: they build and register the destruction of an object
# no code of ours reaches (C20o: MFC's fixed allocators of the temporary windows and menus, whose classes
# CTempWnd and CTempMenu are never made; C14i: CString's four allocators, CString being std::string, and
# CStringArray's runtime class, linked into MFC's class list by AfxClassInit, the class being gone; C14d:
# _simpleNotSupportedException, which only CStdioFile threw). Each is the
# constructor call and the atexit (or the one call); the object's range and the exit function's guard byte are in no
# named range or source, and nothing reads them.
UNUSED_INITIALISERS = {
    # C50b3: the options are a plain global of ours.
    0x00458700: "the options 0079BC70 (exit function 00458720): options_construct.cpp's g_options",
    # C50b2: the camera and the scene renderer are plain globals of ours.
    0x0040C880: "the camera 0079B2A8 (exit function 0040C8A0): camera_object.cpp's g_camera",
    0x00500930: "the scene renderer 007AF060 (exit function 00500950): renderer_lifetime.cpp's g_sceneRenderer",
    # C50b1: the worlds are plain globals of ours (GameMain registers EmptyWorlds for the exit order).
    0x00560620: "the client world 007AF5B8 (exit function 00560640): world_construct.cpp's g_clientWorld",
    0x00589BE0: "the server world 007AFF60 (exit function 00589C00): world_construct.cpp's g_serverWorld",
    # C54g1: the party manager, the flow, the save stream and the saved parties are plain globals of ours.
    0x004A56D0: "the party manager 007AA5A0 (exit function 004A56F0): app_lifetime.cpp's g_partyManager",
    0x00578440: "the flow 007AFE20 (exit function 005784C0): world_game.cpp's g_gameFlow",
    0x00578530: "the save stream 007AFDF8 (exit function 00578550): world_game.cpp's g_saveStream",
    0x0065F2A0: "the saved parties 007C82A0 (exit function 0065F2C0): party_file.cpp's g_savedParties",
    # C50c1 / C50e: the databases the interface reads and the disc / data folders are plain globals of ours.
    0x0045E6C0: "the map database 0079BF00 (exit function 0045E6E0): map_database.cpp's g_mapDatabase",
    0x0051FAF0: "the skill database 007AF268 (exit function 0051FB40): unit_db_globals.cpp's g_skillDatabase",
    0x005399F0: "the unit database 007AF358 (exit function 0053A040): unit_db_globals.cpp's g_unitDatabase",
    0x0055E7E0: "the data folder 007AF460 (exit function 0055E800, bit 2 of 007AF438): game_folders.cpp's g_dataFolder",
    0x0055E860: "the disc folder 007AF458 (exit function 0055E880, bit 4 of 007AF438): game_folders.cpp's g_discFolder",
    0x00684360: "the spell database 007C8510 (exit function 00684380): database_globals.cpp's g_spellDatabase",
    0x006A6540: "the item database 007C86D8 (exit function 006A6560): database_globals.cpp's g_itemDatabase",
    0x006ADF10: "the quest database 007C87A8 (exit function 006ADF30): database_globals.cpp's g_questDatabase",
    0x006D7080: "the speeches database 007C8A60 (exit function 006D70A0): database_globals.cpp's g_ackDatabase",
    # C54g: the cursor and the interface manager are plain globals of ours.
    0x005E4DD0: "the cursor 007C7BD8 (exit function 005E4DF0): ui_cursor_setup.cpp's g_cursor",
    0x005F4680: "the interface manager 007C7D50 (exit function 005F4780): ui_small_members.cpp's g_interface",
    # C50a: plain globals of ours (constructed and destroyed by the C++ runtime) in place of the static objects.
    0x005B31C0: "the unit shape cache 007C2F70 (exit function 005B31E0): ai_movement.cpp's g_unitShapes",
    0x005BF810: "the sector spiral's offsets 007C2FF8 (exit function 005BF830): sector_spiral.cpp's g_spiralOffsets",
    0x005BF840: "the sector spiral's radius tree 007C2FD8 (exit function 005BF860): sector_spiral.cpp's g_spiralRadii",
    0x005BF8B0: "the sector spiral's lengths 007C2FC0 (exit function 005BF8D0): sector_spiral.cpp's g_spiralLengths",
    0x005C41C0: "the path smoother's 81 tables 007C3058 (exit function 005C41F0): ai_path_smooth.cpp's g_smoothTables",
    0x005C4460: "the path smoother's node pool 007C3038 (exit function 005C4480): ai_path_smooth.cpp's g_smoothPool",
    0x005C8F90: "the AI events' pool 007C43A8 (exit function 005C8FB0): ai_events.cpp's g_eventPool",
    # C50a1: the AI's tables are built on first use (sector_spiral.cpp's SectorSpiralTables, ai_path_smooth.cpp's
    # SmoothTables).
    0x005BF8E0: "the sector spiral's builder 005BF8F0: sector_spiral.cpp's SectorSpiralTables",
    0x005C42A0: "the path smoother's table builder 005C42B0: ai_path_smooth.cpp's SmoothTables",
    0x0067E4C0: "the spells' footprint set 007C8478 (exit function 0067E500): ai_footprint.cpp's g_spellFootprint",
    # C50b: plain globals of ours in place of the static objects.
    0x004630F0: "the lever table 0079C1A0 (exit function 00463110): lever_table.cpp's g_leverTable",
    0x004B17C0: "the quest's objectives 007AA608 (exit function 004B17E0): script_exec_helpers.cpp's "
                "g_scriptQuestObjectives",
    0x004B1790: "the quest's name 007AA61C (exit function 004B17B0): script_exec_helpers.cpp's g_scriptQuestName",
    0x0066C4D0: "the received messages' stream 007C83D0 (exit function 0066C520, guard bit 0 of 007C83C8): "
                "player_server_handlers.cpp's g_receivedMessages",
    0x00668B40: "the client's party id set 007C8370 (exit function 00668B60): player_client.cpp's g_partyIdSet",
    0x0056A3F0: "the frame's deferred units 007AF5B4 (exit function 0056A4C0): world_client_view.cpp's g_deferredUnits",
    0x00455E30: "the copy protection's check list 0079BC2C (exit function 00455F00): nothing fills it, so "
                "DelayedProtectionCheck (app_helpers.cpp) only draws its number",
    0x0057F460: "the zone table 007AFDE0 (exit function 0057F4A0): world_game.cpp's g_zoneTable",
    0x00580980: "the quest text files 007AFDD0 (exit function 005809C0): world_briefing.cpp's g_questFiles",
    0x00581050: "the quest folders 007AFDC0 (exit function 00581090): world_briefing.cpp's g_questFolders",
    # C50b: the draw work arrays are plain globals of ours.
    0x00498910: "the particles' draw order 007A9C50 (exit function 00498930): particle_draw.cpp's g_drawOrder",
    0x0051CBD0: "the shadow's ground quad 007AF208 (exit function 0051CC00): map_object_shadow.cpp's g_quads",
    0x0051CDA0: "the shadow's projected points 007AF1F8 (exit function 0051CDD0, guard bit 0 of 007AF1F0): "
                "map_object_shadow.cpp's g_points",
    # C50b: the frame's draw lists are plain globals of ours (world_client_view.cpp).
    0x0056A4D0: "the draw list 007AF5A8 (exit function 0056A510): world_client_view.cpp's g_visibleBuildings",
    0x0056A520: "the draw list 007AF598 (exit function 0056A560): world_client_view.cpp's g_visibleOther",
    0x0056A570: "the draw list 007AF588 (exit function 0056A5B0): world_client_view.cpp's g_visibleLevers",
    0x0056A5C0: "the draw list 007AF578 (exit function 0056A600): world_client_view.cpp's g_visibleUnits",
    0x0056A610: "the draw list 007AF568 (exit function 0056A650): world_client_view.cpp's g_visibleTrees",
    0x0056A660: "the draw list 007AF558 (exit function 0056A6A0): world_client_view.cpp's g_shadowBuildings",
    0x0056A6B0: "the draw list 007AF548 (exit function 0056A6F0): world_client_view.cpp's g_shadowOther",
    0x0056A700: "the draw list 007AF538 (exit function 0056A740): world_client_view.cpp's g_shadowLevers",
    0x0056A750: "the draw list 007AF528 (exit function 0056A790): world_client_view.cpp's g_shadowUnits",
    0x0056A7A0: "the draw list 007AF518 (exit function 0056A7E0): world_client_view.cpp's g_shadowTrees",
    0x0056A7F0: "the draw list 007AF508 (exit function 0056A830): world_client_view.cpp's g_touchedModels",
    0x00444660: "the debug panel 0079B930 (exit function 00444680; the panel went in C55)",
    0x0051CE00: "DrawShadow 0051CE60's texture pixels 007AF1E0 (exit function 0051CE30, guard bit 2 of 007AF1F0): only "
                "the 64 x 64 shadow texture read them, which is not made (R26)",
    0x0070D3CE: 'CTempWnd and its allocator 007CA940 (guard byte 007C9138)',
    0x0070DD0B: 'CTempMenu and its allocator 007CAAA8 (guard byte 007CAAA0)',
    0x00700718: "CString's allocator of 64 characters 007C8B80",
    0x00700747: "CString's allocator of 128 characters 007C8B58",
    0x00700779: "CString's allocator of 256 characters 007C8B30",
    0x007007AB: "CString's allocator of 512 characters 007C8B08",
    0x0070BE81: "CStringArray's runtime class 00796EE8, linked into the module state's class list",
    0x0070BEC9: "CWordArray's runtime class 00796F20, linked into the module state's class list (the class went in "
                "C14zb)",
    0x0070C130: "CDWordArray's runtime class 00797140, linked into the module state's class list (the class went in "
                "C14zb)",
    0x0070C17E: "CByteArray's runtime class 00797198, linked into the module state's class list (the class went in "
                "C14zb)",
    0x0070C1ED: "MFC's _simpleNotSupportedException 007C8D50 (only CStdioFile threw it; the game's text files are "
                "lib::TextFile since C14d)",
    0x006D4A70: "CDib's runtime class 00796CD0, linked into the module state's class list (nothing asks a CDib its "
                "class since C14ze)",
    0x0070C093: "MFC's fixed allocator of the temporary DCs 007C8D18 (guard byte 007C8BB8, bit 1; no CDC since C14ze: "
                "the interface draws on plain HDCs)",
    0x0070C0D3: "MFC's fixed allocator of the temporary GDI objects 007C8CF0 (guard byte 007C8BB8, bit 2; no "
                "CGdiObject since C14ze)",
    0x00704410: "MFC's static window wndTop 007CAA28 (a CWnd nothing reads; the class went in C14zh; guard bit 1 of "
                "007CAA24)",
    0x0070444E: "MFC's static window wndBottom 007CA9E8 (guard bit 2 of 007CAA24)",
    0x0070448C: "MFC's static window wndTopMost 007CA9A8 (guard bit 4 of 007CAA24)",
    0x007044CA: "MFC's static window wndNoTopMost 007CA968 (guard bit 8 of 007CAA24)",
    # C14zi: MFC's static state nothing of ours reads any more (no CWnd, threads, handle maps or MFC exceptions).
    0x0070BEFA: "MFC's _simpleResourceException 007C8C58 (nothing of ours throws or reads it)",
    0x0070BF32: "MFC's _simpleUserException 007C8BC0 (only AfxThrowUserException threw it, not in our source)",
    0x0070C1B5: "MFC's _simpleMemoryException 007C8DE8 (nothing of ours throws or reads it)",
    0x0070B5FB: "wingdix.cpp's exit function for the halftone brush 007C8D40 (made only by code the game never "
                "reaches)",
    0x0070D12E: "MFC's afxData 007C90C8 (AUX_DATA: system metrics, colours and cursors only MFC's windows read)",
    0x007043FA: "MFC's DragList message id 007CAA64 (CWnd's message map entry; the class went in C14zh)",
    0x0070DC70: "MFC's _afxDBCS 007CAA98 (read only by _AfxCbtFilterHook: no game window is a CWnd)",
    0x0070CC0C: "the exit function of MFC's thread local _afxThreadState 007C90C0 (nothing fills it)",
    0x0070CE56: "the exit function of MFC's process local _afxBaseModuleState 007C90BC (nothing fills it)",
    0x0070D5D8: "the exit function of MFC's thread local _afxCtl3dThread 007CAA84 (nothing fills it)",
    0x0070D5FE: "the exit function of MFC's process local _afxCtl3dState 007CAA88 (nothing fills it)",
    0x0070DC27: "the exit function of MFC's process local _afxWinState 007CAA8C (nothing fills it)",
    0x004ED520: "the DirectDraw driver list 007AEE64 (never filled or read; its guard byte 007AEE60)",
    0x005DA360: "the loading movie's surface holder 007C551C (exit function 005DA380, guard byte 007C5518): the movie "
                "players draw through the new renderer without a holder (R27f)",
    # C50c: plain globals of ours in place of the static objects.
    0x006D00B0: "the prints database 007C89C8 (exit function 006D00D0): database_globals.cpp's g_printsDatabase",
    0x00698E50: "the trader stock's taken prototypes 007C8680 (exit function 00698E90): locals of item_stock.cpp's "
                "generators",
    0x00698EA0: "the trader stock's taken spells 007C8670 (exit function 00698EE0): a local of "
                "GenerateMagicInstructions (item_stock.cpp)",
    0x00698EF0: "the trader stock's taken modifiers 007C8660 (exit function 00698F30): a local of "
                "GenerateMagicInstructions (item_stock.cpp)",
    0x00698F40: "the trader stock's material candidates 007C8650 (exit function 00698F80): a local of "
                "ItemStock::GenerateItems (item_stock.cpp)",
    0x00698F90: "the trader stock's taken materials 007C8640 (exit function 00698FD0): a local of "
                "ItemStock::GenerateMaterials (item_stock.cpp)",
    0x00432CA0: "the network's send buffer 0079B900 (exit function 00432CC0, bit 0 of 0079B8FC): net_session.cpp's "
                "g_sendBuffer",
    0x00432CE0: "the network's receive buffer 0079B8E0 (exit function 00432D00, bit 1 of 0079B8FC): net_session.cpp's "
                "g_receiveBuffer",
    # C50d: plain globals of ours in place of the static objects.
    0x00408D30: "the chunk archive's class registry 0079B270 (exit function 00408D50): chunk_archive.cpp's "
                "g_classRegistry",
    0x0042D8E0: "the clip work words 0079B668 (exit function 0042D900) that only the original's screen clipper "
                "used (clip.cpp went in R31a)",
    0x00455CE0: "the camera file 0079BA1C (exit function 00455D00): app_main.cpp's app::g_cameraFile",
    0x00455D10: "the command line's script 0079BA18 (exit function 00455D30): app_main.cpp's app::g_commandLineScript",
    0x00455D40: "the command line's mob file 0079BA14 (exit function 00455D60): app_main.cpp's app::g_commandLineMob",
    0x00455D70: "the command line's map 0079BA10 (exit function 00455D90): app_main.cpp's app::g_commandLineMap",
    0x00455DA0: "the movie folder 0079BA0C (exit function 00455DC0): app_main.cpp's app::g_movieFolder",
    0x004DB870: "a hash_set 007AB8D8 (exit function 004DB890) that only the original reads",
    0x0055E760: "the registry root 007AF450 (exit function 0055E780, bit 0 of 007AF438): game_folders.cpp's "
                "g_registryRoot",
    0x0055E7A0: "the start folder 007AF454 (exit function 0055E7C0, bit 1 of 007AF438): game_folders.cpp's "
                "g_startFolder",
    0x0055E820: "the text folder 007AF45C (exit function 0055E840, bit 3 of 007AF438): game_folders.cpp's "
                "g_textFolder",
    0x005DC050: "the cheat command map 007C7B38 (exit function 005DC070): console_streams.cpp's "
                "console::g_cheatCommands",
    0x005DC120: "the public command map 007C7B28 (exit function 005DC140): console_streams.cpp's "
                "console::g_publicCommands",
    # C50d: the draws' work arrays are plain globals of ours.
    0x0049B460: "the line lists' vertices 007A9CE0 (exit function 0049B490): renderer_lines.cpp's g_lineVertices",
    0x0049B6B0: "the line lists' outcodes 007A9CC8 (exit function 0049B6D0): renderer_lines.cpp's g_lineOutcodes",
    0x0049B6E0: "the line lists' projected points 007A9CB8 (exit function 0049B710): renderer_lines.cpp's "
                "g_lineProjected",
    0x004E5C40: "the device's outcodes 007AEE30 (exit function 004E5C70): device_buffers.cpp's g_outcodes",
    0x004E5C90: "the device's corner outcodes 007AEE20 (exit function 004E5CC0): gone (R30f: the mesh draws clip on the GPU)",
    0x004E5CE0: "the device's projected vertices 007AEE10 (exit function 004E5D10): device_buffers.cpp's g_projected",
    0x004E5D30: "the device's vertices 007AEE00 (exit function 004E5D60): gone (R30f: the mesh draws clip on the GPU)",
    0x004E5DD0: "the device's clip indices 007AEDE0 (exit function 004E5E00): gone (R30f: the mesh draws clip on the GPU)",
    0x004F2AF0: "the moved packets 007AEED0 (exit function 004F2B20): link_mesh_draw.cpp's g_movedPackets",
    0x005A0C80: "the offset vertices 007C2E18 (exit function 005A0CB0): figure_mesh.cpp's g_offsetVertices",
    0x006BFA00: "the terrain projection's input 007C88F8 (exit function 006BFA30, bit 0 of 007C88C9): "
                "terrain_sector.cpp's g_projectionInput",
    0x006BFA60: "the terrain projection's output 007C88E8 (exit function 006BFA90, bit 1 of 007C88C9): "
                "terrain_sector.cpp's g_projectionOutput",
    0x006BFAC0: "the terrain projection's outcodes 007C88D0 (exit function 006BFAE0, bit 2 of 007C88C9): "
                "terrain_sector.cpp's g_projectionOutcodes",
    # C50d1: the console's streambufs and streams are plain globals of ours (console_streams.cpp).
    **{a: f"the console's streambuf {o:08X} (exit function {e:08X}): console_streams.cpp's {n}"
       for a, o, e, n in ((0x005DC1E0, 0x007C76D0, 0x005DC2C0, 'g_aiLogBuf'),
                          (0x005DC2D0, 0x007C7278, 0x005DC2F0, 'g_eventLogBuf'),
                          (0x005DC300, 0x007C6E20, 0x005DC320, 'g_graphicsLogBuf'),
                          (0x005DC330, 0x007C69C8, 0x005DC350, 'g_rpgLogBuf'),
                          (0x005DC360, 0x007C6570, 0x005DC380, 'g_unitAcksLogBuf'),
                          (0x005DC390, 0x007C6118, 0x005DC3B0, 'g_netLogBuf'),
                          (0x005DC3C0, 0x007C5CC0, 0x005DC3E0, 'g_allLogBuf'),
                          (0x005DC3F0, 0x007C5868, 0x005DC410, 'g_consoleBuf'))},
    **{a: f"the console's stream {o:08X} (exit function {a + 0x20:08X}): console_streams.cpp's {n}"
       for a, o, n in ((0x005DC420, 0x007C5810, 'g_aiLogOut'), (0x005DC460, 0x007C57B8, 'g_eventLogOut'),
                       (0x005DC4A0, 0x007C5760, 'g_graphicsLogOut'), (0x005DC4E0, 0x007C5708, 'g_rpgLogOut'),
                       (0x005DC520, 0x007C56B0, 'g_netLogOut'), (0x005DC560, 0x007C5658, 'g_unitAcksLogOut'),
                       (0x005DC5A0, 0x007C5600, 'g_allLogOut'), (0x005DC5E0, 0x007C55A8, 'g_consoleOut'))},
    # C50d2: objects nothing of ours reads, and the logic settings' defaults as member initialisers.
    0x004DDD30: "the system information 007AB8F8 (CSysInfo; exit function 004DDD60): it collected the machine's "
                "version, memory and names, which nothing read",
    0x004F0B10: "the device lock 007AEE78 (a CRITICAL_SECTION; exit function 004F0B30): only game.exe's DirectDraw "
                "device took it; our renderer has no lock",
    0x005CDFB0: "the logic settings' defaults 007C53F8 (005CDFC0): logic_settings.h's member initialisers of "
                "g_logicSettings",
    # C54f: a plain global of ours in place of the static object.
    0x0064BE00: "the objectives window's last quest 007C8178 (exit function 0064BE20, guard bit 0 of 007C8174): "
                "ui_objectives.cpp's g_lastQuest",
}


def unread_objects(img, decoded, exits):
    """[(begin, size, initialiser)] of the objects nothing but their initialiser reaches: each the contiguous stores
    of one initialiser that stores constants only (UNREAD_KINDS), into .data, whose bytes
      - no instruction of game.exe's code reaches but the initialisers' and their exit functions' own (an absolute
        operand inside the range; the same object read elsewhere in the original is not unread),
      - no address literal of src/ or tests/ names, no initialiser call or exit function takes, no initialiser reads,
      - no named range (config/named_data.csv) holds,
    and right below which (UNREAD_NEAR) no address referenced elsewhere starts an object the stores may lie inside.
    These are the per-file copies of header constants (zero vectors, identity rotations, 1 / 1, pi's folds) that
    the original's code never reads: nothing of ours needs them.
    An initialiser that also registers an exit function (`stores + atexit`) whose only work is freeing this object's
    block ('free': `if (data) operator delete(block)`, registered by this initialiser alone) qualifies too: nothing
    else writes the object, its data pointer stays null and the exit function does nothing. Its atexit and the exit
    function are left out with the stores (UNREAD_EXITS; C18k: the unused aligned arrays 007A9CA8, 007AEDF0, ...)."""
    UNREAD_EXITS.clear()
    STALE_REVIEWED.clear()
    reviewed_used, read_used = set(), set()
    frees = {}   # the freed object's address -> (exit function, the initialiser registering it)
    for fn, name, body, _, initialisers in exits:
        m = re.fullmatch(r'if \(d\.NonZero\((0x[0-9A-F]{8})\)\) d\.Cdecl\(0x[0-9A-F]{8}, d\.Dword\(d\.Dword\(\1\) - 4\)\);', body)
        if name == 'free' and m and len(initialisers) == 1:
            frees[int(m.group(1), 16)] = (fn, initialisers[0])
    sizes = load_sizes()
    own, exit_fns = [], set()
    for a, kind, _, targets, _, spans in decoded:
        shadow = SHADOW.get(a)
        if shadow:   # an initialiser ported by hand: its own instructions and exit function, as decoded
            spans, targets = shadow[1], shadow[2]
        own += [(x, x + n) for x, n in spans] or [(a, a + sizes.get(a, 1))]
        exit_fns.update(fn for k, fn in targets if k == 'exit')
    own += [(fn, fn + size) for fn, _, _, size, _ in exits]
    # The exit functions ported by hand (no longer in `exits`): their instructions are the initialisers' own too.
    own += [(fn, fn + sizes[fn]) for fn in exit_fns if fn in sizes]
    own.sort()
    starts = [b for b, _ in own]

    def owned(at):
        i = bisect.bisect_right(starts, at) - 1
        return i >= 0 and own[i][0] <= at < own[i][1]

    dead_ranges = dead_code()
    dead_starts = [b for b, _ in dead_ranges]

    def dead(at):   # inside code nothing runs (config/dead_code.csv): its reads do not count
        i = bisect.bisect_right(dead_starts, at) - 1
        return i >= 0 and at < dead_ranges[i][1]

    _, va, vsz, rp, _ = next(s for s in img.sections if s[0] == '.text')
    text = img.data[rp:rp + vsz]
    referenced, foreign = set(), set()
    for k in range(4):
        n = (len(text) - k) // 4
        for i, (v,) in enumerate(struct.iter_unpack('<I', text[k:k + 4 * n])):
            if DATA_SECTIONS[0] <= v < DATA_SECTIONS[1]:
                referenced.add(v)
                if not owned(va + k + 4 * i) and not dead(va + k + 4 * i):
                    foreign.add(v)
    touched = hand_literals()
    stored, groups = set(), []
    for a, kind, lines, _, _, _ in decoded:
        # An initialiser ported by hand still stores into its object (through the name): those cells are stored.
        for line in SHADOW.get(a, ((),))[0]:
            stored.update(written_cells(line) or ())
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
            if run and c == run[-1][0] + 4 and c not in REVIEWED_SPLIT:
                run.append((c, call))
                continue
            if run:
                groups.append((a, kind, run))
            run = [(c, call)]
    for fn, _, body, _, _ in exits:
        touched.update(int(x, 16) for x in re.findall(r'0x([0-9A-F]{8})', body) if int(x, 16) not in frees
                       or frees[int(x, 16)][0] != fn)
    named = named_ranges()
    points = sorted(referenced | stored)
    out = []
    exits_of = {a: [fn for kind, fn in targets if kind == 'exit'] for a, _, _, targets, _, _ in decoded}
    for a, kind, run in groups:
        lo, hi = run[0][0], run[-1][0] + 4
        freed = [p for p in frees if lo <= p < hi]
        with_exit = kind == 'stores + atexit' and freed == [lo] and frees[lo][1] == a and exits_of[a] == [frees[lo][0]]
        if ((kind not in UNREAD_KINDS and not with_exit) or (freed and not with_exit) or any(call for _, call in run)
                or not DATA_SECTIONS[0] <= lo < hi <= DATA_SECTIONS[1]
                or (any(lo <= v < hi for v in foreign) and lo not in REVIEWED_READ)
                or any(lo <= v < hi for v in touched) or any(b < hi and lo < e for b, e, _ in named)):
            continue
        if lo in REVIEWED_READ:
            read_used.add(lo)
        i = bisect.bisect_left(points, lo) - 1
        below = points[i] if i >= 0 else 0
        if below in foreign and below not in stored and lo - below < UNREAD_NEAR:
            if lo not in REVIEWED_NEAR:
                continue
            if f'{below:08X}' != REVIEWED_NEAR[lo][0]:
                STALE_REVIEWED.append(f'{lo:08X}: reviewed against {REVIEWED_NEAR[lo][0]}, the object below is now {below:08X}')
                continue
            reviewed_used.add(lo)
        out.append((lo, hi - lo, a))
        if with_exit:
            UNREAD_EXITS[a] = frees[lo][0]
    STALE_REVIEWED.extend(f'{lo:08X}: reviewed as read (REVIEWED_READ), no longer a group the original reads that would '
                          'be unread (named, written or gone: drop the entry)' for lo in sorted(set(REVIEWED_READ) - read_used))
    reported = {int(x[:8], 16) for x in STALE_REVIEWED}
    STALE_REVIEWED.extend(f'{lo:08X}: reviewed as next to {REVIEWED_NEAR[lo][0]}, no longer a group UNREAD_NEAR keeps out '
                          '(named, read or gone: drop the entry)' for lo in sorted(set(REVIEWED_NEAR) - reviewed_used - reported))
    return out


def unread_csv(img, decoded, unread):
    values = stored_values(img, decoded)
    rows = [UNREAD_HEADER]
    for lo, size, a in sorted(unread):
        # A fold of ln (or of another unknown value) is not computed: '????????'.
        rows.append(f'{lo:08X},{size},{a:08X},' + ' '.join('?' * 8 if values[c] is None else f'{values[c]:08X}'
                                                          for c in range(lo, lo + size, 4)))
    return '\n'.join(rows) + '\n'


LEFT_OUT = {}   # initialiser -> number of statements left out (render's note)


def leave_out(decoded, ranges, exits=None):
    """The initialisers without the stores, float folds and fills into `ranges` ([(begin, end)]) and without the
    atexit of `exits` ({initialiser: exit function}: an unread object's, UNREAD_EXITS); problems for a statement that
    writes across a range's end or stores a call's result there (port the initialiser by hand)."""
    LEFT_OUT.clear()
    out, problems = [], []
    for a, kind, lines, targets, inlined, spans in decoded:
        kept = []
        for line in lines:
            if exits and a in exits and line.strip() == f'd.AtExit({hexa(exits[a])});':
                LEFT_OUT[a] = LEFT_OUT.get(a, 0) + 1
                continue
            if a in UNUSED_INITIALISERS and line.strip().startswith('d.'):
                LEFT_OUT[a] = LEFT_OUT.get(a, 0) + 1
                continue
            written = written_cells(line)
            inside = [c for c in written or () if any(b <= c < e for b, e in ranges)]
            if not inside:
                kept.append(line)
                continue
            m = STORE_LINE.match(line)
            if m and CALL.match(m.group(3)) and inside[0] in GENERATED_NAMES:
                kept.append(line)   # stored into the name in list order (name_operands)
                continue
            if len(inside) != len(written):
                problems.append(f'{a:08X}: {line.strip()} writes across the end of a named or unread range')
            elif m and CALL.match(m.group(3)):
                problems.append(f'{a:08X}: {line.strip()} stores a call\'s result into a named range: port the '
                                'initialiser by hand on the name (a row of config/static_init_ports.csv)')
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
    for fn, _, _, _, initialisers in exits:
        if len(initialisers) == 1 and initialisers[0] in UNUSED_INITIALISERS:
            UNREAD_EXITS[initialisers[0]] = fn
    dropped = set(UNREAD_EXITS.values())
    exits = [e for e in exits if e[0] not in dropped]
    ranges = [(b, e) for b, e, _ in named_ranges()] + [(lo, lo + n) for lo, n, _ in unread]
    kept, problems = leave_out(decoded, ranges, dict(UNREAD_EXITS))
    starts = {b for b, _, _ in named_ranges()}
    problems += [f'GENERATED_NAMES: {a:08X} ({name}) starts no range of config/named_data.csv'
                 for a, (name, _) in GENERATED_NAMES.items() if a not in starts]
    return kept, exits, unread_csv(img, decoded, unread), problems + STALE_REVIEWED


# The exit functions the initialisers register with atexit, each a whole-function pattern of
# instructions as capstone prints them. {NAME} matches a number, the same wherever the name repeats;
# {@n} is the address of the pattern's n-th instruction (a branch inside the function), {@E} of its
# last. The C++ is the body of ExitFunction's case over the `d.` operations of the initialisers (a guard is
# d.Once). A function that matches none stops the script: add a pattern or port it
# by hand, next to the object it destroys (net_session.cpp's ReleaseGameServer, ...).
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
                    if m.group(1) == 'S':
                        return import_ref(v)   # CALL [slot]
                    if m.group(1).startswith('A') and DATA[0] <= v < DATA[1]:
                        return arg_lit(v)   # an argument: a data address goes through the seam
                    return hexa(v) if v >= 0x10000 else lit(v)
                return name, re.sub(r'\{(\w+)\}', value, body), at - address
    return None


# The exit functions of initialisers ported by hand whose port registers nothing in their place: the object of ours
# has no destructor to run. exit function -> why.
UNREGISTERED_EXITS = {}

# The call sites of runtime_atexits' registrations (render_exits says "atexit at" for them).
RUNTIME_SITES = set()

# Functions whose port registers none of the original's function-local statics' exit functions (C18): their
# runtime atexit sites never run, like dead code's. begin -> (end, why).
RETIRED_STATICS = {
    # C50d
    0x00502CA0: (0x00503680, "CSceneRenderer::DrawSprites (renderer_sprites.cpp) uses plain globals of ours: the "
                             "original's work arrays 007AEF60..007AEFA0 (guard byte 007AEFB0) and sort arrays "
                             "007AEFB8..007AEFE8 (guard byte 007AEFF8) are never made"),
    0x00501D90: (0x00502A20, "CSceneRenderer::DrawStrip (renderer_strip.cpp) uses plain globals of ours: the original's "
                             "work arrays 007AF000..007AF040 (guard byte 007AF050) are never made"),
    0x006C8ED0: (0x006C9270, "Terrain::SetSun (terrain_animation.cpp) sorts the lit sectors in a local: the original's "
                             "static array 007C8938 (guard bit 0 of 007C8948, freed by 006C9270) is never made"),
    0x005A7640: (0x005A7A50, "FigureMeshDrawer::DrawPlain (figure_part_draw_plain.cpp) uses a plain global of ours: the "
                             "original's moved positions 007C2E60 (guard bit 0 of 007C2E70, freed by 005A7A50) are "
                             "never made"),
    0x005A7A70: (0x005A8AE0, "FigureMeshDrawer::DrawLit (figure_part_draw.cpp) uses a plain global of ours: the "
                             "original's moved positions 007C2E48 (guard bit 0 of 007C2E58, freed by 005A8AE0) are "
                             "never made"),
    # C50c
    0x004346D0: (0x00434980, "Connection::CollectChanges (net_connection.cpp) lists the replicated objects in a local: "
                             "the original's static vector 0079B8D0 (guard bit 0 of 0079B8DC, freed by 00434980) is "
                             "never made"),
    # C50b
    0x0057C700: (0x0057CB50, "GameFlow::ProcessRequest (world_game.cpp) calls UseResources, which the original inlines "
                             "with its function-local string 007AFDEC (guard bit 0 of 007AFDF0, destroyed by 0057E6A0)"),
    0x0057DEE0: (0x0057E1A0, "GameFlow::UseResources (world_game.cpp) keeps the resources in use in a function-local "
                             "static of ours: the original's string 007AFDEC (guard bit 0 of 007AFDF0, destroyed by "
                             "0057E6A0) is never made"),
    0x00467590: (0x004679B0, "CLightning::DrawGlow (lightning_draw.cpp) uses plain globals of ours: the original's "
                             "sprite records 0079C1E0 and points 0079C1D0 (guard bits 0 / 1 of 0079C1F4, destroyed by "
                             "00467A10 / 004679F0) are never made"),
    0x004976B0: (0x00497CE0, "CParticleSource::Draw (particle_render.cpp) uses plain globals of ours: the original's "
                             "sprites 007A9BE0 and vertices 007A9BD0 (guard bits 0 / 1 of 007A9BF4, destroyed by "
                             "00497D30 / 00497D10) are never made"),
    0x004E1AB0: (0x004E1C60, "CVisibilityFog::UpdateCells (effects_weather.cpp) lists the dead cells in a local: the "
                             "original's static array 007ABB58 (guard bit 0 of 007ABB64, freed by 004E1C50) is never "
                             "made"),
    0x004E1EB0: (0x004E2290, "CVisibilityFog::Draw (effects_weather_draw.cpp) uses plain globals of ours: the "
                             "original's sprites 007ABA40 and points 007ABA30 (guard bits 0 / 1 of 007ABA54, destroyed "
                             "by 004E2280 / 004E2260) are never made"),
    0x0056C750: (0x0056CE40, "CWorldClient::ProjectDecal (world_client_decals.cpp) uses plain globals of ours: the "
                             "original's decal builder 007AF488 and part points 007AF478 (guard bits 0 / 1 of 007AF504, "
                             "the points freed by 0056CE10) are never made"),
    0x0058B750: (0x0058C4F0, "CWorldServer::SyncPlayer (world_sync.cpp) diffs in local arrays: the original's static "
                             "arrays 007AFF18..007AFF48 (guard bits 0-3 of 007AFF58, freed by 0058C470..0058C4D0) are "
                             "never made"),
    0x0058D3D0: (0x0058D5D0, "CWorldServer::WakeUnitsAround (world_tick.cpp) keeps its spiral cache in a function-local "
                             "static of ours: the original's 007AFF08 (guard bit 0 of 007AFF10) is never made"),
    0x0058D670: (0x0058DAF0, "CWorldServer::UpdateUnits (world_tick.cpp) lists the ids in locals: the original's static "
                             "arrays 007AFEF0 / 007AFEE0 (guard bits 0 / 1 of 007AFF00, freed by 0058DA20 / 0058DA00) "
                             "are never made"),
    0x005B0050: (0x005B034A, "CRegionMap::LabelRegions (ai_region_reach.cpp) floods with local lists: the original's static "
                             "flood queue 007C2F10 and second list 007C2EF8 (guard bits 0 / 1 of 007C2F24, destroyed by "
                             "005B0360 / 005B0350) are never made"),
    0x005B07C0: (0x005B0AD6, "CRegionMap::ReachesRegion (ai_region_reach.cpp) lists the 3 x 3 blocks in a local: the "
                             "original's static list 007C2EE0 (guard bit 0 of 007C2EF4, destroyed by 005B0AE0) is never "
                             "made"),
    0x005B7EF0: (0x005B80BD, "CAIMap::StampWaterPatch (ai_water_patch.cpp) keeps its patch in a function-local static of "
                             "ours: the original's 007C2F64 (guard bit 0 of 007C2F68, released by 005B80C0) is never made"),
    0x006BFF40: (0x006C1390, "terrain::SectorData::DrawPage (terrain_passes.cpp) hands the page to src/render: the "
                             "original's tile arrays and triangle lists 007C8848..007C88C8 are never built"),
    0x0047ADF0: (0x0047B119, "ObjectSetSortUnique (world_object_sets.cpp) sorts by object id with std::sort: the "
                             "original's radix sort scratch array 007A9738 (freed by 0047B1D0) is never made"),
    0x004F4850: (0x004F4B2A, "CLinkMesh::RasterisePart rasterised a part into the 64 x 64 shadow texture, which is not "
                             "made (R26): its texture pixels 007AEEB8 (freed by 004F4B30) are never made"),
    0x005D2390: (0x005D2568, "AggressionMode::Evaluate (unit_ai_modes.cpp) takes its candidates in a local list: the "
                             "original's static list 007C54C8 (guard bit 0 of 007C54E4, destroyed by 005D2570) is "
                             "never made"),
    0x005A0E80: (0x005A116B, "CFigureMesh::Rasterise rasterised a part into the 64 x 64 shadow texture, which is not "
                             "made (R26): its texture points 007C2E00 (freed by 005A1170) are never made"),
}


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
            if kind != 'exit' or fn in registered or fn in UNREGISTERED_EXITS:
                continue
            if a not in by.setdefault(fn, []):
                by[fn].append(a)
    RUNTIME_SITES.clear()
    dead_ranges = dead_code()
    dead_starts = [b for b, _ in dead_ranges]
    # The instructions of the initialisers left out whole (UNUSED_INITIALISERS), with the functions they inline.
    unused = [(x, x + n) for a, _, _, _, _, spans in decoded if a in UNUSED_INITIALISERS for x, n in spans]

    def dead(at):   # a registration inside code nothing runs (config/dead_code.csv, RETIRED_STATICS, an unused
        # initialiser's) never runs
        i = bisect.bisect_right(dead_starts, at) - 1
        return ((i >= 0 and at < dead_ranges[i][1]) or any(b <= at < e for b, (e, _) in RETIRED_STATICS.items())
                or any(b <= at < e for b, e in unused))

    for fn, sites in runtime_atexits(img, md).items():
        sites = [s for s in sites if not dead(s)]
        if sites and fn not in registered and fn not in UNREGISTERED_EXITS and fn not in by:
            by[fn] = sites
            RUNTIME_SITES.update(sites)
    out, bad = [], []
    for fn, initialisers in by.items():
        m = match_exit(img, fn, md)
        if m:
            out.append((fn, *m, initialisers))
        elif not all(i in UNUSED_INITIALISERS for i in initialisers):   # an unused initialiser's goes with it
            bad.append(f'exit function {fn:08X} (atexit by {initialisers[0]:08X}): no pattern matches')
    return out, bad


# ---- Calls by name (C20) --------------------------------------------------------------------------------------------
#
# A call, an atexit or an exit function's call of a function ported by hand (config/static_init_ports.csv: address,
# name, file) is written by the function's name: `d.Call(Fn, args...)`, `d.Call(&Class::Method, object, args...)`,
# `d.AtExit(Fn)`; static_init.h's Call converts the arguments to the declared parameters. The csv also lists the
# initialisers, exit functions and atexit targets ported by hand, which are neither decoded nor inlined
# (registered_addresses). The csv's name is fully qualified and its file holds the definition; the call is the
# definition's name, and a header that declares it in the same namespace is included, otherwise an initialiser's or
# exit function's `void Fn()` is declared at the top of the generated file. A function defined in an anonymous
# namespace has no name another file can call, and a function with no row has no name at all: the tool stops with a
# problem (move it out of the namespace, add the row; nothing is called by address, C20 batch 3). Statements stay in
# address form until render, so the analyses above (unread objects, stored values, named_data.py) read them
# unchanged.

PORTS = os.path.join(ROOT, 'config', 'static_init_ports.csv')
RET = 'nothing'   # the exit functions that do nothing (static_exit.cpp's 'nothing'): never registered


def ports():
    """config/static_init_ports.csv: address -> (name, file)."""
    with open(PORTS, encoding='utf-8', newline='') as f:
        return {int(r['address'], 16): (r['name'], r['file']) for r in csv.DictReader(f)}


class Port:
    def __init__(self, path, name, offset):
        self.path, self.name, self.offset = path, name, offset


class Names:
    """Address -> how the generated code calls it: (expression, header to include or None, declaration or None);
    None (with a problem) for an address with no name."""

    def __init__(self, exits=()):
        import srcscan
        self.srcscan = srcscan
        self.sources = {}
        self.ports = {a: Port(os.path.join(ROOT, *path.split('/')), name, 0) for a, (name, path) in ports().items()}
        self.headers = {}   # last name -> [(header, namespaces)] of a declaration at namespace scope
        self.classes = {}   # class name -> [(header, namespaces)]
        for d, _, names in os.walk(os.path.join(ROOT, 'src')):
            for n in sorted(names):
                if not n.endswith('.h'):
                    continue
                p = os.path.join(d, n)
                code, scopes = self.code(p), self.scopes(p)
                for m in re.finditer(r'(?<![\w.>:~])([A-Za-z_]\w*)\s*\(', code):
                    path = scopes.namespace_at(m.start())
                    if path is not None:
                        self.headers.setdefault(m.group(1), []).append((p, path))
                for begin, _, kind, name in scopes.blocks:
                    path = scopes.namespace_at(begin)
                    if kind == 'class' and path is not None:
                        self.classes.setdefault(name, []).append((p, path))
        self.exits = {fn: name for fn, name, _, _, _ in exits}
        self.resolved, self.problems = {}, []

    def code(self, path):
        if path not in self.sources:
            raw = open(path, encoding='utf-8', errors='replace').read()
            code = self.srcscan.blank_comments(raw)
            self.sources[path] = (code, self.srcscan.Scopes(code))
        return self.sources[path][0]

    def scopes(self, path):
        self.code(path)
        return self.sources[path][1]

    def __call__(self, address):
        if address not in self.resolved:
            self.resolved[address] = self.resolve(address)
        return self.resolved[address]

    def method(self, port):
        """A method: Class::Method with the class declared in a header (the csv's qualified name): (expression,
        header, None), else None."""
        parts = port.name.split('::')
        last, qual = parts[-1], parts[:-1]
        scope = [x for x in self.scopes(port.path).namespace_at(port.offset) or [] if x]
        if qual:
            named = [(h, p) for h, p in self.classes.get(qual[-1], []) if (p + qual[-1:])[-len(qual):] == qual]
            found = [(h, p) for h, p in named if len(qual) > 1 or p == scope[:len(p)]] or                     (named if len({tuple(p) for _, p in named}) == 1 else [])
            if found:
                h, p = max(found, key=lambda x: len(x[1]))
                return '&' + '::'.join(p + [qual[-1], last]), h, None
        return None

    def definitions(self, port):
        """A free function's definitions in the csv's file: [(namespace path, head, parameters)]."""
        parts = port.name.split('::')
        last, qual = parts[-1], parts[:-1]
        code, scopes = self.code(port.path), self.scopes(port.path)
        defs = []
        for m in re.finditer(r'(?<![\w.>~])((?:\w+::)*)' + re.escape(last) + r'\s*\(([^;{}()]*)\)\s*(?:noexcept\s*)?\{',
                             code):
            path = scopes.namespace_at(m.start())
            if path is None:
                continue
            full = [x for x in path] + [x for x in m.group(1).split('::') if x]
            if qual and [x for x in full if x][-len(qual):] != qual:
                continue
            head = code[max(code.rfind(c, 0, m.start()) for c in ';{}') + 1:m.start()].strip()
            defs.append((full, head, m.group(2).strip()))
        return defs

    def resolve(self, a):
        if a in self.exits:   # this tool's own exit functions (static_exit.cpp)
            if self.exits[a] == 'nothing':
                return RET, None, None
            return f'Exit{a:08X}', None, ('ei::static_init', f'void Exit{a:08X}();')
        port = self.ports.get(a)
        if port is None:
            self.problems.append(f'{a:08X} is called but has no port: port it (a row of config/static_init_ports.csv)')
            return None
        if port.name.startswith('gameapi::'):   # an import, called through its slot (lib/gameapi.h)
            return port.name, port.path, None
        rel = os.path.relpath(port.path, ROOT).replace(os.sep, '/')
        if not os.path.exists(port.path):
            self.problems.append(f'{rel}: {port.name} ({a:08X}): no such file (config/static_init_ports.csv)')
            return None
        parts = port.name.split('::')
        last, qual = parts[-1], parts[:-1]
        method = self.method(port)
        if method is not None:
            return method
        defs = self.definitions(port)
        if len(defs) != 1:
            self.problems.append(f'{rel}: {port.name} ({a:08X}): {len(defs)} definition(s) found')
            return None
        full, head, params = defs[0]
        if '' in full:
            self.problems.append(f'{rel}: {port.name} ({a:08X}) is in an anonymous namespace: give it a name (move it '
                                 'out of the namespace)')
            return None
        expr = '::'.join(full + [last])
        same = [h for h, p in self.headers.get(last, []) if p == full]
        if same:
            own = os.path.splitext(port.path)[0] + '.h'
            return expr, own if own in same else sorted(same)[0], None
        if re.match(r'static\b', head):
            self.problems.append(f'{rel}: {port.name} ({a:08X}) is static: give it external linkage')
            return None
        if head != 'void' or params not in ('', 'void'):
            self.problems.append(f'{rel}: {port.name} ({a:08X}) is `{head} {last}({params})`: declare it in a header')
            return None
        return expr, None, ('::'.join(full), f'void {last}();')


DROPPED = '@@dropped@@'
CALL_BY_ADDRESS = re.compile(r'\bd\.(Cdecl|Stdcall|Thiscall|AtExit)\((0x[0-9A-F]{8})\b')


def name_calls(text, names, headers, declarations):
    """`text` with each call and atexit of a function in the map written by its name (Names); adds the headers and
    declarations it needs."""
    def named(m):
        found = names(int(m.group(2), 16))
        if found is None:
            return m.group(0)
        expr, header, declaration = found
        if expr == RET and m.group(1) == 'AtExit':   # an exit function that does nothing is not registered
            return DROPPED + '('
        if header:
            headers.add(os.path.relpath(header, os.path.join(ROOT, 'src')).replace(os.sep, '/'))
        if declaration:
            declarations.add(declaration)
        return f'd.{"AtExit" if m.group(1) == "AtExit" else "Call"}({expr}'
    return re.sub(r'[ \t]*' + re.escape(DROPPED) + r'\([^\n]*', '', CALL_BY_ADDRESS.sub(named, text))


# What static_init.h has (the rest of the operations the analysis knows is for data that is left out or named).
PLAIN_ZERO_ARGUMENTS = re.compile(r'(?m)^(\s*)d\.Call\(([\w:]+)\);$')
OPERATIONS = {'Call': 'Call', 'Once': 'Once', 'Local': 'Local', 'AtExit': 'std::atexit'}


def plain(text, names):
    """`text` (statements over the `d.` operations) as written: a call of a ported function without arguments is the
    call, an atexit is std::atexit's; an operation static_init.h does not have is a problem."""
    text = PLAIN_ZERO_ARGUMENTS.sub(r'\1\2();', text)
    text = re.sub(r'\bd\.(\w+)\(', lambda m: OPERATIONS.get(m.group(1), m.group(0)) + '(', text)
    for m in re.finditer(r'\bd\.\w+', text):
        names.problems.append(f'{m.group(0)} is written by the analysis but static_init.h has no such function: '
                              f'{text.strip()}')
    return text


def render_declarations(declarations):
    """The declarations name_calls collected, by namespace."""
    out = []
    for ns in sorted({n for n, _ in declarations}):
        lines = sorted(d for n, d in declarations if n == ns)
        out += ([f'namespace {ns} {{'] if ns else []) + lines + ([f'}} // namespace {ns}'] if ns else []) + ['']
    return '\n'.join(out)


def render_exits(exits, names):
    kinds = collections.Counter(name for _, name, _, _, _ in exits)
    functions = []
    headers, declarations = set(), set()
    for fn, name, body, size, initialisers in exits:
        by = ', '.join(f'{a:08X}' for a in initialisers[:3]) + (f' and {len(initialisers) - 3} more' if len(initialisers) > 3 else '')
        how = 'atexit at' if initialisers[0] in RUNTIME_SITES else 'atexit by'
        note = f'// {name}, {size} byte(s); {how} {by}'
        if name == 'nothing':   # the initialisers do not register it
            continue
        body = name_calls(name_operands(body.replace('\n        ', '\n    '), headers), names, headers, declarations)
        if re.search(r'\bd\.(?:Cdecl|Stdcall|Thiscall)\(0x', body):
            names.problems.append(f'exit function {fn:08X}: {body.strip()} calls by address: give the callee a name')
        functions += [note, f'void Exit{fn:08X}() {{', f'    {plain(body, names)}', '}', '']
    summary = ', '.join(f'{n} {k}' for k, n in kinds.most_common())
    return (EXIT_HEADER.replace('@COUNT@', str(len(exits))).replace('@SUMMARY@', summary)
            .replace('@INCLUDES@', includes(headers)).replace('@DECLARATIONS@', render_declarations(declarations)) +
            '\n'.join(functions) + '\n} // namespace ei::static_init\n')


def load_sizes():
    """src/hook/function_sizes.inc as {address: size}."""
    return {int(a, 16): int(n) for a, n in re.findall(r'\{0x([0-9A-F]+), (\d+)\}', open(SIZES).read())}


def write_sizes(exits):
    """Adds the exit functions' sizes to config/extra_function_sizes.csv and src/hook/function_sizes.inc
    (the tools' function sizes: tools/check_signatures.py, coverage.py, check_stolen.py)."""
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




def render(decoded, names):
    kinds = collections.Counter(k for _, k, _, _, _, _ in decoded)
    body = []
    for a, kind, lines, _, inlined, _ in decoded:
        note = f'{a:08X}: {kind}' + (f' (inlined {", ".join(f"{t:08X}" for t in inlined)})' if inlined else '')
        if LEFT_OUT.get(a):
            note += f'; {LEFT_OUT[a]} left out'
        body.append(f'    // {note}')
        body.extend('    ' + line for line in lines)
    summary = ', '.join(f'{n} {k}' for k, n in kinds.most_common())
    headers, declarations = set(), set()
    body = [name_calls(name_operands(line, headers), names, headers, declarations) for line in body]
    body = [line for line in body if line.strip()]   # an atexit of an exit function that does nothing is gone
    for line in body:
        if re.search(r'd\.(?:Cdecl|Stdcall|Thiscall|Import|AtExit)\(', line):
            names.problems.append(f'static initialiser: {line.strip()} calls by address: give the callee a name')
    body = [plain(line, names) for line in body]
    return (HEADER.replace('@SUMMARY@', summary).replace('@COUNT@', str(len(decoded)))
            .replace('@INCLUDES@', includes(headers)).replace('@DECLARATIONS@', render_declarations(declarations)) +
            '\n'.join(body) + FOOTER)


def check_ports():
    """Every row of config/static_init_ports.csv names a file and a definition in it (the build runs this)."""
    names, problems = Names(), []
    for a, port in sorted(names.ports.items()):
        rel = os.path.relpath(port.path, ROOT).replace(os.sep, '/')
        if port.name.startswith('gameapi::'):
            continue
        if not os.path.exists(port.path):
            problems.append(f'{rel}: {port.name} ({a:08X}): no such file (config/static_init_ports.csv)')
        elif names.method(port) is None and not names.definitions(port):
            problems.append(f'{rel}: {port.name} ({a:08X}): no definition found (config/static_init_ports.csv)')
    if problems:
        print('\n'.join(problems))
        return 1
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--exe')   # default: tools/exe_path.py
    ap.add_argument('--check-ports', action='store_true',
                    help='only check that every row of config/static_init_ports.csv names one definition (no game.exe)')
    args = ap.parse_args()
    if args.check_ports:
        return check_ports()
    args.exe = exe_path.resolve(args.exe)
    decoded, exits, unread, problems = generate(Image(args.exe))
    if problems:
        print('\n'.join(problems))
        return 1
    names = Names(exits)
    text, text_exit = render(decoded, names), render_exits(exits, names)
    if names.problems:
        print('\n'.join(names.problems))
        return 1
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    with open(OUT_EXIT, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text_exit)
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
// The standalone image runs them before WinMain (RunStaticInitialisers). A call or atexit of a ported function names
// it.
// tools/gen_static_init.py writes this file from game.exe's initialisers.
#include "static_init.h"

#include <cstdlib>

@INCLUDES@
@DECLARATIONS@namespace ei::static_init {

void RunStaticInitialisers() {
'''

FOOTER = '''
}

} // namespace ei::static_init
'''

EXIT_HEADER = '''\
// Generated by tools/gen_static_init.py from game.exe's code. Do not edit.
//
// The @COUNT@ exit functions game.exe registers with atexit that no rewrite ports, the static
// initialisers' ("atexit by" the initialiser; docs/notes/standalone.md, S3a; by hand next to
// their objects) and the function-local statics' ("atexit at" the call; S16a): @SUMMARY@.
// Each is decoded from its instructions as a whole-function pattern and written as plain calls (static_init.h),
// calling the destructors by name; static_init.cpp registers them by name. Those that do nothing are not
// registered.
// tools/gen_static_init.py writes this file from game.exe's exit functions.
#include "static_init.h"

@INCLUDES@
@DECLARATIONS@namespace ei::static_init {

'''

if __name__ == '__main__':
    sys.exit(main())
