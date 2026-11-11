Showcase mod for the Evil Islands rebuild (docs/modding.md in its repository).

A new exit on the first island, a few steps east of where a new game lands, leads to an island of the mod's own: its
own ground, a stone golem of the mod's own and two shards of it guarding the path, and one quest. The quest gives the
party a spell of the mod's own, Chain Spark (put it on a hero in the camp's magic page): a spark that strikes its
target, then leaps on to the nearest enemy it has not struck, each strike weaker than the one before. Break the golem,
and its hammer and boots are the party's. The exit beside the landing leads back to the first island; the one by the
old base's gate (east of the golem) leads to the base.

Files (every one the mod's own; LICENSE.txt):
- config.reg.toml: the mod's title in the main menu's list.
- Res\texts\map.toml: the island's zone, its exits, and the first island's exit to it.
- Maps\showcase\: the island's ground (map.toml, heights.png, tiles.png).
- Maps\showcase.mob.toml: the island's objects: the golem and its two shards.
- Maps\showcase.lua: the island's script: the quest, and the spell it gives.
- Res\textures\showcase000.png: the ground's tiles; showcasemap.png, showcasequest.png: the island's maps.
- Res\textures\scgolem00.png, Res\figures\scgolem.glb, Res\database\scgolem.adb.toml: the golem's skin, its figure
  with its animations and its animation list; Res\textures\scshard00.png: the shards' skin.
- Res\database\units.udb.toml: the golem's model, its prototype and the shards'.
- Res\database\items.idb.toml, Res\figures\initwehm9.glb, initarbt20.glb, Res\redress\*.png: the hammer and the boots,
  their inventory models and textures.
- Res\database\spells.sdb.toml, Res\spells\chain_spark.lua: the spell Chain Spark, its record and its script.
- Res\database\spells.sdb.toml, Res\spells\ember_brand.lua: the curse Ember Brand (no item carries it; a script
  casts it with CastSpellUnit("ember", ...)): while it lasts the victim's strength is lower and it burns every 20
  ticks.
- Res\effects\ChainSpark.effect.toml, ChainSparkHit.effect.toml, Res\textures\chainspark.png: the spell's spark in
  flight and its strikes, and their pictures; Res\textures\spell0101.png, prototype0101.png: the spell's pictures.
- Res\database\quests.qdb.toml, Res\texts\texts.toml: the quest's experience, the mod's texts.

The pictures and models are made by tools/make_showcase_mod.py in the rebuild's repository.

Install: put this folder in <game folder>\starter\Mods\ and pick "Showcase mod" in the main menu's list, or set
[game] Mod = starter\Mods\showcase_mod in ei_settings.ini.
