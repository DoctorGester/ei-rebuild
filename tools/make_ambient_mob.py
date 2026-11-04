"""Writes zone15.mob with its sounds made ambient, for tests/scenarios/ambient_sounds.txt (H43c).

usage: python tools/make_ambient_mob.py <saves dir> [--zone NAME] [--source <NAME.mob>]
  <saves dir>  the run's harness_saves folder. The harness's copy-on-write overlay keeps files there by their path
               under the install, and a read takes the overlay copy when there is one, so <saves dir>\\Maps\\NAME.mob
               is what the zone loader finds as maps\\NAME.mob. Nothing under the install is written.
  --zone       the zone's map file (default zone15, gz15h's: twelve sounds)
  --source     default the vanilla install's Maps\\NAME.mob (EI_GAME_VANILLA; read only)

None of the game's zones has an ambient sound (a sound record's chunk 0xCC0D, one byte, is 0 in every sound of every
.mob): the sound manager's emitter, which the server's EffectAmbientSound feeds the client, is then never made. This
copy has the byte set to 1 in each of the zone's sound records (0xCC01, children of the objects chunk 0xB000), the
file's size and every other byte unchanged.
"""
import argparse, os, struct, sys

import exe_path

OBJECTS = 0xB000
SOUND = 0xCC01
AMBIENT = 0xCC0D


def chunks(data, start, end):
    off = start
    while off + 8 <= end:
        cid, size = struct.unpack_from('<II', data, off)
        if size < 8 or off + size > end:
            raise ValueError(f'bad chunk at {off:#x}')
        yield cid, off, size
        off += size


def make_ambient(data):
    """The .mob's bytes with every sound record's ambient byte set; the number of records changed."""
    data = bytearray(data)
    changed = 0
    root = next(c for c in chunks(data, 0, len(data)) if c[0] == 0xA000)
    for cid, off, size in chunks(data, root[1] + 8, root[1] + root[2]):
        if cid != OBJECTS:
            continue
        for c, o, s in chunks(data, off + 8, off + size):
            if c != SOUND:
                continue
            for field, fo, fs in chunks(data, o + 8, o + s):
                if field == AMBIENT and fs == 9:
                    data[fo + 8] = 1
                    changed += 1
    return bytes(data), changed


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('saves')
    parser.add_argument('--zone', default='zone15')
    parser.add_argument('--source', default=None)
    args = parser.parse_args()
    source = args.source or exe_path.install_file('vanilla', 'Maps', args.zone + '.mob')
    data, changed = make_ambient(open(source, 'rb').read())
    if not changed:
        sys.exit(f'{source}: no sound record with an ambient field')
    folder = os.path.join(args.saves, 'Maps')
    os.makedirs(folder, exist_ok=True)
    path = os.path.join(folder, args.zone + '.mob')
    open(path, 'wb').write(data)
    print(f'{path}: {changed} sounds made ambient, {len(data)} bytes')


if __name__ == '__main__':
    main()
