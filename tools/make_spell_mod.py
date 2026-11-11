r"""A mod with a scripted spell for the UI, the AI, the trader and network play (MF17d; docs/notes/items_spells.md,
"Scripted spells"; tests/scenarios/mod_spell_*.txt, net_pair_mod_spell.txt).

usage: python tools/make_spell_mod.py <saves dir>

Writes <saves dir>\spell_mod (the scenarios' `mod harness_saves\spell_mod`): a config.reg with [MOD] Title, Rebirth.txt
and the spell's files (add_spell), on the game's own zone 1:
  Res\database\spells.sdb.toml   the spell "mf17d_bolt" (place 42), whose script is Res\spells\mf17d_bolt.lua: an
                                 attack of lightning for the AI (ai, damage_type), offensive for the HUD's use at once,
                                 icon 100, sold by Ingos' trader (class 3) with a scroll recipe of its own
  Res\spells\mf17d_bolt.lua      its cast: a lightning bolt from the caster to the target and a blow of lightning
                                 through the game's own bindings (CreateLightning, InflictDamage), the bolt taken away
                                 after 60 ticks; its hooks say on the console who cast it at whom and count the casts
                                 into the game variables mf17d.casts, mf17d.caster and mf17d.target
  Res\texts\texts.toml           its name and description (SPELL mf17d_bolt)
  Res\databaselmp\, Res\textslmp\  the same record and texts for network games (databaselmp.res, textslmp.res)
  Res\textures\spell0100.png     the picture of its spell items and scrolls, prototype0100.png that of its prototype
                                 (the spell maker's loot item): flat colours of their own, 64 x 64
tools/make_mod_pair.py adds the same files to a copy of the showcase mod (`add_spell`) for the pair's games.
"""
import os, struct, sys, zlib

import make_test_mod as mod

MOD = 'spell_mod'
CODE = 'mf17d_bolt'

SPELL_RECORD = '''
# MF17d's spell (tools/make_spell_mod.py): a scripted bolt the AI casts as an attack of lightning.
[[spell]]
name = "MF17d Bolt"
code = "mf17d_bolt"
script = "mf17d_bolt.lua"
price = 200.0
school = "lightning"
mana = 4.0
complexity = 1
speed = 10.0
range = 12.0
damage = 30.0
target_kind = "unit"
target_count = 1
cast_time = 30
needs_sight = true
icon = 100
allowed_modifiers = { range = 1, effect = 1 }
skill = 1
sold_by = ["ingos_trader"]
ai = "attack"
damage_type = "lightning"
offensive = true

[[scroll_recipe]]
spell = "mf17d_bolt"
sold_by = ["ingos_trader"]
'''
TEXTS = '''
"SPELL mf17d_bolt" = """
MF17d Bolt\\r
A scripted spell of a test mod: a bolt of lightning at one unit.\\r
"""
'''
SPELL = '''-- MF17d's spell (tools/make_spell_mod.py): a bolt from the caster to its target, made with the game's own bindings,
-- and a blow of lightning; the bolt goes after LIFE ticks and the cast with it. The hooks say on the console who cast
-- it at whom and count the casts into game variables.
local LIFE = 60
local DAMAGE = {damage}
local BOLT_ID = 1700

local spell = {}

function spell.OnCast(cast)
    local target = cast.target
    if not target then
        print("MF17d: OnCast " .. cast.code .. " without a target")
        cast:Finish()
        return
    end
    local caster = cast.caster and GetObjectID(cast.caster) or 0
    local casts = GetGameVar(0, "mf17d.casts") + 1
    SetGameVar(0, "mf17d.casts", casts)
    SetGameVar(0, "mf17d.caster", caster)
    SetGameVar(0, "mf17d.target", GetObjectID(target))
    print(string.format("MF17d: OnCast %s by %d at %d", cast.code, caster, GetObjectID(target)))
    cast.data.bolt = BOLT_ID + casts
    CreateLightning(cast.data.bolt, cast.from.x, cast.from.y, cast.from.z + 1, GetX(target), GetY(target),
                    GetZ(target) + 1, 0.3)
    InflictDamage(target, DAMAGE)
    cast:After(LIFE, "Fade")
end

function spell.Fade(cast)
    DeleteLightning(cast.data.bolt)
    cast:Finish()
end

function spell.OnEnd(cast, reason)
    print("MF17d: OnEnd " .. reason .. " at tick " .. cast.tick)
end

return spell
'''


def png(width, height, colour):
    """A PNG of one RGB colour with a darker frame."""
    dark = tuple(c // 3 for c in colour)
    rows = b''
    for y in range(height):
        row = bytearray(b'\0')
        for x in range(width):
            edge = x < 4 or y < 4 or x >= width - 4 or y >= height - 4
            row += bytes(dark if edge else colour)
        rows += bytes(row)

    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF)
    header = struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header) + chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b'')


def write(path, data, append=False):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'ab' if append else 'wb') as f:
        f.write(data)


def add_spell(root, damage=5):
    """The spell's files into the mod folder `root`, its records and texts after any the mod's TOML files have; its
    blow takes `damage` hit points before protections (the pair's 20: a blow of 5 left its hero's hit points as they were)."""
    for database, texts in (('database', 'texts'), ('databaselmp', 'textslmp')):
        write(os.path.join(root, 'Res', database, 'spells.sdb.toml'), SPELL_RECORD.encode('utf-8'), append=True)
        write(os.path.join(root, 'Res', texts, 'texts.toml'), TEXTS.encode('utf-8'), append=True)
    write(os.path.join(root, 'Res', 'spells', CODE + '.lua'), SPELL.replace('{damage}', str(damage)).encode('utf-8'))
    write(os.path.join(root, 'Res', 'textures', 'spell0100.png'), png(64, 64, (90, 150, 230)))
    write(os.path.join(root, 'Res', 'textures', 'prototype0100.png'), png(64, 64, (230, 200, 60)))


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    root = os.path.join(sys.argv[1], MOD)
    write(os.path.join(root, 'config.reg'), mod.registry({'MOD': {'Title': 'MF17d spell mod'}}))
    write(os.path.join(root, 'Rebirth.txt'), b'')
    add_spell(root)
    print(f'{root}: {CODE}')


if __name__ == '__main__':
    main()
