r"""Writes a multiplayer hero, mp\<n>.mp, into a run's overlay (a '# prepare:' tool; SV5d3).

usage: python tools/make_mp_hero.py <saves dir> [--name NAME] [--file N.mp] [--stats S D I] [--skills N]
  <saves dir>  the run's harness_saves folder: the game's mp\ folder is read from <saves dir>\mp only (harness.cpp
               OverlayOnly), so the hero screen lists exactly the heroes written there.
  --name       the hero's name (default "host")
  --file       the file name (default 1.mp: the game numbers its heroes 1.mp, 2.mp, ...)
  --stats      strength, dexterity and intelligence (default 25 25 25, a new hero's)
  --skills     every skill byte (default 0, a new hero's)
  --damage     the weapon's damage (default 9.12, the new hero's axe)
  --absorb     every armour piece's absorption of each damage type, both layers (default: the new kit's)

The file is what the game writes for a hero made on its multiplayer hero screen (MpHeroRecord::WriteFile 00662AC0;
the same bytes on both installs): SaveFileWrite's frame (magic 0x114, version 0x74, the checksum of the plain data)
with the data stored uncompressed (flag 0, which SaveFileRead 00579830 reads as it is), and CParty::SaveParty's
data with its variables (tools/savefile_scenario.py read_party): the zone bz1mpg, the new hero's kit (a weapon,
seven armour pieces, a quick item and a bag item with their two spells), one PartyUnitRecord and 500 gold. With
the defaults the decoded file equals a new hero's but for the money key. A stronger hero (net_pair_quest: a
level 1 hero loses every multiplayer quest's fight) raises the three statistics and their hit points as the
game's own UnitStats::Recompute / RecomputeHitPoints would (the hit point curve is linear in strength; the six
body slots share the maximum 1:3:1:1:2:2), the skill bytes, the weapon's damage and the armour's absorption. `python tools/savefile.py show <file>` decodes it.
"""
import argparse, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import savefile

VERSION = 0x74
END_OF_OBJECTS = -0x521          # CBaseManager::SaveObjects' end mark
HERO_ID = 1000011214             # a text-key unit id (IsTextKey: 1000000000..1999999999), as the game gave one
MONEY_KEY = 0x5A5A1234           # the money is stored XOR a random key (CParty::SaveParty)
BODY_HP_AT_25 = 47.5613632       # the body slot's maximum (the hit point maximum) of a new hero, strength 25

ARMOUR_LIGHT = (1.947963, 2.43495369)
ARMOUR_HEAVY = (3.86809778, 4.83512259)

# (id, prototype, condition, charge, weight, price, value3C, absorbs of layer 0, of layer 1): a new hero's armour.
ARMOUR = [
    (2, 0x000D0000, 2057.19995, 9.0, 6.0, 64.0, 3.0, ARMOUR_HEAVY, ARMOUR_HEAVY),
    (4, 0x00090000, 6171.6001, 12.0, 60.0, 104.0, 30.0, ARMOUR_LIGHT, None),
    (6, 0x000A0000, 3093.19995, 9.0, 40.0, 88.0, 20.0, ARMOUR_LIGHT, ARMOUR_LIGHT),
    (8, 0x000B0000, 3093.19995, 8.0, 6.0, 48.0, 3.0, ARMOUR_LIGHT, ARMOUR_LIGHT),
    (10, 0x000C0000, 3093.19995, 8.0, 2.0, 32.0, 1.0, ARMOUR_LIGHT, ARMOUR_LIGHT),
    (12, 0x000F0000, 6171.6001, 9.0, 60.0, 134.0, 30.0, ARMOUR_HEAVY, ARMOUR_HEAVY),
    (14, 0x000E0000, 12358.0, 12.0, 60.0, 134.0, 30.0, ARMOUR_HEAVY, ARMOUR_HEAVY),
]
QUICK_ITEM, WEAPON, BAG_ITEM = 24, 30, 37
QUICK_SPELL, BAG_SPELL = 15, 22


class Out:
    def __init__(self):
        self.b = bytearray()

    def i32(self, v):
        self.b += struct.pack('<i', v)

    def u32(self, v):
        self.b += struct.pack('<I', v)

    def i16(self, v):
        self.b += struct.pack('<h', v)

    def u8(self, v):
        self.b += struct.pack('<B', v)

    def f32(self, v):
        self.b += struct.pack('<f', v)

    def floats(self, values):
        for v in values:
            self.f32(v)

    def string(self, text):
        self.b += text.encode('cp1251') + b'\0'

    def raw(self, data):
        self.b += data


def item(o, prototype, condition, charge, weight, price, value3C, spell=-1, magic_level=4, charge_max=None):
    """CItem::Save (006A0360)."""
    o.u32(prototype)
    o.floats((condition, condition, charge, charge if charge_max is None else charge_max, 0.00100000005, weight,
              price, value3C))
    o.i32(spell)
    o.i32(1)
    o.u32(magic_level)


def spell(o, prototype_index, damage, speed, range_, mana, price, skill):
    """CSpell::SaveSpell (0068B920)."""
    o.u8(prototype_index)
    o.u8(16)
    o.i32(1)
    o.floats((damage, 0.0, speed, range_))
    o.i32(1)
    o.u32(0x10000000)
    o.f32(mana)
    o.i32(2)
    o.f32(price)
    o.u32(0x2D)
    o.raw(bytes([10, 0, 0, 0, 0, 0, 0, 0]))
    o.u32(0)
    o.i32(skill)


def stats(o, strength, dexterity, intelligence, skills):
    """UnitStats::SaveTo (00528800): the 0x704-byte block of a new human hero (tools/savefile_unit.py read_stats)."""
    start = len(o.b)
    o.u32(0)                                     # builtFlags
    o.floats((500.0, 0.0))                       # experience, spent
    for value in (strength, dexterity, intelligence):
        o.floats((value, value))
    o.floats((1.0, 1.0, 15.0, 15.0))             # speed, rate
    o.floats((0.466666698, 0.200000018, 0.160000011, 0.125066683))
    o.floats((-2.0, 2.5999999, -4.37113883e-08, 0.0, 300.0, 1.0, 1.0))
    o.i32(1)                                     # movementKind
    o.u32(0x3ECCCCCD)
    for v in (3, 0, 1, 1):
        o.i16(v)
    o.floats((0.0, 12.0, 40.0, 12.0, 1.0, 1.0, 1.0))
    o.u32(0x42480000)
    o.floats((0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0))
    o.i32(8)                                     # naturalWeapon
    for v in (13.0, 100.0, 0.0, 4.30000019, 0.0, 0.0):
        o.floats((v, v))
    for _ in range(6):
        o.floats((1.0, 1.0))
    body = BODY_HP_AT_25 * strength / 25.0
    for kind, mask, share, factor in ((0, 0xE, 1.0, 1.00999999), (1, 0xE, 3.0, 1.00999999),
                                      (2, 0xF, 1.0, 0.340000004), (2, 0xF, 1.0, 0.340000004),
                                      (3, 0xF, 2.0, 0.669999957), (3, 0xF, 2.0, 0.669999957)):
        o.i32(kind)
        o.u32(mask)
        o.raw(bytes(3 * 18 * 4))                 # three empty armour layers
        o.i32(3)
        value = struct.unpack('<f', struct.pack('<f', body * share / 3.0))[0]
        o.floats((value, value, share, factor))
    o.raw(bytes(3))                              # attributes
    o.raw(bytes([skills]) * 8)
    o.raw(bytes(0x24 + 1))                       # skill levels
    o.u32(0)                                     # flags
    o.floats((0.0, 0.0))
    assert len(o.b) - start == 0x704


def party(name, strength, dexterity, intelligence, skills, damage=9.11999989, absorb=None):
    """CParty::SaveParty (00661C10) with its variables."""
    o = Out()
    o.string('bz1mpg')
    o.i32(0)
    items = [(a[0], 0x3005, a[1]) for a in ARMOUR] + [(QUICK_ITEM, 0x3006, 0x00020000), (WEAPON, 0x3004, 19),
                                                       (BAG_ITEM, 0x3008, 0x00010000)]
    o.i32(len(items))
    for oid, kind, proto in items:
        o.i32(oid)
        o.i32(kind)
        o.i32(proto)
    o.i32(2)
    for oid, proto in ((QUICK_SPELL, 0x00180000), (BAG_SPELL, 0)):
        o.i32(oid)
        o.i32(0x3002)
        o.i32(proto)
    for oid, proto, condition, charge, weight, price, value3C, layer0, layer1 in ARMOUR:
        o.i32(oid)
        item(o, proto, condition, charge, weight, price, value3C)
        for layer in (layer0, layer1):
            if absorb is not None:
                layer = (absorb, absorb)
            o.floats((layer[0],) + (layer[1],) * 6 if layer else (0.0,) * 7)
    o.i32(QUICK_ITEM)
    item(o, 0x00020000, 1400.0, 20.0, 1.0, 100.0, 1.0, spell=QUICK_SPELL)
    o.u32(2)
    o.i32(WEAPON)
    item(o, 0x13, 75301.6016, 42.0, 16.0, 560.0, 4.0, magic_level=10)
    o.u32(0)
    o.floats((damage, 7.5999999, 0.0))
    o.i32(BAG_ITEM)
    item(o, 0x00010000, 1000.0, 0.0, 1.0, 1300.0, 1.0, spell=BAG_SPELL, magic_level=0)
    o.u32(1)
    o.i32(END_OF_OBJECTS)
    o.u8(0)
    o.i32(QUICK_SPELL)
    spell(o, 24, 40.0, -1.0, 10.0, 50.0, 1200.0, 19)
    o.i32(BAG_SPELL)
    spell(o, 0, 14.0, 10.0, 15.0, 40.0, 1300.0, 17)
    o.i32(END_OF_OBJECTS)
    o.u8(0)
    o.i32(1)                                     # the party's own items: the bag item
    o.i32(BAG_ITEM)
    o.i32(1)                                     # members
    for text in (name, 'unhuma', '', '', 'Human Gipath Brigand Deputy -1'):
        o.string(text)
    o.i32(HERO_ID)
    stats(o, strength, dexterity, intelligence, skills)
    o.floats((0.5, 0.600000024, 0.699999988))
    for ids in ((WEAPON,), (QUICK_ITEM,), tuple(a[0] for a in ARMOUR), ()):
        o.i32(len(ids))
        for oid in ids:
            o.i32(oid)
    o.string('Hero')
    o.i32(2)                                     # moveMode2
    o.u32(0)                                     # weaponIndex
    o.u8(0)                                      # selected
    o.i32(-1)                                    # leaderId
    o.i32(1)                                     # fightAtWill
    o.string('Human Hero')
    o.string('z.MPGame1')
    o.f32(1.0)
    o.string('')
    o.u32(500 ^ MONEY_KEY)
    o.u32(MONEY_KEY)
    return bytes(o.b)


def hero_file(name='host', strength=25.0, dexterity=25.0, intelligence=25.0, skills=0, damage=9.11999989, absorb=None):
    plain = party(name, strength, dexterity, intelligence, skills, damage, absorb)
    return struct.pack('<IIIB', savefile.MAGIC_PARTY, VERSION, savefile.checksum(plain), 0) + plain


def main():
    parser = argparse.ArgumentParser(usage=__doc__)
    parser.add_argument('saves')
    parser.add_argument('--name', default='host')
    parser.add_argument('--file', default='1.mp')
    parser.add_argument('--stats', nargs=3, type=float, default=(25.0, 25.0, 25.0))
    parser.add_argument('--skills', type=int, default=0)
    parser.add_argument('--damage', type=float, default=9.11999989)
    parser.add_argument('--absorb', type=float, default=None)
    a = parser.parse_args()
    folder = os.path.join(a.saves, 'mp')
    os.makedirs(folder, exist_ok=True)
    with open(os.path.join(folder, a.file), 'wb') as f:
        f.write(hero_file(a.name, *a.stats, skills=a.skills, damage=a.damage, absorb=a.absorb))


if __name__ == '__main__':
    main()
