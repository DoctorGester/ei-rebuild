r"""A mod whose zone 1 mission script is Lua (docs/notes/mods.md, "Scripts in Lua"; tests/scenarios/mod_lua_*.txt).

usage: python tools/make_lua_mod.py <saves dir> plain|changed|none

Writes <saves dir>\lua_mod (the scenarios' `mod harness_saves\lua_mod`): a config.reg with [MOD] Title, Rebirth.txt and
  plain    Maps\zone1.lua   the game's zone1.mob script as ei-convert writes it (a converted install's own Maps\zone1.lua,
                            else made by the run's build's ei-convert, EI_BUILD_DIR, else build\verify, from a copy of
                            the vanilla install's zone1.mob): the zone runs on Lua and plays as the game's script does
  changed  Maps\zone1.lua   the same with the mod's lines (CHANGES): its WorldScript prints two console lines (the
                            second the keys `pairs` walks), starts a definition of the mod's and then counts in a local
                            variable every 20 ticks into the game variable l8.count; the definition prints a line when
                            the count reaches 3 and then fails (its error on the console)
  none     no Maps\zone1.lua: the same mod on the old executor (mod_lua_none, mod_lua_plain's twin)
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


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(data)


def zone1_lua():
    """The game's zone1.mob script as Lua (ei-convert's): a converted install's own Maps\\zone1.lua, else written by
    the run's build's ei-convert from a copy of the vanilla install's zone1.mob."""
    converted = exe_path.install_file('vanilla', 'Maps', 'zone1.lua')
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
        with open(os.path.join(scratch, 'out', 'Maps', 'zone1.lua'), 'r', encoding='utf-8', newline='') as f:
            return f.read()


def changed(text):
    """The converted zone1.lua with the mod's definition before its WorldScript and its lines at the WorldScript's end."""
    text = text.replace('\r\n', '\n')
    start = re.search(r'^function WorldScript\(\)\n', text, re.M)
    end = re.compile(r'^end\n', re.M).search(text, start.end()) if start else None
    if not end:
        sys.exit('make_lua_mod: zone1.lua has no WorldScript function')
    return text[:start.start()] + DEFINITION + text[start.start():end.start()] + CHANGES + text[end.start():]


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ('plain', 'changed', 'none'):
        sys.exit(__doc__)
    root = os.path.join(sys.argv[1], MOD)
    write(os.path.join(root, 'config.reg'), mod.registry({'MOD': {'Title': 'L8 Lua mod'}}))
    write(os.path.join(root, 'Rebirth.txt'), b'')
    kind = sys.argv[2]
    if kind != 'none':
        text = zone1_lua()
        write(os.path.join(root, 'Maps', 'zone1.lua'), (changed(text) if kind == 'changed' else text).encode('utf-8'))
    print(f'{root}: {kind}')


if __name__ == '__main__':
    main()
