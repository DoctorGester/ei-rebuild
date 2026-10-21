"""Dead-code analysis of game.exe: reachability from roots over code and data references.

Nodes: every function start in config/functions.csv (+ extra_function_sizes.csv) and, inside the code
Ghidra left outside any function body, pseudo nodes split after every RET/JMP. Edges: any reference
(rel32/rel8 branch, immediate, displacement, raw dword) from inside a node to an address inside
another node; fall-through into the next node; vtable -> its slots. Roots: the entry point, any
pointer stored in .rdata/.data outside a known vtable, references from Rebirth's binaries.
"""
import bisect, csv, os, re, struct, sys
import capstone

# It rewrites config/dead_code.csv from scratch, which drops rows added by hand (functions Ghidra
# never made, coincidental pointers ruled out by reading). Refuse to run with any argument
# (e.g. --help) so it is never run by accident.
if len(sys.argv) > 1:
    print(__doc__)
    print('Takes no arguments. Warning: it rewrites config/dead_code.csv and drops hand-added rows.')
    sys.exit(0)
from capstone import x86

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import exe_path  # noqa: E402

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = exe_path.resolve()
EXTRA_BINS = [r'F:\games\Evil Islands (Rebirth)\starter\addon.dll',
              r'F:\games\Evil Islands (Rebirth)\EI_Plugin.asi',
              r'F:\games\Evil Islands (Rebirth)\mp3dec.asi']
BASE = 0x400000

d = open(EXE, 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]
optsz = struct.unpack_from('<H', d, pe + 20)[0]
entry = struct.unpack_from('<I', d, pe + 24 + 16)[0] + BASE
secs = []
for i in range(nsec):
    o = pe + 24 + optsz + 40 * i
    name = d[o:o + 8].rstrip(b'\0').decode()
    vsz, va, rsz, roff = struct.unpack_from('<IIII', d, o + 8)
    secs.append((name, va + BASE, min(vsz, rsz), roff))
TEXT_START, TEXT_END = [(s[1], s[1] + s[2]) for s in secs if s[0] == '.text'][0]

def va_bytes(va, n):
    for name, sva, sz, roff in secs:
        if sva <= va < sva + sz:
            return d[roff + va - sva: roff + va - sva + n]
    return b''

funcs = {}
for r in csv.DictReader(open(os.path.join(R, 'config/functions.csv'))):
    funcs[int(r['address'], 16)] = r
gsize = {}
for line in open(os.path.join(R, 'src/hook/function_sizes.inc')):
    m = re.match(r'\{0x([0-9A-F]+), (\d+)\}', line)
    if m:
        gsize[int(m.group(1), 16)] = int(m.group(2))
for r in csv.DictReader(open(os.path.join(R, 'config/extra_function_sizes.csv'))):
    gsize[int(r['address'], 16)] = int(r['size'])
real = sorted(set(funcs) | set(gsize))
realset = set(real)

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = True
md.skipdata = True
TERM = ('ret', 'retf', 'jmp', 'int3', 'hlt', 'ljmp')

# pseudo nodes in code Ghidra left outside function bodies
pseudo = set()
for i, a in enumerate(real):
    nxt = real[i + 1] if i + 1 < len(real) else TEXT_END
    if a >= TEXT_END:
        continue
    gs = gsize.get(a)
    if gs is None:  # lib/eh: size_est is up to the next start
        continue
    p = a + gs
    while p < nxt:
        while p < nxt and va_bytes(p, 1)[0] in (0x90, 0xCC):
            p += 1
        if p >= nxt:
            break
        pseudo.add(p)
        cut = None
        for ins in md.disasm(va_bytes(p, nxt - p), p):
            if ins.mnemonic in TERM:
                cut = ins.address + ins.size
                break
        if cut is None:
            break
        p = cut
starts = sorted(realset | pseudo)
startset = set(starts)

def containing(va):
    if not (TEXT_START <= va < TEXT_END):
        return None
    i = bisect.bisect_right(starts, va) - 1
    return starts[i] if i >= 0 else None

def node_end(s):
    i = bisect.bisect_right(starts, s)
    return starts[i] if i < len(starts) else TEXT_END

vtables = {}
for r in csv.DictReader(open(os.path.join(R, 'config/vtables.csv'))):
    vtables[int(r['vtable'], 16)] = int(r['slots'])
# vtables.csv can run a vtable into the next one: any address inside a vtable that is itself
# referenced (raw dword anywhere in the file) starts a separate vtable node
alldw = {struct.unpack_from('<I', d, o)[0] for o in range(len(d) - 3)}
for v, n in list(vtables.items()):
    cuts = [v + 4 * k for k in range(1, n) if v + 4 * k in alldw]
    prev = v
    for c in cuts + [v + 4 * n]:
        vtables[prev] = (c - prev) // 4
        prev = c
vt_starts = sorted(vtables)

def in_vtable(va):
    i = bisect.bisect_right(vt_starts, va) - 1
    if i >= 0:
        v = vt_starts[i]
        if v <= va < v + 4 * vtables[v]:
            return v
    return None

edges, roots, reasons = {}, set(), {}

def add(src, dst, why):
    if src == dst:
        return
    if src is None:
        roots.add(dst)
        reasons.setdefault(dst, []).append(why)
    else:
        edges.setdefault(src, set()).add(dst)

# 1) raw dword scan of every section (any alignment) for exact node starts and vtable addresses;
#    aligned dwords anywhere for addresses inside a node (jump tables, mid-function pointers)
for name, sva, sz, roff in secs:
    for off in range(0, sz - 3):
        v = struct.unpack_from('<I', d, roff + off)[0]
        loc = sva + off
        if v in vtables:
            f = containing(loc)
            add(('f', f) if f is not None else None, ('v', v), f'data {loc:08X}')
        # mid-node pointers count only inside .text (switch tables); in .rdata/.data they are
        # almost all coincidences (e.g. two-letter strings like 'EN' = 0x00454E45)
        if TEXT_START <= v < TEXT_END and (v in startset or (off % 4 == 0 and name == '.text')):
            t = ('f', containing(v))
            vt = in_vtable(loc)
            if vt is not None:
                add(('v', vt), t, '')
                continue
            f = containing(loc)
            add(('f', f) if f is not None else None, t, f'data {loc:08X}->{v:08X}')

# 2) raw rel32 branches anywhere in .text
_, tva, tsz, troff = [s for s in secs if s[0] == '.text'][0]
tb = d[troff:troff + tsz]
for off in range(len(tb) - 6):
    b = tb[off]
    if b in (0xE8, 0xE9):
        dst = tva + off + 5 + struct.unpack_from('<i', tb, off + 1)[0]
    elif b == 0x0F and 0x80 <= tb[off + 1] <= 0x8F:
        dst = tva + off + 6 + struct.unpack_from('<i', tb, off + 2)[0]
    else:
        continue
    if dst in startset:
        add(('f', containing(tva + off)), ('f', dst), '')

# 3) decode every node: branch targets, immediates and displacements into .text; fall-through
fallthrough = {}
for s in starts:
    e = node_end(s)
    last = None
    for ins in md.disasm(va_bytes(s, e - s), s):
        if ins.mnemonic in ('nop', 'int3') and last is not None and last.mnemonic in TERM:
            continue
        if ins.mnemonic != 'nop':
            last = ins
        if ins.id == 0:
            continue
        for op in ins.operands:
            v = None
            if op.type == x86.X86_OP_IMM:
                v = op.imm & 0xFFFFFFFF
            elif op.type == x86.X86_OP_MEM:
                v = op.mem.disp & 0xFFFFFFFF
            if v is not None and TEXT_START <= v < TEXT_END:
                add(('f', s), ('f', containing(v)), '')
    if last is None or last.mnemonic in TERM:
        continue
    # a switch table at the end of the node is not code that falls through
    tail = va_bytes(e - 4, 4)
    while e > s and va_bytes(e - 1, 1)[0] in (0x90, 0xCC):
        e -= 1
    tv = struct.unpack_from('<I', va_bytes(e - 4, 4))[0]
    if s <= tv < node_end(s):
        continue
    nxt = node_end(s)
    if nxt < TEXT_END:
        add(('f', s), ('f', nxt), '')
        fallthrough[s] = nxt

# 4) entry point, references from Rebirth's binaries
add(None, ('f', containing(entry)), 'entry point')
for p in EXTRA_BINS:
    xb = open(p, 'rb').read()
    for off in range(len(xb) - 3):
        v = struct.unpack_from('<I', xb, off)[0]
        if v in realset:
            add(None, ('f', v), f'{os.path.basename(p)}+{off:X}')

live = set()
stack = list(roots)
while stack:
    n = stack.pop()
    if n in live:
        continue
    live.add(n)
    stack.extend(edges.get(n, ()))

# write config/dead_code.csv: game functions not reached, minus library.csv and Rebirth-patched ones
lib = {int(r['address'], 16) for r in csv.DictReader(open(os.path.join(R, 'config/library.csv')))}
patched = {int(l[:8], 16) for l in open(os.path.join(R, 'docs/rebirth-patched-functions.txt'))
           if re.match(r'[0-9A-F]{8} ', l)}
rev = {}
for src, ds in edges.items():
    for t in ds:
        rev.setdefault(t, set()).add(src)
dead = [a for a in real if a in funcs and funcs[a]['kind'] == 'game' and ('f', a) not in live
        and a not in lib and a not in patched]
with open(os.path.join(R, 'config/dead_code.csv'), 'w', newline='') as f:
    w = csv.writer(f, lineterminator='\n')
    w.writerow(['address', 'size', 'name', 'evidence'])
    for a in dead:
        srcs = sorted(rev.get(('f', a), ()))
        fs = [f'{x:08X}' for k, x in srcs if k == 'f']
        vs = [f'{x:08X}' for k, x in srcs if k == 'v']
        if not srcs:
            ev = 'no references at all (no call/jmp, no absolute pointer in code or data)'
        else:
            parts = []
            if fs: parts.append('dead code ' + ' '.join(fs[:6]) + (' ...' if len(fs) > 6 else ''))
            if vs: parts.append('vtable(s) installed only by dead code ' + ' '.join(vs))
            ev = 'referenced only from ' + '; '.join(parts)
        w.writerow([f'{a:08X}', gsize.get(a, 0), funcs[a]['name'], ev])
print(f'{len(dead)} dead game functions ({sum(gsize.get(a, 0) for a in dead):,} bytes) -> config/dead_code.csv;'
      f' {len([p for p in pseudo if ("f", p) not in live])} dead code chunks outside Ghidra functions')
