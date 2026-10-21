"""Compressed storage of the harness's frame captures (ticket WB11).

A 4K frame is a 24 MB BMP and a run keeps up to three dozen of them; build/reference and build/verify/runs held
20 GB of them. A frame is stored as <name>.bmp.z instead: the BMP file's bytes, each byte XORed with the one a
pixel before (flat areas become zeros), then zlib. Lossless, standard library only, about 3x smaller than the
BMP. Everything that reads frames (compare_frames.py, frame_preview.py, run_all's --widescreen) goes through
read_frame / frame_names, which take either form; run_all packs a finished run's frames (pack_run: pack_folder in a
process of its own, after the game has given back its slot).

usage: python tools/frame_store.py pack [--jobs N] FOLDER...   pack the .bmp files (over 256 KB) under the folders,
                                                               in place, N at once (default 4)
       python tools/frame_store.py unpack FILE.bmp.z            write FILE.bmp next to it
"""
import concurrent.futures, contextlib, os, re, struct, subprocess, sys, threading, zlib

SUFFIX = '.z'
MAGIC = b'EIZ1'
MIN_SIZE = 256 * 1024            # smaller BMPs stay as they are
LEVEL = 3


def step_of(data):
    """The BMP's bytes per pixel (3 or 4); 1 for anything else (the XOR then runs byte to byte)."""
    if len(data) >= 30 and data[:2] == b'BM':
        bits, = struct.unpack_from('<H', data, 28)
        if bits in (24, 32):
            return bits // 8
    return 1


def delta_of(data):
    """(step, data's bytes each XORed with the one a pixel before): what pack() compresses."""
    step = step_of(data)
    n = len(data)
    mask = (1 << (8 * n)) - 1
    x = int.from_bytes(data, 'little')
    return step, ((x ^ (x << (8 * step))) & mask).to_bytes(n, 'little')


def header(step, n):
    return MAGIC + bytes([step]) + struct.pack('<Q', n)


def pack(data, level=LEVEL):
    step, delta = delta_of(data)
    return header(step, len(data)) + zlib.compress(delta, level)


def unpack(blob):
    if blob[:4] != MAGIC:
        raise ValueError('not a packed frame')
    step, n = blob[4], struct.unpack_from('<Q', blob, 5)[0]
    delta = zlib.decompress(blob[13:])
    if len(delta) != n:
        raise ValueError('packed frame has the wrong length')
    mask = (1 << (8 * n)) - 1
    x = int.from_bytes(delta, 'little')
    shift = 8 * step
    while shift < 8 * n:                       # prefix XOR with the pixel before, in log2(n) passes
        x ^= (x << shift) & mask
        shift *= 2
    return x.to_bytes(n, 'little')


def exists(path):
    return os.path.exists(path) or os.path.exists(path + SUFFIX)


def read_frame(path):
    """The BMP file's bytes of path (a .bmp), whether it is stored plain or as path + '.z'."""
    try:
        with open(path, 'rb') as f:
            return f.read()
    except FileNotFoundError:
        with open(path + SUFFIX, 'rb') as f:
            return unpack(f.read())


def frame_names(folder):
    """The sorted .bmp names in a folder, plain or packed (a.bmp.z is listed as a.bmp)."""
    names = set()
    for n in os.listdir(folder):
        low = n.lower()
        if low.endswith('.bmp'):
            names.add(n)
        elif low.endswith('.bmp' + SUFFIX):
            names.add(n[:-len(SUFFIX)])
    return sorted(names)


def pack_file(path):
    """Replaces path (a .bmp) by path + '.z'; the BMP goes only after the compressed bytes read back equal. (The
    check decompresses and compares the XORed bytes; undoing the XOR too, unpack(), cost twice the packing in pure
    Python, 0.5-0.9 s a 4K frame, SV10h; tools/test_frame_store.py checks that unpack inverts pack.)"""
    with open(path, 'rb') as f:
        data = f.read()
    step, delta = delta_of(data)
    body = zlib.compress(delta, LEVEL)
    tmp = path + SUFFIX + '.tmp'
    try:
        with open(tmp, 'wb') as f:
            f.write(header(step, len(data)))
            f.write(body)
        if zlib.decompress(body) != delta:
            raise ValueError('packing did not round-trip')
        os.replace(tmp, path + SUFFIX)
    except BaseException:
        with contextlib.suppress(OSError):
            os.remove(tmp)
        raise
    os.remove(path)
    return len(data), 13 + len(body)


def to_pack(folder):
    """The .bmp files of MIN_SIZE and more under folder."""
    todo = []
    for where, _, files in os.walk(folder):
        for n in files:
            p = os.path.join(where, n)
            with contextlib.suppress(OSError):
                if n.lower().endswith('.bmp') and os.path.getsize(p) >= MIN_SIZE:
                    todo.append(p)
    return todo


def pack_folder(folder, jobs=4, say=None):
    """Packs every .bmp of MIN_SIZE and more under folder, in this process. Never raises (a frame that fails stays a
    plain BMP); returns (bytes before, bytes after)."""
    todo = to_pack(folder)

    def one(p):
        try:
            return pack_file(p)
        except Exception as e:
            if say:
                say(f'{p}: not packed ({e})')
            return 0, 0

    before = after = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        for b, a in pool.map(one, todo):
            before += b
            after += a
    return before, after


# A run's frames are packed in a process of its own (pack_run): the packing is mostly pure Python big-number
# arithmetic, which holds the GIL, and in the threads of the scheduler or of run_all it slowed every other run's set-up
# and reporting (four runs packing at once: 25-30 s for 5-15 frames, another run's 0.1 s set-up 7-10 s; SV10h). At
# most PACK_PROCESSES of them at once from one Python process, PACK_THREADS frames each (zlib lets go of the GIL),
# below normal priority: the CPU is the games' first.
PACK_PROCESSES = int(os.environ.get('EI_PACK_PROCESSES', '4'))
PACK_THREADS = 2
_pack_slots = threading.BoundedSemaphore(max(PACK_PROCESSES, 1))
TOTAL = re.compile(r'^(\d+) bytes -> (\d+) bytes', re.M)


def pack_run(folder):
    """pack_folder(folder) in a python process of its own, once one of PACK_PROCESSES is free. Never raises (frames
    it could not pack stay plain BMPs); returns (bytes before, bytes after)."""
    if not to_pack(folder):
        return 0, 0                       # most runs keep no frame: no process
    flags = 0x00004000 | 0x08000000 if os.name == 'nt' else 0    # BELOW_NORMAL_PRIORITY_CLASS, CREATE_NO_WINDOW
    with _pack_slots:
        try:
            proc = subprocess.run([sys.executable, os.path.abspath(__file__), 'pack', '--jobs', str(PACK_THREADS), folder],
                                  capture_output=True, text=True, creationflags=flags)
        except OSError:
            return 0, 0
    m = TOTAL.search(proc.stdout)
    return (int(m[1]), int(m[2])) if m else (0, 0)


def main():
    args = sys.argv[1:]
    if len(args) < 2 or args[0] not in ('pack', 'unpack'):
        print(__doc__)
        return 2
    if args[0] == 'unpack':
        for p in args[1:]:
            plain = p[:-len(SUFFIX)] if p.endswith(SUFFIX) else p
            with open(plain, 'wb') as f:
                f.write(read_frame(plain))
        return 0
    jobs = 4
    if args[1] == '--jobs':
        jobs, args = int(args[2]), args[2:]
    before = after = 0
    for folder in args[1:]:
        b, a = pack_folder(folder, jobs, say=print)
        before, after = before + b, after + a
    print(f'{before} bytes -> {after} bytes ({before / 1e6:.0f} MB -> {after / 1e6:.0f} MB)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
