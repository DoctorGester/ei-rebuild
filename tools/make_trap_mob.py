"""Writes traps.mob: zone2's seven magic traps and nothing else, for tests/scenarios/magic_traps.txt.

usage: python tools/make_trap_mob.py <saves dir> [--source <zone2.mob>]
  <saves dir>   the run's harness_saves folder (the harness sends <game root>saves\\ there); the
                scenario loads the file with AddMob("..\\saves\\traps.mob") (AddMob's names are
                relative to <game root>maps\\: 00595BA0 / 0055E990).
  --source      default F:\\games\\EvilIslands\\Maps\\zone2.mob (read only)

The full zone2.mob stalls the original game when AddMob loads it into zone1 (one of its other
objects or units). The .mob object reader (00481F00) walks the children of the root chunk 0xA000:
0xC000 / 0xD000 say whether the diplomacy table (0xDDDDDDD1) is applied to the world afterwards,
0xB000 holds the object records (0xBBAB0000 a magic trap), and 0xABD0 (weather), the scripts
(0xACCEECCB, read only by the zone loader) and the other chunks are not needed. The trimmed file is
one 0xA000 root holding 0xD000 (keep zone1's diplomacy), zone2's 0xDDDDDDD1 (the reader still fills
its local table from it, and a trap's side mask comes from that table, 005927C0 record type 7) and
a 0xB000 with only the 0xBBAB0000 records, all copied byte for byte. Chunk = id, size (both u32,
the size counting the 8-byte header), then the contents.
"""
import argparse, os, struct, sys

ROOT_CHUNK = 0xA000
NO_DIPLOMACY = 0xD000
DIPLOMACY = 0xDDDDDDD1
OBJECTS = 0xB000
MAGIC_TRAP = 0xBBAB0000


def chunks(data, start, end):
    off = start
    while off + 8 <= end:
        cid, size = struct.unpack_from('<II', data, off)
        if size < 8 or off + size > end:
            raise ValueError(f'bad chunk at {off:#x}')
        yield cid, off, size
        off += size


def chunk(cid, body):
    return struct.pack('<II', cid, 8 + len(body)) + body


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('saves')
    parser.add_argument('--source', default=r'F:\games\EvilIslands\Maps\zone2.mob')
    args = parser.parse_args()
    data = open(args.source, 'rb').read()
    root = next(c for c in chunks(data, 0, len(data)) if c[0] == ROOT_CHUNK)
    diplomacy = b''
    traps = []
    for cid, off, size in chunks(data, root[1] + 8, root[1] + root[2]):
        if cid == DIPLOMACY:
            diplomacy = data[off:off + size]
        elif cid == OBJECTS:
            traps = [data[o:o + s] for c, o, s in chunks(data, off + 8, off + size) if c == MAGIC_TRAP]
    if not diplomacy or not traps:
        sys.exit(f'{args.source}: no diplomacy chunk or no magic traps')
    out = chunk(ROOT_CHUNK, chunk(NO_DIPLOMACY, b'') + diplomacy + chunk(OBJECTS, b''.join(traps)))
    os.makedirs(args.saves, exist_ok=True)
    path = os.path.join(args.saves, 'traps.mob')
    open(path, 'wb').write(out)
    print(f'{path}: {len(traps)} magic traps, {len(out)} bytes')


if __name__ == '__main__':
    main()
