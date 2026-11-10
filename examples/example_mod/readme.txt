Example mod for the Evil Islands rebuild (docs/modding.md in its repository).

- config.reg.toml: the mod's title in the main menu's list.
- Res\texts\texts.toml: the in-game menu's "quit" button renamed; the new axe's name and description.
- Res\database\items.idb.toml: the stone axe hits harder; a new weapon, "example axe".

tools/make_example_mod.py copies this folder and adds two parts made from your own install (the repository holds no
game data):
- Res\textures\Zone1000.png .. Zone1007.png: the first island's ground, exported by ei-convert and recoloured (the
  grass turned red);
- Maps\zone1.mob.toml: the first island's objects, exported by ei-convert, with a pig "ExamplePig" added beside the
  party's start (examples\example_mod_additions\Maps\zone1.mob.toml).

Install: put the made folder in <game folder>\starter\Mods\ and pick "Example mod" in the main menu's list, or set
[game] Mod = starter\Mods\example_mod in ei_settings.ini.
