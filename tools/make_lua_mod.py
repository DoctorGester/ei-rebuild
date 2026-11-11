r"""A mod whose zone 1 mission script is Lua (docs/notes/mods.md, "Scripts in Lua"; tests/scenarios/mod_lua_*.txt).

usage: python tools/make_lua_mod.py <saves dir> plain|changed|none|named|broken|addmob

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
    kinds = ('plain', 'changed', 'none', 'named', 'broken', 'addmob')
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
    print(f'{root}: {kind}')


if __name__ == '__main__':
    main()
