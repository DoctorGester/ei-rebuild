"""Checks the ports of the game functions under five bytes (EI_REPLACE_SHORT, src/game/short_functions.cpp).

For every EI_REPLACE_SHORT(address, Kind) in src/:
  - the kind's code (tools/gen_short_functions.py) is game.exe's bytes at the address, the whole
    function (src/hook/function_sizes.inc), which is under five bytes;
  - the next function starts at least five bytes later (room for the standalone image's jump);
  - nothing else registers the address (EI_REPLACE / EI_REPLACE_METHOD / UI input handlers).
And every unported game function under five bytes has one (gen_short_functions.unported).
With --map (build.cmd passes it, except for the debug build): each kind's compiled code in
ei_hook.dll (next to the map; incremental-link jumps followed) starts with the same bytes (MSVC writes
RET as C2 00 00, RET 0, which is the same instruction).

usage: python tools/check_short_functions.py [--exe game.exe] [--map build\\<cfg>\\ei_hook.map]
exit code 1 on any problem
"""
import argparse, os, re, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_short_functions as gen  # noqa: E402

RET_0 = bytes([0xc2, 0, 0])   # RET 0, as MSVC writes RET
SHORT = re.compile(r'\bEI_REPLACE_SHORT\(\s*0x([0-9A-Fa-f]+)\s*,\s*(\w+)(?:\s*<\s*(0x[0-9A-Fa-f]+|\d+)\s*>)?\s*\)')


def registrations():
    out = []
    for d, _, names in os.walk(os.path.join(gen.ROOT, 'src')):
        for n in names:
            if n.endswith(('.cpp', '.h')):
                p = os.path.join(d, n)
                text = gen.COMMENT.sub('', open(p, encoding='utf-8', errors='replace').read())
                for m in SHORT.finditer(text):
                    arg = int(m.group(3), 0) if m.group(3) else None
                    out.append((int(m.group(1), 16), m.group(2), arg, os.path.relpath(p, gen.ROOT)))
    return out


def compiled_code(map_path):
    """(kind, offset or None) -> the first bytes of our compiled function, from the map and the DLL."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from check_signatures import undecorate
    text = open(map_path, errors='replace').read()
    base = int(re.search(r'Preferred load address is ([0-9a-fA-F]+)', text).group(1), 16)
    entries = re.findall(r'^\s*[0-9a-f]{4}:[0-9a-f]{8}\s+(\?\S+)\s+([0-9a-f]{8})\s+(?:f\s+)?(?:i\s+)?short_functions(?:\.cpp)?\.obj\s*$',
                         text, re.M)
    und = undecorate({d for d, _ in entries})
    dll = open(os.path.join(os.path.dirname(map_path), 'ei_hook.dll'), 'rb').read()
    pe = struct.unpack_from('<I', dll, 0x3c)[0]
    nsec, optsz = struct.unpack_from('<H', dll, pe + 6)[0], struct.unpack_from('<H', dll, pe + 20)[0]
    sections = [struct.unpack_from('<IIII', dll, pe + 24 + optsz + 40 * i + 8) for i in range(nsec)]

    def at(rva, n):
        for vsize, va, rawsize, raw in sections:
            if va <= rva < va + max(vsize, rawsize):
                return dll[raw + rva - va:raw + rva - va + n]
        return b''

    out = {}
    for dec, addr in entries:
        m = re.search(r'ei::short_functions::(\w+)(?:<(\d+)>)?\(', und.get(dec, ''))
        if not m:
            continue
        rva = int(addr, 16) - base
        for _ in range(4):   # incremental-link jump stubs
            code = at(rva, 8)
            if code[:1] != b'\xe9':
                break
            rva += 5 + struct.unpack_from('<i', code, 1)[0]
        out[(m.group(1), int(m.group(2)) if m.group(2) else None)] = at(rva, 8)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--exe', default=gen.EXE)
    ap.add_argument('--map')
    args = ap.parse_args()
    exe = open(args.exe, 'rb').read()
    sizes = gen.load_sizes()
    starts = sorted(sizes)
    problems = []
    regs = registrations()
    seen = {}
    elsewhere = gen.ported_elsewhere()
    for a, kind, arg, path in regs:
        where = f'{a:08X} {gen.kind_name(kind, arg)} ({path})'
        want = gen.kind_bytes(kind, arg)
        size = sizes.get(a, 0)
        i = starts.index(a) if a in sizes else -1
        room = starts[i + 1] - a if 0 <= i < len(starts) - 1 else 0
        if a in seen:
            problems.append(f'{where}: registered twice (also {seen[a]})')
        seen[a] = where
        if want is None:
            problems.append(f'{where}: not a kind of tools/gen_short_functions.py')
        elif not size:
            problems.append(f'{where}: not a known function start')
        elif exe[a - gen.BASE:a - gen.BASE + size] != want:
            problems.append(f'{where}: game.exe has {exe[a - gen.BASE:a - gen.BASE + size].hex(" ")}, the kind is {want.hex(" ")}')
        if size >= gen.SHORT:
            problems.append(f'{where}: {size} bytes, hookable: use EI_REPLACE')
        if room < gen.SHORT:
            problems.append(f'{where}: {room} bytes to the next function, no room for a jump')
        if a in elsewhere:
            problems.append(f'{where}: also registered by a rewrite')
    for a in sorted(set(gen.unported(sizes)) - set(seen)):
        problems.append(f'{a:08X}: game function under 5 bytes with no port (run tools/gen_short_functions.py)')
    kinds = {(k, arg) for _, k, arg, _ in regs}
    if args.map:
        code = compiled_code(args.map)
        for k, arg in sorted(kinds, key=str):
            want = gen.kind_bytes(k, arg)
            got = code.get((k, arg))
            if want and got is None:
                problems.append(f'{gen.kind_name(k, arg)}: not found in {args.map}')
            elif want and got[:len(want)] != want and got[:len(want) + 2] != want[:-1] + RET_0:
                problems.append(f'{gen.kind_name(k, arg)}: compiles to {got.hex(" ")}, the original is {want.hex(" ")}')
    for p in problems:
        print('check_short_functions:', p)
    print(f'check_short_functions: {len(regs)} short function(s), {len(kinds)} kind(s)'
          f'{", compiled code checked" if args.map else ""}, {len(problems)} problem(s)')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
