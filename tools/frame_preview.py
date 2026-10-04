"""Writes a PNG preview of harness frame captures (BMP), side by side, optionally cropped and
downscaled (box average). Standard library only.

usage: python tools/frame_preview.py OUT.png [--scale N] [--crop X0 Y0 X1 Y1] A.bmp [B.bmp ...]
  --scale N   downscale by N in both directions (default 4)
  --crop      the region of each frame to show (full-resolution pixels, X1 / Y1 exclusive)
"""
import argparse
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare_frames  # noqa: E402  (BMP reader)


def shrink(image, scale, crop):
    x0, y0, x1, y1 = crop if crop else (0, 0, image.width, image.height)
    width, height = (x1 - x0) // scale, (y1 - y0) // scale
    rows = []
    n = scale * scale
    for oy in range(height):
        band = image.rows[y0 + oy * scale:y0 + (oy + 1) * scale]
        row = bytearray()
        for ox in range(width):
            b = g = r = 0
            for src in band:
                for k in range(scale):
                    i = 3 * (x0 + ox * scale + k)
                    b += src[i]
                    g += src[i + 1]
                    r += src[i + 2]
            row += bytes((r // n, g // n, b // n))
        rows.append(row)
    return width, height, rows


def write_png(path, width, height, rows):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    raw = b''.join(b'\x00' + bytes(r) for r in rows)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(png)


def main():
    ap = argparse.ArgumentParser(description='PNG preview of frame captures.')
    ap.add_argument('out')
    ap.add_argument('frames', nargs='+')
    ap.add_argument('--scale', type=int, default=4)
    ap.add_argument('--crop', type=int, nargs=4, metavar=('X0', 'Y0', 'X1', 'Y1'))
    args = ap.parse_args()
    if args.scale < 1:
        ap.error('--scale must be positive')
    parts = [shrink(compare_frames.read_bmp(p), args.scale, args.crop) for p in args.frames]
    gap = 8
    width = sum(p[0] for p in parts) + gap * (len(parts) - 1)
    height = max(p[1] for p in parts)
    rows = []
    for y in range(height):
        row = bytearray()
        for k, (w, h, rs) in enumerate(parts):
            row += rs[y] if y < h else bytes(w * 3)
            if k + 1 < len(parts):
                row += b'\xff' * (gap * 3)
        rows.append(row)
    write_png(args.out, width, height, rows)
    print(args.out, width, height)


if __name__ == '__main__':
    main()
