Showcase mod for the Evil Islands rebuild (docs/modding.md in its repository).

A new game starts on an island of the mod's own in place of the first island: its own ground, a stone golem of the
mod's own guarding the path, and one quest. Break the golem, and its hammer and boots are the party's. The exit by
the old base's gate (east of the golem) still leads to the base, as on the first island.

Files (every one the mod's own; LICENSE.txt):
- config.reg.toml: the mod's title in the main menu's list.
- Maps\zone1\: the island's ground (map.toml, heights.png, tiles.png), in place of the first island's.
- Maps\zone1.mob.toml: the island's objects: the golem.
- Maps\zone1.lua: the island's script: the quest.
- Res\textures\zone1000.png: the ground's tiles; zone1map.png, zone1quest.png: the island's maps.
- Res\textures\scgolem00.png, Res\figures\scgolem.glb, Res\database\scgolem.adb.toml: the golem's skin, its figure
  with its animations and its animation list.
- Res\database\units.udb.toml: the golem's model and prototype.
- Res\database\items.idb.toml, Res\figures\initwehm9.glb, initarbt20.glb, Res\redress\*.png: the hammer and the boots,
  their inventory models and textures.
- Res\database\spells.sdb.toml: a spell record, "Spark" (it has no effect yet: a new spell cannot have one).
- Res\database\quests.qdb.toml, Res\texts\texts.toml: the quest's experience, the mod's texts.

The pictures and models are made by tools/make_showcase_mod.py in the rebuild's repository.

Install: put this folder in <game folder>\starter\Mods\ and pick "Showcase mod" in the main menu's list, or set
[game] Mod = starter\Mods\showcase_mod in ei_settings.ini.
