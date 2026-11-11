# Modding Evil Islands with the rebuild

This guide is for people who make mods for `ei-rebuild.exe`, the rebuilt Evil Islands exe in this repository. A mod
is a folder (or one .zip) of files that stand over the game's own. The shipped mods (Rebirth's Lost in Astral, IronMod,
AllHeroes) still play as they are. On top of the original formats the rebuild also reads common formats: TOML for
records and texts, PNG for textures and map layers, glTF 2.0 for models and animations. A mod made only of those needs
no packing tool and no hex editor.

Everything here is what the code does today. The developers' notes on each part are in `docs/notes/mods.md`.

Contents: [Quick start](#quick-start) · [A mod's folder](#a-mods-folder) · [Installing and choosing a mod](#installing-and-choosing-a-mod)
· [How the game finds a file](#how-the-game-finds-a-file) · [ei-convert: the game's data as editable files](#ei-convert-the-games-data-as-editable-files)
· [Records as TOML](#records-as-toml) · [Texts](#texts) · [Databases](#databases) · [Textures as PNG](#textures-as-png)
· [Models as glTF](#models-as-gltf) · [Animations as glTF](#animations-as-gltf) · [Maps as folders](#maps-as-folders)
· [Map objects](#map-objects) · [Scripts](#scripts) · [Effects](#effects) · [What stays in the original formats](#what-stays-in-the-original-formats)
· [Limits and troubleshooting](#limits-and-troubleshooting) · [The example mod](#the-example-mod) · [The showcase mod](#the-showcase-mod)

## Quick start

1. Make the example mod from your own install with `ei-convert`, which every release package holds beside
   `ei-rebuild` with the `examples` folder (or build this repository: `build.cmd release` gives
   `build\release\ei-convert.exe` and copies `examples` beside it). In the package's folder:
   ```
   ei-convert --example-mod "F:\games\EvilIslands"
   ```
   This writes `starter\Mods\example_mod\` into that game folder (making `starter\Mods` on a GOG install). Point it
   at a copy of the install if you want the original left untouched.
2. Start `ei-rebuild.exe` in the game folder. The main menu now has a list at its bottom left; pick "Example mod
   (single player)". The game switches to the mod at once.
3. Start a new game. The ground of the first island is red, a pig stands by the party, and the in-game menu's
   quit button reads "Leave the islands". In the console (`docs/notes/console.md`), `thingamabob` (the cheat word)
   and then `@GiveItem(0,"example axe.malachite")` give the party the mod's new axe, named by the mod's text.

[The example mod](#the-example-mod) explains each of its files.

## A mod's folder

A mod's folder is laid out like the game folder: what the mod has in its `Res\`, `Maps\`, `Config\`, `Camera\`,
`Movies\` and `Stream\` stands for the game's file of the same path.

```
example_mod\
  config.reg.toml                 the mod's title and settings (or config.reg, the game's binary registry)
  readme.txt                      anything you like at the root: the game reads no root file but the config
  Res\texts\texts.toml            texts.res's entries changed or added
  Res\database\items.idb.toml     items.idb's records changed or added
  Res\textures\Zone1000.png       textures.res's Zone1000.mmp, as a PNG
  Maps\zone1.mob.toml             zone 1's objects
  Maps\zone1.lua                  zone 1's script
```

- The root's files (`config.reg`, a readme, Rebirth's `.plg` plugins) are the mod's own and replace nothing: the game
  folder's root holds the exe and its settings.
- An archive (`.res`) in the mod does not replace the game's archive of that name. A mod that ships archives names them
  in its config's `[RES]` section, and they are combined with the game's (below). A mod made of loose files needs no
  `[RES]`.
- `config.reg` or `config.reg.toml` must be there: the menu's list shows only folders that have one.

The config, as TOML:

```toml
[MOD]
Title = "Example mod"          # the menu's list shows it (else the folder's name)
Author = "Your name"
Version = "1.0"
Single = true                  # Single true and Multi false: "(single player)" after the title;
Multi = false                  # Single false and Multi true: "(multiplayer)". A label only.

[RES]                          # only for a mod that ships archives:
"textures.res" = ["Res\\mytextures.res"]   # read before the game's textures.res
"outro.res" = ["Res\\myoutro.res", "-"]    # "-": the game's archive is not read at all
```

A key with a dot in it is quoted in TOML (`"textures.res"`); a value can be one string or a list.

### Zip mods

A mod can be one .zip, read in place: the zip's files at its root, or below one top folder of the mod's name (as
Rebirth's `LostInAstral.zip`). The game reads it without unpacking. Files the game writes for a zip mod (help topics
seen, an edited address book) go to `saves\<mod>\files\`. A zip whose files are below one top folder is recognised by
a binary `config.reg` there; a mod with only `config.reg.toml` keeps its files at the zip's root.

## Installing and choosing a mod

- Put the mod's folder or .zip in `<game folder>\starter\Mods\` (Rebirth's installs have that folder; on a GOG install
  make it).
- The main menu shows a list at its bottom left when there is at least one mod: "No mod", then every mod by its title.
  A pick switches the game to that mod at once (no restart; the main menu only, not during a network game) and stores
  the choice.
- Or set it in `ei_settings.ini` beside the exe:
  ```ini
  [game]
  Mod = starter\Mods\example_mod
  ```
  The folder or .zip relative to the exe's folder, or a full path (a mod need not be in `starter\Mods`). Empty: no mod.
  A folder that does not exist is logged and the game runs without a mod.
- Each mod keeps its own saves in `saves\<mod's folder name>\`, and its multiplayer heroes in `saves\<mod>\mp\`, never
  mixed with the plain game's.
- The log, `ei-rebuild.log` in the game folder, names the mod played and every file of it that could not be read
  (lines starting with `warning:`).

## How the game finds a file

Every archive the game opens (a `.res`, a map's `.mpr`, a quest zone's `.mq`) has a folder of its name without the
extension standing over it: `Res\textures\` over `Res\textures.res`, `Maps\zone1\` over `Maps\zone1.mpr`. A file in that
folder is read in place of the archive's entry of the same name. So `textures.res : foo.mmp` is looked for in this order:

1. the mod's `Res\textures\` folder;
2. the mod's `[RES]` archives for textures.res, each one's own folder first, then its entries;
3. the game folder's `Res\textures\` (an unpacked install);
4. the archive's own entries (none when the mod's `[RES]` says `"-"`).

- Names match without case; a sub-folder is part of the name (sfx.res's `weapons\Slash\Stone\1.wav` is
  `Res\sfx\weapons\Slash\Stone\1.wav`).
- A file outside archives (`Config\ai.reg`, `Camera\bz10k.cam`, `Movies\*.bik`, `Stream\*.mp3`) is simply the mod's when
  the mod has it.
- At each of those places a common format is asked before the original:
  - a record file `<name>` (a `.reg`, a database, a texts entry, ...): `<name>.toml` beside it ([Records as TOML](#records-as-toml));
  - a texture `<name>.mmp`: `<name>.png` ([Textures as PNG](#textures-as-png));
  - a figure's model files: `<figure>.gltf` or `<figure>.glb` ([Models as glTF](#models-as-gltf));
  - a map's file and sectors: the map's folder of PNGs ([Maps as folders](#maps-as-folders));
  - a zone's objects `Maps\<zone>.mob`: `Maps\<zone>.mob.toml` ([Map objects](#map-objects)).
- The original formats keep loading everywhere: a mod may mix them with the common ones.
- A folder's files are listed when its archive opens, so add files while the game is closed (or switch mods).

To see the game's files as loose files, `python tools\unpack_install.py <install> <target>` copies an install with
each archive unpacked into its folder (`--folders-only`: the folders alone). It only reads the install.

## ei-convert: the game's data as editable files

`ei-convert` writes the game's data in the common formats, read by the game's own loaders, and checks that each file
comes back the same. Every release package holds it beside `ei-rebuild` (`ei-convert.exe` on Windows); a build of this
repository (`build.cmd release`, see `README.md`) has it as `build\release\ei-convert.exe`. Run it from a command
prompt or terminal (on macOS clear the download's quarantine flag first, as the package's README.txt says).

```
ei-convert <install or archive> <target folder>          write the converted files into the target
ei-convert --check <install or archive>...               only check every round trip
ei-convert --example-mod <game folder> [<mods folder>]   make the example mod (below) from the install
```

- An install as the source writes the target laid out as the game folder: `Res\database\items.idb.toml`,
  `Res\textures\Zone1000.png`, `Maps\zone1\map.toml`, `Maps\zone1.mob.toml`, `Camera\bz10k.cam.toml`,
  `Config\ai.reg.toml`, ... and `converted.txt`, the originals it converted. Only converted files are written; the rest
  of the game's data stays in its archives.
- An archive as the source (`ei-convert F:\games\EvilIslands\Res\database.res out`) writes its folder alone:
  `out\database\items.idb.toml`. Handy for a few files.
- It only reads the source. Never give an install (or a folder inside one) as the target.
- The whole GOG install takes about a minute and 350 MB: 3,700 textures, 870 figures with their animations, 38 maps,
  99 zones' objects, every database, text, registry and camera path.

Copy into your mod only the files you change, at the same path. A converted record file starts with `replace = true`
(below): copied whole it stands for the whole original. For small changes write a short file of your own instead.

## Records as TOML

Every record file of the game can be TOML: a file of the original's name plus `.toml`, wherever the original would be
looked for. TOML is UTF-8. A mod's TOML file is merged over the game's records, so it holds only what it changes.

| Original | The mod's TOML | Matched by |
|---|---|---|
| a registry `.reg`: `Config\ai.reg`, `Res\music.reg`, `Res\menus\menus.reg`, `Res\aiinfo\tileDesc.reg`, ... | `Config\ai.reg.toml`, ... | section and key (no case) |
| a material, `Res\materials\<name>.mat` | `Res\materials\<name>.mat.toml` | key |
| an animation list, `Res\database\<model>.adb` | `Res\database\<model>.adb.toml` | header keys; `[[animation]]` by name |
| unit speech, `Res\database\acks.db` | `Res\database\acks.db.toml` | `[[unit]]`, `[[model]]`, `[[extra]]` by name |
| ground prints, `Res\database\prints.db` | `Res\database\prints.db.toml` | `[[blood]]`, `[[footprint]]`, `[[scorch]]` by name |
| texts.res's entries | `Res\texts\texts.toml` (textslmp.res: `Res\textslmp\texts.toml`) | entry name ([Texts](#texts)) |
| the game databases (units, items, spells, perks, quests, levers) | `Res\database\items.idb.toml`, ... | [Databases](#databases) |
| a camera path, `Camera\<name>.cam` | `Camera\<name>.cam.toml` | none: the file is the whole path |

The merge rules, the same for every table:
- A record of the TOML is matched to the game's record of its key (the name) and takes only the fields it gives; the
  record's other fields stay the game's (or those of a TOML file below it).
- A key the game's table lacks is a new record at the end of its table. Records are never removed or reordered:
  saves, scripts and the network refer to records by their index.
- A list field (a unit's speech list, an animation's `sounds`) given is the whole list.
- A record without its key, with an unknown key, a name its field does not have or a value of the wrong type is
  logged (`warning: <file> line n: ...; the record is left out`, with the valid names) and left out whole: the game's
  record keeps what it had, a new one is not added. A file that is not TOML is logged and not read.
- Several places can have a TOML of the same file (a mod's over an unpacked install's): the bottom one is merged first,
  the mod's last, so the mod's wins.
- A file with `replace = true` at its top is the whole file: nothing below it (the game's original, lower TOML files) is
  read. ei-convert writes it into every file it converts. Leave it out of a mod's file of changes.
- Where a name repeats in the game's table (quests.qdb has 17 quests "dummy"; spells have a recipe per class), the
  n-th record of that name in the TOML merges into the n-th one of the table: list the ones before it too, in order.

Examples, each a whole mod file:

```toml
# Config\ai.reg.toml: one value of the AI settings; the other keys stay the game's.
[Logic]
GuardRadius = 12
```

```toml
# Res\database\prints.db.toml: blood on stone dries lighter; its other values stay.
[[blood]]
name = "stone"
wet = { alpha = 0.25 }
```

```toml
# Res\database\unhuma.adb.toml: the hero's animation "cidle01" chosen less often; its other fields stay.
[[animation]]
name = "cidle01"
chance = 40
```

```toml
# Res\database\acks.db.toml: a unit's attack speech list replaced (the list is the unit of a field).
[[unit]]
name = "Human Mercenary Warrior"
attack = [
    { sound = "Acks\\MyMod\\Attack1.wav", chance = 50, text = "For the islands!" },
]
```

The sound file of that example is a loose file of `sfx.res`'s folder or the speech archives'; see the converted
`acks.db.toml` for the paths the game uses.

## Texts

Every entry of `texts.res` (names, descriptions, the interface's strings, briefings, `map.txt`) is a key of
`Res\texts\texts.toml`: `"<entry name>" = "<text>"`. Each entry is merged on its own, so a mod's file holds only the
entries it changes or adds:

```toml
"string quit" = "Leave the islands"

"weapon example_axe malachite" = """
Example axe\r
The example mod's axe: lighter than a stone axe, and it hits harder.\r
"""
```

- The game's texts end each line with CR LF: write `\r` before each line's end in a multi-line string, as ei-convert
  does. An interface string (`string <name>`) is one line.
- An item's text is `"<kind> <name> <material>"` with the name's and material's spaces as `_`, in any case: `weapon`,
  `armor`, `qitem` (quick items; a potion has no material), `litem` (loot, no material), `questitem` (no material).
  Its first line is the name, the rest the description.
- A file of the entry's own name in `Res\texts\` also works (`Res\texts\string quit`, plain text in the asset code
  page); it comes before the folder's texts.toml.
- The game holds its text in code page 1251 (Cyrillic and Latin): a character outside it is shown as `?`.

## Databases

The six game databases are TOML records like the others, merged by their key:

| Original (`Res\database\`, also `Res\databaselmp\`) | Tables |
|---|---|
| `units.udb` | `[[level]]`, `[[model]]`, `[[unit]]` (prototypes), `[[npc]]`: by name |
| `perks.pdb` | `[[skill]]`, `[[perk]]`: by name |
| `spells.sdb` | `[[spell]]`, `[[modifier]]`: by code; `[[scroll_recipe]]`, `[[armor_recipe]]`, `[[weapon_recipe]]`: by spell |
| `items.idb` | `[[material]]`: by key; `[[weapon]]`, `[[armor]]`, `[[quick_item]]`, `[[quest_item]]`, `[[loot]]`: by name |
| `quests.qdb` | `[[quest]]`, `[[briefing]]`: by name |
| `Levers.ldb` | `[[lever]]`: by name |

```toml
# Res\database\items.idb.toml
[[weapon]]                 # the game's stone axe: only its damage changes
name = "stone axe"
damage = 3.0

[[weapon]]                 # a new weapon: the fields left out keep their defaults
name = "example axe"
family = "axe"
type = "axe"
material_class = "Stone"
number = 1
price = 600.0
weight = 3.0
sold_by = ["gipat_smith"]
damage = 5.0
damage_spread = 1.0
damage_types = { slashing = 1.0 }
```

- Take a record's field names from ei-convert's file of that database. ei-convert writes a field only when it differs
  from a new record's default (0, false, an empty list or name; an item's `buildable` true), so a field missing there
  is its default.
- An item in the game is a prototype and a material: `GiveItem(0, "example axe.malachite")`; its damage is the
  weapon's times the material's factor.
- Fields named `unread...` are bytes no code reads; leave them out.
- A new record comes after every record of the game's table, so the game's indexes (in saves, in the network) do not
  move. Saves made with a mod's new items need the mod to load them.

### Values by name

Codes, masks and fixed lists are written by name. A name the field does not know is an error of its record, logged
with the valid ones (`warning: items.idb: <file> line 9: type has a value it does not take (one of: sword, axe, ...);
the record is left out`); the record is not read. The names (docs/notes/mods.md, "Readable values", says which code reads each):

| Field | Names |
|---|---|
| weapon `type`, a unit's `natural_weapon` | `sword`, `axe`, `dagger`, `spear`, `club`, `bow`, `crossbow`; `none`, `spells_only` (natural weapons) |
| armor `type` | `helmet`, `body_armor`, `leggings`, `shirt`, `pants`, `boots`, `gloves` |
| `sold_by` (items, materials, spells, modifiers, recipes) | the traders that sell it: `gipat_smith`, `gipat_witch`, `ingos_trader`, `suslanger_smith`, `rick`; a trader class of a mod's script by its number (6..32) |
| `damage_types`, `absorb_first`, `absorb_others`, `absorb_factors`, a model's `damage_weights` and `protection` | a table by damage type: `piercing`, `slashing`, `crushing`, `fire`, `acid`, `lightning`, `general` |
| quick item `category` | `danger_sensor`, `consumable`, `wand`, `potion` |
| loot `kind`, `record_type` | `self`, `material`, `record`; `weapon`, `armor`, `quick_item`, `spell`, `modifier`; `none` |
| model `class` | `hero`, `creature`, `animal` |
| model `movement` | `walks`, `flies`, `rooted` |
| model `blood` | `none`, `red`, `green`, `blue`, `brown` (a shower and a stain on the ground), `red_spray`, `green_spray`, `blue_spray`, `brown_spray` (the shower alone) |
| model `footprint` | the picture in footprints.png, by the game creature whose footprint it is: `none`, `human`, `driad`, `cyclope`, `skeleton`, `troll`, `golem`, `elemental`, `goblin`, `toad`, `ogre`, `lizardman`, `zombie`, `unicorn`, `horse`, `scrab`, `orc`, `pig`, `hyena`, `boar`, `cow`, `wolf`, `tiger`, `hare`, `rat`, `deer`, `dog` |
| model `step_sounds` | a table by the ground's surface: `grass`, `ground`, `stone`, `sand`, `rock`, `field`, `water`, `road`, `astral` (outside the map), `snow`, `ice`, `dry_grass`, `snowballs`, `lava`, `swamp`, `high_rock` |
| model `mode_speeds`, `action_speeds` | `{ run, walk, crawl, lie }`; `{ attack, cast, hit, death, cross, special }` |
| unit `combat` | `{ reach, attack, attack_spread, defence, speed }` |
| unit `perception`, `conspicuity` | `{ sight, day_sight, near, hearing, smell, unread }` (`unread`: no code reads it; a prototype's conspicuity `sight` also scales its footprints) |
| unit `science`, `taming`, `magic_skills` | the unit's science (and the unread second attribute) and taming; `{ elemental, sense, astral }` (its magic skills); `skill_level` is every skill's and attribute's start |
| npc and map unit `attributes`, quick item `attribute_bonus` | `{ science, unread, taming }` (science: the Use and Steal skills; taming; the second is read by nothing) |
| npc `skills` | `{ melee, archery, backstab, elemental, sense, astral, unread_6, unread_7 }` (the last two are read by nothing) |
| weapon `skill_bonus` | `{ melee, archery }` |
| level `slot_type`, `severed_by` | `head`, `body`, `arm`, `leg`, `none`; the damage types whose full hit severs the slot (a list of the damage types' names) |
| perk `required_skill` | the skill it needs at its `required_skill_value`: `melee`, `archery`, `backstab`, `elemental`, `sense`, `astral`, `unread_6`, `unread_7`; `none` |
| spell `magic` | `elemental`, `sense` (cast without noise, the caster not seen casting), `astral` |
| loot `index` | the record it stands for, by the name `GiveItem` takes after its dot (a weapon's, armour's or quick item's name, a spell's or modifier's code), looked up in its `record_type`'s table: give `record_type` first. The game's files have none (the number 4294967293) |
| quest `places` | the quest's points on the objectives map: `[[x, y], ...]` |
| unit `hit_sound` | the sound of its natural attack's hit: a weapon type (that weapon's sound), `none`, `spells_only` (no sound), or a folder of `Res\sfx\weapons\`: `pierce`, `crash`, `slash`, `hands1` (the humans' unarmed hits), `hands4` (the ogres'), `hands6` (the trolls'), `hands7` (the animals': no install has the folder, so they hit silently; a mod may add `weapons\hands7\<material>\1.wav`) |
| npc `stats` | `{ strength, dexterity, intelligence }` |
| spell `school` | `fire`, `lightning`, `acid`, `illusion`, `divination`, `enchantment`, `healing`, `domination`, `none` |
| spell `target_kind`, `allowed_modifiers`; modifier `kind` | `unit`, `point`; `{ range, targets, area, effect, duration }`; `range`, `targets`, `area`, `effect`, `duration`, `mana`, `filter`, `item` |
| spell `ai`, `damage_type` (a new spell's only: the game's 42 keep their own) | how the AI casts it: `none` (never, the default), `attack`, `area_attack`, `lasting_area`, `curse`, `protect`, `enhance`, `heal`; the damage types' names (`general` the default) |
| true / false | `buildable`, `shows_wounds`, `cast_effect`, `swap_feet`, `voiced`, `needs_sight`, `learnable`, a new spell's `offensive` (cast at enemies: the hotkey aims it at the nearest, the target turns on the caster, the battle music); config.reg.toml's `[MOD]` `Single`, `Multi`, `GameSaves`, `ForceShowTutorial` |
| map.toml `[[material]] kind` | `dry` (the AI walks under it as on dry ground), `water` (the AI's water), `still` (no waves) |
| map objects: a lever's `lock.mask` | `science` (the Use skill opens it at the lock's `level`), `key` (the quest item whose id is the lock's `id`), `science_2`, `science_3` (no key in the game has them); `[]`: no lock |
| map objects: `side` | the player's number, or its name when the file's `[players] names` gives it one (`side = "Golems"`) |

A table gives only the elements it changes (`perception = { sight = 9.0 }`); a long one can be a sub-table of the
record (`[model.step_sounds]` after the model's other fields). A value without a name is a number: the game data has
two, Shapechange's `target_kind` 115 (neither a unit nor a point: the interface asks for a point) and Lost in Astral's
`blood = 9` (the same as `none`). A list of bits takes a number where the code gives one and no
name does: a weapon type in an animation's `weapons`, a trader class in `sold_by`.

### Animation lists

An `.adb.toml`'s `[[animation]]` says when the game plays it by parts: `kind` (`special`, `attack`, `cast`, `run`,
`walk`, `idle`, `death`, `hit`, `cross`), `weapons` (the weapon types it is for, `any` for all seven, `none` for the
unarmed), `move` (`stop`, `walk`, `crawl`, `lie`), `play` (`once`, `looped`), and what the kind adds: an attack's or
cast's `height` (`high`, `middle`, `low`) and `blow = true`, a death's or hit's `side` (`back`, `front`, `minus_x`,
`plus_x`) and `fall` (`running`: a hero hit while it runs, `moving`: while it moves otherwise, `none`: no fall), a
special's `skill` (`tame`, `steal`, `use`, `search_body`, `briefing01`, ...; another number is the one a briefing
phrase plays), a walk's or run's `legs` (`wounded`, `badly_wounded`: a hero with hurt legs, at most ai.reg's
`[RPG] DamageLevel1Value` / `DamageLevel2Value`, walks or runs with it: "cwalk08", "cwalk06"), an idle's `idle`
(`special`: every idle the game asks for has it; `unused`: none asks for it, so it never plays), a cross's `to` (the
movement mode it changes to, `move` the one it leaves: `hero_walk` (a hero's own walk, whose `move` is left out),
`walk`, `crawl`, `lie`, `stop`), else a `number` (no game file has one). `flags` lists `arrow` (a
bow shows its arrow from `arrow`'s first frame to its second), `with_under_armour` (a hero wearing a shirt, pants, boots
or gloves may play it), `with_outer_armour` (a hero wearing a helmet, body armour or leggings may play it), and
`death_side` is where a dead body lies. `index` is the animation's place unless given. Copy the parts from a game creature's animation of the same name.
- A new record comes after every record of the game's table, so the game's indexes (in saves, in the network) do not
  move. Saves made with a mod's new items need the mod to load them.

## Textures as PNG

A texture `<name>.mmp` can be `<name>.png` in the same place: the folder of the archive that holds it
(`Res\textures\` for textures.res; ei-convert writes each PNG there). At load the PNG is made into the .mmp the game
would have read, in the .mmp's own format (DXT1, PNT3, 1555, ...), with its mip levels made by a box filter.

- Any PNG an image editor writes: palette, grey, RGB, RGBA, 8 or 16 bits a channel (16 is taken to 8). Straight
  (not premultiplied) alpha. Gamma and colour profiles are not applied.
- Settings: ei-convert's PNGs carry a text chunk `ei-texture` (TOML: `format = "DXT1"`, `channels`, `levels`). Image
  editors usually drop it; then the settings are taken from the game's .mmp of that name, so a repainted texture keeps
  its format. A PNG with neither is a new texture, kept as 32-bit colour with every mip level.
- Keep a texture's size where the game expects one: a map's ground pages are its `page_size` (512 x 512). A PNG of
  another size is read, in the .mmp's format.
- DXT textures are compressed again at load (nearly exact when the PNG is ei-convert's unchanged; close otherwise), so
  a mod of large DXT textures loads a little slower than the game.
- A PNG that cannot be read is logged (`warning: textures: ...`) and the .mmp is read.

The example mod's ground: ei-convert's `Zone1000.png` .. `Zone1007.png` of textures.res, recoloured, in the mod's
`Res\textures\`.

## Models as glTF

A figure (a unit's or an object's model) can be one glTF 2.0 file, `<figure>.glb` or `<figure>.gltf`, in the folder of
the archive that holds the figure's files (`Res\figures\` for figures.res). It stands for the figure's `.mod`, `.bon`,
`.lnk`, `.fig` files, and for its `.anm` when it has animations. ei-convert writes every figure as a `.glb`
(`Res\figures\unhuma.glb` is the human hero).

What the file must keep:
- **Node names**: a node is a part of the figure, named as the part (`hp`, `bd`, `hd`, ...); the game attaches
  weapons, armour and animations by those names. The node tree is the figure's part tree.
- **One mesh per node**, triangles. Several primitives are merged into one (the first one's material counts).
- **Translations only**: a node's translation places its part. A node's rotation and scale are left out (logged), so
  apply rotation and scale to the mesh before export.
- **The morph targets** `s`, `t`, `st`, `u`, `su`, `tu`, `stu` (Blender's shape keys, named so): the game blends the
  eight shapes of each part by three coefficients from the unit's statistics. Missing targets are copies of the base
  shape (the part does not change with the build).
- **Material names** `"<kind> <texture kind>"` (`"skin 2"`, `"wood 8"`): the material kind (leather, wood, metal, cloth,
  skin, stone, magic, or its number) and which of the unit's textures the part takes. A part without one is stone,
  kind 1. The glTF has no images: the game picks the texture by the unit's or object's database record.
- Axes: glTF's y up; ei-convert's files have the game's z up turned to glTF's y up, so Blender (which turns y up back
  into its z up) shows the model the right way up. Export with Blender's default "+Y Up".

ei-convert also writes `extras.ei` on the meshes, nodes and scene: exact data so that an unchanged file reads back
byte for byte. They are used only when they still agree with the mesh; an edited mesh is rebuilt from the glTF's data
(its vertex slots numbered by value, its bounds fitted), so it does not matter whether your tool keeps them (Blender
writes extras only with "Custom Properties" checked).

In Blender: File > Import > glTF 2.0 the converted `.glb`, edit, File > Export > glTF 2.0 with "Shape Keys" on and the
mesh's transforms applied, under the same name into your mod's `Res\figures\`. Blender itself is not part of this
repository's checks: the loader is checked on ei-convert's files, on files with the extras removed
(`mod_gltf_animation`), and on hand-written glTF.

### Worn models

A weapon in hand and a helmet, body armour or leggings worn are parts of the unit's figure: the game loads the parts
named after the item (`rh3.club05` for club number 5, `<bone>.armor12` for armour number 12) and the skeleton's bone of
that name carries each one. A new item number has no such part, so it brings its own **worn model**: one glTF named as
the item's model without its material and variant, `<type code>_<number>.glb` (`Res\figures\hm_09.glb` for hammer
number 9: the same name its textures take after the figure's, `Res\redress\unhumahm_09.gr.0.png`), in the figures'
folder.

- Its **root nodes** are named as bones of the figures that wear the item: the bone each part hangs from, as the
  game's parts of that kind do (`rh3.axe00` for axes and clubs, `rh3.sword00` for swords and daggers, `rh3` for pikes;
  an armour part `<bone>.armor<number>` under its `<bone>`, `hd` for a helmet's). Their meshes, if any, are left out,
  so you can export a part together with the hero's bone it sits on.
- Their **children** are the item's parts, named as the game loads them, with their meshes and translations as any
  figure's parts (the morph targets too). A part under another part of the item joins with it.
- Every figure that has the root bones and lacks the parts gains them, for every unit of it: the four human and orc
  figures (`unhuma`, `unhufe`, `unorma`, `unorfe`) for a club's `rh3.axe00`. A figure that has a part of the name
  keeps its own.
- The parts take the unit's second texture (material `"<kind> 2"`, as the game's weapons and helmets): its left 0.3 is
  the weapon's picture, the middle the helmet's. The item's picture (`unhumahm_09.<material>.<variant>.png`) is drawn
  into that texture over the helmet's, so leave it clear outside the item's own rectangle.
- A part has no animation of its own: it follows its bone (the game's clubs hang still from `rh3.axe00`, which the
  animations turn).

The showcase mod's hammer is one (`tests/scenarios/mod_showcase_worn.txt` checks it in the hero's hand).

## Animations as glTF

A figure's animations are glTF animations in the same `.glb`: each one named as the game names it (`cidle01`,
`uattack02`; the converted `.adb.toml` lists them), its channels on the parts' nodes.

- Time: the game plays one key a world tick, 55 ms. Keys at other times (Blender's frames) are resampled at every tick
  (STEP, LINEAR, CUBICSPLINE). In Blender a frame rate of 20 / 1.1 (Custom: FPS 20, Base 1.1) gives one frame a tick.
- How far the game plays an animation is its `frames` in the model's `.adb`: a longer or shorter animation needs that
  record changed too, e.g. `Res\database\unhuma.adb.toml`:
  ```toml
  [[animation]]
  name = "cidle18"
  frames = 210
  ```
- Rotations of every part; translations of the root part only (they move the whole figure). Scale and morph-weight
  channels are left out (logged).
- An animation without a name is left out. A `.glb` with no animations leaves the game's `.anm` in use, so a mod can
  replace a model and keep its animations.
- Vertex animation (wings' membranes, faces, hair: 222 tracks of 13 figures) is kept only in ei-convert's extras;
  Blender does not show it, and an export without the extras plays those parts without it.

## Maps as folders

A map can be a folder, `Maps\<zone>\`, which stands over `Maps\<zone>.mpr` (ei-convert writes every map so):

| File | Holds |
|---|---|
| `map.toml` | the header: `name`, `height_range`, `sectors = [x, y]`, `water_sectors`, the tile classes (`classes`: per tile texture its surface by name, as `step_sounds`, or `line`), animated tiles, water `[[material]]` |
| `heights.png` | the ground's height, a pixel a vertex, 16-bit grey: value x height_range / 65534 |
| `normals.png` | the ground's normals (16-bit RGB) |
| `offsets.png` | the vertices' sideways shifts (8-bit RGB: 128 is none) |
| `tiles.png` | the ground's tiles, a pixel a tile (8-bit RGB: R and G the texture number, B the quarter turns x 85) |
| `water.png`, `water_normals.png`, `water_tiles.png`, `water_materials.png` | the water's layers |
| `seams.toml` | where two sectors' copies of an edge vertex differ |

- A sector is 32 x 32 cells; a cell is one world unit. The vertex layers are (32 x sectors + 1) pixels each way (zone
  1, `sectors = [8, 6]`: 257 x 193), the tile layers 16 x sectors. The image's top left is the world's (0, 0).
- Each file can come from another place: a mod's `heights.png` over a full map takes the rest from it. When the
  heights come from a place above the normals, the game works the normals out from the heights.
- A mod that changes a map ships the whole map, because the game reads a zone's map from the mod as soon as the mod's
  `Maps\` has that zone's folder: either the `.mpr` (copied from the game) with the changed PNGs in `Maps\<zone>\`, or
  ei-convert's whole folder. Painting heights in the whole folder: delete its `normals.png` and `seams.toml` (the game
  then works out normals that fit the new heights, and the old edge differences are not added).
- A PNG of the wrong size or kind is logged (`warning: maps: ...`) and left out.
- The objects on the map are its `.mob` ([Map objects](#map-objects)). The AI's map of the ground is made from the
  map's files, so painted heights reach the AI too.

## Map objects

A zone's objects (`Maps\<zone>.mob`: units, buildings, trees, levers, lights, sounds, traps, effects; the weather and the
players' relations) can be `Maps\<zone>.mob.toml`. Unlike the other records it is **not merged**: the top file found is
all of the zone's objects. So a mod's file is the whole of ei-convert's file with its changes. The zone's AI graph is
still read from the `.mob` (the mod's, else the game's), and so is its script unless a `.lua` stands for it
([Scripts](#scripts)), so a mod's `.mob.toml` can stand alone.

```toml
script = "zone1.lua"           # the zone's script, when that file is there (see Scripts)
applies_relations = true

[[weather]]
wind = [-0.7282, -0.6854, 0.0]
wind_strength = 0.2
time = 13.43

[players]                      # only what differs from the defaults
names = { 3 = "Golems" }       # by player number; the others "Player-<n>"
hostile = [[0, 3], [3, 0]]     # [player, other] pairs; also allied, neutral (the default; allied with itself)

[[object]]                     # one per object, in the file's order
kind = "unit"
id = 3001
name = "ExamplePig"
texture = "unanhopi"
primary_texture = "PigF0"
skin_texture = "default0"
parent_iname = "Pig00"
class = "animal"
position = [51.0, 142.0, 0.0]
side = 5
prototype = "PigF0"

[[object.behaviour]]
type = "guard"
alarm = "watch_fearful"
guard = [51.0, 142.0, 0.0]
guard_radius = 5.0
call_radius = 5.0
```

- `kind`: `object`, `unit`, `flame`, `trap`, `lever`, `light`, `sound`, `particle`. A key left out takes the kind's
  default; ei-convert writes only the keys that differ from it.
- `id`: unique in the file; scripts find map units by it (`GetObject(3001)`). Pick one no record has.
- `class` is the object's class and must fit its kind: `hero`, `creature` or `animal` for units (copy it from a unit
  like yours), `tree`, `building`, `stone`, `stuff` (other objects), `torch` (`flame`), `magic_trap` (`trap`), `lever`
  (an `object` record may also hold a unit's class). A missing or wrong one is logged at the zone's load (`warning: map
  objects: <zone> record <id>: class <n> does not fit its kind; the record is left out`) and the record is skipped.
  `side` is the player number (0..31) the object belongs to, as `[players]` numbers them, or the player's name
  where `[players] names` gives one (the names are the file's own labels: the game reads none).
  `parent_iname` is the editor's class name.
- With `apply_stats` left out (0), a unit's `[object.stats]` are not applied: it has its prototype's statistics from
  units.udb. Its items: `carried`, `quick_items`, `spells`, `weapons` and `armour` (`"<name>.<material>"`, as
  ei-convert writes them).
- `position` is `[x, y, z]` in world units (z above the ground); `rotation` a quaternion `[w, x, y, z]`; `scale`
  `[x, y, z]`.
- A `flame`'s and a `particle`'s `effect` name particle effects ([Records that name effects](#records-that-name-effects)):
  the torch's fire (`CampFire` unless given), the effect a `particle` record stands for.
- The order of the records is the order the zone creates them: keep the game's order and add new records at the end.
- An unknown kind or key, or a value of the wrong type, is logged (`warning: map objects: <file> line n: ...`) and left
  out.

Moving or adding objects keeps the zone's AI graph from the `.mob` (a building moved onto a path does not change how
the AI plans its way across the map). A zone with no `.mob` anywhere (a mod's new zone, [Zones](#zones)) has its graph
built from its map when it loads.

## Zones

The game's zones, their exits and the islands (allods) they lie on are its zone table, `map.txt` (and `map-LMP.txt`
for multiplayer) in `texts.res`. A mod adds zones or changes the game's with `Res\texts\map.toml`, merged over the table
by name: a zone (allod, quest) the table has takes the keys given and keeps the rest; a new name is a new zone. So a
mod's file holds only its own zones and what it changes of the game's, never a copy of `map.txt`.

```toml
# Res\texts\map.toml
[[zone]]
name = "showcase"                 # what exits, LeaveToZone and the q.<zone>.* / z.<zone> variables name it by
allod = "gipat"                   # the island it lies on (its global map)
kind = "game"                     # game (a battle zone), brief (a base), edge (a way between zones)
map = "showcase"                  # Maps\showcase.mpr or the map folder Maps\showcase\
mob = "showcase"                  # Maps\showcase.mob, or Maps\showcase.mob.toml alone
size = [8, 6]                     # in sectors: the minimap's and objectives map's extent
minimap = "showcasemap"           # Res\textures: the minimap's picture
objectives_map = "showcasequest"  # and the objectives screen's
position = [-2.37, 0.12, 0.805]   # where the global map's arrow stands while the party is here

[[zone.exit]]                     # exits in order: exit 1, 2, ...
target = "gz1g"                   # the zone it leads to ("none": nowhere)
entrance = 3                      # which exit of the target the party arrives at
deploy = [49.0, 133.0, 59.0, 143.0]   # x1, y1, x2, y2: where the party stands arriving through this exit
remove = [42.0, 134.0, 47.0, 142.0]   # the area that leads out (the deploy rectangle when left out)
view = 90.0                       # the camera's turn on arrival, degrees

[[zone]]
name = "gz1g"                     # a game zone: only what changes
[[zone.exit]]
number = 3                        # its exit 3 (after the game's two): a new exit
target = "showcase"
entrance = 1
deploy = [57.0, 135.0, 61.0, 141.0]
remove = [63.0, 134.0, 69.0, 142.0]
```

- Zone keys: `name`, `allod`, `kind`, `map`, `mob`, `camera` (a base's camera path), `size`, `minimap`,
  `objectives_map`, `figure` (its marker on the global map; a zone without one is not shown there), `position`,
  `weather` (`none`, `rain`, `snow`, `tornado`; a new zone has rain), `sky` (`normal`, `cave`), `cage` (true: a base
  where the party does not walk to the people), `restrict` (`[x, y, radius]`: the camera kept in that circle).
- Exit keys: `number`, `target`, `entrance`, `deploy`, `remove`, `angle` (the area's turn about its centre, degrees),
  `pass_time` (the way's time on the global map), `view`, `deploy_angle` (the party's facing on arrival, degrees).
  `number` is the exit's place, as map.txt's `#exit n` and the `entrance` of other exits count: the zone's exit of
  that number takes the keys given; an exit without one, or past the zone's last, is added at the end. A new exit
  leads nowhere until it has a `target`.
- `[[allod]]` (`name`, `figures`: its five global map figures) and `[[quest]]` (`name`, `position` on the global map,
  `places`: `[[x, y], ...]` the objectives screen marks) merge the same way.
- A zone given another `allod` moves to that island's list. Names match without case, as the game looks them up.
- A record with an unknown key, or a value of the wrong type, is logged (`warning: map: <file> line n: ...`) and left
  out whole; `replace = true` at the top makes the file the whole table (map.txt and map-LMP.txt not read).
- A new zone needs its map (a folder of its own is enough: [Maps as folders](#maps-as-folders)), its objects
  ([Map objects](#map-objects): the `.mob.toml` alone is enough; the AI graph is then built at the load), a name text
  (`"zone <name>"` in `Res\texts\texts.toml`) and a way in: an exit of a game zone, or a script's `LeaveToZone`.

## Scripts

A zone's mission script runs from a Lua file when there is one: `Maps\<zone>.lua` (or the file the `.mob.toml`'s
`script` key names, in the same folder), in the mod's folder or the game's, down to the folder that holds the zone's
`.mob`. The `.lua` is then the whole of the zone's script: the `.mob`'s own script does not run (it is still read for
its names, which the quests and the `.mob`s the zone loads later refer to). A zone without a `.lua` runs its `.mob`'s
script as the game always has. A mod that ships its own `.mob` takes the `.lua` from its own folder only.

Start from ei-convert's file: `ei-convert <install> <folder>` writes every zone's script as `Maps\<zone>.lua`, with the
original's comments. Copy the zone's file into the mod's `Maps\` and edit it; unchanged, it plays as the original
script does.

```lua
GlobalVars {                   -- the script's globals, each at its type's zero value: 0, "", nil, an empty group
    Heroes = "group",
    count = "float",
}

-- A definition: each instance (a call of it) checks its rules in order on every tick.
Watch = Script {
    {
        condition = function(unit) return IsDead(unit) ~= 0 end,   -- optional: without one the rule always holds
        action = function(unit)                                     -- runs once the condition holds
            KillScript()                                            -- this instance checks no more rules
            print("my mod: " .. GetObjectID(unit) .. " died")
            Sleep(20)                                               -- waits 20 ticks, then goes on
            SetGameVar(0, "mymod.deaths", GetGameVar(0, "mymod.deaths") + 1)
        end,
    },
}

-- Runs when the zone loads (and again at the zone's reset).
function WorldScript()
    Watch(GetObject(1947))
    local n = 0
    while true do              -- a WorldScript or an action can wait anywhere, loops included
        Sleep(20)
        n = n + 1
        count = n
    end
end
```

- **Built-ins.** Every command and function of the game's script language (and Rebirth's additions) under its Lua
  name: the game's name, except where it abbreviates (`UMGuard` is `UnitModeGuard`, `GSSetVar` / `GSGetVar` are
  `SetGameVar` / `GetGameVar`, `SetCP` is `SetPosition`, `For` is `ForEach`; [Script functions](#script-functions)
  lists them all, each with the game's name). They take and give what the game's do: numbers (single precision, as in
  the game; a condition holds unless it is 0), strings, objects (`nil` for none) and groups.
- **Waits.** `Sleep(ticks)`, `WaitSegment(ticks)`, `SleepUntil(condition)` and `SleepUntilIdle(unit)` pause the
  running function and go on in a later tick. `ForEach(group, function(member) ... end)` walks a group from its last
  member to its first; a wait inside its body ends the body for that member, as the game's For does.
- **Loop counters.** `ForEach`, `ForEachIf`, `Every`, `Any` and `Sum` take the name of a counter global first if you
  give one, as ei-convert's files do: `ForEach("i", Heroes, function(i) ... end)` is the game's `For(i, Heroes)`.
  Like the game's loop it then sets that global (an `object` of `GlobalVars`, else an error) to each member before
  calling the function, so after the loop it holds the last member it reached: the first member after `ForEach` and
  `ForEachIf`, the member `Every` or `Any` stopped at (the last one after `Sum`); a number for the condition, or an
  empty group, leaves it as it was. Other code that reads the global (another definition, a later `Sleep`, the
  console's `listgvar`) sees that value. Leave the name out and the loop writes no global: the member is only the
  function's parameter.
- **Randomness.** `Random(n)` draws from the game's generator, so the game plays the same from a save and in a network
  game. Lua's `math.random` is not there.
- **What else Lua gives.** Lua 5.3's base functions, `string`, `table`, `math`, `utf8` and `coroutine`. `print(...)`
  writes a console line. Not there: `io`, `os`, `debug`, `package` / `require`, `dofile` / `loadfile`; `load` takes
  text only. `pairs` walks a table's keys in the same order every time: numbers, then strings (by their bytes), then
  `false` and `true`.
- **Errors** go to the console (and `ei-rebuild.log`) as `Script error: ...`. A file that does not load: `Script
  error: zone1.mob line 0: Maps\zone1.lua:<line>: <message>`, and the zone runs without its script. A `.mob` whose own
  script the game cannot read (`Script error: <zone>.mob line <n>: ...`) keeps the game's handling: the zone's script
  does not run, `.lua` or not. An error while it runs: `Script error: Maps\zone1.lua:<line>: <message>`; that
  function stops (an error in a condition: the rule does not hold), and the rest of the script goes on.
- **Saves.** A game saved in such a zone keeps the script's whole Lua state, every waiting function with its local
  variables, and goes on from there when it is loaded. A loaded save keeps running the code it was saved with, even
  when the mod's file has changed since; the new file runs the next time the zone is loaded fresh.

### Lua in an editor

The whole API (the functions below, the spells' cast value, methods and hooks, the active effects' stats) is one Lua
definition file for a Lua language server, so an editor completes the names and checks the arguments' types:
`examples\lua_api\ei_api.lua` beside ei-convert in a release (the repository's `examples/lua_api/ei_api.lua`), or
written anew by `ei-convert --lua-api <file>`. In VS Code with the Lua extension (sumneko's LuaLS), put a
`.luarc.json` in the mod's folder that names the file's folder as a library:

```json
{
    "runtime.version": "Lua 5.3",
    "workspace.library": ["C:/Games/ei-rebuild/examples/lua_api"]
}
```

In a spell file, annotate the table it returns (`local spell = {} ---@type ei.Spell`) and its hooks' arguments get
their types: `cast` is an `ei.Cast`, `stats` an `ei.Stats`.

### Script functions

Every mission script function, by subject. The types: `object` a map object (a unit, an item, a lever, ...; `nil` for
none), `group`, `number`, `string`; `name?:` may be left out (and what follows it), `...:` takes any number more.
The functions also work in the spells' hooks. These tables, and the spells' below, are generated from
`src/game/script/script_lua_api.inc` (`ei-convert --lua-api-docs`).

<!-- lua-api: functions -->

#### Script flow

| Function | Gives | What it does |
|---|---|---|
| `GlobalVars(declarations: table<string, string>)` |  | the script's globals, each set to its type's zero value: `{ name = "float" \| "string" \| "object" \| "group" }` (0, `""`, nil, an empty group) |
| `Script(rules: {condition?: fun(...): number\|boolean, action?: fun(...)}[])` | `fun(...)` | a definition: each call starts an instance with those arguments, which checks its rules in order every tick and runs the action of one that holds |
| `Sleep(ticks: number\|fun(): number)` |  | waits that many ticks, then the function goes on (a function argument is called again on every tick) |
| `WaitSegment(ticks: number\|fun(): number)` |  | waits until the running instance has run that many ticks |
| `SleepUntil(condition: number\|fun(): number)` |  | waits until the condition holds (pass a function: it is called again on every tick) |
| `SleepUntilIdle(unit: object\|fun(): object)` |  | waits while the unit's AI has an action under way |
| `KillScript()` |  | the running instance checks no more rules (not in the WorldScript) |
| `WaitEndAnimation()` |  | does nothing (as in the game) |

#### Groups and loops

| Function | Gives | What it does |
|---|---|---|
| `NewGroup()` | `group` | an empty group |
| `CopyGroup(group: group)` | `group` | a copy of the group (a group value is shared, as a table is; the game's assignment copied) |
| `AddObject(group: group, object: object)` |  | puts the object into the group (once) |
| `RemoveObject(group: group, object: object)` |  | takes the object out of the group |
| `GroupSize(group: group)` | `number` | the number of objects in the group (also #group) |
| `GroupHas(group: group, object: object)` | `number` | 1 when the object is in the group, else 0 |
| `GroupUnion(a: group, b: group)` (`GroupAdd`) | `group` | a new group of the objects in either |
| `GroupDifference(a: group, b: group)` (`GroupSub`) | `group` | a new group of a's objects that are not in b |
| `GroupIntersection(a: group, b: group)` (`GroupCross`) | `group` | a new group of the objects in both |
| `RandomObject(group: group)` (`RandObj`) | `object` | a member at random (the game's generator); nil for an empty group |
| `ForEach(group: group, body: fun(member: object))`<br>`ForEach(counter: string, group: group, body: fun(member: object))` (`For`) |  | calls the body for each member from the last to the first; a wait in the body ends it for that member. A counter global's name first: it holds each member in turn |
| `ForEachIf(group: group, condition: number\|fun(member: object): number\|boolean, body: fun(member: object))`<br>`ForEachIf(counter: string, group: group, condition: number\|fun(member: object): number\|boolean, body: fun(member: object))` (`ForIf`) |  | `ForEach`, the body only for the members the condition holds for |
| `Every(group: group, condition: number\|fun(member: object): number\|boolean)`<br>`Every(counter: string, group: group, condition: number\|fun(member: object): number\|boolean)` | `number` | 1 when the condition holds for every member (from the first; it stops at one that fails), else 0 |
| `Any(group: group, condition: number\|fun(member: object): number\|boolean)`<br>`Any(counter: string, group: group, condition: number\|fun(member: object): number\|boolean)` | `number` | 1 when the condition holds for a member (from the first; it stops there), else 0 |
| `Sum(group: group, condition: number\|fun(member: object): number\|boolean)`<br>`Sum(counter: string, group: group, condition: number\|fun(member: object): number\|boolean)` | `number` | the sum of the condition's values over the members |

#### Finding objects

| Function | Gives | What it does |
|---|---|---|
| `GetObject(id: number)` | `object` | the object with that id (nil when there is none) |
| `GetObjectByName(name: string)` | `object` | the object whose id is the text key of that name |
| `GetObjectByID(id: string)` | `object` | the object whose id is that decimal number |
| `GetLeader()` | `object` | the party leader: the server's in a network game, else player 0's first unit |
| `GetUnitOfPlayer(player: number, index: number)` | `object` | the player's party member at that index, from 0 |
| `NearestUnit(x: number, y: number, radius: number, filter: number)` | `object` | the nearest object within `radius` of (x, y) that the `filter` passes: + 1 units, + 2 other objects, + 4 any living object with a named id |
| `UnitsNearPoint(x: number, y: number, radius: number, filter: number)` | `group` | every object within `radius` of (x, y) that the filter passes (as `NearestUnit`'s) |
| `GetObjectsOnMap(class: number, ...: number)` | `group` | the world's objects whose class id is one of those given; -1: every object |
| `GetPlayerUnits(player: number)` | `group` | the world's units of that player |
| `SeenByUnit(unit: object)` (`UnitSee`) | `group` | the objects the unit sees |
| `SeenByGroup(group: group)` (`GroupSee`) | `group` | the objects the group's units see |
| `SeenByPlayer(player: number)` (`PlayerSee`) | `group` | what the player sees: its units, what they see and what it has spotted |

#### Reading objects

| Function | Gives | What it does |
|---|---|---|
| `GetObjectID(object: object)` | `number` | the object's id; 0 for none |
| `GetObjectStringID(object: object)` | `string` | the object's id as a decimal string |
| `GetObjectTypeID(object: object)` | `number` | the object's class id; -1 for none |
| `GetPrototype(unit: object)` | `string` | the unit's prototype name |
| `GetRace(unit: object)` | `string` | the unit's race |
| `GetX(object: object)` | `number` | the object's x; 0 for none |
| `GetY(object: object)` | `number` | the object's y; 0 for none |
| `GetZ(object: object)` | `number` | the object's height: the ground's at its place plus its own z |
| `GetFutureX(object: object, ticks: number)` | `number` | where the moving object's x will be that many ticks from now |
| `GetFutureY(object: object, ticks: number)` | `number` | where the moving object's y will be that many ticks from now |
| `DistanceUnitUnit(a: object, b: object)` | `number` | the distance between two objects on the ground plane (1e30 when one is missing) |
| `DistanceUnitPoint(object: object, x: number, y: number)` | `number` | the distance from the object to (x, y) on the ground plane (1e30 for none) |
| `DistancePointPoint(x: number, y: number)` | `number` | the distance of (x, y) from (0, 0): Rebirth's signature passes two of the four numbers its function reads |
| `IsInSquare(x: number, y: number, x1: number, y1: number, x2: number, y2: number)` | `number` | 1 when (x, y) lies in the rectangle from (x1, y1) to (x2, y2), else 0 |
| `UnitInSquare(object: object, x1: number, y1: number, x2: number, y2: number)` | `number` | 1 when the object stands in the rectangle from (x1, y1) to (x2, y2), else 0 |
| `IsUnitInWater(object: object)` | `number` | 1 when the object stands in water, else 0 |
| `IsUnitVisible(object: object)` | `number` | 1 when player 0 sees the object, else 0 |
| `IsUnitBlocked(object: object)` | `number` | 1 when the object is blocked (`BlockUnit`), else 0 |
| `WasLooted(object: object)` | `number` | 1 when the object's loot was taken (or it is gone), else 0 |
| `GetAIClass(unit: object)` | `number` | the unit's size class; -1 for none |
| `IsPlayer(unit: object)` | `number` | 1 when the unit is a named party member, else 0 |

#### Units' stats

| Function | Gives | What it does |
|---|---|---|
| `IsAlive(object: object)` | `number` | 1 when the object is not dead, else 0 (also for none) |
| `IsDead(object: object)` | `number` | the object's death state: 0 alive, not 0 dead (0 for none) |
| `HitPoints(unit: object)` (`HP`) | `number` | the unit's hit points |
| `MaxHitPoints(unit: object)` (`MaxHP`) | `number` | the unit's maximum hit points |
| `Mana(unit: object)` | `number` | the unit's mana |
| `MaxMana(unit: object)` | `number` | the unit's maximum mana |
| `GetStrength(unit: object)` | `number` | the unit's strength |
| `GetDexterity(unit: object)` | `number` | the unit's dexterity |
| `GetIntelligence(unit: object)` | `number` | the unit's intelligence |
| `GetSkill(unit: object, skill: string)` | `number` | the unit's skill: science, melee, archery, elemental, sense or astral |
| `GetPerk(unit: object, perk: string)` | `number` | the unit's level of the perk by its name |
| `GetActions(unit: object)` | `number` | the unit's action rate |
| `GetActionsBase(unit: object)` | `number` | the unit's action rate before its effects |
| `GetEncumbrance(unit: object)` | `number` | the weight the unit carries |
| `GetMaxEncumbrance(unit: object)` | `number` | the weight the unit can carry |
| `GetArmor(unit: object, kind: number, part: number)` | `number` | the unit's protection of the body part (0 to 6) by damage kind (0 to 5); a negative kind: the part's total |
| `GetUnitExperience(unit: object, what?: string)` (`GetUnitExp`) | `number` | the unit's experience: "max" (the default), "spent", "penalty" or "real" (max + penalty) |

#### Changing objects

| Function | Gives | What it does |
|---|---|---|
| `SetPosition(object: object, x: number, y: number, z: number)` (`SetCP`) |  | moves the object there; units near a moved object step off cells they cannot stay in |
| `SetPositionFast(object: object, x: number, y: number, z: number)` (`SetCPFast`) |  | moves the object there without moving the units around it |
| `HideObject(object: object, hide: number)` |  | hides the object (not 0) or shows it (0) |
| `BlockUnit(unit: object, block: number)` |  | the unit is blocked (not 0: it does not act) or free (0) |
| `KillUnit(object: object)` |  | kills the object (a plain hit that kills) |
| `InflictDamage(unit: object, amount: number)` |  | the unit takes a hit of that much lightning damage |
| `SetPlayer(unit: object, player: number)` |  | the unit joins the player's side, with its enemies |
| `SetEnemy(unit: object, player: number)` |  | the unit takes the player's side as an enemy |
| `GiveStrength(unit: object, amount: number)` |  | raises the unit's strength |
| `GiveDexterity(unit: object, amount: number)` |  | raises the unit's dexterity |
| `GiveIntelligence(unit: object, amount: number)` |  | raises the unit's intelligence |
| `GiveSkill(unit: object, skill: string, amount: number)` |  | raises the unit's skill: science, melee, archery, backstab, elemental, sense, astral, stealth or awareness |
| `GiveUnitExperience(unit: object, amount: number)` (`GiveUnitExp`) |  | the unit gains that much experience |
| `GiveUnitSpell(unit: object, spell: string)` |  | the unit gets the spell made from that text |
| `GiveUnitQuestItem(unit: object, item: string)` |  | the unit gets the quest item of that name |
| `RemoveQuestItem(unit: object, id: number)` |  | takes the unit's first quest item of that id |
| `GiveUnitWeapon(unit: object, item: string, override: number)` |  | the unit gets the weapon of that name; `override` 1 (a unit without a named id) also takes its stats from it |
| `GiveUnitArmor(unit: object, item: string, override: number)` |  | the unit gets the armour of that name; `override` 1 (a unit without a named id) also takes its protection from it |
| `AddUnitToServer(id: string, prototype: string, name: string, x: number, y: number, z: number)` |  | a new unit of the prototype at (x, y, z), its id the text key of `id` |
| `RemoveUnitFromServer(unit: object)` |  | takes the unit out of the world |
| `RemoveObjectFromServer(object: object)` |  | takes the object out of the world |
| `PlayAnimation(unit: object, animation: string)` |  | the unit plays the animation once |
| `StartAnimation(unit: object, animation: string)` |  | does nothing (as in the game) |
| `SetSpellAggression(unit: object, value: number)` |  | does nothing (as in the game) |
| `SetDirectionToObject(value: number)` |  | does nothing (as in the game) |

#### Unit orders

| Function | Gives | What it does |
|---|---|---|
| `MoveToPoint(unit: object, x: number, y: number)` |  | the unit walks to (x, y) |
| `MoveToObject(unit: object, target: object)` |  | the unit walks to where the target stands |
| `RotateTo(unit: object, x: number, y: number)` |  | the unit turns towards (x, y) |
| `Follow(unit: object, leader: object)` |  | the unit follows the leader |
| `Attack(unit: object, target: object)` |  | the unit attacks the target |
| `ResetTarget(unit: object)` |  | the unit drops its target and goes back to its behaviour |
| `Cast(unit: object, x: number, y: number)` |  | the unit casts its first spell at (x, y) on the ground |
| `CastSpellPoint(spell: string, x: number, y: number, toX: number, toY: number)` |  | the spell made from that text cast with no caster from (x, y) at the point (toX, toY) |
| `CastSpellUnit(spell: string, x: number, y: number, target: object)` |  | the spell made from that text cast with no caster from (x, y) at the target |
| `Idle(unit: object)` |  | the unit's modes cleared and its action stopped |
| `Guard(unit: object, x: number, y: number, radius: number)` |  | the unit guards the circle around (x, y) |
| `Sentry(unit: object, x: number, y: number)` |  | the unit stands sentry at (x, y) |
| `Lie(unit: object)` |  | the unit lies down |
| `Crawl(unit: object)` |  | the unit crawls |
| `Stand(unit: object)` |  | the unit walks upright |
| `Walk(unit: object)` |  | the unit walks |
| `Run(unit: object)` |  | the unit runs |
| `Rest(unit: object)` |  | the unit stops what it has queued |

#### Unit modes (the AI)

| Function | Gives | What it does |
|---|---|---|
| `UnitModeClear(unit: object)` (`UMClear`) |  | takes every mode off the unit |
| `UnitModeStandard(unit: object)` (`UMStandard`) |  | adds aggression, suspicion and the corpse watcher |
| `UnitModeAggressionLevel(unit: object, level: number)` (`UMAg`) |  | the unit's temper from 0 (fearful) to 3 (aggressive): its aggression, fear, suspicion and corpse watcher modes replaced |
| `UnitModeAggression(unit: object)` (`UMAggression`) |  | adds aggression: the unit attacks enemies it notices |
| `UnitModeRevenge(unit: object)` (`UMRevenge`) |  | adds revenge: aggression against who attacks it |
| `UnitModeFear(unit: object, fearful: number)` (`UMFear`) |  | adds fear: the unit flees (not 0: from any enemy) |
| `UnitModeSuspicion(unit: object)` (`UMSuspection`) |  | adds suspicion: the unit goes to look at what it hears |
| `UnitModeCorpseWatcher(unit: object)` (`UMCorpseWatcher`) |  | adds the corpse watcher: the unit raises the alarm at corpses |
| `UnitModeSentry(unit: object, x: number, y: number)` (`UMSentry`) |  | adds a sentry post at (x, y) |
| `UnitModeGuard(unit: object, x: number, y: number, radius: number)` (`UMGuard`) |  | adds guarding the circle around (x, y) |
| `UnitModeGuardEx(unit: object, x: number, y: number, radius: number, chase: number)` (`UMGuardEx`) |  | adds guarding the circle around (x, y), chasing up to the second radius |
| `UnitModeFollow(unit: object, leader: object)` (`UMFollow`) |  | adds following the leader |
| `UnitModePatrol(unit: object)` (`UMPatrol`) |  | adds a patrol (its points from `UnitModePatrolAddPoint`) |
| `UnitModePatrolClear(unit: object)` (`UMPatrolClear`) |  | empties the unit's patrol route |
| `UnitModePatrolAddPoint(unit: object, x: number, y: number)` (`UMPatrolAddPoint`) |  | adds a point to the unit's patrol route |
| `UnitModePatrolAddPointLook(unit: object, angle: number, wait: number, turn: number)` (`UMPatrolAddPointLook`) |  | at the route's last point the unit looks towards the angle for `wait`, turning in `turn` |
| `UnitModePlayer(unit: object)` (`UMPlayer`) |  | the unit's modes replaced by the player's (it obeys its player) |

#### Players, parties and diplomacy

| Function | Gives | What it does |
|---|---|---|
| `GetPlayer(unit: object)` | `number` | the unit's side (player number) |
| `GetPlayerOfUnit(unit: object)` | `number` | the player whose party the unit is in; -1 for none |
| `PlayerExists(player: number)` | `number` | 1 when there is a player of that number, else 0 |
| `AddUnitUnderControl(player: number, unit: object)` |  | the player controls the unit |
| `RemoveUnitFromControl(player: number, unit: object)` |  | no player controls the unit |
| `GetDiplomacy(a: number, b: number)` | `number` | the two players' relation: 1 allied, -1 hostile, 0 neutral |
| `SetDiplomacy(a: number, b: number, relation: number)` |  | sets the two players' relation: above 0 allied, below 0 hostile, else neutral |
| `IsEnemy(unit: object, player: number)` | `number` | 1 when the unit is hostile to the player, else 0 |
| `SetPlayerAggression(player: number, value: number)` |  | does nothing (as in the game) |
| `IsPlayerInDanger(player: number)` | `number` | 1 when the player's party is in danger, else 0 |
| `IsPlayerInSafety(player: number)` | `number` | 1 when the player's party is safe, else 0 |
| `GodMode(player: number, on: number)` |  | the player's units cannot die (not 0) or can (0) |
| `GetMercenaryCount(player: number)` (`GetMercsNumber`) | `number` | the player's living mercenaries |
| `RecalculateMercenaryBriefings()` (`RecalcMercBriefings`) |  | the mercenaries' briefings worked out again |
| `CreateParty(player: number, party: string)` |  | a new party record of that name |
| `RemoveParty(player: number, party: string)` |  | removes the party record |
| `SetCurrentParty(player: number, party: string)` |  | the party the player plays next |
| `SendCurrentParty(player: number)` |  | sends the player's party to its client again |
| `AddUnitToParty(player: number, unit: string, prototype: string)` |  | adds a unit of the prototype to a party record ("party.unit") |
| `RemoveUnitFromParty(player: number, unit: string)` |  | removes the unit ("party.unit") from its party record |
| `RedeployParty(player: number)` |  | places the player's party again at its start |
| `CopyItems(player: number, from: string, to: string)` |  | one party unit's items copied to another ("party.unit") |
| `CopyStats(player: number, from: string, to: string)` |  | one party unit's stats copied to another ("party.unit") |
| `LeaveToZone(player: number, zone: string, entrance: number)` |  | the player's party leaves for the zone, at that entrance |
| `ShowAllUnits(show: number)` |  | 0 draws every unit; anything else only the visible ones |

#### Items and money

| Function | Gives | What it does |
|---|---|---|
| `GiveItem(player: number, item: string)` |  | the player gets the item of that name |
| `GiveQuestItem(player: number, item: string)` |  | the player gets the quest item of that name |
| `EraseQuestItem(player: number, id: number)` |  | takes the player's quest item of that id |
| `HaveItem(player: number, id: number)` | `number` | 1 when the player has the quest item of that id, else 0 |
| `GetLootItemsCount(player: number)` | `number` | the number of the player's items |
| `GetMoney(player: number)` | `number` | the player's party's money |
| `GiveMoney(player: number, amount: number)` |  | the player's party gets that much money |
| `AddLoot(player: number, from: string, to: string)` |  | one party record's items and money added to another's |
| `CopyLoot(player: number, from: string, to: string)` |  | one party record's items and money replace another's |
| `Loot(player: number, unit: object)` |  | the player loots the dead unit, which then goes |
| `FixItems()` |  | repairs every item |
| `GiveExperience(player: number, amount: number)` (`GiveExp`) |  | the player's party is awarded that much experience |

#### Quests and game variables

| Function | Gives | What it does |
|---|---|---|
| `QuestStart(name: string)` (`QStart`) |  | starts building the quest of that name |
| `QuestObjectiveArea(area: number)` (`QObjArea`) |  | the quest's objective: the heroes reach the area |
| `QuestObjectiveSeeObject(object: string)` (`QObjSeeObject`) |  | the quest's objective: the heroes come near the object (a script name) |
| `QuestObjectiveSeeUnit(unit: string)` (`QObjSeeUnit`) |  | the quest's objective: the heroes see the unit |
| `QuestObjectiveKillUnit(unit: string)` (`QObjKillUnit`) |  | the quest's objective: the unit dies |
| `QuestObjectiveKillGroup(group: string)` (`QObjKillGroup`) |  | the quest's objective: every unit of the group dies |
| `QuestObjectiveUse(lever: string, state: number)` (`QObjUse`) |  | the quest's objective: the lever is in that state |
| `QuestObjectiveGetItem(id: number)` (`QObjGetItem`) |  | the quest's objective: the heroes have the quest item |
| `QuestFinish()` (`QFinish`) |  | ends the quest begun by `QuestStart` and adds its script |
| `QuestComplete(player: number, quest: string)` |  | the quest is done: its experience to the player |
| `SetGameVar(player: number, name: string, value: number)` (`GSSetVar`) |  | sets the game variable (the game server's in a network game) |
| `SetGameVarMax(player: number, name: string, value: number)` (`GSSetVarMax`) |  | raises the game variable to the value when it is lower |
| `DeleteGameVar(player: number, name: string)` (`GSDelVar`) |  | deletes the game variable |
| `GetGameVar(player: number, name: string)` (`GSGetVar`) | `number` | the game variable's value (0 when not set) |
| `SetPlayerGameVar(player: number, name: string, value: number)` (`GSSetPlayerVar`) |  | sets the player's own variable |
| `GetPlayerGameVar(player: number, name: string)` (`GSGetPlayerVar`) | `number` | the player's own variable |
| `ListPlayerGameVars(player: number)` (`GSListPlayerVars`) |  | writes the player's variables to the console |

#### Areas and alarms

| Function | Gives | What it does |
|---|---|---|
| `AddRectToArea(area: number, x1: number, y1: number, x2: number, y2: number)` |  | adds the rectangle from (x1, y1) to (x2, y2) to the area of that number |
| `AddRoundToArea(area: number, x: number, y: number, radius: number)` |  | adds the circle around (x, y) to the area of that number |
| `DeleteArea(area: number)` |  | deletes the area |
| `IsInArea(area: number, x: number, y: number)` | `number` | 1 when (x, y) lies in the area, else 0 |
| `InvokeAlarm(alarm: number, x: number, y: number)` |  | raises the alarm of that number at (x, y) |
| `IsAlarm(alarm: number)` | `number` | 1 while the alarm is raised, else 0 |
| `AlarmTime(alarm: number)` | `number` | the tick the alarm was raised at |
| `AlarmPositionX(alarm: number)` (`AlarmPosX`) | `number` | the x where the alarm was raised |
| `AlarmPositionY(alarm: number)` (`AlarmPosY`) | `number` | the y where the alarm was raised |

#### Levers and traps

| Function | Gives | What it does |
|---|---|---|
| `GetLeverState(lever: object)` | `number` | the lever's state |
| `SwitchLeverState(lever: object, state: number)` |  | switches the lever to the state |
| `SwitchLeverStateEx(lever: object, state: number, time: number)` |  | switches the lever to the state over that time |
| `EnableLever(lever: object, on: number)` |  | the lever can be used (not 0) or not (0) |
| `SetScience(lever: object, key: number, level: number, a: number, b: number, c: number, d: number)` |  | locks the lever: the key's id, the lock's level and four flags (not 0: set) |
| `ActivateTrap(trap: object, on: number)` |  | arms the trap (not 0) or disarms it (0) |

#### World, time and weather

| Function | Gives | What it does |
|---|---|---|
| `GetWorldTime()` | `number` | the hour of the day, 0 to 24 |
| `IsNight()` | `number` | how much night it is, 0 by day |
| `RunWorldTime(hour: number)` |  | sets the hour and lets the time run |
| `FixWorldTime(hour: number)` |  | sets the hour and stops the time there |
| `SetWeather(weather: string)` |  | "rain", "snow" or "clear" |
| `SetWind(x: number, y: number, z: number, strength: number)` |  | the wind's direction and strength |
| `SetWaterLevel(material: number, level: number, ticks: number)` |  | the water of the terrain material rises or falls to the level over that many ticks |
| `SetBackgroundColor(r: number, g: number, b: number)` (`SetBackGroundColor`) |  | the fog's colour, 0 to 255 each |
| `SetSunLight(a: number, b: number, c: number, d: number, e: number, f: number, g: number, h: number, i: number)` |  | does nothing (as in the game) |
| `GetGridHeight(x: number, y: number)` (`GetZValue`) | `number` | the ground's height at (x, y) on the terrain grid |
| `GetSurfaceHeight(x: number, y: number)` (`GetBSZValue`) | `number` | the surface's height at (x, y), objects such as bridges included |
| `AddMob(file: string)` |  | loads the objects of the .mob file into the zone |
| `AddMobEx(file: string, keepIds?: number)` |  | `AddMob` without the loading movie; `keepIds` 1: the objects keep their ids |
| `PauseGame(pause: number)` |  | outside network games: 0 pauses the game, anything else goes on |
| `GameOver()` |  | the game is over for player 0 |
| `GameOverMessage(title: string, text: string)` (`GameOverMSG`) |  | the game is over for player 0, with a message |
| `FileExists(path: string)` (`IsFileExists`) | `number` | 1 when the mod's folder holds the file, else 0 |

#### Lights, particles and sounds

| Function | Gives | What it does |
|---|---|---|
| `CreatePointLight(id: number, x: number, y: number, z: number, radius: number, r: number, g: number, b: number)` |  | a point light (a light of that id replaced), its colour 0 to 255 each |
| `MovePointLight(id: number, x: number, y: number, z: number, ticks: number, remove: number)` |  | the light moves to (x, y, z) over that many ticks; `remove` 1: then it goes |
| `DeletePointLight(id: number)` |  | removes the light |
| `CreateParticleSource(id: number, x: number, y: number, z: number, size: number, effect: string)` |  | a particle effect (the game's by its name or one of `Res\effects`); a negative id takes a new one |
| `MoveParticleSource(id: number, x: number, y: number, z: number, ticks: number, remove: number)` |  | the effect moves to (x, y, z) over that many ticks; `remove` 1: then it goes |
| `DeleteParticleSource(id: number)` |  | removes the particle effect |
| `AttachParticleSource(id: number, object: number)` |  | the effect follows the object of that id |
| `AttachParticles(id: number, object: object)` |  | the effect follows the object |
| `SetParticleSourceSize(id: number, size: number)` |  | the effect's size |
| `CreateLightning(id: number, x1: number, y1: number, z1: number, x2: number, y2: number, z2: number, width: number)` |  | a lightning bolt from one point to the other; a negative id takes a new one |
| `DeleteLightning(id: number)` |  | removes every bolt of that id |
| `CreateSound(x: number, y: number, z: number, volume: number, radius: number, sound: string)` (`CreateFX`) |  | plays the sound once at (x, y, z) |
| `CreateSoundSource(id: number, x: number, y: number, z: number, volume: number, radius: number, sound: string)` (`CreateFXSource`) |  | a looping sound at (x, y, z); a negative id takes a new one |
| `CreateRandomizedSoundSource(id: number, x: number, y: number, z: number, volume: number, radius: number, minDelay: number, maxDelay: number, spread: number, sound: string)` (`CreateRandomizedFXSource`) |  | a sound played again and again after random delays (ticks), spread around (x, y, z) |
| `DeleteSoundSource(id: number)` (`DeleteFXSource`) |  | removes the sound source |
| `PlaySound(x: number, y: number, z: number, volume: number, sound: string)` (`PlayFX`) |  | does nothing (as in the game) |

#### Camera, movies, music and messages

| Function | Gives | What it does |
|---|---|---|
| `PlayCamera(path: string)` |  | plays the camera path |
| `IsCameraPlaying()` | `number` | not 0 while a camera path plays |
| `StartCameraSave(file: string)` |  | records the camera's moves into the file |
| `StopCameraSave()` |  | stops recording the camera |
| `SetCameraOrientation(angle: number)` |  | does nothing (as in the game) |
| `SetCameraPosition(x: number, y: number, z: number)` |  | does nothing (as in the game) |
| `PlayMovie(file: string)` |  | plays the movie of the `Movies` folder |
| `PlayMusic(player: number, music: string)` |  | the player hears the jingle |
| `ShowCredits(file: string)` |  | does nothing (as in the game) |
| `ShowBitmap(value: number, file: string)` |  | does nothing (as in the game) |
| `SendEvent(kind: number, value: number)` |  | sends the value to every player |
| `SendStringEvent(kind: number, text: string)` |  | sends the text to every player |
| `Say(text: string, ...: string)` |  | in a network game, the texts joined as a chat line from the server |
| `ConsoleFloat(value: number)` |  | writes the number as a console line |
| `ConsoleString(text: string, ...: string)` |  | writes the texts joined as a console line |

#### Numbers and strings

| Function | Gives | What it does |
|---|---|---|
| `IsEqual(a: number, b: number)` | `number` | 1 when a equals b, else 0 |
| `IsLess(a: number, b: number)` | `number` | 1 when a is less than b, else 0 |
| `IsGreater(a: number, b: number)` | `number` | 1 when a is greater than b, else 0 |
| `IsEqualString(a: string, b: string)` | `number` | 1 when the strings are equal, else 0 |
| `Not(a: number)` | `number` | 1 when a is 0, else 0 |
| `And(a: number, b: number, ...: number)` | `number` | 1 when no argument truncates to 0, else 0 |
| `Or(a: number, b: number, ...: number)` | `number` | 1 when an argument does not truncate to 0, else 0 |
| `Add(a: number, b: number, ...: number)` | `number` | the sum, in single precision |
| `Sub(a: number, b: number)` | `number` | a - b, in single precision |
| `Mul(a: number, b: number, ...: number)` | `number` | the product, in single precision |
| `Div(a: number, b: number)` | `number` | a / b, in single precision |
| `Max(a: number, b: number, ...: number)` | `number` | the largest |
| `Min(a: number, b: number, ...: number)` | `number` | the smallest |
| `Abs(a: number)` | `number` | a without its sign |
| `Round(a: number)` | `number` | a rounded to the nearest whole number, halves away from 0 |
| `Ceil(a: number)` | `number` | the smallest whole number not below a |
| `Floor(a: number)` | `number` | the largest whole number not above a |
| `Sin(radians: number)` | `number` | the sine |
| `Cos(radians: number)` | `number` | the cosine |
| `Pow(a: number, b: number)` | `number` | a to the power b |
| `Random(a: number, b?: number)` | `number` | a draw of the game's generator: from 0 to `a`; with `b`, from `a` to `b` |
| `RandomString(a: string, b: string, ...: string)` (`RandStr`) | `string` | one of the strings at random (the game's generator) |
| `FloatToString(value: number, digits?: number)` (`FloatToStr`) | `string` | the number in decimals (3 unless given, 0 to 8), trailing zeros cut |
| `StringToFloat(text: string)` (`StrToFloat`) | `number` | the number the text starts with (0 when none) |
| `StringConcat(a: string, b: string, ...: string)` (`StrCat`) | `string` | the strings joined |
| `StringLength(text: string)` (`StrLen`) | `number` | the text's length in characters |
| `StringContains(text: string, part: string)` (`StrHas`) | `number` | 1 when the text contains the part, else 0 |
| `StringMid(text: string, start: number, count: number)` (`StrMid`) | `string` | `count` characters from `start` (1 the first, -1 the last; `count` 0: the rest) |
| `StringLower(text: string)` (`StrLower`) | `string` | the text in lower case |
| `StringUpper(text: string)` (`StrUpper`) | `string` | the text in upper case |

<!-- /lua-api -->

## Effects

A particle effect of your own is one file, `Res\effects\<name>.effect.toml`; a script starts it by that name:
```
CreateParticleSource(1, x, y, z, 1, "my_aura")
```
(the numbers: an id of your choice, the position, the size). The effect says what particles it throws, how they move,
grow, fade and animate, and how that changes over time and with the values the game gives it. The full schema is in
docs/notes/effects.md, "Data effects"; the showcase mod's `Res\effects\showcase_aura.effect.toml` uses most of it.

```toml
texture = "Teleport"          # an effects texture (Res\textures), 16 frames in 4 columns
blend = "additive"

[inputs]                      # the values the game (or a script) sets
charge = { type = "float", default = 0.0 }

[emit]
rate = "2 + charge * 6"       # particles per tick: any value can be an expression

[[particle]]
name = "spark"
lifetime = "range(10, 20)"    # ticks; range() draws a random number
spawn = { shape = "disc", radius = "size", on_ground = 0.05 }
velocity = [0, 0, 0.03]
size = { start = 0.2, growth = 0.95 }
color = "FFD890"
alpha = { tick = "255 * (1 - p.t)" }   # p.t: the particle's age, 0 to 1
frame = { start = "pick(16)", delay = 1 }
```

- **Stages** (`[[stage]]`) make it change over time: each lasts `ticks` or `until` a condition holds, then goes to its
  `next` (`"end"` ends the effect), with its own `rate`, and `enter` actions (a burst of particles, an input set,
  another effect started).
- **Anchors** (`[[anchor]]`) are moving points it owns (orbs, rings); a particle kind with `at = "<anchor>"` starts at
  them, and with `space = "anchor"` rides them.
- **Inputs**: `float`, `vector`, `bool`, `enum` (named values) and `event` (fired; `[[on]] event = "..."` reacts).
- **Expressions**: arithmetic, comparisons, `? :`, `sin`, `cos`, `min`, `max`, `clamp`, `lerp`, `pow`, `exp`, `log`,
  random `range`, `chance`, `pick`; tables (`fade[p.age]`) and curves (`glow(p.t)`) from `[tables]` and `[curves]`;
  the world's wind (`wind.x/y/z`, `wind_strength`) and the carrier's `carrier_radius` and `carrier_z`; when it is a
  unit, `carrier_unit` (1), `carrier_unit_radius` (its own radius), `carrier_running`, `carrier_dead`, its own x and y axes `carrier_axis.x/y/z` and
  `carrier_axis_y.x/y/z`, its feet
  `carrier_left_foot.x/y/z` and `carrier_right_foot.x/y/z`. A particle's own vars may not reuse the built-in names
  (`p.age`, `p.life`, `p.x`...). A var with `at_create = true` starts when the source is made (a random count drawn
  once, before the first tick); so does an anchor group with `at_create = true` (the fireball's fragments). The
  scene's sun colour is `sun.r/g/b` (0..1: the tornado takes its colour from it).
- **Vectors**: a value can be a vector of up to four numbers. `origin`, `motion`, `wind`, `extent`, `spread_min`,
  `spread_max`, `side`, `up`, `sun`, `carrier_axis`, `carrier_axis_y`, the feet, a `vector` input, a list's point
  `path[i]`, `p.position`, `p.velocity`, `p.spawn`, `p.color` (0..255), `a.position` and `a.spawn` are vectors;
  `vec3(x, y, z)` makes one (`vec3(0.5)`: 0.5 in all three; `vec2`, `vec4` too). `.x`, `.xy`, `.zyx`, `.rgb` pick its
  numbers (`origin.z`, `sun.r`). Arithmetic and the maths functions work number by number, a single number counting
  for every one (`origin + p.spawn * 0.5`); `dot`, `length`, `normalize`, `cross` and `distance` are the usual ones;
  `ball()` is a random point in the unit ball, `shell()` one on its surface, `disc()` one in the unit circle on the
  ground, `circle(angle)` the circle's point at that angle, and `range(lo, hi)` of vectors draws each number;
  `platform_normal(point)` is a platform's slope there and `on_ground(point, height)` the point moved to that height
  over the ground (`ground`, `surface` and `platform_height` take a point too). A var is as wide as its value (`dir =
  "normalize(p.spawn)"`, read as `p.dir` or `p.dir.z`); a particle's vars hold eight numbers between them. A field of
  three (`position`, `velocity`, `gravity`, `color`, a spawn's `min`, `max`, `from`, `to`, an anchor's `position`) is
  one vector expression or `[x, y, z]`.
- **Reactions** (`[[on]] event = "tick"`, with a `when` condition) run actions each tick: `{ set = "height", value =
  ... }` changes an input, a var or the source's own `size`, `height`, `growth`, `alpha` or `param`, `{ stop_emitting =
  true }` ends the emission (the particles live on), `{ stop = true }` stops the source, `{ let_go = true }` lets go
  of the carrier (the source stays where it is), `{ shake = "size", time = 5, decay = 0.25 }` shakes the camera by
  that strength around the source (as the mushroom's burst), `{ scorch = "size", alpha = 0.6 }` leaves a scorch mark
  of that radius on the ground under it. `set` also takes the spread box and the extent
  (`spread_min.x`, `extent.y`...). `[[on]] event = "emit"` runs after each particle the emission makes,
  at once (the next particle of the tick sees it): a count for the next tick by the carrier's state, a stop when the
  carrier is dead, or `{ emit = 25, kind = "puff" }` (a burst of another kind made right after it, as the poison fog's
  grid of puffs at its first particle). `[[on]] event = "spawn"` runs once when the game's effect object puts the
  effect into the world (a spell's blast): the fireball's and the lightning's scorch marks and the lightning's shake.
- **Particles that change the source**: `source = { size = "..." }` in a particle kind sets the source's value at
  each of its updates (the particles after it read the new value), as a fading mark that shrinks its source; `drop =
  "..."` throws a particle away once it is made (its random draws stay drawn); `anchor_index = "p.gust"` starts it at
  the anchor of that number, chosen after its vars, and `pick = "first"` at the group's first live one; the spawn
  shape `flat_ball` draws a point of the flat disc as `ball` draws one of the ball, `segment` a point between `from`
  and `to` (by default the source's extent: a beam's or a wall's length). `source` also writes an effect var by its
  name and the particle's anchor's vars (`source = { a.size = "p.drawn" }`: the wisps of an orb read how big
  the orb is). `jump = "<condition>"` makes a particle jump: on an update where it holds, its previous place, size and colour
  are its new ones (drawn there, not moving there from the old place); `jump = { when = "...", size = "..." }` also
  gives the previous size (a ball thrown back to its start).
- **More than one anchor's values**: `swirl.px[2]` reads the var `px` of the group `swirl`'s anchor number 2 (counted
  from 0 in the order they were made, held to the first and last), `swirl.x[0]` its place; a light that takes the
  first light's orbit, a spark pulled towards a random swirl.
- **Random points**: `ball_x()` draws a random point in the unit ball and gives its x, then `ball_y()` and `ball_z()`
  the same point's y and z (a random direction for three vars). The `ball` and `shell` spawns take `tries` (10: the
  draws before the last is kept; 0: draw until the point is inside) and `shift = [x, y, z]` (the ball moved by it, so
  more points fall on that side: the fireball's fragments fly upwards).
- **Anchors made as it goes**: `{ add_anchor = "orb", count = 3 }` makes anchors at any time (`count = 0` in the
  `[[anchor]]` makes none at the start), and an anchor's own `spawn = { shape = "ball" }` gives each a random point
  (`a.spawn.x/y/z`) to place it by; `[[on]] event = "before_emit"` runs before each particle is made, so the first
  particle can find its anchors ready (`when = "anchors.orb == 0"`).
- **The ground and lists**: `ground(x, y)` is the ground's height there (a kill plane: `kill = "p.z < ground(p.x,
  p.y)"`), `platform_height(x, y)` the height of a platform (a bridge, a floor) there and `platform_normal_x(x, y)`,
  `_y`, `_z` its slope (0 and an upright normal where there is none), `surface(x, y)` the top of what stands there
  (the platform, else the ground: the path markers' glows); an input `{ type = "list" }` holds points the
  game gives (`path.count`, `path.x[i]`, `path.y[i]`, `path.z[i]`), with a `default = [[x, y, z], ...]`.
- **When a particle dies**: `on_death = [{ emit = 4, kind = "ember" }, { effect = "Sparks" }]` emits particles of
  another kind where it died, at once, or starts an effect there.
- **Layers**: a mod's file of the name merges over a lower one field by field, by the rule of variants below;
  `replace = true` replaces it whole.
- **Variants**: `like = "<effect>"` starts your effect from that effect as the game has it (with every mod's files
  over it, and its own `like` if it has one); your file names only what differs:
  ```
  like = "CastingIllusion"   # the illusionist's cast, with one light turning faster, on the healer's texture
  texture = "Casting1"

  [vars]
  orbiters = 1
  turn = 0.5
  ```
  Your fields merge over the base's:
  - A table (`[vars]`, `[emit]`, a kind's `size = { ... }`) merges key by key. A value you give replaces the base's,
    in its place; a new key is added.
  - `[[particle]]`, `[[stage]]` and `[[anchor]]` merge with the base's of the same `name`; a new name is added after
    the base's.
  - `[[on]]` merges with the base's of the same `name`, or, without one, of the same `event`, in order: your first
    `[[on]] event = "tick"` with the base's first "tick" reaction, your second with its second; one more than the base
    has is added.
  - A list of actions (`do`, `enter`, `on_death`) and a list of numbers (a table, a vector, a colour) replace the
    base's whole.
  - `remove = true` takes the base's out: `[[particle]] name = "spark" remove = true`, `[vars] glow = { remove = true }`.
  - `before = "<name>"` puts a new one before the base's of that name instead of after the last (kinds are tried in
    order, vars tick in order): `[[particle]] name = "marker" before = "spark"`, `[vars] roll = { start = 1, tick =
    "...", before = "rise" }`.

  Your effect keeps its own name. A mod that changes the base changes your variant too, where your file does not name
  that field. A missing base, or a chain that comes back to itself, is a problem: the effect shows nothing. The game's
  own families are written this way (CastingDomination, CastingEnchantment and CastingHealing on CastingIllusion,
  Shower2-4 on Shower1, the Modifier rings).
- **Placing a particle yourself**: `position = [x, y, z]` starts it where the expressions say (`origin.x`, the
  source's motion, its `side` and `up` axes), and `position = { tick = [...] }` moves it each tick by your own sum
  instead of its velocity.
- **Frames**: `frame = { start, delay, mode, tick }`; past the texture's last frame `mode = "loop"` (the default)
  starts again, `"hold"` stays on the last, `"die"` ends the particle and `"free"` keeps the number (drawn as the first
  frame; a table read by it sees the real count).
- **The game's own effects** are all written this way (`assets/effects` in the source:
  FireStar, AcidStar, CurseStars, PortalStar, Sparks, FeebleMind, ZoneExit, the trails FireBall, FireArrow,
  RikArrowSmoke, AcidRay, the fires and smokes CampFire, Fire, Smoke, Tendril, VulcanSmoke, and Portal, TeleportRing,
  Teleport, Banshee, BlueGas, Nuke, the marks on units Stun, Regeneration, VisionStar1-4, FeetCloud1 / 2, StartTrans,
  the leaves Falling1-4, the protection spheres SphereAcid, SphereElectricity, SphereFire and Antimagic, the blood
  showers Shower1-4, the poison clouds PoisonFog and PoisonCloud, the mushroom and its Transform, the dust GroundDust1
  / 2, the beam Link, the rings Modifier1-12 and ClayRing, the casting circles Cylinder1 / 2, the casts Casting,
  Healing, CastingFire, CastingElectricity, CastingAcid, CastingDivination, CastingIllusion, CastingDomination,
  CastingEnchantment and CastingHealing, the regeneration orbs Silence and the bag's balls Bag, the blasts FireBlast and
  LightningBlast, the Geyser, the funnels Tornado and CurseHolder and the path markers' glow ShapeChange); a mod's
  `Res\effects\FireStar.effect.toml` retunes the fire arrow's flash with only
  the fields it names, and `Res\effects\ZoneExit.effect.toml` the glow over every zone exit.
- **Problems** are `warning: effects: <file>:<line>: ...` lines in `ei-rebuild.log`; an effect with an error shows
  nothing. An unknown key is only noted.
- In a network game every player needs the same effect files: they count in the mod's content hash.

### Records that name effects

Spells, units and map objects name the particle effects they show, each by an effect's name (yours from
`Res\effects`, or a game's: `CastingFire`, `Shower1`, `Sparks`, `CampFire`, ...). The game's own choices are data
built into the executable below every layer (the source's `assets/spells/spells.sdb.toml` and
`assets/units/units.udb.toml`), so a mod's file changes them like any record's fields:

| Where | Field | Shown |
|---|---|---|
| spell (`spells.sdb.toml` `[[spell]]`) | `cast_look` | on the caster while it casts the spell (a unit whose model has `cast_effect`); without it its school's, below |
| `spells.sdb.toml` `[cast_looks]` | by school: `fire`, `lightning`, `acid`, `illusion`, `divination`, `enchantment`, `healing`, `domination`, `none` | on the caster of a spell of that school (or of the record's `cast_school`) without a `cast_look`; the game's: CastingFire, CastingElectricity, CastingAcid, CastingIllusion, CastingDivination, CastingEnchantment, CastingHealing, CastingDomination, and Casting for `none` |
| spell | `look`, `look_ring` | on a unit while an effect of the spell's kind lasts on it, and as it comes and goes ([Active effects](#active-effects)) |
| model (`units.udb.toml` `[[model]]`) | `blood_look` | on a unit of the model where a blow hits it (at the part hit, sized by it); without it its `blood` kind's, below |
| `units.udb.toml` `[blood_looks]` | by blood kind: `none`, `red`, `green`, `blue`, `brown`, `red_spray`, `green_spray`, `blue_spray`, `brown_spray` | on a unit whose model has that `blood` and no `blood_look`; the game's: Shower1 to Shower4 for red, green, blue and brown (a spray as its colour), nothing for `none` |
| model | `shock_look` | on a unit of the model hit by lightning (sized by the unit); without it the file's top-level `shock_look`, the game's `Sparks` |
| map object `flame` (`Maps\<zone>.mob.toml`) | `effect` | the torch's fire and smoke (the game's `CampFire`, the default) |
| map object `particle` | `effect` | the effect the object stands for (ei-convert writes the game's by name: `effect = "Fire"`); none when empty |
| a spell's script | `cast:Effect(name)`, `cast:Missile { effect = name }` | a cast's effects and its missile ([Spells](#spells)) |
| a zone's script | `CreateParticleSource(id, x, y, z, size, name)` | an effect at a point ([Lights, particles and sounds](#lights-particles-and-sounds)) |

```toml
# Res\database\units.udb.toml: the golem bleeds stone chips; every red-blooded creature a darker shower.
[[model]]
name = "Showcase Golem"
blood_look = "golem_chips"     # Res\effects\golem_chips.effect.toml

[blood_looks]
red = "dark_blood"             # Res\effects\dark_blood.effect.toml
```

A name no effect has is a `warning: effects: ...` line in the log, and nothing shows. The blood kind still decides the
stain on the ground (`red` to `brown` leave one, the sprays none).

### The effect viewer

`ei-effects [<effect>] [--mod <folder>]` is a program of its own, shipped beside ei-rebuild: copy it into the game
folder and start it there (or name the folder with `EI_GAME_ROOT`). It loads the install's files and, with `--mod`,
your mod's over them (a folder or a .zip, as the settings' `Mod` names it). `<effect>`, when you give one, plays at
once.

The scene is a flat checkered ground with a hero on it, the figure effects go on. In the Figure section it plays any
animation of its database, walks a circle and runs (what an effect takes its carrier for). The camera turns with the
right mouse button, pans with the middle one, zooms with the wheel and moves with W A S D or the arrows.

Its windows (Effects, Timeline, Inputs, Fields, Text) dock where you drag them, or float; the layout is kept in
`ei-rebuild-effects\layout.ini` in your user data folder. The Effects window lists:
- the 83 original effect types, marked `data`: each runs from its built-in file;
- your mod's `Res\effects` files, marked `mod`.

Pick one and it plays. Its textures and figures are your install's, as the game loads them.

- **Playing**:
  - Play, Pause, Step (one tick) and Restart; Loop starts an ended effect again.
  - The Tick slider scrubs back and forth: the effect is played again from its start to that tick, and every replay
    draws the same particles.
  - Size is the effect's size, as a script's `CreateParticleSource` gives it.
- **Place**: on the figure or free at a point. "At the camera's focus" puts it where you look.
- **Drive**:
  - Start emitting, Stop emitting and Stop, as the game calls them.
  - The source's alpha, param and extent.
  - A data effect's inputs: a slider for a float or vector, a box for a bool, a list for an enum, a button that fires an
    event.
  - Everything you change goes on the Timeline at its tick, and the replays make it again there.
- **File** (data effects):
  - The effect's top file: your mod's, else the built-in one.
  - The Fields window edits it value by value. The Text window edits it as text.
  - Every change plays at once, from the start up to the tick you were at. Problems are listed under it.
  - Fields drops the text's comments; Text keeps them.
  - A variant (`like`) plays as it resolves over its base; Fields and Text show its own file, the fields it names.
- **Save** writes `<folder>\Res\effects\<name>.effect.toml`.
  - The folder is your active mod's when it lies outside the game folder, else `ei-rebuild-effects` in your user data
    folder (`%LOCALAPPDATA%` on Windows). You can type another.
  - An edited built-in effect is saved whole with `replace = true` at the top, so your file is exactly what you saw.
  - The viewer writes nothing else, and nothing into the game folder unless you type that folder in.
- **New effect**: type a name under the list and press New effect. It starts from the example above, plays at once
  and is saved like any other.
- **Sub-effects**: effects that a data effect starts itself (its `effect` actions) play on the viewer's clock, so
  they pause, step and scrub with it.
- **Limits**: the viewer plays its effect alone, in a world of its own; the game's saves and network games never see
  it. The log is `ei-effects.log` beside the program.

## Spells

A spell of your own is a record and a Lua file: the record (`Res\database\spells.sdb.toml`, [Databases](#databases))
names the file with `script`, and the file below `Res\spells\` runs every cast of it, on the server. The game's own
spells are on the same path: the lightning arc and curse magic are `Res\spells\Lightning.lua`, the fire arrow, Rick
magic and the acid ray `Res\spells\Missile.lua`, the fireball `Res\spells\Fireball.lua`, the light strike and the
acid column `Res\spells\Strike.lua`, the twenty buffs (protections to shrink) `Res\spells\Buff.lua`, the fire wall,
the light wall and the poison cloud `Res\spells\LastingArea.lua`, healing `Healing.lua`, teleport `Teleport.lua`,
clairvoyance `Clairvoyance.lua`, fireworks `Fireworks.lua`, vision fog `VisionFog.lua` and the campfire (each flame
a zone places on the ground casts one) `Campfire.lua` (built into the game; the
source's `assets/spells/` has them and the fields the game's records get), which a mod's file of that name replaces.
The showcase mod's Chain Spark ([The showcase mod](#the-showcase-mod)) is this example: a spark that strikes its
target, then leaps to the nearest enemy it has not struck, each strike 0.6 of the one before, each leaving its victim
shocked (slowed and crackling with sparks: the spell's own active effect, [Active effects](#active-effects)).

```toml
[[spell]]
name = "Chain Spark"
code = "spark"
script = "chain_spark.lua"     # Res\spells\chain_spark.lua
school = "lightning"
target_kind = "unit"
mana = 8.0
range = 15.0
area = 78.5                    # the leap's reach: radius sqrt(78.5 / pi), about 5 (cast.radius)
damage = 12.0                  # the first strike (cast.damage)
target_count = 3               # strikes (cast.targetCount)
icon = 101                     # Res\textures\spell0101.png, prototype0101.png
light_color = [0.3, 0.6, 0.9]
light_range = 2.0
ai = "attack"                  # how the AI casts it (none: never, the default)
damage_type = "lightning"
offensive = true
look = "ChainSparkShock"       # its own effect's look on a struck unit (Res\effects; Active effects)
cast_look = "ChainSparkCast"   # the caster's look while it casts (without it: its school's, CastingElectricity)
```

- **What the game reads of the record** besides its numbers: `ai` (`none`, `attack`, `area_attack`, `lasting_area`,
  `curse`, `protect`, `enhance`, `heal`), `damage_type` (the AI's estimate), `offensive` (the HUD's use at once aims at
  the nearest enemy), `turns_victim` (the unit it is cast at turns on the caster) and `fighting` (casting it starts the
  battle music; both as `offensive` unless given), `ai_effect` (the code of the spell whose effect the AI looks for
  before casting a curse or a protection again; without it the spell's own `effect`), `cast_look` (the particle effect
  on the caster while it casts, by name; without it the look of its school, or of `cast_school` when given, from the
  database's `[cast_looks]`: [Records that name effects](#records-that-name-effects)), `item_heals` (on an item the spell heals: armour casts it when
  its wearer is not hit, where other armour spells go off when it is, and an item's periodic cast of it waits until
  the wearer is hurt, one such cast a tick; the game's healing has it).
- **What its effect on a unit does**: `effect` names it, for every active effect of the spell (`cast:Apply(unit,
  "own")`, and `cast:Apply(unit, "<this spell's code>")` from any spell): its stat effect, its look and sounds on the
  unit, and what the AI looks for. The kinds are the game's buffs by their codes; `own` (the default) is the spell's own
  (what your script's `OnStats` gives: [Active effects](#active-effects)), `none` nothing. Several effects of one
  kind: the strongest counts.

| `effect` | What it does while it lasts (`value`: the spell's damage) |
|---|---|
| `prot_fire`, `prot_electro`, `prot_acid` | fire / lightning / acid protection + the value (the larger of it and an `antimagic`'s); a sphere of fire / sparks / acid |
| `antimagic` | fire, lightning and acid protection + the value (where no protection spell's is larger); its own look |
| `eagle_sight`, `infravision`, `detect_life` | sight / sight by night / the near sense + the value; a star over the head |
| `invisibility` | harder to see: sight conspicuity - value % (the unit drawn faded) |
| `silence`, `lichdom` | harder to hear / to sense near: conspicuity - the value (lichdom: a ring as it starts and ends) |
| `stench` | easier to smell: smell conspicuity + the value; a gas cloud and its sounds |
| `stun` | stunned: no actions (the unit drawn tinted) |
| `teleport` | stunned like `stun`, with no look or sound (the teleport's arrival) |
| `strength`, `weak` | more / fewer hit points (more for a larger value), the figure larger / smaller; a ring as it starts and ends |
| `enlarge`, `shrink` | the figure larger / smaller |
| `regeneration` | the healing rate times the value |
| `feeblemind` | no casting; its sounds |
| `speed`, `slow` | the action rate + / - the value; a cloud at the feet |
- **One cast or one per target.** A scripted spell is one cast whatever its `target_count` and `f` runes: the script
  picks its targets. `per_target = true` has the game pick them as for its own attack spells (the unit aimed at, then
  those nearest the aim within range that the runes and the line of sight allow, up to the target count) and cast it
  once at each. `ends_with_target = true` ends a cast at once, with no hook, when its target leaves the world (as the
  game's spells do); without it the cast goes on with `cast.target` nil. `max_ticks = n` sets the ticks a cast lives
  at most (see "How long a cast lives"); `max_ticks = 0` lifts the limit, for a spell whose script always ends it (the
  game's walls, whose light lingers after their duration).

```lua
-- Res\spells\chain_spark.lua (the showcase mod's, without its game variables)
local FALLOFF = 0.6     -- each strike deals this share of the one before
local LEAP_TICKS = 10   -- the arc stands this long before the spark leaps along it

local spell = {}

local function Launch(cast, from, victim, damage)
    cast:Missile { from = from, to = victim, effect = "ChainSpark", payload = { damage = damage } }
end

function spell.OnCast(cast)
    if not cast.target then return cast:Finish() end
    cast.data.struck = { [GetObjectID(cast.target)] = true }
    cast.data.strikes = 0
    Launch(cast, nil, cast.target, cast.damage)        -- nil: from the caster
end

function spell.OnHit(cast, victim, payload, point)
    if not victim then return end                      -- the unit is gone: the chain ends here
    cast:Damage(victim, payload.damage, "lightning")
    cast:Apply(victim, "own", { ticks = 40, value = 1 })  -- the shock: OnStats below, the record's look
    cast:Effect("ChainSparkHit", { attach = victim, size = cast.power, lifetime = 30 })
    cast:Sound("magic\\lightning\\start.wav", victim)
    cast.data.strikes = cast.data.strikes + 1
    if cast.data.strikes >= cast.targetCount then return end
    local next = cast:FindUnits(victim, cast.radius, {
        enemies = true, alive = true, sight = true, affect = true, exclude = cast.data.struck,
    })[1]
    if not next then return end
    cast.data.struck[GetObjectID(next)] = true
    cast:Bolt(victim, next, { width = 2 * cast.power, lifetime = LEAP_TICKS + 4 })
    cast:After(LEAP_TICKS, "Leap", { from = victim, to = next, damage = payload.damage * FALLOFF })
end

function spell.Leap(cast, leap)                        -- the timer cast:After named
    if not leap.to then return end
    Launch(cast, leap.from, leap.to, leap.damage)
end

function spell.OnStats(effect, stats)                  -- the shock slows as the game's slow does
    stats:Subtract("Haste", effect.value)
end

return spell
```

- **The hooks.** The file returns a table of them, each optional; a function of another name is a timer:
  `cast:After(ticks, "Name", payload)` runs `spell.Name(cast, payload)` that many ticks later.

<!-- lua-api: hooks -->

| Hook | When it runs |
|---|---|
| `OnCast(cast)` | once, when the cast starts |
| `OnTick(cast)` | every tick after `OnCast` (a file with it keeps its casts going until `cast:Finish()` or the limit) |
| `OnHit(cast, target, payload, point)` | a missile of the cast landed: `target` nil when the unit is gone, `payload` what `cast:Missile` was given, `point` where it ended |
| `OnEnd(cast, reason)` | once, at the end of the cast: `reason` `"finished"`, `"idle"`, `"limit"` or `"error"` |
| `OnResume(cast, old)` | a save made before a game's spell moved onto this file held a cast of it in flight (`old.state` `"missile"`) or lingering (`"lasting"`), `old.ticks` left on the game's own count: the cast's objects are already its own (a missile still lands in `OnHit`, with a nil payload; `old.visual`, `old.sound` and `old.light` are their handles, for `cast:Release` later; `old.direction` the direction of its wall or bolt); set its timers and data here (the world methods are off in this hook, but `cast:Block`). Without it such a cast ends at once |
| `OnStats(effect, stats)` | the effect's kind on a unit works out its stats ([Active effects](#active-effects)); the world's functions are off in it |
| `OnEffectApplied(effect, unit)` | an effect of the spell's kind came on a unit ([Active effects](#active-effects)), run at the end of that tick |
| `OnEffectEnded(effect, unit, reason)` | an effect of the spell's kind left a unit: `reason` `"expired"` (its ticks ran out), `"removed"` (`cast:Remove`, `effect:Remove`), `"renewed"` (a new one of the kind and strength replaced it) or `"died"` (the unit died) |
| `OnEffectTick(effect, unit)` | every tick an effect of the spell's kind lasts on a unit, after its count down (`effect.ticks` left, above 0) |

<!-- /lua-api -->

- **No waits.** `Sleep` and the other waits are errors in a hook; pace the spell with `cast:After` and `OnTick`.
- **The cast value** holds the cast and the spell's built numbers (runes included). It is the same table at every
  hook of a cast, its fields brought up to date before each one: read them, but keep your own values in `cast.data`
  (what you set on the cast value itself is not saved).

<!-- lua-api: cast-fields -->

| Field | Type | What it holds |
|---|---|---|
| `cast.id` | `integer` | the cast's number |
| `cast.code` | `string` | the spell's code |
| `cast.caster` | `object` | the unit that cast it, or nil |
| `cast.target` | `object` | the unit it was cast at, or nil (also once it is gone) |
| `cast.from` | `point` | where the cast started |
| `cast.point` | `point` | the point it was cast at |
| `cast.tick` | `integer` | ticks since the cast |
| `cast.damage` | `number` | the spell's damage (runes included, as every number here) |
| `cast.range` | `number` | its range |
| `cast.radius` | `number` | its radius |
| `cast.duration` | `integer` | its duration in ticks |
| `cast.targetCount` | `integer` | its number of targets |
| `cast.speed` | `number` | its speed |
| `cast.mana` | `number` | its mana cost |
| `cast.power` | `number` | the game's power curve of its damage, about 0.8 to 1.8 |
| `cast.areaPower` | `number` | the same curve of its area over the record's (the fireball's blast is the larger of the two) |
| `cast.lightRise` | `number` | how fast the game's spell lights rise away: the record's `light_range / max(light_rise_divisor, 1)` a tick |
| `cast.lightRange` | `number` | the record's `light_range` |
| `cast.school` | `string` | its school's name |
| `cast.silent` | `boolean` | an item's own periodic cast: the game's buffs end without their sound and its healing makes no sound or light on a unit at full health |
| `cast.data` | `table` | a table of your own, kept with the cast and in saves: numbers, strings, booleans, units, tables |

<!-- /lua-api -->

- **The methods.** A `position` is a `{x, y, z}` table or a unit (where the unit stands); a `point` is a
  `{x, y, z}` table. Each does what the game's own spells do:

<!-- lua-api: cast-methods -->

| Method | Gives | What it does |
|---|---|---|
| `cast:FindUnits(at: position, radius: number, filter?: FindUnitsFilter)` | `object[]` | the units within `radius` of `at`, nearest first (`filter`: FindUnitsFilter below) |
| `cast:CanAffect(unit: object)` | `boolean` | the spell's hits would hit the unit (its rune filters) |
| `cast:Damage(unit: object, amount: number, type: string, options?: DamageOptions)` | `number` | a spell's hit of `amount` of a damage type (`fire`, `lightning`, `acid`, `piercing`, `slashing`, `crushing`, `general`) on a unit (other objects are not hit): armour and protections count, the caster is the attacker unless `noCredit`. Gives the hit points lost |
| `cast:Heal(unit: object, amount: number)` | `number` | heals as the healing spell does; gives the hit points gained |
| `cast:Apply(unit: object, kind: string, options?: ApplyOptions)` |  | an active effect on the unit: `kind` a spell's code (`slow`, `stun`, `weak`, `prot_fire`, ...: that spell's effect and look) or `"own"` (this spell's, with its icon, doing what its `effect` says: nothing unless given). The values default to the cast's (duration, power, damage, radius) |
| `cast:Remove(unit: object, kind: string)` | `integer` | the unit's effects of that kind removed; how many |
| `cast:Has(unit: object, kind: string)` | `boolean` | whether the unit has an effect of that kind |
| `cast:Move(unit: object, to: position, options?: MoveOptions)` | `boolean` | the unit moved to the point (at its own height) unless it cannot stand there; true when it moved. A push is a small move each tick |
| `cast:Bolt(from: position, to: position, options?: BoltOptions)` | `integer` | a lightning bolt; an end that is a unit follows it. Gives a handle |
| `cast:Missile(options: MissileOptions)` | `integer` | a spell missile flying at the unit `to` (at the game's missile speed) with the spell's light; `OnHit` runs when it lands. Gives a handle |
| `cast:Effect(name: string, options?: EffectOptions)` | `integer` | a particle effect: one of yours (`Res\effects`, [Effects](#effects)) or a game's by its script name (`Sparks`, `LightningBlast`, `FireBlast`, `FireBall`, `Nuke`, `Healing`, ...). A `free` one (a flash that plays once) is not the cast's: it lives its own life, also after the cast ends. Gives a handle |
| `cast:Sound(name: string, at: position, volume?: number, hearing?: number)`<br>`cast:Sound(name: string, at: position, options: SoundOptions)` |  | a sound played once (`magic\...\x.wav`), moving with `velocity` |
| `cast:SoundLoop(at: position, loop: string\|string[], options?: SoundLoopOptions)` | `integer` | sounds that go on while the cast holds them: `first` once, then `loop` (a name, or a list played in random order) over and over, as the fireball's flight and the walls sound. Gives a handle |
| `cast:Light(at: position, options?: LightOptions)` | `integer` | a point light, by default in the record's `light_color` and `light_range`, rising by `rise` a tick or moving by `velocity`. Gives a handle |
| `cast:SetVelocity(handle: integer, velocity: point)` | `boolean` | the cast's bolt, missile, effect or light of that handle moves by so much a tick from now on (a light sent up as it fades); false when it is gone |
| `cast:Eye(unit: object)` | `point` | the point a unit's spells start from (its eye), where the game's lightning puts its light |
| `cast:HitPoint(unit: object)` | `point` | the point where a hit on the unit shows, as the game's missiles splash: the ground below it plus its height as it stands (lower crawling, lying or dead, so ask after the damage); another object's position |
| `cast:IsUnit(object: object)` | `boolean` | whether it is a unit (false for nil, a unit that is gone, or another object) |
| `cast:Ground(at: position, options?: GroundOptions)` | `point` | the point on the ground at that place, `above` higher; `water`: on the water's surface where there is water, as the game's area spells aim |
| `cast:Flight(from: position, to: position, speed?: number)` | `Flight` | a flight along the ground at `speed` a second (default the spell's; 5 without one): the ticks it takes and the velocity a tick for `Effect`'s `velocity` |
| `cast:Release(handle?: integer)` | `boolean` | the cast's bolt, missile, effect, light or sound loop of that handle goes now (a missile's `OnHit` never runs); false when it is gone or the handle is nil |
| `cast:Noise(at: position, loudness: number, delay?: integer)` |  | the caster (a unit; nothing otherwise) makes a noise there that hostile units near it hear and come looking for the caster, as the fireball's blast does (`delay` the AI's call delay, 26 unless given) |
| `cast:Across(from: position, to: position, length: number)` | `point` | the vector `{x, y, 0}` `length` long square to the ground line from `from` to `to` (to its left; along x when the two stand on one spot), as the game's walls stand across the line from their caster to their point: the wall's half from its middle to an end |
| `cast:Position(object: object)` | `point` | the point where the object is (its own position, not the ground below it), where the teleport's departure shows |
| `cast:IsDead(unit: object)` | `boolean` | whether the unit is dead, as the game's spells test it; false for nil or another object |
| `cast:IsHurt(unit: object)` | `boolean` | whether the unit's hit points as the players see them are not its maximum (the healing's test); false for nil or another object |
| `cast:Shout(unit: object, ack: string)` |  | the unit says an ack to its player, by acks.db's names (`"cant_teleport"`, `"link_ended"`, ...), as the teleport's caster says it cannot go |
| `cast:Reveal(unit: object)` |  | the caster's player sees the unit this tick, as clairvoyance shows the units around its point (call it every tick) |
| `cast:Wake(at: position, radius: number)` |  | the units within radius of the position wake, as fireworks and clairvoyance wake those around them |
| `cast:Notice(unit: object, at: position, level: integer)` |  | the unit's AI notices the position at `level` when that is above what it noticed last: fireworks frighten with 300 falling to 50 over their duration |
| `cast:Vision(seer: object, observer: object, options?: VisionOptions)` | `integer` | the fog of war shows what the unit `seer` sees with the unit `observer`'s senses, as vision fog shows its caster what its target sees. Gives a handle |
| `cast:Block(at: position, options: BlockOptions)` |  | the ground under a wall from `at - along` to `at + along`, or a disc of `radius` around `at`, counts as blocked for the units' paths (they go round it, as round the game's walls and clouds) until `Unblock` or the cast's end; one at a time (a second `Block` replaces the first). `Block` also works in `OnResume` |
| `cast:Unblock()` | `boolean` | the ground `Block` blocked is free again; whether there was one |
| `cast:After(ticks: number, name: string, payload?: any)` |  | a timer: the spell file's function of that name runs that many ticks later: `spell.Name(cast, payload)` |
| `cast:Finish()` |  | the cast ends after this hook (its bolts, effects and lights go with it) |

<!-- /lua-api -->

- **The methods' tables**: the keys their options (and `cast:Flight`'s result) take, each optional:

<!-- lua-api: options -->

| Table | Key | Type | What it does |
|---|---|---|---|
| `FindUnitsFilter` | `enemies` | `boolean` | only the caster's enemies (with no caster every unit is an enemy) |
|  | `friends` | `boolean` | only the caster's friends |
|  | `alive` | `boolean` | only living units |
|  | `area` | `boolean` | only those area spells hit |
|  | `sight` | `boolean` | only those in the line of sight the spell needs from at |
|  | `affect` | `boolean` | only those the rune filters let the spell hit |
|  | `exclude` | `table<integer, boolean>` | a set of ids to leave out: `{[GetObjectID(unit)] = true}` |
|  | `along` | `point` | a direction: only those within 0.5 of the upright plane through at along it, as the game's walls hit |
|  | `unsorted` | `boolean` | in the map's own order (the order the game's area spells hit in), not nearest first |
| `DamageOptions` | `area` | `boolean` | a hit with no direction, as the walls' and clouds' |
|  | `noCredit` | `boolean` | the caster is not the attacker |
|  | `split` | `number` | shares the hit between n targets as the game's area spells do (n above 1: the amount and the hit's strength x 4 / n; they pass `cast.duration`) |
| `ApplyOptions` | `ticks` | `integer` | how long it lasts (the cast's duration) |
|  | `power` | `number` | its look's size (the cast's power) |
|  | `value` | `number` | its value, `effect.value` in `OnStats` (the cast's damage) |
|  | `strength` | `number` | its strength (the cast's radius) |
| `MoveOptions` | `check` | `boolean` | only to a place the unit can stand on (true) |
| `BoltOptions` | `width` | `number` | the bolt's width |
|  | `lifetime` | `integer` | ticks it lasts (else as long as the cast) |
|  | `demon` | `boolean` | the curse magic's look |
| `MissileOptions` | `to` | `object` | the unit it flies at |
|  | `from` | `position` | where it starts (the caster) |
|  | `effect` | `string` | its particle effect (FireArrow) |
|  | `size` | `number` | the effect's size (the cast's power) |
|  | `sound` | `string` | the sound it makes in flight |
|  | `payload` | `any` | given to OnHit |
| `EffectOptions` | `at` | `position` | where it shows (the cast's point) |
|  | `attach` | `object` | the unit it shows on |
|  | `size` | `number` | its size (1) |
|  | `alpha` | `number` | its opacity (1) |
|  | `param` | `number` | its source's parameter (the fire blast's spread: the fireball passes its radius) |
|  | `velocity` | `point` | it flies by so much a tick |
|  | `target` | `position` | a flying one heads for this point |
|  | `unit` | `object` | a flying one starts on the players' screens at this unit's hand (the fireball) |
|  | `direction` | `point` | a wall's (Tendril, Link) extent from its middle to an end (the fire wall's) |
|  | `lifetime` | `integer` | ticks it lasts (else as long as the cast) |
|  | `free` | `boolean` | not the cast's: it lives its own life |
| `SoundOptions` | `volume` | `number` | its volume |
|  | `hearing` | `number` | how far it is heard |
|  | `velocity` | `point` | it moves by so much a tick |
| `SoundLoopOptions` | `first` | `string` | the sound played once before the loop |
|  | `volume` | `number` | their volume |
|  | `hearing` | `number` | how far they are heard |
|  | `velocity` | `point` | they move by so much a tick |
|  | `lifetime` | `integer` | ticks they last (else as long as the cast) |
| `LightOptions` | `color` | `number[]` | {r, g, b} (the record's light_color) |
|  | `range` | `number` | its range (the record's light_range) |
|  | `rise` | `number` | it rises by so much a tick |
|  | `velocity` | `point` | it moves by so much a tick |
|  | `lifetime` | `integer` | ticks it lasts (else as long as the cast) |
| `GroundOptions` | `above` | `number` | so much higher |
|  | `water` | `boolean` | on the water's surface where there is water |
| `VisionOptions` | `lifetime` | `integer` | ticks it lasts (else as long as the cast) |
| `BlockOptions` | `along` | `point` | the wall's half: from `at - along` to `at + along` |
|  | `radius` | `number` | a disc of that radius instead |
| `Flight` | `ticks` | `integer` | the ticks the flight takes |
|  | `distance` | `number` | its length along the ground |
|  | `speed` | `number` | its speed a second |
|  | `velocity` | `point` | its velocity a tick, for Effect's velocity |

<!-- /lua-api -->

- **Handles.** `Bolt`, `Missile`, `Effect`, `Light`, `SoundLoop` and `Vision` give a handle (a number) for `SetVelocity` and
  `Release`; keep it in `cast.data` to use it in a later hook.
- **Exact numbers.** Lua numbers are doubles and the game's are floats: a cast's number passed on, or put through one
  operation, rounds back to what the game would compute; a chain of operations may not. A constant the game wrote as a
  float needs its exact value (`0.699999988079071` for 0.7, `0.4000000059604645` for 0.4; Missile.lua has both). A
  product the game compares is a float first: `string.unpack("f", string.pack("f", x))` rounds a number to one
  (Fireball.lua's `Float`).
- **How long a cast lives.** A bolt, effect or light lasts `lifetime` ticks, and while one does, the cast goes on; one
  given no `lifetime` lasts as long as the cast. The cast ends after a hook calls `cast:Finish()` (its bolts, effects
  and lights go with it), when nothing is left (no timer, no missile in flight, nothing with a lifetime, no `OnTick`:
  `"idle"`), or at its limit: for a file with `OnTick` the record's `duration` in ticks (600 without one), for one paced
  by its timers alone 600 ticks or the `duration` when longer; the record's `max_ticks` instead, when it has one (0:
  none).
- **Randomness**: `Random(n)`, the game's generator, as in mission scripts. The game's script functions
  (`GetObjectID`, `IsAlive`, `GetX`, `SetGameVar`, ...) work in the hooks too; the file's top level runs before any
  cast and cannot reach the world.
- **Errors** go to the console as `Script error: chain_spark.lua:<line>: <message>`; the cast then ends
  (`OnEnd(cast, "error")`), the game goes on. A file that does not load is said once per zone.
- **Saves and network games.** A save keeps every cast with its data, timers and missiles; the file is read again at
  the load, so a fixed file reaches saved casts. Only the server runs the spell; the players see its bolts, missiles,
  effects, sounds and lights. `Res\spells\` counts in the mod's content hash: every player needs the same files.
- `tools/make_lua_mod.py spellapi` writes a test mod whose spell uses every method (`tests/scenarios/mod_spell_api.txt`).

### Active effects

What an active effect does to a unit's stats is Lua too: its kind's script (the file of the spell its `effect` names,
or the spell's own for `own`) has `OnStats(effect, stats)`. The game's buffs are `Res\spells\Buff.lua`'s and the
teleport's stun `Teleport.lua`'s; a buff of yours gets its own by `effect = "own"` (the default) and the hook in its
file:

```lua
-- Res\spells\iron_skin.lua: a buff that hardens the skin against fire and slows the unit a little.
local spell = {}

function spell.OnCast(cast)
    if cast.target then cast:Apply(cast.target, "own") end
    cast:Finish()
end

function spell.OnStats(effect, stats)
    stats:Larger("FireProtection", effect.value)
    stats:Subtract("Haste", 0.5)
end

return spell
```

- **When it runs.** Whenever the game works out the unit's stats (after a hit, a heal, its regeneration, an item
  change, an effect that comes or goes) the effects on it are taken by kind: each kind once, with `effect.kind` its
  spell's code (lower case) and `effect.value` the strongest value of its effects on the unit (from 0; the value is
  the spell's damage unless `cast:Apply` gave one). The kinds run in the order of their spells in the database. The
  result is kept until the kinds or their values change, so the hook sees nothing but the effect: the world's methods
  and `Random` are errors in it, as at a file's top level. An error ends that kind's part (it adds nothing), says
  `Script error: ...` on the console, and the game goes on. A file holding several kinds (Buff.lua) looks at
  `effect.kind`.
- **The effect**: its kind and value.

<!-- lua-api: effect-fields -->

| Field | Type | What it holds |
|---|---|---|
| `effect.kind` | `string` | its spell's code (lower case) |
| `effect.value` | `number` | the strongest value of its kind's effects on the unit (from 0; the spell's damage unless `cast:Apply` gave one) |

<!-- /lua-api -->

- **The stats** are totals the effects add up to, each starting at 0, that the game then applies where it did for its
  own buffs. Each method is one float operation on the total, with `x` rounded to a float; as for spells, pass a value
  on or put it through one operation (`effect.value * 0.009999999776482582`, the float 0.01), never a chain, and the
  numbers are the game's.

<!-- lua-api: stats-methods -->

| Method | Gives | What it does |
|---|---|---|
| `stats:Add(name: stat, x: number)` |  | the total plus `x` |
| `stats:Subtract(name: stat, x: number)` |  | the total minus `x` |
| `stats:Larger(name: stat, x: number)` |  | the larger of the total and `x`: how a protection and antimagic's take the larger of the two |
| `stats:Multiply(name: stat, x: number)` |  | the total times `x` |

<!-- /lua-api -->

<!-- lua-api: stats -->

| Stat | What the game does with it |
|---|---|
| `FireProtection` | added to the unit's protection from fire (the protection spells and antimagic: `Larger`) |
| `LightningProtection` | added to the unit's protection from lightning |
| `AcidProtection` | added to the unit's protection from acid |
| `Sight` | added to the sight range (eagle sight) |
| `DaySight` | added to the sight's percentage by day (infravision) |
| `NearSense` | added to the range sensed whatever the light (detect life) |
| `SightConspicuity` | added to how visible the unit is, which stays at least 0 (invisibility subtracts its value in percent) |
| `HearingConspicuity` | added to how loud the unit is, at least 0 (silence subtracts its value) |
| `NearConspicuity` | added to how near the unit is sensed, at least 0 (lichdom subtracts its value) |
| `SmellConspicuity` | added to how the unit smells (stench) |
| `Haste` | added to the action rate, which stays at least 1 (speed adds its value, slow subtracts its) |
| `Might` | hit points times (1 + `Might` x 0.005) / (1 + `Frailty` x 0.005), at least 1, and the unit's damage the same with 0.003 (strength) |
| `Frailty` | the divisor of `Might`'s formula (weak) |
| `Regeneration` | while above 0, the hit points regenerate that many times as fast (regeneration) |
| `Girth` | the figure grows by 0.4 a step: with `Size` 0, `Girth` steps wider (for the hero only in width), else `Size` + `Girth` steps on every axis (strength and weak 1 and -1) |
| `Size` | steps of the figure's size on every axis (enlarge and shrink 1 and -1) |
| `Stunned` | while above 0 the unit is stunned: no actions (stun; the teleport's arrival) |
| `NoCasting` | while above 0 the unit casts no spells, unless its natural weapon is spells only (feeblemind) |
| `Strength` | added to the strength (after the skill levels'): with it the hit points, the carried weight and the skills it leads (no game buff touches this one or those below) |
| `Dexterity` | added to the dexterity: with it the mana, the action rate and the skills it leads |
| `Intelligence` | added to the intelligence: with it the experience gained and the magic skills |
| `MaxHitPoints` | added to the hit point maximum, before `Might`'s scale |
| `MaxMana` | added to the mana maximum of a hero or a named unit (another unit's is its prototype's, unchanged) |
| `HitPointRegen` | added to the share of the maximum the hit points regenerate every 15 ticks while hurt (0.005: half a percent), before `Regeneration`'s factor |
| `ManaRegen` | added to the share of the maximum the mana regenerates every 15 ticks out of a fight |
| `PiercingProtection` | added to the unit's protection from piercing |
| `SlashingProtection` | added to the unit's protection from slashing |
| `CrushingProtection` | added to the unit's protection from crushing |
| `GeneralProtection` | added to the unit's protection from general damage |
| `Hearing` | added to the hearing range |
| `Smell` | added to the sense of smell |
| `Attack` | added to the attack of the unit's blows and shots (whole numbers count) |
| `Defence` | added to the defence against blows (whole numbers count; none against shots) |
| `Damage` | added to the damage of a weapon's or a natural attack's hit, before `Might`'s scale |
| `Speed` | added to the movement speed, which stays at least 0 |
| `MeleeSkill` | added to the melee skill (the attack and defence with a blade, an axe, a club, ...) |
| `ArcherySkill` | added to the archery skill (bows and crossbows) |
| `ElementalSkill` | added to the elemental magic skill (fire, lightning, acid: what spells it may cast) |
| `SenseSkill` | added to the sense magic skill (illusion, divination) |
| `AstralSkill` | added to the astral magic skill (enchantment, healing, domination) |
| `Science` | added to the science (the Use and Steal skills) |
| `Taming` | added to the taming |

<!-- /lua-api -->

**The order they apply in.** The totals change a unit's stats where the game works them out, every total added even
at 0 (the game's own buffs' numbers stay the same). `UnitStats::Recompute`, after a hit, a heal, a regeneration, an
item change or an effect that comes or goes, rebuilds them from the unit's bases in this order:
1. the bases, then the skill levels' bonuses (strength, dexterity, intelligence, the day sight's percentage);
2. `Strength`, `Dexterity`, `Intelligence`: everything below that is worked out from them has them;
3. the seven protections (`PiercingProtection` ... `GeneralProtection`, the protections from fire, lightning and acid);
4. the perception (`Sight`, `DaySight`, `NearSense`, `Hearing`, `Smell`), then the conspicuity (`SightConspicuity`,
   `HearingConspicuity`, `NearConspicuity` at least 0, `SmellConspicuity`);
5. the hit point maximum: the experience curve and the strength (a hero's or a named unit's; another unit's fixed
   maximum) plus `MaxHitPoints`, times `Might`'s scale over `Frailty`'s, at least 1; the mana maximum (a hero's or a
   named unit's) the curve and the dexterity plus `MaxMana`;
6. the carried weight's limit (from the strength), the movement speed plus `Speed` (at least 0), the action rate (from
   the dexterity) plus `Haste` (at least 1);
7. the hit points within the new maximum.

The others are read where the game uses them, from the totals as they are then: `HitPointRegen` and `ManaRegen` (and
`Regeneration`'s factor) at each regeneration; `Attack`, `Defence` and `Damage` (then `Might`'s scale of the damage) at
each attack; `MeleeSkill` and `ArcherySkill` where the weapon skill counts (the attack and the defence with a weapon),
`ElementalSkill`, `SenseSkill` and `AstralSkill` where a spell's skill is checked (whether the unit may cast it),
`Science` and `Taming` where those skills are rolled; `Girth`, `Size` (the figure), `Stunned` and `NoCasting` as an
effect comes or goes. The interface's skill numbers and `GetSkill` show the learnt skills, without the effects'.

**How it shows.** The players' screens show an effect by fields of its kind's record (the spell its `effect` names,
or the spell itself for `own`), since they have the effects' kinds but not their stats. The game's twenty buffs have
theirs in the built-in `spells.sdb.toml` (`Res\database\spells.sdb.toml` merges over them: a mod can give
protection from fire another look); a buff of yours gives its own:

```toml
# Res\database\spells.sdb.toml: the iron skin's look and sounds.
[[spell]]
code = "iron_skin"
script = "iron_skin.lua"
look = "IronSkin"                              # Res\effects\IronSkin.effect.toml on the unit while it lasts
sound_start = "magic\\Fireshield\\start.wav"
sound_end = "magic\\Fireshield\\end.wav"
```

| Field | What it shows while an effect of the kind lasts |
|---|---|
| `look` | a particle effect on the unit (yours from `Res\effects`, [Effects](#effects), or a game's by its name: `SphereFire`, `VisionStar1`, `FeetCloud1`, ...); one for all the kind's effects on the unit, at the size of the strongest (its cast's power, about 0.8 to 1.8, in steps of 1/16: the effect's `size`) |
| `look_ring` | a particle effect started once as the kind comes on the unit and once as it goes, the second at the negated size (the game's rings: `ClayRing` for lichdom, `Modifier1` for strength and weak) |
| `look_ring_scale` | the ring's size times this (1; weak's -1: its ring closes as it comes and opens as it goes) |
| `figure` | `plain` (the default), `faded` (the figure pulses half-seen and casts no shadow: invisibility) or `tinted` (it pulses red: stun) |
| `sound_start`, `sound_end` | played at the unit as the kind comes and as it goes |
| `sound_loop` | one to five sounds that go on while the kind lasts: after the start sound, one looped, several one after another in random order, following the unit (feeblemind's `circle.wav`) |
| `sound_loop_ticks` | instead, one of `sound_loop` at random every that many ticks + 1 (stench's three every 17) |
| `quiet_steps` | the unit's steps make no sound (silence) |
| `greys_spells` | the unit's own interface offers none of its spells (feeblemind); with `NoCasting` in `OnStats` the server refuses them too |

A unit that comes into view (or a load) with the effect already on shows the look and the loop at once, without the
start sound or the ring; a unit that dies with it makes no end sound or ring.

**One-shot hooks.** What an effect does as it comes, as it goes and while it lasts is the kind's module's too:
`OnEffectApplied(effect, unit)`, `OnEffectEnded(effect, unit, reason)` and `OnEffectTick(effect, unit)` ([Spells](#spells),
the hooks' table). They run on the server at the end of the tick in which it happened, in order, with the world's
functions (and `Random`), each for one effect on one unit; a kind whose module has none of them costs nothing more.
`OnEffectTick` runs every tick the effect lasts after its count down (`effect.ticks` left, above 0; also while the unit
sleeps out of the player's sight), `OnEffectEnded` once as it goes. A unit that left the world (removed) gets no more
of its hooks; a dead one gets `"died"`. Keep your numbers in `OnStats`: a stat that `OnEffectApplied` added and
`OnEffectEnded` took away would come apart when two effects of a kind overlap (the strongest counts), when a renewed
one replaces another, or in a save; the hooks are for what happens once or now and then: a burn every few ticks, a
heal as it runs out. Nothing of a hook is saved: count ticks with `effect.ticks`.

```lua
-- Res\spells\ember_brand.lua: a curse that burns its victim every 10 ticks and saps its strength while it lasts.
local spell = {}

function spell.OnCast(cast)
    if cast.target then cast:Apply(cast.target, "own", { ticks = 60, value = 5 }) end
    cast:Finish()
end

function spell.OnStats(effect, stats)
    stats:Subtract("Strength", effect.value)
end

function spell.OnEffectTick(effect, unit)
    if effect.ticks % 10 == 0 then effect:Damage(1, "fire") end
end

function spell.OnEffectEnded(effect, unit, reason)
    if reason == "expired" then effect:Heal(2) end
end

return spell
```

- **The effect** of these hooks is one effect on the unit (not a kind's strongest, as in `OnStats`):

<!-- lua-api: active-effect-fields -->

| Field | Type | What it holds |
|---|---|---|
| `effect.kind` | `string` | its spell's code (lower case) |
| `effect.value` | `number` | its value (the spell's damage unless `cast:Apply` gave one) |
| `effect.ticks` | `integer` | the ticks it has left |
| `effect.power` | `number` | its look's size (its cast's power) |

<!-- /lua-api -->

- **Its methods** act on the hook's unit and effect, only while that hook runs:

<!-- lua-api: active-effect-methods -->

| Method | Gives | What it does |
|---|---|---|
| `effect:Damage(amount: number, type: string, options?: DamageOptions)` | `number` | a hit of `amount` of a damage type on the unit, as `cast:Damage`'s without a caster (no one has the credit; `noCredit` is not read). Gives the hit points lost |
| `effect:Heal(amount: number)` | `number` | heals the unit as `cast:Heal` does; gives the hit points gained |
| `effect:Remove()` | `boolean` | the effect taken off the unit now (its `OnEffectEnded` follows, `"removed"`); whether it was still on |

<!-- /lua-api -->

## What stays in the original formats

ei-convert converts every record, text, texture, model, animation, map and map object of the game. What stays as it
is (and can still be replaced by a loose file of the same name in the archive's folder):
- sounds and speech: `.wav` (sfx.res) and `.mp3` (speech.res), music `.mp3` (`Stream\`), already common formats;
- movies: Bink `.bik` (`Movies\`);
- the quest zones (`Maps\*.mq`): their `quest.reg`, `map.txt` and briefing texts;
- a `.mob`'s AI graph (its script can be a `.lua`: [Scripts](#scripts));
- a few small files no tool edits: `aiinfo.res`'s `normal.dat`, `gfx.res`'s `lightning.dat`, the credits
  (`outro.res`), the start-up picture archive (`autorun.res`, `.bmp`).

## Limits and troubleshooting

- A mod adds and changes; it does not remove. A record merge never deletes a record; to empty a list, give it as `[]`;
  to take the game's file away entirely, give a whole file with `replace = true`.
- Only one mod is played at a time. A mod's file stands over the game's file of the same path, not over another mod's.
- A network game: give every player the same mod (the same databases and maps).
- `.mob.toml` is the whole zone: a game update to the zone's objects is not seen through a mod's copy.
- Map changes need the whole map in the mod ([Maps as folders](#maps-as-folders)).
- glTF: no textures in the file, one mesh per part, node rotation and scale left out, no vertex animation from Blender.
- Text is shown in code page 1251.
- A zone's `.lua` stands for its whole script; it cannot add to the `.mob`'s script (copy ei-convert's file and add to
  it).
- Rebirth's mod plugins (`.plg` DLLs) are never loaded. Lost in Astral's shop fix is built in; IronMod's plugin is not
  supported.
- Something does not show up? Read `ei-rebuild.log`: every TOML, PNG, glTF or map file that was not read, and every key
  that was not understood, is a `warning:` line with the file and the line. Check the file's place: the archive's folder
  (`Res\textures\` for textures.res), the name of the original plus `.toml`, `.png`, `.glb`.

## The example mod

`examples/example_mod/` holds the mod's own files, hand-written, and `examples/example_mod_additions/` what it adds
to zone 1's objects and script; `ei-convert --example-mod` copies the first and makes the rest from your install
(neither the repository nor a release holds game data):

| File | What it shows |
|---|---|
| `config.reg.toml` | the title and the single-player label in the menu's list |
| `Res\texts\texts.toml` | an interface string changed ("string quit"); a new item's name and description |
| `Res\database\items.idb.toml` | one field of the game's stone axe changed; a new weapon "example axe" |
| `Res\textures\Zone1000.png` .. `Zone1007.png` (made) | zone 1's ground pages as ei-convert writes them, recoloured (green becomes red); the PNGs keep their settings chunk |
| `Maps\zone1.mob.toml` (made) | zone 1's objects as ei-convert writes them, with `examples/example_mod_additions/Maps/zone1.mob.toml` (a pig) appended |
| `Maps\zone1.lua` (made) | zone 1's script as ei-convert writes it, with `examples/example_mod_additions/Maps/zone1.lua` put in before its WorldScript (a console line, and a definition whose rule has the pig greet the party) and a call of it as the WorldScript's first line |
| `readme.txt` | the mod's own notes; a root file, read by nothing |

```
ei-convert --example-mod <game folder> [<mods folder>]
```
writes `<mods folder>\example_mod`, by default `<game folder>\starter\Mods\example_mod`. It reads the install only
(a converted copy's own PNGs, `zone1.mob.toml` and `zone1.lua` are taken as they are) and the mod's own files from the
`examples` folder beside the exe, so keep the package's folder together; files already in `example_mod` are written
over. Then pick "Example mod" in the main menu's list ([Installing and choosing a mod](#installing-and-choosing-a-mod)).

The test scenario `tests/scenarios/mod_example.txt` makes it the same way (its prepare step,
`tools/make_example_mod.py`, runs the build's `ei-convert --example-mod`) and plays it on the GOG and the Rebirth data: the changed text, the two
weapons' damage after `GiveItem`, the pig by the party's start and its greeting, the red ground in a capture, and the
game reading each of the mod's files.

## The showcase mod

`examples/showcase_mod/` is a whole small mod made only of new files (no game data, so release packages carry it):
an exit east of a new game's landing on the first island leads to an island of its own, where a stone golem and its
two shards guard the path; the quest gives you a spell for the fight, Chain Spark; break the golem, and its hammer and
boots are yours. Read its files as a recipe for each kind of new content:

| Part | Files |
|---|---|
| A zone | `Res\texts\map.toml` (the zone "showcase", its two exits, and the first island's exit 3 to it; [Zones](#zones)), `Maps\showcase\map.toml`, `heights.png`, `tiles.png` (a map folder with no `.mpr`), `Res\textures\showcase000.png` (its ground tiles), `showcasemap.png` / `showcasequest.png` (the minimap and objectives pictures), `Maps\showcase.mob.toml` (its objects, no `.mob`), `Maps\showcase.lua` (its script), `"zone showcase"` in `Res\texts\texts.toml` (its name) |
| A creature | `Res\figures\scgolem.glb` (parts and animations), `Res\database\scgolem.adb.toml` (its animation list), `units.udb.toml` (a `[[model]]` and a `[[unit]]`), `Res\textures\scgolem00.png` (the skin its placement names); the shards: a second `[[unit]]` on the golem's model at a smaller scale, `Res\textures\scshard00.png` |
| A weapon, boots | `items.idb.toml`, `Res\figures\initwehm9.glb`, `initarbt20.glb` (inventory models: a loose figure named `initwe<type code><number>` / `initar...`, one part `weapon` / `armor`), `Res\figures\hm_09.glb` (the hammer's worn model, in the hand: [Worn models](#worn-models)), `Res\redress\unhumahm_09.gr.0.png`, `unhumabt_20.le.0.png` (their textures: the hammer's in the weapons' strip, the boots' a layer over the hero's skin) |
| A quest | `Maps\showcase.lua` (the quest's variable `q.<zone>.<quest>`, its reward), `quests.qdb.toml` (its experience), `Res\texts\texts.toml` (`quest <name>`: the title, the description, `#subobj` lines) |
| A spell | `spells.sdb.toml` (Chain Spark's record, `script` naming its file), `Res\spells\chain_spark.lua` (its mechanic: a missile with `OnHit`, the next victim by `FindUnits`, `Damage`, `Apply`, `Effect`, `Bolt`, a timer for the leap; [Spells](#spells)), `Res\textures\spell0101.png`, `prototype0101.png` (its pictures, `icon = 101`), `Maps\showcase.lua` (`GiveItem` of its spell container); `tests/scenarios/mod_showcase_spell.txt` casts it |
| A buff | `spells.sdb.toml` (Ember Brand's record, `code = "ember"`), `Res\spells\ember_brand.lua` (a curse: `OnStats` lowers the victim's `Strength`, `OnEffectTick` burns it every 20 ticks with `effect:Damage`, `OnEffectEnded` says how it went; [Active effects](#active-effects)); `tests/scenarios/mod_showcase_buff.txt` casts it with `CastSpellUnit` |
| An effect | `Res\effects\showcase_aura.effect.toml` (orbs trailing light and rising motes, driven by its inputs; `tests/scenarios/mod_effect.txt` starts it from the console); the spell's `ChainSpark.effect.toml` (the missile's spark and its trail) , `ChainSparkHit.effect.toml` (a strike's flash and ring of sparks), `ChainSparkShock.effect.toml` (the look of the shock a strike leaves: the record's `look`, [Active effects](#active-effects)) and `ChainSparkCast.effect.toml` (the caster's look while it casts: the record's `cast_look`, a variant of the game's CastingElectricity; [Records that name effects](#records-that-name-effects)), all on the mod's own texture `Res\textures\chainspark.png` (16 pictures, 4 x 4) |

The pictures and models are made by `tools/make_showcase_mod.py` in this
repository.
