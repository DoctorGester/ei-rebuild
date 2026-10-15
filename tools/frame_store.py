"""Compressed storage of the harness's frame captures (ticket WB11).

A 4K frame is a 24 MB BMP and a run keeps up to three dozen of them; build/reference and build/verify/runs held
20 GB of them. A frame is stored as <name>.bmp.z instead: the BMP file's bytes, each byte XORed with the one a
pixel before (flat areas become zeros), then zlib. Lossless, standard library only, about 3x smaller than the
BMP. Everything that reads frames (compare_frames.py, frame_preview.py, run_all's --widescreen) goes through
read_frame / frame_names, which take either form; run_all packs a finished run's frames (pack_folder).

usage: python tools/frame_store.py pack FOLDER...     pack the .bmp files (over 256 KB) under the folders, in place
       python tools/frame_store.py unpack FILE.bmp.z  write FILE.bmp next to it
"""
import concurrent.futures, contextlib, os, struct, sys, zlib

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


def pack(data, level=LEVEL):
    step = step_of(data)
    n = len(data)
    mask = (1 << (8 * n)) - 1
    x = int.from_bytes(data, 'little')
    delta = (x ^ (x << (8 * step))) & mask
    return MAGIC + bytes([step]) + struct.pack('<Q', n) + zlib.compress(delta.to_bytes(n, 'little'), level)


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
    """Replaces path (a .bmp) by path + '.z'; the BMP goes only after the packed file reads back equal."""
    with open(path, 'rb') as f:
        data = f.read()
    blob = pack(data)
    tmp = path + SUFFIX + '.tmp'
    try:
        with open(tmp, 'wb') as f:
            f.write(blob)
        if unpack(blob) != data:
            raise ValueError('packing did not round-trip')
        os.replace(tmp, path + SUFFIX)
    except BaseException:
        with contextlib.suppress(OSError):
            os.remove(tmp)
        raise
    os.remove(path)
    return len(data), len(blob)


def pack_folder(folder, jobs=4, say=None):
    """Packs every .bmp of MIN_SIZE and more under folder. Never raises (a frame that fails stays a plain BMP);
    returns (bytes before, bytes after)."""
    todo = []
    for where, _, files in os.walk(folder):
        for n in files:
            p = os.path.join(where, n)
            with contextlib.suppress(OSError):
                if n.lower().endswith('.bmp') and os.path.getsize(p) >= MIN_SIZE:
                    todo.append(p)

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


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ('pack', 'unpack'):
        print(__doc__)
        return 2
    if sys.argv[1] == 'unpack':
        for p in sys.argv[2:]:
            plain = p[:-len(SUFFIX)] if p.endswith(SUFFIX) else p
            with open(plain, 'wb') as f:
                f.write(read_frame(plain))
        return 0
    before = after = 0
    for folder in sys.argv[2:]:
        b, a = pack_folder(folder, say=print)
        before, after = before + b, after + a
    print(f'{before / 1e6:.0f} MB -> {after / 1e6:.0f} MB')
    return 0


if __name__ == '__main__':
    sys.exit(main())
