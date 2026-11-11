r"""A mod whose zone 1 mission script is Lua (docs/notes/mods.md, "Scripts in Lua"; tests/scenarios/mod_lua_*.txt).

usage: python tools/make_lua_mod.py <saves dir> plain|changed|none|named|broken|spell|spellapi|effecthooks|addmob

Writes <saves dir>\lua_mod (the scenarios' `mod harness_saves\lua_mod`): a config.reg with [MOD] Title, Rebirth.txt and
  plain    Maps\zone1.lua   the game's zone1.mob script as ei-convert writes it (a converted install's own Maps\zone1.lua,
                            else made by the run's build's ei-convert, EI_BUILD_DIR, else build\verify, from a copy of
                            the vanilla install's zone1.mob): the zone runs on Lua and plays as the game's script does
  changed  Maps\zone1.lua   the same with the mod's lines (CHANGES): its WorldScript prints two console lines (the
                            second the keys `pairs` walks), starts a definition of the mod's and then counts in a local
                            variable every 20 ticks into the game variable l8.count; the definition prints a line when
                            the count reaches 3 and then fails (its error on the console)
  none     no Maps\zone1.lua: the same mod with the .mob converted at its load (mod_lua_none, mod_lua_plain's twin)
  named    Maps\zone1.mob.toml   zone1.mob's objects as ei-convert writes them, its `script` key naming zone1_named.lua
           Maps\zone1_named.lua  the conversion with a console line at the WorldScript's end ("L8b: the TOML's script")
           Maps\zone1.lua        the same with another line ("L8b: zone1.lua"), which the key passes over
  broken   Maps\zone1.lua   a file Lua does not load (a function left open at its end): error 18 at the zone's load
  spell    Res\database\spells.sdb.toml  a spell "mf17b_pulse" whose script is Res\spells\mf17b_pulse.lua (SPELL;
                            docs/notes/items_spells.md, "Scripted spells"): its cast pulses every 10 ticks into the
                            game variable mf17b.count, 40 times, then finishes (no Maps\zone1.lua: the zone's .mob runs)
  spellapi Res\database\spells.sdb.toml  a spell "mf17c_storm" whose Res\spells\mf17c_storm.lua (SPELL_API) uses each
           Res\effects\mf17c_glow.effect.toml  world binding of the cast (docs/modding.md, "Spells") on the unit nearest
                            its point, fires two missiles with OnHit and keeps a data effect (GLOW_EFFECT) for 300 ticks;
                            its values go into the game variables mf17c.*
  effecthooks Res\effects\mf15f1_hooks.effect.toml, mf15f1_hooks.lua  MF15f1's timing effect (HOOKS_EFFECT): 2,100
           Res\effects\mf15f1_data.effect.toml  sparks whose script's on_spawn and on_update run at the particle
                            budget, and its data alone (a variant without the script); no Maps\zone1.lua
                            (tests/scenarios/effect_hooks_cost.txt)
and for addmob, no mod:
  addmob   <saves dir>\l8b_addmob.lua  the Lua of <saves dir>\l8b_addmob.mob (make_trap_mob.py makes the .mob, no
                            script in it), which `AddMob("..\saves\l8b_addmob.mob")` loads into zone 1's mission script
                            while it runs. Its WorldScript prints a line with two of the zone's globals, starts a definition that prints
                            when the count reaches 3, and counts every 20 ticks into the game variable l8b.count
"""
import os, re, subprocess, sys, tempfile

import exe_path
import make_test_mod as mod

MOD = 'lua_mod'

# Before the WorldScript: the mod's definition. At the end of the WorldScript: its lines.
DEFINITION = '''-- L8's mod (tools/make_lua_mod.py changed): a definition of its own, started by the WorldScript below.
L8Watch = Script {
    {
        condition = function() return GetGameVar(0, "l8.count") >= 3 end,
        action = function()
            KillScript()
            print("L8 mod: the count reached 3")
            error("L8 mod: a failing run")
        end,
    },
}

'''
CHANGES = '''    -- L8's mod: two console lines, its definition, then a count kept in a local variable through saves.
    print("L8 mod: zone1.lua runs")
    local keys = {}
    for key in pairs({ delta = 4, alpha = 1, [2] = 0, charlie = 3, [1] = 0, bravo = 2 }) do keys[#keys + 1] = tostring(key) end
    print("L8 mod: pairs " .. table.concat(keys, ","))
    L8Watch()
    local count = 0
    while true do
        Sleep(20)
        count = count + 1
        SetGameVar(0, "l8.count", count)
    end
'''
# The named kind's lines, at the end of each file's WorldScript.
NAMED = '    print("L8b: the TOML\'s script")\n'
UNNAMED = '    print("L8b: zone1.lua")\n'
# The spell kind: the record (a new spell after the game's 42) and its script.
SPELL_RECORD = b'''[[spell]]
name = "MF17b Pulse"
code = "mf17b_pulse"
script = "mf17b_pulse.lua"
target_kind = "unit"
icon = 1
'''
SPELL = '''-- MF17b's spell (tools/make_lua_mod.py spell): a cast that pulses every PERIOD ticks into the game variable
-- mf17b.count, PULSES times, then finishes; its hooks say on the console what ran. A save and load mid-cast goes on
-- from the saved count (the cast's data and timer are in the save).
local PERIOD = 10
local PULSES = 40

local spell = {}

function spell.OnCast(cast)
    cast.data.count = 0
    SetGameVar(0, "mf17b.target", GetObjectID(cast.target))
    print("MF17b: OnCast " .. cast.code)
    cast:After(PERIOD, "Pulse")
end

function spell.Pulse(cast)
    cast.data.count = cast.data.count + 1
    SetGameVar(0, "mf17b.count", cast.data.count)
    if cast.data.count < PULSES then cast:After(PERIOD, "Pulse") else cast:Finish() end
end

function spell.OnEnd(cast, reason)
    SetGameVar(0, "mf17b.ended", cast.tick)
    print("MF17b: OnEnd " .. reason .. " after " .. cast.data.count .. " pulses at tick " .. cast.tick)
end

return spell
'''
# The spellapi kind: the record, its script and the data effect it starts.
SPELL_API_RECORD = b'''[[spell]]
name = "MF17c Storm"
code = "mf17c_storm"
script = "mf17c_storm.lua"
school = "lightning"
target_kind = "point"
mana = 1.0
range = 30.0
damage = 4.0
skill = 1
icon = 1
light_color = [0.3, 0.6, 0.9]
light_range = 3.0
light_rise_divisor = 10.0
'''
SPELL_API = '''-- MF17c's spell (tools/make_lua_mod.py spellapi): a cast that uses each world binding of the cast value on the unit
-- nearest its point (not the caster; the scenario's WolfTutor has one hit point, so the hits are small), then goes
-- on: a pulse every PERIOD ticks, PULSES times (the "own" mark removed at pulse UNMARK, a second missile at pulse
-- SECOND) while its data effect glows for GLOW ticks; with the glow gone and nothing pending the cast ends ("idle") at
-- tick GLOW. Its values go into the game variables mf17c.*, its hooks say on the console what ran.
local PERIOD = 10
local PULSES = 20
local UNMARK = 10
local SECOND = 15
local GLOW = 300

local spell = {}

local function Var(name, value) SetGameVar(0, "mf17c." .. name, value) end
local function Flag(value) return value and 1 or 0 end

function spell.OnCast(cast)
    local near = cast:FindUnits(cast.point, 2, { alive = true, exclude = { [GetObjectID(cast.caster)] = true } })
    local target = near[1]
    cast.data = { target = target, count = 0, hits = 0 }
    Var("target", GetObjectID(target))
    Var("near", #near)
    Var("enemies", #cast:FindUnits(cast.point, 30, { enemies = true, alive = true }))
    Var("friends", #cast:FindUnits(cast.point, 30, { friends = true, area = true, affect = true, sight = true }))
    Var("affects", Flag(cast:CanAffect(target)))
    Var("lightning", Flag(cast.school == "lightning"))
    Var("power", cast.power)
    Var("lost", cast:Damage(target, 0.3, "fire", { noCredit = true }))
    Var("healed", cast:Heal(target, 0.2))
    cast:Apply(target, "slow", { ticks = 200 })
    cast:Apply(target, "own", { ticks = 600 })
    Var("slowed", Flag(cast:Has(target, "slow")))
    Var("marked", Flag(cast:Has(target, "own")))
    Var("moved", Flag(cast:Move(target, { x = GetX(target) + 1, y = GetY(target), z = 0 })))
    cast:Bolt(cast.caster, target, { lifetime = 10 })
    cast:Light(target, { lifetime = 30, rise = 0.05 })
    cast:Sound("magic\\\\lightning\\\\start.wav", target)
    cast:Effect("mf17c_glow", { attach = target, lifetime = GLOW })
    cast:Missile { to = target, effect = "AcidRay", sound = "magic\\\\Acidray\\\\circle.wav", payload = { shot = 1 } }
    print(string.format("MF17c: OnCast %s, school %s, power %.2f", cast.code, cast.school, cast.power))
    cast:After(PERIOD, "Pulse")
end

function spell.OnHit(cast, target, payload, point)
    cast.data.hits = cast.data.hits + 1
    Var("hits", cast.data.hits)
    Var("hitLost", cast:Damage(target, 0.2, "acid", { area = true }))
    cast:Effect("Sparks", { at = point, lifetime = 15 })
    print("MF17c: OnHit shot " .. payload.shot)
end

function spell.Pulse(cast)
    local count = cast.data.count + 1
    cast.data.count = count
    Var("count", count)
    if count == UNMARK then
        Var("removed", cast:Remove(cast.data.target, "own"))
        Var("marked", Flag(cast:Has(cast.data.target, "own")))
    end
    if count == SECOND then cast:Missile { to = cast.data.target, payload = { shot = 2 } } end
    if count < PULSES then cast:After(PERIOD, "Pulse") end
end

function spell.OnEnd(cast, reason)
    Var("ended", cast.tick)
    print("MF17c: OnEnd " .. reason .. " at tick " .. cast.tick .. " after " .. cast.data.hits .. " hits")
end

return spell
'''
GLOW_EFFECT = b'''# MF17c's data effect (tools/make_lua_mod.py spellapi): sparks rising around what it is attached to.
texture = "Teleport"
blend = "additive"

[emit]
rate = 1

[[particle]]
name = "spark"
lifetime = 20
spawn = "disc() * 0.5"
velocity = "vec3(0, 0, 0.03)"
size = 0.2
color = "80C0FF"
frame = { start = "pick(16)", delay = 1 }
'''
HOOKS_EFFECT = b'''# MF15f1's timing effect (tools/make_lua_mod.py effecthooks; tests/scenarios/effect_hooks_cost.txt):
# with `level` at 1, 100 sparks a tick that live 21 ticks. Its script places each (on_spawn) and turns each round the
# centre at every update it lives through (on_update): 100 + 2,000 calls a tick, over the particle budget (2,000),
# so the hooks run at it.
texture = "Teleport"
blend = "additive"
script = "mf15f1_hooks.lua"

[inputs]
level = { type = "float", default = 0.0 }

[emit]
rate = "level * 100"
max = 2400

[[particle]]
name = "spark"
lifetime = 21
position = "origin + disc() * size"
velocity = "vec3(0, 0, 0.02)"
size = 0.1
color = "FFD890"
alpha = { tick = "255 * (1 - p.t)" }
frame = { start = "pick(16)", delay = 1 }
'''
# The same effect as its data alone: the variant without the script.
DATA_EFFECT = b'''# MF15f1's timing effect as data alone (tools/make_lua_mod.py effecthooks): mf15f1_hooks without its script.
like = "mf15f1_hooks"
script = ""
'''
HOOKS_SCRIPT = '''-- MF15f1's timing hooks (tools/make_lua_mod.py effecthooks): written as a modder would, through e:get and p:get / p:set.
local TURN = 0.05
local hooks = {}

-- Each spark on a ring round the source, at a random turn.
function hooks.on_spawn(e, p, index)
    local x, y, z = e:get("origin")
    local angle = e:random(0, 2 * math.pi)
    local out = e:get("size") * e:random(0.5, 1)
    p:set("position", x + math.cos(angle) * out, y + math.sin(angle) * out, z + 0.1)
end

-- Each spark turned round the centre at every update.
function hooks.on_update(e, p)
    local cx, cy = e:get("origin")
    local x, y, z = p:get("position")
    local dx, dy = x - cx, y - cy
    local c, s = math.cos(TURN), math.sin(TURN)
    p:set("position", cx + dx * c - dy * s, cy + dx * s + dy * c, z)
end

return hooks
'''
BROKEN = '-- L8b (tools/make_lua_mod.py broken): Lua does not load this file.\nfunction WorldScript(\n'
ADDMOB = '''-- L8b (tools/make_lua_mod.py addmob): the Lua of an AddMob'd .mob, loaded into zone 1's mission script while it
-- runs (docs/notes/mods.md, "Scripts in Lua").
L8bWatch = Script {
    {
        condition = function() return GetGameVar(0, "l8b.count") >= 3 end,
        action = function()
            KillScript()
            print("L8b: the count reached 3")
        end,
    },
}

function WorldScript()
    print("L8b: the AddMob'd Lua runs: heroes " .. #Heroes .. ", Ogre " .. (Ogre and "kept" or "none"))
    L8bWatch()
    local count = 0
    while true do
        Sleep(20)
        count = count + 1
        SetGameVar(0, "l8b.count", count)
    end
end
'''


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(data)


def zone1_converted(name):
    """A file ei-convert writes for the game's zone1.mob (zone1.lua, zone1.mob.toml): a converted install's own, else
    written by the run's build's ei-convert from a copy of the vanilla install's zone1.mob."""
    converted = exe_path.install_file('vanilla', 'Maps', name)
    if os.path.exists(converted):
        with open(converted, 'r', encoding='utf-8', newline='') as f:
            return f.read()
    build = os.environ.get('EI_BUILD_DIR') or os.path.join(exe_path.ROOT, 'build', 'verify')
    convert = os.path.join(build, 'ei-convert.exe' if os.name == 'nt' else 'ei-convert')
    with tempfile.TemporaryDirectory() as scratch:
        source = os.path.join(scratch, 'in', 'Maps', 'zone1.mob')
        with open(exe_path.install_file('vanilla', 'Maps', 'zone1.mob'), 'rb') as f:
            write(source, f.read())
        subprocess.run([convert, os.path.join(scratch, 'in'), os.path.join(scratch, 'out')], check=True, capture_output=True)
        with open(os.path.join(scratch, 'out', 'Maps', name), 'r', encoding='utf-8', newline='') as f:
            return f.read()


def changed(text, definition, lines):
    """The converted zone1.lua with `definition` before its WorldScript and `lines` at the WorldScript's end."""
    text = text.replace('\r\n', '\n')
    start = re.search(r'^function WorldScript\(\)\n', text, re.M)
    end = re.compile(r'^end\n', re.M).search(text, start.end()) if start else None
    if not end:
        sys.exit('make_lua_mod: zone1.lua has no WorldScript function')
    return text[:start.start()] + definition + text[start.start():end.start()] + lines + text[end.start():]


def main():
    kinds = ('plain', 'changed', 'none', 'named', 'broken', 'spell', 'spellapi', 'effecthooks', 'addmob')
    if len(sys.argv) != 3 or sys.argv[2] not in kinds:
        sys.exit(__doc__)
    kind = sys.argv[2]
    if kind == 'addmob':
        write(os.path.join(sys.argv[1], 'l8b_addmob.lua'), ADDMOB.encode('utf-8'))
        print(f'{sys.argv[1]}: addmob')
        return
    root = os.path.join(sys.argv[1], MOD)
    write(os.path.join(root, 'config.reg'), mod.registry({'MOD': {'Title': 'L8 Lua mod'}}))
    write(os.path.join(root, 'Rebirth.txt'), b'')
    maps = os.path.join(root, 'Maps')
    if kind in ('plain', 'changed'):
        text = zone1_converted('zone1.lua')
        if kind == 'changed':
            text = changed(text, DEFINITION, CHANGES)
        write(os.path.join(maps, 'zone1.lua'), text.encode('utf-8'))
    elif kind == 'named':
        text = zone1_converted('zone1.lua')
        objects = zone1_converted('zone1.mob.toml').replace('\r\n', '\n')
        objects, count = re.subn(r'^script = "zone1\.lua"$', 'script = "zone1_named.lua"', objects, flags=re.M)
        if count != 1:
            sys.exit('make_lua_mod: zone1.mob.toml has no script key naming zone1.lua')
        write(os.path.join(maps, 'zone1.mob.toml'), objects.encode('utf-8'))
        write(os.path.join(maps, 'zone1_named.lua'), changed(text, '', NAMED).encode('utf-8'))
        write(os.path.join(maps, 'zone1.lua'), changed(text, '', UNNAMED).encode('utf-8'))
    elif kind == 'broken':
        write(os.path.join(maps, 'zone1.lua'), BROKEN.encode('utf-8'))
    elif kind == 'spell':
        write(os.path.join(root, 'Res', 'database', 'spells.sdb.toml'), SPELL_RECORD)
        write(os.path.join(root, 'Res', 'spells', 'mf17b_pulse.lua'), SPELL.encode('utf-8'))
    elif kind == 'spellapi':
        write(os.path.join(root, 'Res', 'database', 'spells.sdb.toml'), SPELL_API_RECORD)
        write(os.path.join(root, 'Res', 'spells', 'mf17c_storm.lua'), SPELL_API.encode('utf-8'))
        write(os.path.join(root, 'Res', 'effects', 'mf17c_glow.effect.toml'), GLOW_EFFECT)
    elif kind == 'effecthooks':
        effects = os.path.join(root, 'Res', 'effects')
        write(os.path.join(effects, 'mf15f1_hooks.effect.toml'), HOOKS_EFFECT)
        write(os.path.join(effects, 'mf15f1_hooks.lua'), HOOKS_SCRIPT.encode('utf-8'))
        write(os.path.join(effects, 'mf15f1_data.effect.toml'), DATA_EFFECT)
    print(f'{root}: {kind}')


if __name__ == '__main__':
    main()
