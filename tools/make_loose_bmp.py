"""Writes loose .bmp textures for tests/scenarios/render_loose_bmp.txt (A-I23a).

usage: python tools/make_loose_bmp.py <saves dir> [--size N]
  <saves dir>  the run's harness_saves folder. The harness's copy-on-write overlay keeps the install's files
               there by their relative path and a read (CFileFind::FindFile, CFile::Open) takes the overlay
               copy when there is one, so <saves dir>\textures\<name>.bmp is what the game finds as its
               textures\<name>.bmp. Nothing under the install is written (it has no textures\*.bmp).
  --size       the textures' width and height (default 256; the .mmp pages are 512 x 512)

CTextureManager::LoadTexture (006922A0) looks for <game root>textures\<name>.bmp before the archives and
loads it with CTexture::LoadBmp (00690260) -> LoadBmpFiles (0068FBF0), which reads it with CDib::Read
(006D5910) and its alpha from a_<name>.bmp, an 8-bit grey .bmp, when that exists. Zone 1's terrain pages
are the textures Zone1000 .. Zone1007 (512 x 512 .mmp in textures.res; Terrain::SetupPages 006C5AA0), drawn
over all the ground around the hero. Each one is replaced by four quadrants, seen from the top of the
picture: red and green above, blue and yellow below (the picture is stored bottom-up, as .bmp files are).
The alpha file is a horizontal ramp (the terrain pages' format 5:6:5 has no alpha: the file is read, its
8-bit depth checked, its bytes unused).
"""
import argparse, os, struct

PAGES = [f'Zone1{i:03d}' for i in range(8)]
QUADRANTS = [[(255, 0, 0), (0, 255, 0)], [(0, 0, 255), (255, 255, 0)]]   # (R, G, B), top row first


def bmp24(size):
    rows = []
    for y in range(size - 1, -1, -1):      # stored bottom-up
        row = bytearray()
        for x in range(size):
            r, g, b = QUADRANTS[y * 2 // size][x * 2 // size]
            row += bytes((b, g, r))
        rows.append(bytes(row) + b'\0' * (-len(row) % 4))
    data = b''.join(rows)
    info = struct.pack('<IiiHHIIiiII', 40, size, size, 1, 24, 0, len(data), 2835, 2835, 0, 0)
    return b'BM' + struct.pack('<IHHI', 14 + len(info) + len(data), 0, 0, 14 + len(info)) + info + data


def bmp8(size):
    palette = b''.join(bytes((i, i, i, 0)) for i in range(256))
    ramp = bytes(x * 256 // size for x in range(size))
    data = (ramp + b'\0' * (-len(ramp) % 4)) * size
    info = struct.pack('<IiiHHIIiiII', 40, size, size, 1, 8, 0, len(data), 2835, 2835, 256, 0)
    head = 14 + len(info) + len(palette)
    return b'BM' + struct.pack('<IHHI', head + len(data), 0, 0, head) + info + palette + data


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('saves')
    parser.add_argument('--size', type=int, default=256)
    args = parser.parse_args()
    folder = os.path.join(args.saves, 'textures')
    os.makedirs(folder, exist_ok=True)
    colour, alpha = bmp24(args.size), bmp8(args.size)
    for name in PAGES:
        open(os.path.join(folder, name + '.bmp'), 'wb').write(colour)
        open(os.path.join(folder, 'a_' + name + '.bmp'), 'wb').write(alpha)
    print(f'{folder}: {len(PAGES)} textures of {args.size} x {args.size}, {len(colour)} + {len(alpha)} bytes each')


if __name__ == '__main__':
    main()
