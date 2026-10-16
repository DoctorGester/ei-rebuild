"""Compares frames captured by the harness ("capture <name>": <run folder>\\frames\\<name>.bmp).

usage: python tools/compare_frames.py A B [--tolerance N] [--max-fraction F] [--block N] [--worst N]
                                          [--diff OUT] [--mask M1 M2 [--mask-background]]
                                          [--exclude E1 E2 ...] [--exclude-grow N] [--exclude-mask M ...]
                                          [--exclude-rect X0 Y0 X1 Y1 ...] [--write-skip OUT] [--shift N]
                                          [--max-block F]
  A, B             two BMP files, or two folders: run folders (their frames\\ subfolder is used) or
                    frames folders; every <name>.bmp present in both is compared
  --tolerance N     a pixel differs when one of its channels differs by more than N (0..255,
                    default 8: the 5/6-bit steps of a 16-bit surface are 8 and 4)
  --max-fraction F  the comparison fails when more than F of the pixels differ (default 0.001)
  --block N         region size for the worst-regions list and --max-block (default 32 pixels)
  --worst N         regions listed per frame (default 8)
  --diff OUT        write a difference image: a dimmed grey copy of A with the differing pixels in
                    red (brighter = larger difference); with folders OUT is a folder
  --mask M1 M2      compare only the pixels where the frames M1 and M2 differ (by more than the
                    tolerance), e.g. the old frame with and without its terrain to compare the new
                    renderer's terrain (A) with the old one (B) where the terrain is (files only);
                    the fraction is then of the masked pixels
  --mask-background keep only the masked pixels where M2 shows its background (clear) colour: the
                    feature with nothing in front of it (no interface panel, shadow or object)
  --exclude E1 E2   leave out the pixels where the frames E1 and E2 differ (repeatable; with or
                    without --mask), e.g. the old frame with and without its objects to compare
                    only the terrain around them
  --exclude-grow N  widen every excluded area by N pixels (blended edges; default 0)
  --exclude-mask M  leave out the pixels that are not black in the frame M (repeatable), e.g. a mask
                    --write-skip wrote
  --exclude-rect X0 Y0 X1 Y1
                    leave out a rectangle (X1, Y1 excluded; repeatable)
  --write-skip OUT  write the pixels --exclude, --exclude-mask and --exclude-rect leave out of A as a
                    mask (white) to OUT instead of comparing A with B (files only)
  --shift N         a pixel differs only when one of its channels lies more than --tolerance outside the
                    range the other frame's pixels within N pixels of it span (both ways): frames drawn
                    by two renderers, which sample magnified textures a fraction of a texel apart and
                    rasterise edges and lines a pixel apart (run_all --widescreen; default 0)
  --max-block F     the comparison also fails when more than F of one --block region's pixels differ
                    (a moved element among scattered edge differences; default 1)
exit code: 0 when every frame is within --max-fraction (and --max-block), 1 when one is not, 2 when a frame is
missing, unreadable or of another size

Only the standard library: BMP (24 or 32 bits, uncompressed, either row order) is read by hand.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import frame_store  # noqa: E402


class Image:
    def __init__(self, width, height, rows):
        self.width = width
        self.height = height
        self.rows = rows   # top-down, each width * 3 bytes (B, G, R)


def read_bmp(path):
    data = frame_store.read_frame(path)       # a .bmp, or the .bmp.z a run's frames are stored as (WB11)
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


def grow(rows, width, n):
    """The 0/1 rows with every 1 widened by n pixels in each direction (a square)."""
    if n <= 0:
        return rows
    wide = []
    for row in rows:
        out = bytearray(width)
        if any(row):
            for x in range(width):
                if row[x]:
                    out[max(0, x - n):min(width, x + n + 1)] = b'\x01' * (min(width, x + n + 1) - max(0, x - n))
        wide.append(out)
    result = []
    for y in range(len(wide)):
        out = bytearray(width)
        for k in range(max(0, y - n), min(len(wide), y + n + 1)):
            src = wide[k]
            if any(src):
                for x in range(width):
                    if src[x]:
                        out[x] = 1
        result.append(out)
    return result


def apply_excludes(mask, excludes, width, height, tolerance, grow_by):
    """`mask` (rows of 0/1, or None for every pixel) without the pixels where each pair of
    `excludes` differs."""
    if mask is None:
        mask = [bytearray(b'\x01' * width) for _ in range(height)]
    for e1, e2 in excludes:
        drop = grow(make_mask(e1, e2, tolerance), width, grow_by)
        for y in range(height):
            row, d = mask[y], drop[y]
            if any(d):
                for x in range(width):
                    if d[x]:
                        row[x] = 0
    return mask


def near(p0, p1, p2, image, x, y, n, tolerance):
    """Whether each channel of (p0, p1, p2) lies within the range image's pixels within n pixels of
    (x, y) (a square) span in that channel, widened by tolerance: the value a filter sampling a
    fraction of a pixel apart could give."""
    lo0 = lo1 = lo2 = 255
    hi0 = hi1 = hi2 = 0
    for yy in range(max(0, y - n), min(image.height, y + n + 1)):
        r = image.rows[yy]
        for i in range(3 * max(0, x - n), 3 * min(image.width, x + n + 1), 3):
            c0, c1, c2 = r[i], r[i + 1], r[i + 2]
            lo0, hi0 = min(lo0, c0), max(hi0, c0)
            lo1, hi1 = min(lo1, c1), max(hi1, c1)
            lo2, hi2 = min(lo2, c2), max(hi2, c2)
    return (lo0 - tolerance <= p0 <= hi0 + tolerance and lo1 - tolerance <= p1 <= hi1 + tolerance
            and lo2 - tolerance <= p2 <= hi2 + tolerance)


def apply_skips(mask, skips, rects, width, height):
    """`mask` (rows of 0/1, or None for every pixel) without the pixels that are not black in any of
    `skips` (masks written by --write-skip) and the rectangles `rects` ((x0, y0, x1, y1), x1 and y1
    excluded)."""
    if mask is None:
        mask = [bytearray(b'\x01' * width) for _ in range(height)]
    for skip in skips:
        for y in range(height):
            r, row = skip.rows[y], mask[y]
            if any(r):
                for x in range(width):
                    if r[3 * x] or r[3 * x + 1] or r[3 * x + 2]:
                        row[x] = 0
    for x0, y0, x1, y1 in rects:
        x0, x1 = max(0, x0), min(width, x1)
        for y in range(max(0, y0), min(height, y1)):
            if x1 > x0:
                mask[y][x0:x1] = bytes(x1 - x0)
    return mask


def compare(a, b, tolerance, block, mask=None, shift=0):
    """Returns (differing pixels, max channel difference, sum of channel differences, regions,
    diff rows) where regions maps (bx, by) to [differing pixels, max difference]. With `mask`
    (rows of 0/1) only the flagged pixels count. With `shift` a pixel differs only when neither
    frame has a pixel within `shift` pixels of it that matches the other frame's pixel there
    (edges and lines rasterised half a pixel apart)."""
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
            if d > tolerance and shift and near(ra[i], ra[i + 1], ra[i + 2], b, x, y, shift, tolerance) \
                    and near(rb[i], rb[i + 1], rb[i + 2], a, x, y, shift, tolerance):
                d = tolerance
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


def compare_pair(name, path_a, path_b, args, diff_path, mask_paths=None, exclude_paths=None):
    try:
        a = read_bmp(path_a)
        b = read_bmp(path_b)
        masks = [read_bmp(p) for p in mask_paths] if mask_paths else []
        excludes = [(read_bmp(p), read_bmp(q)) for p, q in exclude_paths] if exclude_paths else []
        skips = [read_bmp(p) for p in getattr(args, 'exclude_mask', None) or []]
    except (OSError, ValueError) as e:
        print(f'{name}: {e}')
        return 2
    others = masks + [m for pair in excludes for m in pair] + skips
    if any((m.width, m.height) != (a.width, a.height) for m in [b] + others):
        print(f'{name}: sizes differ ({", ".join(f"{m.width}x{m.height}" for m in [a, b] + others)})')
        return 2
    mask = make_mask(masks[0], masks[1], args.tolerance, args.mask_background) if masks else None
    if excludes:
        mask = apply_excludes(mask, excludes, a.width, a.height, args.tolerance, args.exclude_grow)
    rects = getattr(args, 'exclude_rect', None) or []
    if skips or rects:
        mask = apply_skips(mask, skips, rects, a.width, a.height)
    if getattr(args, 'write_skip', None):
        white, black = b'\xff\xff\xff', b'\0\0\0'
        rows = [b''.join(black if m else white for m in row) for row in mask] if mask is not None else \
            [black * a.width] * a.height
        write_bmp(args.write_skip, a.width, a.height, rows)
        print(f'{name}: {sum(row.count(0) for row in mask) if mask is not None else 0} pixels left out, '
              f'written to {args.write_skip}')
        return 0
    differing, worst, total, regions, diff_rows = compare(a, b, args.tolerance, args.block, mask, args.shift)
    pixels = sum(sum(row) for row in mask) if mask is not None else a.width * a.height
    if mask is not None:
        print(f'{name}: mask of {pixels} pixels ({100.0 * pixels / (a.width * a.height):.2f}% of the frame)')
    if pixels == 0:
        print(f'{name}: nothing to compare')
        return 2
    fraction = differing / pixels if pixels else 0.0

    def area(bx, by):
        x0, y0 = bx * args.block, by * args.block
        return x0, y0, min(x0 + args.block, a.width) - 1, min(y0 + args.block, a.height) - 1

    def share(key):
        x0, y0, x1, y1 = area(*key)
        return regions[key][0] / ((x1 - x0 + 1) * (y1 - y0 + 1))

    densest = max(regions, key=share) if regions else None
    dense = densest is not None and share(densest) > getattr(args, 'max_block', 1.0)
    ok = fraction <= args.max_fraction and not dense
    print(f'{name}: {a.width}x{a.height}, {differing} pixels differ by more than {args.tolerance} '
          f'({100.0 * fraction:.3f}%), largest difference {worst}, mean {total / (3.0 * pixels):.3f} '
          f'per channel: {"ok" if ok else "FAIL"}')
    if dense:
        x0, y0, x1, y1 = area(*densest)
        print(f'    a region differs densely: ({x0},{y0})-({x1},{y1}) {100.0 * share(densest):.1f}% '
              f'(at most {100.0 * args.max_block:.1f}% of a {args.block}-pixel block)')
    ranked = sorted(regions.items(), key=lambda kv: (-kv[1][0], -kv[1][1]))[:args.worst]
    for (bx, by), (count, largest) in ranked:
        x0, y0, x1, y1 = area(bx, by)
        print(f'    ({x0},{y0})-({x1},{y1}): {count} of {(x1 - x0 + 1) * (y1 - y0 + 1)} pixels differ, '
              f'largest difference {largest}')
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
    ap.add_argument('--max-block', type=float, default=1.0)
    ap.add_argument('--worst', type=int, default=8)
    ap.add_argument('--diff')
    ap.add_argument('--mask', nargs=2, metavar=('M1', 'M2'))
    ap.add_argument('--mask-background', action='store_true')
    ap.add_argument('--exclude', nargs=2, action='append', metavar=('E1', 'E2'))
    ap.add_argument('--exclude-grow', type=int, default=0)
    ap.add_argument('--shift', type=int, default=0)
    ap.add_argument('--exclude-mask', action='append', metavar='M')
    ap.add_argument('--exclude-rect', nargs=4, type=int, action='append', metavar=('X0', 'Y0', 'X1', 'Y1'))
    ap.add_argument('--write-skip')
    args = ap.parse_args()
    if args.block < 1:
        ap.error('--block must be positive')

    if (args.mask or args.exclude) and (os.path.isdir(args.a) or os.path.isdir(args.b)):
        ap.error('--mask and --exclude need two files')
    if os.path.isdir(args.a) and os.path.isdir(args.b):
        da, db = frames_dir(args.a), frames_dir(args.b)
        names_a = set(frame_store.frame_names(da))
        names_b = set(frame_store.frame_names(db))
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
    return compare_pair(os.path.basename(args.a), args.a, args.b, args, args.diff, args.mask, args.exclude)


if __name__ == '__main__':
    sys.exit(main())
