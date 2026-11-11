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
repository's checks: the loader is checked on ei-convert's files, on files with the extras removed and scaled
(`mod_gltf_model`), and on hand-written glTF.

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
- The order of the records is the order the zone creates them: keep the game's order and add new records at the end.
- An unknown kind or key, or a value of the wrong type, is logged (`warning: map objects: <file> line n: ...`) and left
  out.

Moving or adding objects keeps the zone's AI graph from the `.mob` (a building moved onto a path does not change how
the AI plans its way across the map).

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
  `SetGameVar` / `GetGameVar`, `SetCP` is `SetPosition`, `For` is `ForEach`; the list is in
  `docs/notes/lua-scripting.md`, "The naming rule"). They take and give what the game's do: numbers (single precision,
  as in the game; a condition holds unless it is 0), strings, objects (`nil` for none) and groups.
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
- **Expressions**: arithmetic, comparisons, `? :`, `sin`, `cos`, `min`, `max`, `clamp`, `lerp`, random `range`,
  `chance`, `pick`; tables (`fade[p.age]`) and curves (`glow(p.t)`) from `[tables]` and `[curves]`.
- **Layers**: a mod's file of the name merges over a lower one field by field, a particle kind or stage by its `name`;
  `replace = true` replaces it whole.
- **Placing a particle yourself**: `position = [x, y, z]` starts it where the expressions say (`origin.x`, the
  source's motion, its `side` and `up` axes), and `position = { tick = [...] }` moves it each tick by your own sum
  instead of its velocity.
- **The game's own effects** are written the same way as they move to data (`assets/effects` in the source:
  FireStar, AcidStar, CurseStars, PortalStar, Sparks, FeebleMind, ZoneExit and the trails FireBall, FireArrow,
  RikArrowSmoke, AcidRay so far); a mod's `Res\effects\FireStar.effect.toml` retunes the fire arrow's flash with only
  the fields it names, and `Res\effects\ZoneExit.effect.toml` the glow over every zone exit.
- **Problems** are `warning: effects: <file>:<line>: ...` lines in `ei-rebuild.log`; an effect with an error shows
  nothing. An unknown key is only noted.
- In a network game every player needs the same effect files: they count in the mod's content hash.

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
a new game starts on an island of its own in place of the first one, where a stone golem guards the path; break it
for the quest, and its hammer and boots are yours. Read its files as a recipe for each kind of new content:

| Part | Files |
|---|---|
| A zone | `Maps\zone1\map.toml`, `heights.png`, `tiles.png` (a map folder with no `.mpr`), `Res\textures\zone1000.png` (its ground tiles), `zone1map.png` / `zone1quest.png` (the minimap and objectives pictures), `Maps\zone1.mob.toml` (its objects), `Maps\zone1.lua` (its script) |
| A creature | `Res\figures\scgolem.glb` (parts and animations), `Res\database\scgolem.adb.toml` (its animation list), `units.udb.toml` (a `[[model]]` and a `[[unit]]`), `Res\textures\scgolem00.png` (the skin its placement names) |
| A weapon, boots | `items.idb.toml`, `Res\figures\initwehm9.glb`, `initarbt20.glb` (inventory models: a loose figure named `initwe<type code><number>` / `initar...`, one part `weapon` / `armor`), `Res\redress\unhumahm_09.gr.0.png`, `unhumabt_20.le.0.png` (their textures; the boots' is a layer over the hero's skin) |
| A quest | `Maps\zone1.lua` (the quest's variable `q.<zone>.<quest>`, its reward), `quests.qdb.toml` (its experience), `Res\texts\texts.toml` (`quest <name>`: the title, the description, `#subobj` lines) |
| A spell | `spells.sdb.toml`: a record and its text only |
| An effect | `Res\effects\showcase_aura.effect.toml` (orbs trailing light and rising motes, driven by its inputs; `tests/scenarios/mod_effect.txt` starts it from the console) |

What it cannot do yet: add a zone beside the game's (it replaces zone 1: the zone list is one text read whole), show
a new weapon in the hero's hand or a new helmet or body armour on him (those are parts of the hero's figure), give a
new spell an effect (the game's 42 spells have their code; a spell after them does nothing yet), and its island keeps zone 1's
path-finding graph. The pictures and models are made by `tools/make_showcase_mod.py` in this repository.
