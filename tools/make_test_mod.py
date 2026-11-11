"""Writes a small Rebirth mod for tests/scenarios/rebirth_mod_folders.txt (S19b).

usage: python tools/make_test_mod.py <saves dir> [--maps-source <zone2.mob>] [--camera-source <bz3g.cam>]
  <saves dir>      the run's harness_saves folder; the mod goes to <saves dir>\\s19b_mod, which the
                   scenario selects with the harness step "mod harness_saves\\s19b_mod" (the settings' Mod)
  --maps-source    passed to tools/make_trap_mob.py (default its own, read only)
  --camera-source  default the vanilla install's camera\\bz3g.cam (EI_GAME_VANILLA; read only)

The mod, as Rebirth's starter\\MODS\\<mod> folders hold one (docs/notes/rebirth.md, "Resources and paths"):
  Rebirth.txt          empty, as every shipped mod has one
  config.reg           [MOD] Title, and [RES] gfx.res = Res\\s19b_gfx.res: that archive comes before the game's
                       gfx.res (ResOverride)
  Res\\s19b_gfx.res     one file, s19b.txt (S19B_TEXT)
  Maps\\traps.mob       zone2's magic traps (tools/make_trap_mob.py): AddMob("traps.mob") finds it there (Maps)
  Camera\\s19b.cam      a copy of the game's camera\\bz3g.cam: PlayCamera("s19b.cam") plays it (CameraPatch)

config.reg is the game's binary registry (src/engine/data/registry.h); the archive the game's .res (src/engine/data/resource.h).
"""
import argparse, os, shutil, struct, subprocess, sys

import exe_path

TOOLS = os.path.dirname(os.path.abspath(__file__))
MOD = 's19b_mod'
S19B_TEXT = b'S19b mod archive file\r\n'   # 23 bytes: the scenario's "modfile gfx.res s19b.txt 23"

REGISTRY_MAGIC = 0x45AB3EFB
RES_MAGIC = 0x019CE23C
REG_INT, REG_STRING, REG_ARRAY = 0, 2, 0x80


def home_slot(name, slots):
    total = 0
    for c in name.encode('cp1251'):
        total += (c - 256 if c >= 128 else c) if not (65 <= c <= 90) else c + 32
    return total % slots


def hash_table(names):
    """Slot of each name and each slot's next (0xFFFF ends a chain): home slots first, the rest in the free ones."""
    n = len(names)
    slot_of, taken = {}, [None] * n
    for name in names:
        h = home_slot(name, n)
        if taken[h] is None:
            taken[h] = name
            slot_of[name] = h
    free = [i for i in range(n) if taken[i] is None]
    nxt = [0xFFFF] * n
    for name in names:
        if name in slot_of:
            continue
        s = free.pop(0)
        taken[s] = name
        slot_of[name] = s
        tail = home_slot(name, n)
        while nxt[tail] != 0xFFFF:
            tail = nxt[tail]
        nxt[tail] = s
    return slot_of, nxt


def text(value):
    raw = value.encode('cp1251')
    return struct.pack('<H', len(raw)) + raw


def key_record(name, value):
    """A key's record: an int, a string, or (a list of strings) an array of them."""
    raw = name.encode('cp1251')
    if isinstance(value, list):
        head = struct.pack('<BH', REG_ARRAY | REG_STRING, len(raw)) + raw
        return head + struct.pack('<H', len(value)) + b''.join(text(v) for v in value)
    head = struct.pack('<BH', REG_INT if isinstance(value, int) else REG_STRING, len(raw)) + raw
    return head + (struct.pack('<I', value) if isinstance(value, int) else text(value))


def section_record(name, keys):
    raw = name.encode('cp1251')
    names = list(keys)
    slot_of, nxt = hash_table(names)
    head = struct.pack('<HH', len(names), len(raw)) + raw
    table_size = 6 * len(names)
    body, offsets = b'', {}
    for k in names:
        offsets[k] = len(head) + table_size + len(body)
        body += key_record(k, keys[k])
    table = bytearray(table_size)
    for k in names:
        struct.pack_into('<HI', table, 6 * slot_of[k], nxt[slot_of[k]], offsets[k])
    return head + bytes(table) + body


def registry(sections):
    names = list(sections)
    slot_of, nxt = hash_table(names)
    head_size = 6 + 6 * len(names)
    body, offsets = b'', {}
    for s in names:
        offsets[s] = head_size + len(body)
        body += section_record(s, sections[s])
    table = bytearray(6 * len(names))
    for s in names:
        struct.pack_into('<HI', table, 6 * slot_of[s], nxt[slot_of[s]], offsets[s])
    return struct.pack('<IH', REGISTRY_MAGIC, len(names)) + bytes(table) + body


def archive(files):
    """A .res: header, the files' data, the entry table (a hash table like the registry's), the names."""
    names = list(files)
    n = len(names)
    slot_of, nxt = hash_table(names)
    data, offsets = b'', {}
    for f in names:
        offsets[f] = 16 + len(data)
        data += files[f]
    name_block, name_offsets = b'', {}
    for f in names:
        name_offsets[f] = len(name_block)
        name_block += f.encode('cp1251')
    table = bytearray(22 * n)
    for f in names:
        s = slot_of[f]
        struct.pack_into('<iIIIHI', table, 22 * s, -1 if nxt[s] == 0xFFFF else nxt[s], len(files[f]), offsets[f], 0,
                         len(f.encode('cp1251')), name_offsets[f])
    return struct.pack('<IIII', RES_MAGIC, n, 16 + len(data), len(name_block)) + data + bytes(table) + name_block


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('saves')
    parser.add_argument('--maps-source')
    parser.add_argument('--camera-source', default=None)
    args = parser.parse_args()
    mod = os.path.join(args.saves, MOD)
    for sub in ('Res', 'Maps', 'Camera'):
        os.makedirs(os.path.join(mod, sub), exist_ok=True)
    open(os.path.join(mod, 'Rebirth.txt'), 'wb').close()
    config = registry({'MOD': {'Title': 'S19b test mod'}, 'RES': {'gfx.res': 'Res\\s19b_gfx.res'}})
    open(os.path.join(mod, 'config.reg'), 'wb').write(config)
    open(os.path.join(mod, 'Res', 's19b_gfx.res'), 'wb').write(archive({'s19b.txt': S19B_TEXT}))
    trap = [sys.executable, os.path.join(TOOLS, 'make_trap_mob.py'), os.path.join(mod, 'Maps')]
    if args.maps_source:
        trap += ['--source', args.maps_source]
    subprocess.run(trap, check=True, capture_output=True)
    camera = args.camera_source or exe_path.install_file('vanilla', 'camera', 'bz3g.cam')
    if not os.path.exists(camera) and os.path.exists(camera + '.toml'):
        # A converted install (tools/convert_install.py): the path's TOML, which stands for it alike.
        shutil.copyfile(camera + '.toml', os.path.join(mod, 'Camera', 's19b.cam.toml'))
    else:
        shutil.copyfile(camera, os.path.join(mod, 'Camera', 's19b.cam'))
    print(f'{mod}: config.reg {len(config)} bytes, Res\\s19b_gfx.res, Maps\\traps.mob, Camera\\s19b.cam')


if __name__ == '__main__':
    main()
