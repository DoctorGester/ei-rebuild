"""The x87 rules hold in the x87 code only, and the SSE2 code keeps none of them.

1. The x87 code (everything but the ei_render and ei_sse2 libraries) must not convert unsigned 32-bit values to
   floating point the modern way: MSVC turns `static_cast<double>(uint32)` into FILD dword + FADD 2^32 (for values
   >= 2^31). That FADD rounds at the FPU's precision control, so the result can differ from the original's exact
   FILD of the zero-extended qword (the original exe never references a 2^32 constant). Use x87::FromUnsigned.
   Checked in the compiled game (ei-rebuild.exe and its map). The SSE2 code is left out (C30a): the renderer, the
   game code built for SSE2 and the CRT's conversions only SSE2 code calls (ftol3.obj: __ltod3 and its kin), whose
   2^32 is SSE arithmetic.
2. The SSE2 sources (src/render, and src/game and src/lib but CMakeLists' EI_X87_GAME_SOURCES) write plain float
   maths: no x87:: helpers (but the control word render.cpp's FpuGuard puts back), no libcrt::Ftol and no D()
   double registers (C30c). Checked in the sources.

usage: python tools/check_x87.py [build/release]   (exit 1 when a reference is found)
"""
import bisect, glob, os, re, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# The map's object column of the SSE2 code: library:object.
SSE2_OBJECTS = ('ei_render:', 'ei_sse2:', 'libcmt:ftol3.obj')
# What the SSE2 sources must not use (comments left out): the x87 helpers but the control word, the CRT's _ftol, D().
SSE2_BANNED = re.compile(r'\bx87::(?!(?:Set)?ControlWord\b)\w+|\blibcrt::Ftol\b|(?<![\w.>:])D\(')


def sse2_sources():
    """The files built for SSE2, from CMakeLists.txt (the same lists the build uses): src/render, and src/game and
    src/lib but EI_X87_GAME_SOURCES."""
    text = open(os.path.join(ROOT, 'CMakeLists.txt'), encoding='utf-8').read()
    listed = re.search(r'set\(EI_X87_GAME_SOURCES(.*?)\)', text, re.S).group(1)
    x87 = {os.path.normpath(os.path.join(ROOT, 'src', name + '.cpp'))
           for name in re.sub(r'#[^\n]*', '', listed).split()}
    found = set()
    for pattern in ('src/game/*.cpp', 'src/lib/*.cpp', 'src/render/*.cpp'):
        found.update(os.path.normpath(f) for f in glob.glob(os.path.join(ROOT, pattern)))
    return sorted(found - x87)


def source_problems():
    problems = 0
    for path in sse2_sources():
        for number, line in enumerate(open(path, encoding='utf-8', errors='replace'), 1):
            m = SSE2_BANNED.search(line.split('//', 1)[0])
            if m:
                print(f'{os.path.relpath(path, ROOT)}:{number}: {m.group(0)} in an SSE2 file (plain float maths there)')
                problems += 1
    return problems


def main():
    build = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'build', 'release')
    dll = open(os.path.join(build, 'ei-rebuild.exe'), 'rb').read()
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
    for line in open(os.path.join(build, 'ei-rebuild.map'), errors='replace'):
        m = re.match(r'\s*[0-9a-f]{4}:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{8})\s.*\s(\S+)$', line)
        if m:
            syms.append((int(m.group(2), 16), m.group(1), m.group(3)))
    syms.sort()
    addrs = [a for a, _, _ in syms]
    problems = source_problems()
    for h in sorted(hits):
        i = bisect.bisect_right(addrs, h) - 1
        name, obj = (syms[i][1], syms[i][2]) if i >= 0 else ('?', '')
        if obj.lower().startswith(SSE2_OBJECTS):
            continue
        print(f'unsigned-to-double FADD 2^32 in {name} (use x87::FromUnsigned)')
        problems += 1
    print(f'{problems} problem(s)')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
