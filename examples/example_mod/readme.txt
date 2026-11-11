Example mod for the Evil Islands rebuild (docs/modding.md in its repository).

- config.reg.toml: the mod's title in the main menu's list.
- Res\texts\texts.toml: the in-game menu's "quit" button renamed; the new axe's name and description.
- Res\database\items.idb.toml: the stone axe hits harder; a new weapon, "example axe".

ei-convert --example-mod copies this folder and adds three parts made from your own install (neither the repository
nor a release holds game data):
- Res\textures\Zone1000.png .. Zone1007.png: the first island's ground, exported by ei-convert and recoloured (the
  grass turned red);
- Maps\zone1.mob.toml: the first island's objects, exported by ei-convert, with a pig "ExamplePig" added beside the
  party's start (examples\example_mod_additions\Maps\zone1.mob.toml);
- Maps\zone1.lua: the first island's script, exported by ei-convert, with the mod's lines: a console line, and the pig
  greeting the party when its leader comes near (examples\example_mod_additions\Maps\zone1.lua).

Make and install it in one command, from the folder that holds ei-convert and this examples folder:

    ei-convert --example-mod "<game folder>"

It writes <game folder>\starter\Mods\example_mod (a second argument names another mods folder). Then pick "Example mod"
in the main menu's list, or set [game] Mod = starter\Mods\example_mod in ei_settings.ini.
