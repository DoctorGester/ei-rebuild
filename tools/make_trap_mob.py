"""Writes traps.mob: zone2's seven magic traps and nothing else, for tests/scenarios/magic_traps.txt.

usage: python tools/make_trap_mob.py <saves dir> [--source <zone2.mob>]
  <saves dir>   the run's harness_saves folder (the harness sends <game root>saves\\ there); the
                scenario loads the file with AddMob("..\\saves\\traps.mob") (AddMob's names are
                relative to <game root>maps\\: 00595BA0 / 0055E990).
  --source      default F:\\games\\EvilIslands\\Maps\\zone2.mob (read only)
  --interval N  sets every trap's interval (field 0xBBAB0005; zone2's are 50 and 150). With 0 the
                trap's update asks MagicTrap::UpdateVictims for newcomers instead of firing every tick
                (magic_traps_victims.txt; none of the game's zones has such a trap).

The full zone2.mob stalls the original game when AddMob loads it into zone1 (one of its other
objects or units). The .mob object reader (00481F00) walks the children of the root chunk 0xA000:
0xC000 / 0xD000 say whether the diplomacy table (0xDDDDDDD1) is applied to the world afterwards,
0xB000 holds the object records (0xBBAB0000 a magic trap), and 0xABD0 (weather), the scripts
(0xACCEECCB, read only by the zone loader) and the other chunks are not needed. The trimmed file is
one 0xA000 root holding 0xD000 (keep zone1's diplomacy), zone2's 0xDDDDDDD1 (the reader still fills
its local table from it, and a trap's side mask comes from that table, 005927C0 record type 7) and
a 0xB000 with only the 0xBBAB0000 records, copied byte for byte except their object ids.

The ids: AddMob keeps each record's id (field 0xB002), and CBaseManager::CreateObject (00403730)
given an id that is already taken simply overwrites the id map's entry. zone2's traps are
0x52C..0x532, which zone1's own objects also use; the orphaned zone1 objects then make
CWorldServer::RemoveObject (005935C0) dereference a missing map entry (0059362A) when save1 is
loaded. The traps get 30001..30007 instead (zone1's ids end at 2008), so the scenario can also find
them with GetObject(id): the zone loader does not register traps by name (005927C0, record type 7).

Chunk = id, size (both u32, the size counting the 8-byte header), then the contents.
"""
import argparse, os, struct, sys

ROOT_CHUNK = 0xA000
NO_DIPLOMACY = 0xD000
DIPLOMACY = 0xDDDDDDD1
OBJECTS = 0xB000
MAGIC_TRAP = 0xBBAB0000
OBJECT_ID = 0xB002      # a record's object id (u32)
FIRST_ID = 30001        # the traps get 30001..30007


def chunks(data, start, end):
    off = start
    while off + 8 <= end:
        cid, size = struct.unpack_from('<II', data, off)
        if size < 8 or off + size > end:
            raise ValueError(f'bad chunk at {off:#x}')
        yield cid, off, size
        off += size


def renumber(data, off, size, new_id):
    """The record at `off` with its object id (field 0xB002) set to `new_id`."""
    record = bytearray(data[off:off + size])
    fields = [(c, o - off) for c, o, s in chunks(data, off + 8, off + size) if c == OBJECT_ID]
    if len(fields) != 1:
        raise ValueError(f'record at {off:#x}: {len(fields)} id fields')
    struct.pack_into('<I', record, fields[0][1] + 8, new_id)
    return bytes(record)


def set_interval(record, value):
    """The magic trap record `record` with its interval field (0xBBAB0005, a u32) set to `value`."""
    record = bytearray(record)
    fields = [o for c, o, s in chunks(record, 8, len(record)) if c == 0xBBAB0005]
    if len(fields) != 1:
        raise ValueError(f'{len(fields)} interval fields')
    struct.pack_into('<I', record, fields[0] + 8, value)
    return bytes(record)


def chunk(cid, body):
    return struct.pack('<II', cid, 8 + len(body)) + body


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('saves')
    parser.add_argument('--source', default=r'F:\games\EvilIslands\Maps\zone2.mob')
    parser.add_argument('--interval', type=int, default=None)
    args = parser.parse_args()
    data = open(args.source, 'rb').read()
    root = next(c for c in chunks(data, 0, len(data)) if c[0] == ROOT_CHUNK)
    diplomacy = b''
    traps = []
    for cid, off, size in chunks(data, root[1] + 8, root[1] + root[2]):
        if cid == DIPLOMACY:
            diplomacy = data[off:off + size]
        elif cid == OBJECTS:
            for c, o, s in chunks(data, off + 8, off + size):
                if c == MAGIC_TRAP:
                    traps.append(renumber(data, o, s, FIRST_ID + len(traps)))
    if args.interval is not None:
        traps = [set_interval(t, args.interval) for t in traps]
    if not diplomacy or not traps:
        sys.exit(f'{args.source}: no diplomacy chunk or no magic traps')
    out = chunk(ROOT_CHUNK, chunk(NO_DIPLOMACY, b'') + diplomacy + chunk(OBJECTS, b''.join(traps)))
    os.makedirs(args.saves, exist_ok=True)
    path = os.path.join(args.saves, 'traps.mob')
    open(path, 'wb').write(out)
    print(f'{path}: {len(traps)} magic traps, {len(out)} bytes')


if __name__ == '__main__':
    main()
