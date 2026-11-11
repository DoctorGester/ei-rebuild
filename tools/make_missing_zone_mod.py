r"""A mod whose quest zones have no objects (MF13d2a; tests/scenarios/net_zone_missing_map.txt).

usage: python tools/make_missing_zone_mod.py <saves dir>

Writes <saves dir>\missing_zone_mod (the scenario's `mod harness_saves\missing_zone_mod`): a config.reg with [MOD]
Title, Rebirth.txt and Res\texts\map.toml with a record for each quest zone on zone3Obr (z3q1-z3q3: the group of the
first multiplayer base sheriff's second topic): a game zone on the game's map zone3Obr whose objects are
"mf13d2a_missing", which neither the mod nor any install has. NewZone refuses them after the old zone was torn down
(docs/notes/world.md, "Party between zones").
- The map stays zone3Obr: the zone table groups the quest zones by their map (GameFlow::BuildZoneTable); a zone on
  another map is not offered.
- Each record is whole (kind and map too): z3q2 and z3q3 come from their quest folders' map.txt, which the game reads
  after the map.toml files, so the mod's record is a new one that FindMap finds first (MapDatabase::LoadAll; MF13d2b).
"""
import os, sys

import make_test_mod as mod

MOD = 'missing_zone_mod'
ZONES = ['z3q1', 'z3q2', 'z3q3']
MAP_TOML = ''.join(f'[[zone]]\nname = "{zone}"\nkind = "game"\nmap = "zone3Obr"\nmob = "mf13d2a_missing"\n\n'
                   for zone in ZONES)


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(data)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    root = os.path.join(sys.argv[1], MOD)
    write(os.path.join(root, 'config.reg'), mod.registry({'MOD': {'Title': 'MF13d2a missing zone'}}))
    write(os.path.join(root, 'Rebirth.txt'), b'')
    write(os.path.join(root, 'Res', 'texts', 'map.toml'), MAP_TOML.encode('utf-8'))
    print(f'{root}: written')


if __name__ == '__main__':
    main()
