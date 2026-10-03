"""Fails when the compiled hook DLL converts unsigned 32-bit values to floating point the modern way.

MSVC turns `static_cast<double>(uint32)` into FILD dword + FADD 2^32 (for values >= 2^31). That FADD
rounds at the FPU's precision control, so the result can differ from the original's exact FILD of
the zero-extended qword (the original exe never references a 2^32 constant). Use x87::FromUnsigned.

usage: python tools/check_x87.py [build/release]   (exit 1 when a reference is found)
"""
import bisect, os, re, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    build = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'build', 'release')
    dll = open(os.path.join(build, 'ei_hook.dll'), 'rb').read()
    pe = struct.unpack_from('<I', dll, 0x3C)[0]
    count = struct.unpack_from('<H', dll, pe + 6)[0]
    opt = struct.unpack_from('<H', dll, pe + 20)[0]
    base = struct.unpack_from('<I', dll, pe + 24 + 28)[0]
    sections = []
    for i in range(count):
        s = pe + 24 + opt + 40 * i
        name = dll[s:s + 8].rstrip(b'\0')
        vsize, va, rsize, raw = struct.unpack_from('<4I', dll, s + 8)
        sections.append((name, va, rsize, raw))
    const = struct.pack('<d', 4294967296.0)
    targets = []
    for name, va, rsize, raw in sections:
        data = dll[raw:raw + rsize]
        i = data.find(const)
        while i != -1:
            targets.append(base + va + i)
            i = data.find(const, i + 1)
    hits = []
    for name, va, rsize, raw in sections:
        if name != b'.text':
            continue
        code = dll[raw:raw + rsize]
        for t in targets:
            pat = struct.pack('<I', t)
            i = code.find(pat)
            while i != -1:
                hits.append(base + va + i)
                i = code.find(pat, i + 1)
    syms = []
    for line in open(os.path.join(build, 'ei_hook.map'), errors='replace'):
        m = re.match(r'\s*[0-9a-f]{4}:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{8})\s', line)
        if m:
            syms.append((int(m.group(2), 16), m.group(1)))
    syms.sort()
    addrs = [a for a, _ in syms]
    for h in sorted(hits):
        i = bisect.bisect_right(addrs, h) - 1
        print(f'unsigned-to-double FADD 2^32 in {syms[i][1] if i >= 0 else "?"} (use x87::FromUnsigned)')
    print(f'{len(hits)} problem(s)')
    return 1 if hits else 0


if __name__ == '__main__':
    sys.exit(main())
