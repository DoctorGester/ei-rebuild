"""Compares frames captured by the harness ("capture <name>": <run folder>\\frames\\<name>.bmp).

usage: python tools/compare_frames.py A B [--tolerance N] [--max-fraction F] [--block N] [--worst N]
                                          [--diff OUT] [--mask M1 M2 [--mask-background]]
  A, B              two BMP files, or two folders: run folders (their frames\\ subfolder is used) or
                    frames folders; every <name>.bmp present in both is compared
  --tolerance N     a pixel differs when one of its channels differs by more than N (0..255,
                    default 8: the 5/6-bit steps of a 16-bit surface are 8 and 4)
  --max-fraction F  the comparison fails when more than F of the pixels differ (default 0.001)
  --block N         region size for the worst-regions list (default 32 pixels)
  --worst N         regions listed per frame (default 8)
  --diff OUT        write a difference image: a dimmed grey copy of A with the differing pixels in
                    red (brighter = larger difference); with folders OUT is a folder
  --mask M1 M2      compare only the pixels where the frames M1 and M2 differ (by more than the
                    tolerance), e.g. the old frame with and without its terrain to compare the new
                    renderer's terrain (A) with the old one (B) where the terrain is (files only);
                    the fraction is then of the masked pixels
  --mask-background keep only the masked pixels where M2 shows its background (clear) colour: the
                    feature with nothing in front of it (no interface panel, shadow or object)
exit code: 0 when every frame is within --max-fraction, 1 when one is not, 2 when a frame is
missing, unreadable or of another size

Only the standard library: BMP (24 or 32 bits, uncompressed, either row order) is read by hand.
"""
import argparse
import os
import struct
import sys


class Image:
    def __init__(self, width, height, rows):
        self.width = width
        self.height = height
        self.rows = rows   # top-down, each width * 3 bytes (B, G, R)


def read_bmp(path):
    with open(path, 'rb') as f:
        data = f.read()
    if len(data) < 54 or data[:2] != b'BM':
        raise ValueError(f'{path}: not a BMP file')
    offset, = struct.unpack_from('<I', data, 10)
    header_size, width, height, planes, bits, compression = struct.unpack_from('<IiiHHI', data, 14)
    if bits not in (24, 32) or compression not in (0, 3):
        raise ValueError(f'{path}: {bits}-bit BMP with compression {compression} is not supported')
    top_down = height < 0
    height = abs(height)
    step = bits // 8
    stride = (width * step + 3) & ~3
    if offset + stride * height > len(data):
        raise ValueError(f'{path}: truncated')
    rows = []
    for y in range(height):
        src = y if top_down else height - 1 - y
        row = data[offset + src * stride:offset + src * stride + width * step]
        if step == 4:
            row = b''.join(row[i:i + 3] for i in range(0, len(row), 4))
        rows.append(bytes(row))
    return Image(width, height, rows)


def write_bmp(path, width, height, rows):
    stride = (width * 3 + 3) & ~3
    pad = b'\0' * (stride - width * 3)
    with open(path, 'wb') as f:
        f.write(b'BM' + struct.pack('<IHHI', 54 + stride * height, 0, 0, 54))
        f.write(struct.pack('<IiiHHIIiiII', 40, width, height, 1, 24, 0, stride * height, 2835, 2835, 0, 0))
        for y in range(height - 1, -1, -1):
            f.write(bytes(rows[y]) + pad)


def make_mask(m1, m2, tolerance, background=False):
    """Rows of 0/1 flags: 1 where m1 and m2 differ by more than `tolerance` in a channel. With
    `background`, only where m2 shows its background colour (the colour m2 has most often where the
    two differ): the feature is visible there with nothing in front of it, no translucent interface
    panel or shadow over it."""
    rows = []
    for y in range(m1.height):
        r1, r2 = m1.rows[y], m2.rows[y]
        row = bytearray(m1.width)
        if r1 != r2:
            for x in range(m1.width):
                i = 3 * x
                if max(abs(r1[i] - r2[i]), abs(r1[i + 1] - r2[i + 1]), abs(r1[i + 2] - r2[i + 2])) > tolerance:
                    row[x] = 1
        rows.append(row)
    if background:
        counts = {}
        for y in range(m2.height):
            r2, row = m2.rows[y], rows[y]
            for x in range(m2.width):
                if row[x]:
                    key = r2[3 * x:3 * x + 3]
                    counts[key] = counts.get(key, 0) + 1
        if counts:
            colour = max(counts.items(), key=lambda kv: kv[1])[0]
            for y in range(m2.height):
                r2, row = m2.rows[y], rows[y]
                for x in range(m2.width):
                    if row[x] and r2[3 * x:3 * x + 3] != colour:
                        row[x] = 0
    return rows


def compare(a, b, tolerance, block, mask=None):
    """Returns (differing pixels, max channel difference, sum of channel differences, regions,
    diff rows) where regions maps (bx, by) to [differing pixels, max difference]. With `mask`
    (rows of 0/1) only the flagged pixels count."""
    differing = 0
    worst = 0
    total = 0
    regions = {}
    diff_rows = []
    for y in range(a.height):
        ra, rb = a.rows[y], b.rows[y]
        out = bytearray(a.width * 3)
        mrow = mask[y] if mask is not None else None
        if ra == rb or (mrow is not None and not any(mrow)):
            for x in range(a.width):
                g = (ra[3 * x] + ra[3 * x + 1] + ra[3 * x + 2]) // 9
                out[3 * x:3 * x + 3] = bytes((g, g, g))
            diff_rows.append(out)
            continue
        by = y // block
        for x in range(a.width):
            i = 3 * x
            if mrow is not None and not mrow[x]:
                out[i:i + 3] = bytes((ra[i] // 6, ra[i + 1] // 6, ra[i + 2] // 6))
                continue
            d0 = abs(ra[i] - rb[i])
            d1 = abs(ra[i + 1] - rb[i + 1])
            d2 = abs(ra[i + 2] - rb[i + 2])
            d = max(d0, d1, d2)
            total += d0 + d1 + d2
            if d > worst:
                worst = d
            if d > tolerance:
                differing += 1
                key = (x // block, by)
                r = regions.get(key)
                if r is None:
                    regions[key] = [1, d]
                else:
                    r[0] += 1
                    if d > r[1]:
                        r[1] = d
                out[i:i + 3] = bytes((0, 0, min(255, 96 + d)))
            else:
                g = (ra[i] + ra[i + 1] + ra[i + 2]) // 9
                out[i:i + 3] = bytes((g, g, g))
        diff_rows.append(out)
    return differing, worst, total, regions, diff_rows


def compare_pair(name, path_a, path_b, args, diff_path, mask_paths=None):
    try:
        a = read_bmp(path_a)
        b = read_bmp(path_b)
        masks = [read_bmp(p) for p in mask_paths] if mask_paths else []
    except (OSError, ValueError) as e:
        print(f'{name}: {e}')
        return 2
    if any((m.width, m.height) != (a.width, a.height) for m in [b] + masks):
        print(f'{name}: sizes differ ({", ".join(f"{m.width}x{m.height}" for m in [a, b] + masks)})')
        return 2
    mask = make_mask(masks[0], masks[1], args.tolerance, args.mask_background) if masks else None
    differing, worst, total, regions, diff_rows = compare(a, b, args.tolerance, args.block, mask)
    pixels = sum(sum(row) for row in mask) if mask is not None else a.width * a.height
    if mask is not None:
        print(f'{name}: mask of {pixels} pixels ({100.0 * pixels / (a.width * a.height):.2f}% of the frame)')
    if pixels == 0:
        print(f'{name}: nothing to compare')
        return 2
    fraction = differing / pixels if pixels else 0.0
    ok = fraction <= args.max_fraction
    print(f'{name}: {a.width}x{a.height}, {differing} pixels differ by more than {args.tolerance} '
          f'({100.0 * fraction:.3f}%), largest difference {worst}, mean {total / (3.0 * pixels):.3f} '
          f'per channel: {"ok" if ok else "FAIL"}')
    ranked = sorted(regions.items(), key=lambda kv: (-kv[1][0], -kv[1][1]))[:args.worst]
    for (bx, by), (count, largest) in ranked:
        x0, y0 = bx * args.block, by * args.block
        x1, y1 = min(x0 + args.block, a.width) - 1, min(y0 + args.block, a.height) - 1
        area = (x1 - x0 + 1) * (y1 - y0 + 1)
        print(f'    ({x0},{y0})-({x1},{y1}): {count} of {area} pixels differ, largest difference {largest}')
    if diff_path:
        write_bmp(diff_path, a.width, a.height, diff_rows)
    return 0 if ok else 1


def frames_dir(path):
    sub = os.path.join(path, 'frames')
    return sub if os.path.isdir(sub) else path


def main():
    ap = argparse.ArgumentParser(description='Compare harness frame captures.')
    ap.add_argument('a')
    ap.add_argument('b')
    ap.add_argument('--tolerance', type=int, default=8)
    ap.add_argument('--max-fraction', type=float, default=0.001)
    ap.add_argument('--block', type=int, default=32)
    ap.add_argument('--worst', type=int, default=8)
    ap.add_argument('--diff')
    ap.add_argument('--mask', nargs=2, metavar=('M1', 'M2'))
    ap.add_argument('--mask-background', action='store_true')
    args = ap.parse_args()
    if args.block < 1:
        ap.error('--block must be positive')

    if args.mask and (os.path.isdir(args.a) or os.path.isdir(args.b)):
        ap.error('--mask needs two files')
    if os.path.isdir(args.a) and os.path.isdir(args.b):
        da, db = frames_dir(args.a), frames_dir(args.b)
        names_a = {n for n in os.listdir(da) if n.lower().endswith('.bmp')}
        names_b = {n for n in os.listdir(db) if n.lower().endswith('.bmp')}
        result = 0
        for n in sorted(names_a ^ names_b):
            print(f'{n}: only in {da if n in names_a else db}')
            result = 2
        if args.diff:
            os.makedirs(args.diff, exist_ok=True)
        for n in sorted(names_a & names_b):
            diff_path = os.path.join(args.diff, n) if args.diff else None
            result = max(result, compare_pair(n, os.path.join(da, n), os.path.join(db, n), args, diff_path))
        if not names_a & names_b:
            print('no frames to compare')
            result = 2
        return result
    if os.path.isdir(args.a) or os.path.isdir(args.b):
        ap.error('give two files or two folders')
    return compare_pair(os.path.basename(args.a), args.a, args.b, args, args.diff, args.mask)


if __name__ == '__main__':
    sys.exit(main())
