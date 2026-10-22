"""Find vtables in game.exe and the functions that install them (constructors/destructors).

A vtable candidate is a run of pointers into .text in .rdata whose first entry address is used as
an immediate in code (e.g. `mov dword ptr [ecx], vtable`). Writes config/vtables.csv.
"""
import bisect, csv, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import exe_path  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = 0x400000
TEXT = (0x401000, 0x73A670)
RDATA = (0x73B000, 0x7828B2)

def main():
    d = open(exe_path.resolve(), 'rb').read()
    u32 = lambda va: struct.unpack_from('<I', d, va - BASE)[0]
    in_text = lambda v: TEXT[0] <= v < TEXT[1]

    funcs = []
    with open(os.path.join(ROOT, 'config', 'functions.csv')) as f:
        for r in csv.DictReader(f):
            funcs.append((int(r['address'], 16), r['name'], r['kind']))
    starts = [a for a, _, _ in funcs]
    # A slot 0 that is no function is no vtable: an EH funclet (functions.csv kind `eh`: the dword is an unwind map
    # entry's action, e.g. 0074E824) or bytes config/dead_code.csv calls no function start (00746E60's dword is the
    # text "ime" that 00704BE6 hands lstrcmpiA).
    not_slot0 = {a for a, _, kind in funcs if kind == 'eh'}
    with open(os.path.join(ROOT, 'config', 'dead_code.csv')) as f:
        not_slot0 |= {int(r['address'], 16) for r in csv.DictReader(f) if 'not a function start' in r['evidence']}
    def func_at(va):
        i = bisect.bisect_right(starts, va) - 1
        return funcs[i] if i >= 0 else None

    # Immediates in .text that point into .rdata: va of the 4-byte operand -> value.
    text = d[TEXT[0] - BASE:TEXT[1] - BASE]
    refs = {}
    for off in range(0, len(text) - 4):
        v = struct.unpack_from('<I', text, off)[0]
        if RDATA[0] <= v < RDATA[1]:
            refs.setdefault(v, []).append(TEXT[0] + off)

    def rtti_name(vt):
        try:
            col = u32(vt - 4)
            if not (RDATA[0] <= col < RDATA[1]) or u32(col) != 0: return ''
            td = u32(col + 12)
            s = d[td + 8 - BASE: td + 8 - BASE + 128].split(b'\0')[0]
            return s.decode('latin1') if s.startswith(b'.?A') else ''
        except struct.error:
            return ''

    def stored(v):
        """Whether the code stores `v` (mov r/m32, imm32: C7 /0; or mov r32, imm32: B8+r): a vtable's start, not a
        slot the code calls through (call [v]) or a byte match."""
        for site in refs.get(v, ()):
            i = site - TEXT[0]
            if i >= 1 and 0xB8 <= text[i - 1] <= 0xBF:
                return True
            for back, mods, rms in ((2, (0,), (0, 1, 2, 3, 6, 7)), (3, (0,), (4,)), (3, (1,), (0, 1, 2, 3, 5, 6, 7)),
                                    (4, (1,), (4,)), (6, (0,), (5,)), (6, (2,), (0, 1, 2, 3, 5, 6, 7)),
                                    (7, (2,), (4,))):
                if i >= back and text[i - back] == 0xC7:
                    m = text[i - back + 1]
                    if m >> 6 in mods and m & 7 in rms and (m >> 3) & 7 == 0:
                        return True
        return False

    rows = []
    for vt in sorted(refs):
        if vt % 4 or not in_text(u32(vt)) or u32(vt) in not_slot0:
            continue
        n = 0
        while vt + 4 * n < RDATA[1] and in_text(u32(vt + 4 * n)):
            n += 1
            # The next vtable starts here: any address the code holds after the first slot (a slot call splits a
            # tail off, docs/notes/standalone.md "Tails"), or right after the first slot one the code stores (a
            # one-slot table, e.g. CSectorMap's 0073F9DC before the area list's 0073F9E0).
            if (vt + 4 * n) in refs and (n > 1 or stored(vt + 4 * n)):
                break
        users = {}
        for site in refs[vt]:
            f = func_at(site)
            if f and f[2] in ('game', 'lib', 'thunk'):
                users[f[0]] = f[1]
        if not users:
            continue
        lib = all(a >= 0x6E6000 for a in users)
        rows.append([f'{vt:08X}', n, rtti_name(vt), 'lib' if lib else 'game',
                     ' '.join(f'{a:08X}' for a in sorted(users)), f'{u32(vt):08X}'])

    out = os.path.join(ROOT, 'config', 'vtables.csv')
    with open(out, 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['vtable', 'slots', 'rtti', 'kind', 'installed_by', 'slot0'])
        w.writerows(rows)
    print(f'{len(rows)} vtables ({sum(r[3] == "game" for r in rows)} game) -> {out}')

if __name__ == '__main__':
    sys.exit(main())
