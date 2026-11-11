# Mods

Rebirth's mods (its install's `starter\Mods\<mod>`: Lost in Astral, IronMod, AllHeroes, Standard) played from our exe:
the settings choose a mod folder, whose files then stand over the game folder's. Tickets MOD1 (the overlay), MOD2
(Lost in Astral played through), MOD3 (the menu's choice, without a restart).

## Files

| File | What |
|---|---|
| `src/platform/path_layers.h`, `.cpp` | the layered path map (`PathLayers`, `PathLayer`), `FullGamePath`, `PathBelow`; the mod (`platform::SwitchMod`, `ModFolder`, `CurrentMod`) |
| `src/platform/seams.cpp` | the game's `SystemPath` / `SystemSearches`: the mod's map, the path itself without a mod |
| `src/platform/files.cpp` | `FolderEntries`: each layer's search (the backends' `SearchFolder`) merged |
| `src/main.cpp` | reads `[game] Mod` after EI_Plugin's values, calls `app::UseMod` (app/main_loop.cpp: the folder in full, `platform::SwitchMod`, the log) |
| `src/game/app/main_loop.cpp` | `StartGame` / `StopGame` (the game's half of the start and the stop), `app::RequestModSwitch` and the switch (`TakeModSwitch`) |
| `src/engine/mods/mod_list.h`, `.cpp` | the main menu's list of mods: `Scan` (starter\Mods\*\config.reg), `IndexOf`, `SettingOf` |
| `src/game/ui/ui_main_menu.cpp` | `MainMenu::AddModList` (the list's drop-down control) and the pick's `RequestModSwitch` (in `Update`) |
| `src/engine/mods/mod_folders.cpp` | Rebirth's mod-folder patches (rebirth.md, "Resources and paths"), whenever a mod is played: `ModPath()` is the Mod folder, `Active()` whether there is one; `Reset` at a switch |
| `src/engine/mods/mod_plugins.h`, `.cpp` | the mods' plugins carried as code ("The mods' plugins"): `modplugins::FourShopClasses` |
| `src/engine/mods/mod_hash.h`, `.cpp`, `mod_identity.h`, `.cpp` | the mod's content hash and the mod a game plays as our peers tell it at login, the refusal texts ("Mods in multiplayer"; network.md, "Mods") |
| `src/engine/particles/particle_data.h`, `.cpp`, `particle_record.*`, `particle_expr.*` | the data effects, `Res\effects\<name>.effect.toml` merged through the layers (effects.md, "Data effects") |
| `src/game/script/script_spell_host.h`, `.cpp` | the scripted spells, `Res\spells\<file>.lua` through the layers (items_spells.md, "Scripted spells") |
| `src/game/script/script_lua_api.inc`, `.h`; `tools/convert/lua_api.h`, `.cpp` | the Lua API declared once, registering the bindings; `ei-convert --lua-api` (a LuaLS file, shipped as `examples/lua_api/ei_api.lua`) and docs/modding.md's API tables (lua-scripting.md, "The API's declarations") |
| `tests/unit/mod_hash.cpp`, `tools/make_mod_pair.py`, `tests/scenarios/net_pair_mod_*.txt` | their checks: the hash of a folder and of its zip, the pairs with the same mod, another version and none |
| `tests/verify/harness.cpp` | `Layers` / `MapPath` / `MapSearches`: the run's overlay as the map's top layer |
| `tests/unit/path_layers.cpp` | the unit check (ei_unit_check, the POSIX port check) |
| `tests/scenarios/mod_lost_in_astral.txt` | a new game in Lost in Astral (Rebirth's data) |
| `tests/scenarios/mod_switch.txt`, `mod_switch_plain.txt` | a switch to Lost in Astral and back, then a new game; the same new game from the start |
| `tests/scenarios/mod_menu_list.txt` | the menu's list picked through the UI, and the settings file written |
| `src/engine/data/resource.h`, `.cpp` | the archives and the loose files over them ("Loose files"): `MappedResource::Sources`, `LooseFolderOf`, `ResourceExists` |
| `tools/unpack_install.py` | an install's archives unpacked into their folders; run_all.py `--unpacked` |
| `tests/scenarios/mod_loose_files.txt` | loose files of a mod over its archives and the game's |
| `src/platform/zip_folders.h`, `.cpp` | a .zip read as a folder ("Zip mods"): `InZip`, `ZipFile`, `SearchZip`, on miniz (third_party/miniz) |
| `tests/scenarios/mod_zip_loose_files.txt`, `mod_zip_menu.txt` | zip mods: mod_loose_files' mod zipped, the menu's list finding one |
| `src/lib/toml.h`, `.cpp`; `third_party/tomlc17/` | TOML read (tomlc17) and written ("Records as TOML") |
| `src/engine/data/resource.h`, `.cpp` | `RecordFile`, `FindRecordFile`, `MappedResource::FindRecord` / `FindText`, `MergeTomls` |
| `tools/convert/convert.cpp` | ei-convert: an install's records as TOML, each round trip checked |
| `tools/convert_install.py` | an unpacked copy converted; run_all.py `--converted` |
| `tests/scenarios/mod_toml_records.txt` | a mod's TOML text and record seen by the game |
| `src/engine/data/database_toml.h`, `.cpp` | the game databases as TOML ("Databases as TOML"): field lists, `WriteRecords`, `MergeRecords` |
| `tools/convert/convert_databases.h`, `.cpp` | ei-convert's databases: each round trip, compared by the records' own serialisers |
| `tests/scenarios/mod_toml_databases.txt` | a mod's items.idb.toml: a weapon changed and one added, given in a new game |
| `src/engine/models/texture_png.h`, `.cpp`; `third_party/stb/` | textures as PNG and back ("Textures as PNG"): `ToPng`, `FromPng`, `ReadLevel`; stb_dxt |
| `src/lib/png.h`, `.cpp`; `third_party/wuffs/` | PNG files written (in segments) and read (Wuffs; a segmented one on threads): `png::Write`, `png::ReadRgba` ("Loading cost") |
| `src/engine/data/resource.cpp` | `MappedResource::PngTexture`: an .mmp's PNG made into it where it is found |
| `tools/convert/convert_textures.h`, `.cpp` | ei-convert's textures: each as PNG, its round trip measured |
| `tests/unit/texture_png.cpp` | the unit check (ei_unit_check, the POSIX port check) |
| `tests/scenarios/mod_png_texture.txt` | a mod's PNG terrain pages seen on the ground |
| `src/engine/data/map_folder.h`, `.cpp` | maps as folders ("Maps as folders"): `Decode`, `EncodeMap` / `EncodeSector`, `WriteFolder`, `Assemble` (PNGs written by `png::Write`, read by `png::ReadRgba` / `ReadRgba16`) |
| `src/engine/data/resource.cpp` | `MappedResource::FolderMap` / `FolderMapFile`: an archive's map file and sectors made from its folder |
| `tools/convert/convert_maps.h`, `.cpp` | ei-convert's maps: each as its folder, the round trip byte for byte |
| `tests/unit/map_folder.cpp` | the unit check (ei_unit_check, the POSIX port check) |
| `tests/scenarios/mod_map_heights.txt` | a mod's heights.png seen as the ground's height (`ground`) |
| `tests/scenarios/mod_map_objects.txt` | a mod's zone1.mob.toml (a pig moved, one added) with no .mob, seen in a new game |
| `src/game/map/mob_toml.h`, `.cpp` | a .mob's objects as TOML and back ("Map objects as TOML"): `Write`, `Read`, `Find`, `FieldImages` |
| `src/game/map/mob_reader.cpp`, `map_spec.h` | `MobContents` (what a .mob holds), `MobFile::ReadChunks`, `LoadMob` (applied to the server) |
| `tools/convert/convert_mobs.h`, `.cpp` | ei-convert's map objects: each .mob's TOML, its round trip compared field by field |
| `src/engine/models/figure_gltf.h`, `.cpp`; `third_party/cgltf/` | figure models as glTF and back ("Models as glTF"): `FromFiles`, `Files`, `ToGlb` (our writer), `FromGltf` (cgltf, reading only) |
| `src/lib/json.h`, `.cpp` | a small JSON reader (the glTF's extras) and the writer's strings and floats |
| `src/engine/data/resource.cpp` | `MappedResource::ModelFile`, `HasModelFile`, `MadeModel`: a figure file answered from a glTF where it is found |
| `tools/convert/convert_models.h`, `.cpp` | ei-convert's models: an archive's figure files grouped into figures, each as a .glb, its round trip compared |
| `tests/unit/figure_gltf.cpp` | the unit check (ei_unit_check, the POSIX port check) |
| `docs/modding.md` | the modder's guide (MF10): keep it in step with the loaders when a format changes |
| `examples/example_mod/`, `examples/example_mod_additions/`, `tools/convert/example_mod.cpp` | the guide's example mod: hand-written TOML and Lua, and zone 1's ground PNGs (recoloured), zone1.mob.toml (a pig appended) and zone1.lua (the mod's lines spliced in) made from the install by `ei-convert --example-mod`, which reads the examples folder beside the exe (the build copies it there; the release packages ship it); `tools/make_example_mod.py` is mod_example's prepare step calling it |
| `tests/scenarios/mod_example.txt` | the example mod played: its text, weapons, pig, red ground (`hues world 0.2 1 red`) and reads |
| `examples/showcase_mod/`, `tools/make_showcase_mod.py` | the showcase mod, all our own files ("The showcase mod"); the tool writes its pictures and models and copies it for a run |
| `tests/scenarios/mod_showcase.txt` | the showcase mod played: the island, the golem's fight, the quest and its reward |
| `figure_gltf.h` (`IsWornModel`, `JoinWorn`), `resource.cpp` (`WornSetFile`, `JoinedSet`) | an item's worn model joined to the figure sets that lack its parts ("Worn models") |
| `tests/scenarios/mod_showcase_worn.txt` | the showcase hammer in the hero's hand (`unitpart`) |
| `src/game/map/map_database.cpp`, `tests/scenarios/mod_showcase_zone.txt` | the zone table, map.txt and a mod's `Res\texts\map.toml` merged by name ("Zones from a mod"); the showcase's zone walked into and out of |
| `tests/scenarios/mod_showcase_long_walk.txt` | a long walk over the island on its built region graph, through a save (docs/notes/ai.md, "Built graphs") |
| `src/engine/models/figure_gltf_animation.cpp`, `figure_gltf_glb.h` | a set's animations as glTF animations and back ("Animations as glTF"); the pieces the two files share (the .glb's writer, the reader's accessors) |
| `tests/scenarios/mod_gltf_animation.txt` | a mod's glTF hero whose idle is twice as slow, played in a new game |
| `src/game/script/script_lua_mission.cpp` | a .mob's script from a .lua ("Scripts in Lua"): `Script::LoadLua`; `Script::Runner`, `GlobalValues`, `GlobalGroup` and the scheduler's views for the digest and the saves |
| `src/game/map/mob_toml.h`, `.cpp` | `FindScriptLua` (a .mob's .lua through the layers), `ScriptFile` (a TOML's `script` key), `ScriptLuaName` |
| `tools/make_lua_mod.py`, `tests/scenarios/mod_lua_plain.txt`, `mod_lua_none.txt`, `mod_lua_changed.txt` | a mod's zone1.lua: the plain conversion and its twin without the file (the .mob converted at its load), and one with the mod's own lines through a save |

## Choosing a mod

`ei_settings.ini`, `[game] Mod = starter\Mods\LostInAstral`: the mod's folder (or its zip, `starter\Mods\LostInAstral.zip`:
"Zip mods") relative to the exe's folder (the game folder for a dropped-in exe; on macOS the app's MacOS folder, so
give it in full there), or in full. Read at the start,
after EI_Plugin's values (the settings' first use writes the defaults); a switch of mods writes it. A folder that is not there
is logged ("mod: no folder ...") and the game runs without a mod. The log names the mod and its saves' folder.

## The layered path map

Every file and folder call of the platform takes its path from `SystemPath(path, use)` (platform.md, "Files and the
harness's overlay"); with a mod, or in a harness run, that is a `PathLayers` map:
- `bases`: the game folder in full (the harness adds the settings' data folder when it differs). A path is made full
  lexically (`FullGamePath`: the current folder for a relative one, `.` and `..` folded, `/` as `\`), and only paths
  below a base are mapped; every other path is the system's as it is. The current folder is the path it was set
  to, symbolic links kept, on every system (POSIX's `CurrentFolder` keeps what `SetCurrentFolder` was given, as
  Windows does): the system's resolved path (macOS's `/private/var` for `/var`, any game folder under a link) would
  lie outside the base, and the mod would be skipped (the v1.0.0 macOS check, 2026-11-05).
- `layers`, top first, each a folder (or a `place` function) standing for the game folder's files, with `owns`: the
  files it answers for even when it lacks them (read as missing, nothing below asked).
- Read: the first layer whose file exists (or owns it); else the path itself.
- Write / Update: with `writesToTop` (the harness) the top layer's path, its folders made, an Update first copying the
  file from the first layer below that has it (or the game's), as `copiesUp` allows; without (the game) the top layer
  that has the file, else the path itself: a mod's file is changed in the mod's folder, the game's own in the game
  folder.
- The game folder's `saves\` is `saves` (the folder made at the first write), and its `mp\` (the multiplayer heroes:
  the hero screen's list, hero create / rename / delete, the hosted game's party, `temp.mp`; every one reaches it as
  `g_dataFolder + "mp\"`, so the one rule here covers them all) is `saves\mp` beside it: a hero that got a mod's item
  must not break the hero list of the game without it (the user's bug, 2026-11-10; Rebirth's addon.dll redirects `mp\`
  per mod the same way, "Set Addon path for: mp\*"). With no mod there is no map: `mp\` is the game's. A path already
  in the saves' folder or the written top layer is not mapped again.
- Folder searches (`SystemSearches`): each layer's folder of the pattern that exists, top first, then the game's; a
  name an upper layer found hides the same name below it (Windows' case rules), and when more than one layer found
  names they are sorted by name (NTFS's order). An owning layer's search is the only one (the harness's mp\). An entry's
  path is in the folder of the game's pattern, so the game's next call on it is mapped again.

### The mod's layer

The mod folder over the game folder, every file of its folders (`Maps\`, `Config\`, `Res\music.reg`, `Camera\`,
`Movies\`, `Stream\`), but:
- not its own root's files (`config.reg`, `readme.txt`, `Rebirth.txt`, the `.plg` plugins): the game folder's root
  holds the exe, its settings and logs, which no mod replaces;
- not a `.res` archive: the mod's `config.reg` `[RES]` names its archives, and Rebirth's ResOverride combines them with
  the game's inside the archive (a file the mod's archive lacks is the game's; Lost in Astral's textures.res holds 43 MB
  of the game's 687), so the game's archive must stay the game's (rebirth.md, "Resources and paths").

A mod's folder of an archive's name (`<mod>\Res\textures\`) is in the layer like any folder; the archive reads it
before the mod's own archives ("Loose files").

The mod's folder is the one named, or its one entry when that is a folder and only it holds the config.reg
(`platform::ModRoot`: a mod zipped with its folder, or such a zip unpacked into a folder of its name). The saves of a
mod are `<game folder>\saves\<mod's name>\`, the name the mod folder's last part (a zip's without ".zip"; the game's
`saves\save*` search does not list it unless the name starts with "save"), never mixed with the game's. `[MOD] GameSaves` is not read: every mod keeps its
own.

### The harness

The harness's map (harness.cpp, `Layers`): the run's overlay (`harness_saves`) on top (`mp\` is the map's `saves\mp`:
`harness_saves\mp\` with no mod, `harness_saves\<mod>\mp\` with one, never the install's; it owns the "hidefile"
files; writes go there, copied up unless a log or over 16 MB), the pinned key bindings and address book
(harness_settings.h) below it, the mod's layer under those; the saves `harness_saves\` (with a mod
`harness_saves\<mod>\`). Around the map MapPath keeps its own rules: `config\movie.ini` and recorded play's movies
read as missing, `config\tutorial.ini` in `harness_saves\`, a write left in an install refused, the traces. The
scenario line `mod <folder>` answers `Mod` in the run's settings file with the folder in full (relative to the game
folder); the snapshot (config/harness/registry.reg) marks `Mod` absent otherwise. After a `waitmenu` or
`waitwindow` line, `mod <folder>` (or `mod none`) is a step instead: a switch of mods in the main menu
(`app::RequestModSwitch`, refused elsewhere: the run fails), the next step once the switch is made; the `read mod` /
`wrote mod` checks then look in that folder. The overlay's map is made once for each mod the run plays (`Layers`).

## Loose files

No archive needs packing (MF1, user 2026-11-09). Every archive the game opens (a `.res`, a map's `.mpr`, a quest
zone's `.mq`; the mod's `[RES]` archives and extra figure and texture archives too) has a folder of its name without the
extension standing over it: `Res\textures\` over `Res\textures.res`, `Maps\zone1\` over `Maps\zone1.mpr`, `Maps\z3q1\`
over `Maps\z3q1.mq`. A file there is read in place of the archive's entry of that name.

`textures.res : foo.mmp` is looked for in this order (`MappedResource::Sources`, src/engine/data/resource.cpp):
1. the folder in each layer of the path map over the game folder, top first: the mod's `<mod>\Res\textures\` (and in a
   harness run the overlay's `harness_saves\Res\textures\` above it);
2. the mod's `[RES]` archives for textures.res (Rebirth's ResOverride), each its own folder (`<mod>\Res\textures\` for
   `res\textures.res`: met in 1, left out) and then its entries;
3. the game folder's `Res\textures\`;
4. the archive's entries.
A mod's `-` for the archive drops 3 and 4. A folder met before in the order is left out; a folder with no files is not
asked. With a mod a `t\` or `s\` name not found is looked for once more without the prefix (Rebirth's).

- Names: an entry's name is the file's path below the folder, a sub-folder's file as `<sub>\<name>`. The archives' own
  names have sub-folders (sfx.res `weapons\Slash\Stone\1.wav`, speech.res `t\Briefing\z41_1\4.mp3`, a .mq's
  `z3q1\quest.reg`), so a sub-folder is part of the name, not flattened away. Matched without case on every system
  (ASCII letters, as the archives' own lookup); a file name in Unicode is the asset code page's (1251) for the match.
  Two files of one name (a system that tells case apart): the first by name is read, the other logged ("warning: loose
  files: ...").
- Each folder is read once, when its archive opens (`FindLooseFiles`; the platform's `FilesByLayer`: the folder in each
  layer of the path map, then its own); files added later are seen at the next open.
- A loose file is loaded whole and shared like an entry (`CloseFile` frees it at its last close); a memory-mapped
  archive (figures, the terrain's .mpr) keeps it until it closes.
- An archive whose file is missing but whose folder is there opens as an archive of no entries
  (`OpenResource`); the map sites ask `ResourceExists` (the file, or for an archive its folder): `MapFileExists`, NewZone's
  `MapFileMissing`, `LoadZoneQuests`. Rebirth's Maps choose the mod's folder for an archive's name when its `Maps\` has
  the folder (`SelectMaps`).
- `ListFiles`: in the same order, a folder's files by name and an archive's entries in the archive's order but those
  its own folder (step 3 or the mod archive's) has, so an archive with no folder lists as before and an unpacked one lists
  the same names (sorted). A mod's folder adds its names as a mod archive does (Rebirth appends every archive's
  listing: a name in both is listed twice either way).
- The zone table lists the quest zones (`IsQuestFile`, mods/maps_folder.h): each `.mq`, and each `Maps\<zone>\` folder holding
  `<zone>\quest.reg` (a map's folder `Maps\zone1\` is not one). A mod's `Maps\` decides the table when it has one.
- The server shares a quest file with a network client by its bytes (`ShareQuestFile`); from a folder it shares none (a
  client never writes them, Rebirth's MQClient).
- The harness's `hidefile maps\z7q1.mq` hides the folder `maps\z7q1\` with it.

### Unpacked installs

`python tools/unpack_install.py <install> <target>` copies an install with each archive replaced by its folder
(`--folders-only`: the folders alone, a modder's start); it only reads the install. `run_all.py --unpacked` (also through
`work.py test`) plays the suites on the unpacked copies of both installs in `<main checkout>\build\unpacked\vanilla` and
`rebirth` (made at the first use, about 30 s: 81 archives, 19,297 files; 149 archives with the Rebirth mods', 33,313
files), with every golden the same. A prepare tool that copies an install's archive packs its folder when the install is
unpacked (make_mod_s19d.py `copy_archive`); one that changes an archive's file writes it loose into the overlay
(make_extra_item_db.py: `harness_saves\Res\databaselmp\items.idb`), which stands over the archive and over its unpacked
folder alike. `mod_menu_list`'s expected settings file names the registry snapshot's `AddonPath` as it is, not as `{install}`: the
snapshot pins it, so the converted and unpacked runs keep the Rebirth install's path there (only `Mod`, which the game
writes, follows the run's install folder).

### Checks

- `mod_loose_files` (both installs; tools/make_mod_s19d.py loose): the mod's folder over its archive, a sub-folder's
  file in any case, the mod archive's own folder over its entry, the mod's folder over the game's archive (outro.res),
  the listing's count, a maps folder for an .mpr but not a .mob.
- `work.py test <ID> --unpacked --golden` and with `--rebirth-only`: every scenario on the unpacked installs.

## Zip mods

A mod can be one .zip, read in place as if it were its folder (MF2, user 2026-11-09): `[game] Mod =
starter\Mods\LostInAstral.zip`, or picked from the menu's list, which lists each `starter\Mods\*.zip` with a config.reg
at its root or in its one top folder. Rebirth's install has such a zip beside the folder (LostInAstral.zip, 726 MB, the
mod's folder below one top folder LostInAstral\: 280 entries, 272 deflated, 8 stored).

- One path for every system file call (src/platform/zip_folders.h): a path through a .zip file
  (`...\LostInAstral.zip\LostInAstral\Maps\zone1.mob`) names the zip's file or folder of that path, matched without
  case (lib/text_case.h's table, as the platform's other names). The backends ask before the system: File (read),
  MappedFile, ReadWholeFile, the folder search (`SearchZip`: the zip's names by name, NTFS's order, with "." and ".."
  on Windows as its searches have them); files.cpp's PathExists, IsFile and CopyFileOver; path_layers.cpp's layer
  lookups (`Exists`, `IsFolderPath`) and the harness's copy-up. So the mod's layer is the zip's folder exactly where a
  mod folder stands (`Mod::folder`, `ModFolder()`), and everything above it is unchanged: the loose archive folders and
  FilesByLayer, Maps\, Config\, the mod's `[RES]` archives (`ModPath()` + `res\x.res`, opened from the zip's bytes as
  any file), Rebirth's Maps, Camera, Movies and Stream patches.
- Read only, with a written layer above it (MF2a, user 2026-11-10): the zip's `PathLayer` is `readOnly`, and over it
  stands a layer of the folder `<game folder>\saves\<mod's name>\files\` (inside the mod's saves, so a path there is
  never mapped again). A write the game makes of a file the zip holds (Lost in Astral's `config\tutorial.ini`, the
  help topics seen, written at every interface shutdown; an edited `addrbook.ini`) goes to that layer, its folders
  made, an Update (a file opened to change it) first copying the zip's bytes up (`PathLayers::Map`); a read finds the
  written copy first, after a restart too. A file the zip lacks is written where a folder mod's would be: in the game
  folder. A folder mod writes into its own folder as before. In a harness run every write goes to the run's overlay
  (the map's `writesToTop`), so that layer is not in the harness's map.
- A zip file is read (its central directory, no file) at the first path through it and kept for the rest of the
  program, open; one changed on disk while the game runs is read again only after a restart. Each file's bytes are
  unpacked whole when opened and shared by every open of it at that time (a weak pointer per entry), freed at the last
  close: a memory-mapped archive inside a zip (figures, an .mpr) is a heap copy while it is open. One unpacking at a time
  per zip (miniz's reader and the file stream behind it); the lookups take no lock once the zip is read.
- Entry names are taken as UTF-8 (the zip's flag or plain ASCII, as every shipped mod's); a name met twice in any case
  keeps its first entry; a folder needs no entry of its own. An entry over 4 GB, or one miniz cannot unpack
  (encrypted, an unknown method), reads as missing.
- Only the path's first part ending in ".zip" that is a file opens a zip, and only a path with such a part is looked
  at (no system call for the others). A zip inside a zip is a file of the outer one, not a folder.
- The menu's list (mod_list.cpp): the entries' folders are `ModRoot`s (in a zip for one), compared with
  `ModFolder()`; a zip's setting is the zip's path (relative to the exe's folder when below it). Its title is
  `[MOD] Title` as for a folder (else the zip's name without ".zip"). With Rebirth's install the list has 6 entries
  (LostInAstral and LostInAstral.zip both "Lost in Astral (single player)", the folder first by name).
- miniz (third_party/miniz, 3.1.2, unmodified) is its own C library target, built without stdio or the clock and
  with its warnings off (CMakeLists.txt); MF8's PNG writer can take its deflate.

### Checks

- `tests/unit/path_layers.cpp` (ei_unit_check, the POSIX port check): a zip of a mod folder (its one top folder) and a
  zip of files at its root, made with miniz's writer: the mod's folder and name, reads through the map (whole, File
  with seeks and ReadAt, MappedFile), a write and an update going to saves\<mod>\files\ and read back after a switch, folder searches merged with the game's, FilesByLayer, ModRoot.
- `mod_zip_loose_files` (make_mod_s19d.py loose --zip s19d_mod.zip --top): mod_loose_files' mod zipped below its
  folder: the same answers, the log naming the zip's s19d_mod\ and saves\s19d_mod.
- `mod_zip_menu` (make_mod_s19d.py loose --zip starter\Mods\0_s19d.zip --top: the zip in the run's overlay of the game
  folder): the list shows it (first by name) on either install, a pick switches to it, the new menu shows it chosen and
  its loose file is read.
- Lost in Astral from Rebirth's LostInAstral.zip (by hand, mod_lost_in_astral with `mod starter\Mods\LostInAstral.zip`;
  not a committed scenario: the Linux stub data leaves zips out, tools/stub_data.py): every check and the golden the
  same as from the folder.

## Records as TOML

The game's record files can be TOML (MF3, user 2026-11-09). Every original still loads; a file of its name plus
`.toml` found where the original would be (the same places, "Loose files": the archive's folder in each layer, the mod's
archives, the game's folder, the archive) is read instead, or merged over it. TOML is UTF-8; the in-memory records keep
the asset code page (1251) as before. The reader is tomlc17 (third_party/tomlc17, unmodified, its README names the
commit), the writer our own (src/lib/toml.h: floats as the shortest text that reads back to the same bits).

| Original | Its TOML | Layout | Over the original |
|---|---|---|---|
| a registry `.reg`: `Config\ai.reg`, `Res\music.reg`, `Res\smessbase.reg`, `Res\streamsn.reg`, aiinfo.res's `tileDesc.reg`, menus.res's `menus.reg`, a mod's `config.reg` | `Config\ai.reg.toml`, `Res\aiinfo\tileDesc.reg.toml`, `<mod>\config.reg.toml`, ... | a table per section, a key per value: integer, float, string, or an array of one (an empty array answers any array type) | merged by section and key (no case) |
| materials.res's `.mat` | `Res\materials\<name>.mat.toml` | name, diffuse / ambient / specular / emissive `[r, g, b, a]`, power | the keys given over the colours |
| database.res's `.adb` | `Res\database\<name>.adb.toml` | name, start, length, end, `[[animation]]` (name, index, the key's and flags' parts ("Readable values"), chance, frames, stride, arrow, steps, moment, sound_frame, sounds) | the header's keys given; an animation's fields given over the first of its name that the file has not merged yet, or it is appended ("Merging by field") |
| `acks.db` (database.res) | `Res\database\acks.db.toml` | `[[unit]]`, `[[model]]`, `[[extra]]`: name and the speech lists by ack kind (AckKind in snake case: `selected`, `move`, ..., `bored` with `environment`, `scenario` the named ones), each speech `{ sound, chance, text, name }` | per field ("Merging by field"): a speech list given replaces that list whole; a record of a new name is appended |
| `prints.db` (database.res, databaselmp.res) | `Res\database\prints.db.toml` | `[[blood]]`, `[[footprint]]`, `[[scorch]]`: name, `dry` / `wet` = `{ alpha, life, lifetime }` (a footprint's `unread`: chunk 10) | per field: `dry` and `wet` merge by their own keys (`wet = { alpha = 0.25 }`); a new name is appended (a table's index is the ground's surface class) |
| texts.res's, textslmp.res's entries | `Res\texts\texts.toml`, `Res\textslmp\texts.toml`: one per archive | `"<entry name>" = "<text>"` (quoted keys, multi-line text as `"""`, CR kept as `\r`) | per entry: a folder's own file of the name, then its texts.toml, in the archive's order of places; so a mod's texts.toml holds only the strings it changes |
| `Camera\<name>.cam` | `Camera\<name>.cam.toml` | `[[key]]`: time, position `[x, y, z]`, rotation `[w, x, y, z]` | the top TOML is the whole path |

Merging (`FindRecordFile` / `MappedResource::FindRecord`, resource.h; `MergeTomls`): the TOML files of every place above
the first place that has the original, and that place's own, are read; the bottom one is merged first over the original,
so the top one (the mod's) wins. A TOML file with no original anywhere builds the records alone. One that does not parse
is logged ("warning: records: ...") and left out; an unknown key is logged with the file. A merge never removes a
record: a mod changes fields of one, or adds one ("Merging by field"). A TOML file with the top-level key `replace = true` is the whole file: the
search ends there, nothing below it (its place's original, lower TOML files, the game's file) is read, as a mod's
original hides the game's file. ei-convert writes it in every record file it converts, so a converted copy plays as
the original: Lost in Astral's own `.adb`, `ai.reg` and the like hide the game's rather than merging over them (without
it mod_lost_in_astral_start's units differed). A mod's file of only the records it changes leaves it out.

The game databases (units.udb, items.idb, ...) can be TOML too: "Databases as TOML"; the maps can be folders ("Maps as
folders"), the maps' objects TOML ("Map objects as TOML"). What stays binary: the quest zones' `quest.reg` (a `.mq`'s,
read as bytes for the briefings), a `.mob`'s scripts and AI graph.

### Merging by field

Every record file of these tables (acks.db, prints.db, the .adb animations, and the game databases of "Databases as
TOML") merges one way (user 2026-11-10; the shared code is `dbtoml::MergeRecords`, src/engine/data/database_toml.h): a TOML
record is matched to the first record of its key (the name) that this file has not merged yet and gives the fields it
has; the others keep what the original, or a TOML file below, had. A key no record has is a new record at the end of its
table, so every record keeps its index. Each record type lists its fields once (`Fields(record, visit)`, in acks_db.cpp,
ground_print.cpp and map_object_manager.cpp), which the writer and the merge both walk. So a mod gives only what it
changes: one ack kind's speech list, one print's `wet = { alpha = 0.25 }`, one animation's `chance = 37`. A list is
the unit of a field: a speech list (`attack = [...]`) given replaces the record's whole list; the arrays of an animation
(`arrow`, `steps`, `sounds`) are read whole, the rest of a short one zero. A record without its `name` is logged and
left out; a new animation's `index` is its place unless the record gives one. Names that repeat (the empty ones in some
.adb) are matched in order, as in "Databases as TOML". Registries (per section and key), `.mat` (per key) and texts (per
entry) merged by key already; a camera's key list is the whole path.

### ei-convert

`build\verify\ei-convert.exe <install or archive> <target>` writes an install's record files as TOML into `<target>`,
laid out as the loose folders (`<target>\Res\database\acks.db.toml`, ...), with `converted.txt` listing the originals it
converted. It reads with the game's own loaders and checks each file's round trip (original read, written, read back,
every field compared, floats by their bits). `ei-convert --check <install or archive>...` only checks. It reads the
source alone; never give it an install as the target.

Counts (2026-11-10), every round trip holding: vanilla 1 acks.db, 54 adb, 18 cameras, 8 materials, 2 prints.db, 8
registries, 3,564 texts; Rebirth (its mods too) 4 acks.db, 163 adb, 22 cameras, 8 materials, 4 prints.db, 17
registries, 7,327 texts. Not exported: the bytes no loader reads (an .adb's unused field 11, a camera key's `reserved`).

### Converted installs

`python tools/convert_install.py <unpacked copy> <target>` hard-links an unpacked copy (tools/unpack_install.py) into
`<target>`, runs ei-convert in place and removes every converted original. `run_all.py --converted` (also through
`work.py test`) plays the suites on `<checkout>\build\converted\vanilla` and `rebirth`, made from the unpacked copies at the
first use or when another ei-convert made them (converted-from.txt), with every golden the same. The TOML files of each
layer are read unmapped (`platform::ReadLayerFile`, `PathUse::Layer`: the path map would answer the top layer's file for
every layer) and the harness traces them; its `read` checks count a read of `<file>.toml` as one of the file, and
`modfile` reads as the texts do (`FindText`: a texts.toml's text too). A prepare tool that copies a camera path copies
its TOML when the install has that alone (make_test_mod.py).

### Checks

- `ei_unit_check`'s toml checks: the writer's strings, keys, numbers and 200,000 floats read back through tomlc17.
- `mod_toml_records` (both installs; tools/make_mod_s19d.py toml): a mod with only `config.reg.toml`, a `texts.toml`
  that changes "string quit", an `acks.db.toml` that gives Haburu's scenario list and the mercenary warrior's attack
  list alone, a `prints.db.toml` that gives the stone blood print's wet alpha alone and a `unanhoco.adb.toml` that
  gives the chance of the animation "cidle01" alone; the interface's text and the records have the mod's fields and
  the game's others (`modtext`, `modack`, `modrecord`: the mercenary's move list is the game's six speeches, the print's
  dry look and its wet life and lifetime, the animation's frames, key, flags and moment).
- `ei-convert --check` on both installs (all round trips hold), and on the stub data in the Linux suites' workflow.
- `work.py test <ID> --converted --golden --rebirth`: every golden the same.

## Databases as TOML

The six game databases of database.res and databaselmp.res (MF5, user 2026-11-09) are record files as above: the same
places, the same `<name>.toml`, the same merge order and `replace = true`, read by each database's `Read(RecordFile)`
(its Load finds the file with `MappedResource::FindRecord`). Each record kind is an array of tables; each field has a
name; numbers, strings (UTF-8; the asset code page in memory), arrays (damage per type, a model's speeds) and name
lists are TOML's own, a model's body slots and a prototype's gold rolls inline tables.

| Original | Tables, each merged by |
|---|---|
| `units.udb` | `[[level]]`, `[[model]]`, `[[unit]]` (the prototypes), `[[npc]]`: name |
| `perks.pdb` | `[[skill]]`, `[[perk]]` (the skill level records): name |
| `spells.sdb` | `[[spell]]`, `[[modifier]]`: code; `[[scroll_recipe]]`, `[[armor_recipe]]`, `[[weapon_recipe]]`: spell |
| `items.idb` | `[[material]]`: key; `[[weapon]]`, `[[armor]]`, `[[quick_item]]`, `[[quest_item]]`, `[[loot]]`: name |
| `quests.qdb` | `[[quest]]`, `[[briefing]]`: name |
| `Levers.ldb` | `[[lever]]`: name |

- A record's fields are listed once (`Fields(record, visit)` beside its Serialize, in each database's file), and the
  writer (`dbtoml::WriteRecords`) and the merge (`dbtoml::MergeRecords`) both walk that list, so a key is spelt the
  same way in both. A prototype's model index and a model slot's level index are not fields: the load resolves them
  by name after the merge, as before.
- Merging: a TOML record is matched to the first record of its key (compared exactly, as the tables' indexes compare)
  that this file has not merged yet, and takes the fields it gives; the others keep what the original (or a TOML file
  below) had. So a mod's file holds only the records it changes or adds, and of each only the fields it changes
  (`[[weapon]] name = "stone axe"`, `damage = 9.5`). A key no record has is a new record (the type's defaults and the
  fields given) at the end of its table. A merge never removes a record or moves one: every record keeps its index,
  which is what the indexed references hold (an item's packed id: prototype and material index; a loot record's
  index; the skill levels and model levels by index; saves and network messages carry those numbers), and new
  records come after the game's. A record without its key is logged and left out; an unknown key, or a value of a
  type its field does not take, is logged ("warning: items.idb: <file> line n: ...") and the field kept.
- Names that repeat in the original: the n-th record of a key in a TOML file merges into the n-th record of that key
  in the table (the "not merged yet" rule), so the converted files read back record for record. The data has them:
  quests.qdb's 17 quests "dummy" (the index by name files the last, as the original's load does), and the recipe lists,
  where a spell has a recipe per class (vanilla's 60 scroll recipes name 30 spells). A mod that changes one of a
  repeated key's records lists the ones before it too, in order.
- Fields no code uses (the original's unread bytes: `unread...` keys) are written only when not zero, so a converted
  file reads back exactly. A lever's `spots` lists only its spot_count spots (Lever::UseSpots reads no more; the count
  is the list's length read back): the bytes of the unused ones (the original data holds garbage there, NaNs among it)
  are not kept, and ei-convert's comparison clears them on both sides.
- Codes, masks and fixed-meaning arrays are written by name, and a field equal to a new record's is left out
  ("Readable values"); a perk's required stats are floats (`required_strength`, held as their bits), its worth
  exponent signed.

Counts (2026-11-10, ei-convert, every round trip holding): vanilla 2 each of units.udb, perks.pdb, spells.sdb, items.idb,
Levers.ldb and 1 quests.qdb (database.res: 389 units, 51 models, 14 NPCs, 5 levels; 78 perks, 7 skills; 42 spells, 38
modifiers, 102 recipes; 192 armours, 37 weapons, 33 materials, 22 quick items, 71 quest items, 95 loot; 109 quests,
141 briefings; 37 levers); Rebirth with its mods 6 each and 4 quests.qdb (Lost in Astral's database.res 716 units, 215
armours, 63 weapons, 126 quests).

### Checks

- `ei-convert --check` on both installs (all round trips hold), and on the stub data in the Linux suites' workflow:
  each record compared by the bytes its own serialiser writes (every field the format holds, floats by their bits),
  the indexes by name and the resolved indexes too.
- `mod_toml_databases` (both installs; tools/make_mod_s19d.py databases): a mod's items.idb.toml changes the weapon
  "stone axe"'s damage and spread and adds the weapon "mf5 axe"; in a new game the party is given both (GiveItem) and
  their damage is the mod's (`modweapon`, WeaponMinDamage / WeaponMaxDamage of the given items' packed ids).
- `net_pair_database` on a converted install: tools/make_extra_item_db.py adds its quest item as an overlay
  `items.idb.toml` merged over the game's TOML (the same index as its binary file's).
- `work.py test <ID> --converted --golden --rebirth`: every golden the same with every database as TOML.

## Readable values

The record TOML says what a value means rather than its bits (MF14, user 2026-11-11: the showcase mod's `class_mask =
0x1`, `key = 0x2890000`, `blood_kind = 8`, unnamed arrays and a 32 x 32 relations matrix could not be written by
hand). One path: ei-convert writes this form, the game reads only this form. The field helpers are database_toml.h's
(`dbtoml::Enum`, `Bits`, `Code`, `Table`, `Bool`, `Masked`, `FloatBitsValues`), which mob_toml.cpp uses too:
- An enum is its name (`natural_weapon = "club"`); a value no name has stays a number (the data's garbage, a code no
  code tests), which the reader also takes.
- A bit mask is a list of names (`sold_by = ["gipat_smith"]`; a name may stand for several bits: `weapons = ["any"]`
  is 0x7F). A bit the code reads as a number and no name covers is that number (`weapons = ["axe", 7]`: the weapon
  bit of type 7; `sold_by = [6]`: a mod's trader class 6). No field holds raw bits: every bit of a key, a mask or a
  flag word is a name or a number of the code, and the bits nothing reads are cleared where the original is read
  (an .adb's flags) or kept as an `unread...` field (MF14b, user 2026-11-11).
- A fixed-meaning array is an inline table by name (`perception = { sight = 8.5, near = 2.8 }`); elements left out
  keep the record's value (0 in a new record).
- Defaults are left out: `dbtoml::WriteRecords` writes a field only when its text differs from a new record's (the
  key always), the merge fills the rest from the record below or the type's defaults; mob.toml did this already.
- An unknown name is a load error of its record, as a record without its key: logged with the file, the line and the
  valid names ("warning: items.idb: <file> line 7: type has a value it does not take (one of: sword, axe, ...); the
  record is left out") and the record left out whole (a game record keeps what it had, a new one is not added). An
  unknown key or a value of the wrong type is the same (`dbtoml::MergeRecords`; mob_toml.cpp's `LeftOut` for a map
  object or weather record). The log is where a modder sees it (docs/modding.md, "Limits and troubleshooting").

Field by field (the names, the code that reads each):

| File | Field | Was | Now |
|---|---|---|---|
| items.idb | weapon `type`, `natural_weapon` (udb) | 0..6, 8, 16 | WeaponType: `sword`, `axe`, `dagger`, `spear`, `club`, `bow`, `crossbow`, `none` (8), `spells_only` (16) |
| items.idb | armor `type` | 0..6 | ArmorType: `helmet`, `body_armor`, `leggings`, `shirt`, `pants`, `boots`, `gloves` |
| items.idb | quick item / quest item / loot `type` | garbage (stack addresses) | `unread_type`: no code reads it (the readers of ItemRecordHead::type, found by compiling with the field renamed, are all a weapon's or armour's) |
| items.idb, spells.sdb | `class_mask` | hex | `sold_by`: the trader class templates (bit class - 1, ClassTemplate::type; the classes by the scripts that open them: `gipat_smith` 1 (basecam "b.smith.constr_1"), `gipat_witch` 2 (bz2g), `ingos_trader` 3 (bz8k Shopper), `suslanger_smith` 4 (bz14h Kuzn), `rick` 5 (bz7g)); another class by its number (6..32: a mod's script's class; no file has one) |
| items.idb | `damage_types`, `absorb_first`, `absorb_others`, material `absorb_factors`; udb `damage_weights`, `protection` | 7-element arrays | tables by damage type (texts "string dmg_0".."dmg_6"): `piercing` (dagger, spear, bows), `slashing` (sword, axe), `crushing` (club), `fire`, `acid`, `lightning`, `general` |
| items.idb | `buildable`, armor `shows_wounds` | 0 / 1 | true / false |
| items.idb | quick item `category` | 0, 5, 8 | QuickItemCategory: `danger_sensor`, `consumable`, `wand`, `potion` |
| items.idb | loot `kind`, `record_type` | numbers | LootKind `self`, `material`, `record`; LootRecordType `weapon`, `armor`, `quick_item`, `spell`, `modifier`; 0x7FFFFFFF `none` |
| units.udb | model `object_class` | 50..52 | `class`: MapObjectType `hero`, `creature`, `animal` (52: Bat, Boar, Cow, ... the code's UnitSpare) |
| units.udb | `movement_kind` | 1..3 | `movement`: `walks` (1: the load factor from the legs), `flies` (2), `rooted` (3: Fungus) |
| units.udb | `mode_speeds` (also mob.toml's `[object.stats]`) | [4] | `{ run, walk, crawl, lie }` (ModeSpeedOf) |
| units.udb | `action_speeds` | [6] | `{ attack, cast, hit, death, cross, special }` (Unit::ActionSpeed) |
| units.udb | `blood_kind` | 0..9 | `blood`: `none`, `red`, `green`, `blue`, `brown` (1..4: a shower and a bloodprints.png decal of that colour), `red_spray`, `green_spray`, `blue_spray`, `brown_spray` (5..8: the shower alone); 9 (Lost in Astral's earth and ice elementals) stays a number: StatChanged and Died, its only readers, do for it what they do for `none` |
| units.udb | `footprint_kind` | -1..25 | `footprint`: the tile of footprints.png (16 x 16 cells, eight across, a cell the left and the right print; PlayStepSounds, Footprint), each named by the vanilla models whose tile it is: every tile is used, each by the models of its name alone (Human Male and Human Female: `human`, the default) (`none` -1, `human`, `driad`, `cyclope`, `skeleton`, `troll`, `golem`, `elemental`, `goblin`, `toad`, `ogre`, `lizardman`, `zombie`, `unicorn`, `horse`, `scrab`, `orc`, `pig`, `hyena`, `boar`, `cow`, `wolf`, `tiger`, `hare`, `rat`, `deer`, `dog`) |
| units.udb | `cast_effect`, `swap_feet` | 0 / 1 | true / false |
| units.udb | model `sounds` | 16 names | `step_sounds`: a table by the ground's surface class (FootstepSound; prints.db's names): `grass`, `ground`, `stone`, `sand`, `rock`, `field`, `water`, `road`, `astral` (8: prints.db has no name; Terrain::SurfaceClass's class outside the map and on a tile without a texture, aiinfo tileDesc.reg's "ASTRAL"; every model gives it "steps", a folder without sounds), `snow`, `ice`, `dry_grass`, `snowballs`, `lava`, `swamp`, `high_rock` |
| units.udb | unit `combat` | [5] | `{ reach, attack, attack_spread, defence, speed }` (CombatIndex) |
| units.udb | unit `perception`, `conspicuity` (also mob.toml's `[object.stats]`) | [6] | `{ sight, day_sight, near, hearing, smell, unread }` (Sense; `smell`: Sense::Smell, read by AIMemory::MeasureExposure, a unit's conspicuity raised by the Stench spell; `unread`: the sixth, copied into the stats and the params (saved, sent) and read by nothing, so kept as data; a prototype's conspicuity `sight` is also its footprints' scale) |
| units.udb | unit `attributes_01`, `attribute_2`, `skills_3_to_5` | floats, [3] | `science` (UnitStats::attributes[0] and [1]: map_object_manager.cpp's prototype stats), `taming` ([2]), `magic_skills` (`{ elemental, sense, astral }`: skill bytes 3..5); `skill_level` still starts every byte |
| units.udb | npc `attributes`, `skills`; mob.toml `[object.stats]` `attributes`; items.idb quick item `attribute_bonus` | [3], [8] | UnitStats::attributes `{ science, unread, taming }` ([0]: UnitServer::UseSkill's Use and Steal, the trade panel's science row, the HUD; [2]: UnitServer::TryTame, kStatTaming; [1]: AttributeWithBonus and AttributeWithItems take any index, but every caller gives 0 or 2), UnitStats::skills `{ melee, archery, backstab, elemental, sense, astral, unread_6, unread_7 }` (skill byte n is perks.pdb's skill n: SkillWithBonus, the trade panel's rows, GetSkill; WeaponSkill reads 0 and 1, SpellSchoolSkill 3..5, BackstabFactor's hover 2; 6 and 7 only through a perk that requires them, none does; two Rebirth NPCs give 6 a 10) |
| units.udb | level `slot_type` | 0..4 | UnitStatSlot::type: `head`, `body`, `arm`, `leg`, `none` (kSlotWeights; LowestSlotRatio's legs and arms; the crippled remarks; 4 the reset type) |
| units.udb | level `vital_mask` | hex | `severed_by`: the damage types whose full hit severs the slot (unit_hits.cpp, bit = DamageType); the bits above 6 (garbage in one Rebirth mod's file: 0x6C2D0E00) are read by nothing (copied into the slot and the params' byte): `unread_severed_bits`, the number, only when not zero |
| units.udb | level `values_a`, `values_b` | [7] of float bits | `unread_values_a`, `unread_values_b` as floats: no code reads UnitLevelRecord::unreadValuesA / unreadValuesB (the rename compile finds only the serialiser) |
| perks.pdb | perk `required_skill_name`, `required_skill` | text, number | one `required_skill`: the skill byte by its name (as `skills`; `none` the data's 0x7FFFFFFF, which CanLearn refuses), which also sets the text CanLearn tests (the check is on unless it is empty or "none"); every file has the text equal to the byte's name, so a record whose text differs would be written `{ skill = n, text = "..." }` |
| perks.pdb | perk `learnable` | 0 / 1 | true / false (CanLearn) |
| spells.sdb | spell `cast_type` | 0..2 | `magic`: `elemental`, `sense`, `astral` (the texts "string school_0".."school_2", the item info's line and the inventory's spell tab; a sense spell is cast without noise and without the casting's visibility (FinishCast, the move factors), an elemental one is planned as an attack) |
| items.idb | weapon `skill_bonus` | [2] | `{ melee, archery }` (UnitStats::WeaponBonus: the skill byte 0 or 1 the weapon type uses) |
| items.idb | armor `first_flags`, `others_flags`, material `armour_flags` | hex | `unread_first_flags`, `unread_others_flags`, `unread_armour_flags` (numbers, only when not zero; no file sets one): the item constructor ors the material's into the armour's, and nothing reads either |
| items.idb | loot `index` | a number | the record of its `record_type`'s table by its key, as GiveItem's `"<loot>.<name>"` names it (LootTableIndex: a weapon's, armour's or quick item's name, a spell's or modifier's code; the last record of the key); an index no record has stays a number: every file's is 0xFFFFFFFD. The tables are the game's (g_itemDatabase, g_spellDatabase: the spells load first, the item tables merge before the loot), so ei-convert, which converts items.idb alone, writes numbers; `record_type` comes before `index` in a record that names one |
| quests.qdb | quest `zone` | a number | `unread_zone`: nothing reads QuestRecord::zone (the quest file's "zone index") |
| quests.qdb | quest `places` | x, y in turn | a list of `[x, y]` points (ObjectivesWindow::LoadQuestPlaces); a last odd value, which nothing reads, is not kept (no file has one) |
| mob.toml | lever `lock.mask` | hex | the bits LockKey::Opens compares: `science` 0x1 (the Use skill's key, {0x1, level: the science}), `science_2` 0x2, `science_3` 0x4 (level bits like science's; no key has them, so a lock of only these opens with none), `key` 0x8 (a quest item's key, {0x8, its id}: Item::GetUse); another bit its number (none has one). The data: `[]`, `["science"]`, `["key"]`, `["science", "science_3"]` |
| mob.toml | trap `sides` | hex | `unread_sides`: mapspec::LoadMob sets every trap's sides from the player table before the trap is made, so the chunk's value (always ~0 in the data) is read by nothing |
| mob.toml | object `side` | a number | the player's name where the file's `[players] names` gives the player one (only that player has it): `side = "Golems"`; else the number. Player names are read by nothing in the game (ZoneDiplomacy::names), so a name is the file's own label |
| mob.toml | `[object.stats]` `damage_weights` | [7] bytes | a table by damage type |
| map.toml | `[[material]] kind` | a number | `dry` 1, `water` 3, `still` 4: the AI map counts the cells under kinds 2 and 3 as under water (ai_map_load.cpp), kind 4's vertices keep the ground's x and y and have no waves (TerrainMaterial::kKeepsGroundXY); 0 and 2 (the same as 1 and 3; no file has them) stay numbers. The files have `dry` (opaque, the colours of the ground) and `water` (the blue water and the red lava alike) |
| config.reg.toml | `[MOD]` `Single`, `Multi`, `GameSaves`, `ForceShowTutorial` | 0 / 1 | true / false: registry.cpp's switch keys (kSwitches) are written so; any registry value read from TOML takes true / false as 1 / 0 (ModEntry reads Single and Multi; GameSaves and ForceShowTutorial are the starter's, read by nothing of ours) |
| units.udb | unit `hit_sound_kind` | 8..16 | `hit_sound`, the sound of its natural attack's hit (HitSoundFormat, its only reader): a weapon type (0..6: that weapon's sound), `none` (8), else by the folder of res\sfx\weapons\ the code picks: `hands1` 9, `pierce` 10, `crash` 11, `hands4` 12, `slash` 13, `hands6` 14, `hands7` 15; `spells_only` 16 (none). hands1 (the humans, skeletons and zombies), hands4 (the ogres), hands6 (the trolls) hold one sound for every material; no install has a hands7 folder (the animals and elementals: they hit silently) |
| units.udb | npc `stats` | [3] | `{ strength, dexterity, intelligence }` |
| .adb | `key` | hex | its parts (object3d.h): `kind` (AnimationKind: `special`, `attack`, `cast`, `run`, `walk`, `idle`, `death`, `hit`, `cross`), `weapons` (the variant bits by weapon type, `any` 0x7F), `move` (`stop`, `walk`, `crawl`, `lie`), `play` (`once`, `looped`); the number (bits 22..29) by the kind: an attack's or cast's `height` (`high`, `middle`, `low`) and `blow` (true), a death's or hit's `side` (BlowSide: `back`, `front`, `minus_x`, `plus_x`) and `fall` (UnitServer::BuildDeathAnimation: `running` 0x08, a hero hit while it runs; `moving` 0x10, while it moves otherwise (HitWhileMoving); `none` 0x18, no fall), a special's `skill` (SkillAnimation by name; another number is one a briefing phrase plays, ui_briefing_phrases.cpp, and is written as the number: 14..76 in the data), a walk's or run's `legs` (AnimationKeyFlag::DamageLevel1 / 2: a hero whose lowest leg slot is at most ai.reg's [RPG] DamageLevel1Value / DamageLevel2Value asks for `wounded` / `badly_wounded`, UnitServer::BuildMoveSteps and NextAnimation; "cwalk08", "cwalk06"), an idle's `idle` (`special` 1: every idle request has it, kAnimationSpecialIdleKey in FillIdle and a client's special idle mode; `unused` 2: no request has it, so the 6 vanilla entries with it never play), a cross's `to` (the mode it goes into, EnterBits; `move` is the one it leaves: `hero_walk` 1, a hero's own walk, Walk without onGuard, whose key has no move bits; `walk` 2, the guarded walk or another unit's; `crawl` 3; `lie` 4; `stop` 5), else `number` (a value no part's names have; no file has one). Every variant bit (0..14) is a weapon bit (AnimationKeyFits; the requests' bits are AnimationWeaponBit of the held or natural weapon's type): `none` is type 8, a type without a name its number (7: the 8 entries of unmoco3 and unmoco4 past their old, shorter entries' end) |
| .adb | `flags` | hex | `flags = ["arrow", "with_under_armour", "with_outer_armour"]` (AnimationFlag: 0x1 UpdateArrow; 0x10 / 0x20 Unit::AcceptsAnimation: a hero wearing a shirt, pants, boots or gloves / a helmet, body armour or leggings plays only the animations with the bit), `death_side` (0x1C00000, FigureAnimationRecord::Side). The files set other bits too (1..3, 15..21, 25, 26: most entries repeat their key's bits) that nothing reads: compiling with the field renamed finds its only readers (those three, the harness's `anim` check and ei-convert's comparison), so ReadAdbImage keeps kAnimationFlagBits and the TOML has no field for the rest |
| .adb | `index` | its place | left out when it is its place (a new animation's default) |
| mob.toml | `parent_id` | 50..60 | `class`: MapObjectType `hero`, `creature`, `animal`, `tree`, `building`, `stone`, `stuff`, `torch`, `magic_trap`, `lever` |
| mob.toml | `[players]` | 32 names, 32 x 32 matrix | `names = { 3 = "Golems" }` only names other than "Player-n"; `allied`, `neutral`, `hostile`: lists of `[player, other]` pairs whose relation differs from the default (neutral, allied with itself); the data's relations are all 0..2 |

Kept as numbers (quantities, indexes, a player number): `number`, `variant`, `magic_level`, `material_units`, a
unit's `level`, `size_class` (the AI map's size class), `foot_part` (the "ll<n>" part), `side` (the owner's player
number, unless the file names the player), `player`, an item's `attack_interval`, a quest item's `id`, a quest's
`order`, a perk's `exclusion_group`, the eyes, the chances.

Also by name: spells.sdb's `school`, `target_kind` (`unit`, `point`; Shapechange's 's' stays the number 115:
Spell::InitFromPrototype, the only reader, gives it neither target flag, so the interface asks for a point and the
cast keeps its target object), `allowed_modifiers` (`{ range, targets, area,
effect, duration }`), a modifier's `kind`, `needs_sight`, and the fields read for a record without a behaviour of its
own (items_spells.md, "Spell behaviours"): `ai` (`none`, `attack`, `area_attack`, `lasting_area`, `curse`, `protect`,
`enhance`, `heal`), `damage_type` (the damage types' names, `general` the default) and `offensive` (true / false);
map.toml's `classes` (the surfaces, `line` 16).

Levers.ldb, acks.db and prints.db were names and numbers already. The names were found as MF14b did: each field
renamed and the tree compiled with `ninja -k 0`, so every reader shows (the level values, a quest's zone, the armour
flags have only the serialiser and the TOML; the material kind has the AI map's and the terrain's).

## Textures as PNG

A texture can be a PNG (MF4, user 2026-11-09). Every .mmp still loads; `<name>.png` found where `<name>.mmp` would be
(each place of "Loose files", top first: a folder's file or an archive's entry) stands for it there: at each place the
PNG is asked before the .mmp, so a mod's PNG hides the game's .mmp and a converted copy's PNG its removed one
(`MappedResource::PngTexture`, resource.cpp). The PNG is made into the .mmp file the game would have read
(src/engine/models/texture_png.h, `FromPng`): its header, its extra block, every level. So every reader of an .mmp sees one
as before: the texture loads, the item and inventory pictures, the cursors, the composed skins' layers, the terrain's
`HasFile`. A made file is shared like a loose one, and kept after its last close ("Loading cost"). A PNG that does not
decode, or whose settings name a format it does not make, is logged ("warning: textures: ...") and the .mmp is read.

- Settings: what is not pixels (the format, the header's channel masks, the level count, the extra block) is in the
  PNG's `tEXt` chunk "ei-texture", TOML: `format = "DXT1"`, `channels = "1555"`, `levels = 5` (not for PNT3),
  `extra = "<hex>"` (only when there is one; no shipped archive texture has one). ei-convert writes it. A PNG without
  the chunk (an image editor drops it) takes them from the .mmp it stands for: in its own place or the first below it.
  A PNG with neither is a new texture: A8R8G8B8, every level down to 1 x 1. A PNG of another size than the .mmp keeps
  the .mmp's settings, its levels at most its own full chain.
- The PNG: any kind (palette, grey, grey and alpha, RGB, RGBA, interlaced or not; 16 bits a channel taken to 8 by the
  high byte; gamma and colour profiles not applied), as 8-bit RGBA with straight alpha. ei-convert writes RGB when
  every texel is opaque, else RGBA, deflated by miniz in segments (`png::Write`, "Loading cost").
- Formats (the texels as the texture holds them; files: ei-convert's counts, vanilla / Rebirth with its mods):

| .mmp format | Files | In the file | The PNG | Back |
|---|---|---|---|---|
| 565 | 37 / 4 | 16-bit, straight | RGB, each channel widened (its bits repeated) | rounded to its bits: exact |
| 1555 | 83 / 57 | 16-bit, straight (17 / 20 files have colour under alpha 0: kept) | RGBA, alpha 0 or 255 | exact; alpha 128 or more is 1 |
| 4444 | 40 / 9 | 16-bit, straight (35 / 4 effect pictures have colour under alpha 0: kept) | RGBA | exact |
| 8888 | 70 / 72 | premultiplied (every texel's colour at most its alpha) | RGBA, colour x 255 / alpha | premultiplied again: exact |
| PNT3 | 2,838 / 3,511 | premultiplied ARGB, runs of transparent black packed; the header's level count is the packed bytes | RGBA as 8888 | premultiplied and packed again (our runs: any run of transparent black, else groups of four) |
| DXT1 | 617 / 990 | BC1 blocks, a block's third mode transparent black | RGBA as the blocks decode (the D3D formulas rounded to nearest) | blocks again |
| DXT3, DXT5 | 15 / 10, 0 / 58 | BC2 / BC3 blocks, straight | RGBA | blocks again |
| DXT2, DXT4 | none | the premultiplied BC2 / BC3 | as 8888 | as DXT3 / DXT5, premultiplied |

  Each format comes back as itself, with the same header (all 19 fields; PNT3's packed size its own). Other formats (an
  unknown code, PNT2) are not made: ei-convert counts them as failed, the game reads the .mmp.
- Alpha: 8888's and PNT3's texels are premultiplied (checked on every shipped file: the compositor and the item
  pictures draw them so, render.h `ImageLayout::Argb`); a PNG holds straight alpha, so the export divides
  (`round(c x 255 / a)`, 0 for alpha 0) and the load multiplies (`round(c' x a / 255)`): every texel whose colour is at
  most its alpha comes back exactly (`ei_unit_check` tries every pair). A texel whose colour is above its alpha cannot be
  held (a straight colour stops at 255) and comes back as its alpha; alpha 0 comes back black. No vanilla file has one;
  Rebirth's mods' PNT3 pictures have them (see the counts below). The 16-bit formats and DXT are straight already and
  are written as they are.
- DXT: the PNG holds the decoded texels; at load each block is made again: the block whose two colours are among its
  texels and that decodes to them exactly when there is one (the shipped blocks' nearly always), else stb_dxt's (high
  quality; a BC1 block with transparent texels in the third mode: stb_dxt's colours for its opaque texels, those the
  nearest of the two and their mean). DXT3's 4-bit alpha is exact; DXT5's alpha block likewise exact when its ends are
  its texels' largest and smallest alpha. GPU memory stays the DXT size.
- Levels: made at load from the first by a 2 x 2 box filter (each texel the rounded mean of the four above it, the
  edges clamped), never taken from a file; the levels of a format with one bit of alpha (DXT1, 1555): a texel is opaque
  when any of its four is, of their mean colour (the shipped files' levels so, nearly texel for texel; a plain mean
  would make the cutouts thin and dark at a distance). The shipped levels were made by another filter from the
  original pictures: ours differ by a few steps (see the counts).

### ei-convert's textures

ei-convert writes each archive's `.mmp` as `<archive folder>\<name>.png` (tools/convert/convert_textures.cpp) and lists
it as converted. Each round trip: the PNG made into an .mmp as the game makes it (no .mmp below), then compared with the
original: the header field by field, the extra block, the first level texel for texel (exact; premultiplied texels
above their alpha expected as their alpha; a DXT format within a mean of 0.5 a channel), the levels below within a mean
of 32 a channel (the colour where both are opaque, the alpha everywhere but below a first level with no transparent
texel: some shipped files have transparent texels in their levels below, a DXT1 block's third mode or a 1555 texel by
mistake, e.g. vanilla's stone02 .. stone07 and eagle00; ours have none). The levels below are only bounded: the shipped
ones were made by another filter, the worst file's mean is 18.6 (below), and levels read from the wrong bytes differ by
50 and more.

Counts (2026-11-10, `ei-convert --check`, every round trip holding; files / first-level texels that differ, by at most /
the worst file's mean a channel over the levels below, colour and alpha / seconds to make the .mmps from the PNGs):

| Format | Vanilla | Rebirth (its mods too) |
|---|---|---|
| 565 | 37 / 0 / 0.95, 0 | 4 / 0 / 0.95, 0 |
| 1555 | 83 / 0 / 3.95, 0 (9 files with shipped holes) | 57 / 0 / 3.95, 0 (9) |
| 4444 | 40 / 0 / 2.50, 14.42 (banshee's alpha) | 9 / 0 / 1.47, 1.22 |
| 8888 | 70 / 0 / one level | 72 / 0 / one level |
| PNT3 | 2,838 / 0 / one level / 1.5 s | 3,511 / 0, 499 texels in 44 files clamped (colour above alpha) / one level / 1.1 s |
| DXT1 | 617 / 35,597 of 93.1 M, by 3 / 14.13, 5.75 (13 with shipped holes) / 3.8 s | 990 / 170.6 M of 1,363 M, by 33, worst file's mean 0.28 / 14.11, 18.64 (2) / 27.5 s |
| DXT3 | 15 / 3,125 of 0.48 M, by 3 / 17.45 (arrow01), 2.05 | 10 / 17,335 of 1.44 M, by 8 / 3.55, 0.51 |
| DXT5 | none | 58 / 1.72 M of 40.8 M, by 18, worst mean 0.15 / 10.38, 7.34 / 0.9 s |

The shipped DXT blocks of vanilla nearly all come back exactly (their ends are among their texels). Rebirth's large
DXT1 and DXT5 textures were compressed by a tool whose ends are not, so most of their blocks are stb_dxt's again: close
(the worst file's mean 0.28 a channel) but not the same. A large level's blocks are made on several threads (the
bytes the same). The stub data (`build\stub`, the Linux workflow's) gives the same kinds of counts, every round trip
holding.

Loading cost. Making a texture is its PNG's decode, the box filter for its levels and the DXT blocks; a converted
Rebirth zone makes hundreds (cz_ingos 447, 132 MB of .mmps), most of them DXT1 terrain pages. What keeps it near an
unconverted load:
- The reader (`png::ReadRgba`, src/lib/png.cpp) decodes with Wuffs (third_party/wuffs, v0.3.6, Apache 2.0), the only
  PNG decoder (`png::ReadRgba16` reads the same as 16-bit RGBA, for a map folder's heights and normals). ei-convert's PNGs are written in segments (`png::Write`): runs of about 256 KB
  of rows, each deflated on its own (a full flush between them) with its first row filtered None or Sub, and an
  "eiSG" chunk naming each segment's rows and its start in the zlib stream. Still one valid PNG any reader takes; ours
  inflates and unfilters the segments on threads and checks the stream's Adler-32 (the segments' combined). A PNG
  without the chunk, or whose segments fail on their own, is read by Wuffs whole.
- The levels' box filter and the DXT blocks run on threads in runs of rows (the bytes the same).
- A map's terrain pages are made together before they are loaded (`Terrain::SetupPages`, `MappedResource::MakePngs`):
  one a processor at a time, up to half of the kept bytes below. Other textures are made at their first GetFile, one
  at a time (in gipat 545 makes, 1.4 s together; its 146 pages take 1.9 s made together).
- A made file is kept after its last close, up to 128 MB an archive (least recently used out first; the loose files'
  reload empties it): a zone's textures are closed and opened again between its loads, and were made two or three
  times each.
Timings (one game, `--quiet`, Rebirth, 2026-11-10), converted against unpacked: `mod_lost_in_astral_cz_ingos` (all six
bases, before H70 split it) 24 s against 24 s (43 s before these), `_gipat` 15 to 17 s against 12 s (33 s),
`_suslanger` 14 s against 12 s (17 s).
What is left in gipat: the textures' making (about 3 s, the DXT encode the most) and a slower start (the first frame
0.97 s against 0.63 s).

### Checks

- `ei_unit_check`'s texture checks (tests/unit/texture_png.cpp): widening and narrowing every channel value, premultiply
  and back for every colour and alpha pair, every 16-bit texel of 565, 1555 and 4444 through a PNG and back, 8888 with
  its levels from the box filter, a PNT3 stream, a DXT1 cutout made over an .mmp below, the settings chunk and a new
  texture's defaults.
- `mod_png_texture` (both installs; tools/make_mod_s19d.py png): a mod whose zone 1 terrain pages are PNGs of four
  coloured quadrants without the settings chunk; the ground shows the colours (`hues`) and the trace has the PNG read
  for the .mmp.
- `ei-convert --check` on both installs (every texture's round trip holding), and on the stub data in the Linux suites'
  workflow.
- `work.py test <ID> --converted --golden` (and `--rebirth-only`): the converted installs have every texture as a PNG.

## Maps as folders

A map can be a folder of TOML and PNGs (MF8, user 2026-11-09). Every `.mpr` still loads. The folder is the archive's
own loose folder ("Loose files"): `Maps\zone1\` for `Maps\zone1.mpr`, with fixed file names, so a modder finds it by
unpacking the archive or converting the install. When a place of the archive's lookup (top first, as "Loose files") has
one of the files below, the archive's map file `<name>.MP` and its sectors `<name>XXXYYY.SEC` are made from the folder
over the original (src/engine/data/map_folder.h, `Assemble`; resource.cpp `MappedResource::FolderMap`) and every reader
reads those: the landscape (`Terrain::LoadMap`, the sectors) and the AI map (`AIMap::LoadTerrain`) alike. The made files
are kept while the archive is open; `ListFiles` lists their names. A folder file that does not read (not a PNG, another
size, a value out of range) is logged ("warning: maps: <folder>: ...") and left out. The `.mob` objects are "Map
objects as TOML"; the quest zones stay as they are.

| File | What | Pixels |
|---|---|---|
| `map.toml` | the header: `name` (the files' name inside the archive), `height_range`, `sectors = [x, y]`, `pages`, `page_size`, `tile_size`, `water_sectors` (a text per row of sectors: `W` with water, `.` without), `classes` (per tile texture its class by name: the surfaces of "Readable values", `line` 16; another as a number), `animated` (`{texture, frames}`), `[[material]]` (`kind`, `colour`, `opacity`, `light`, `priority`) | |
| `heights.png` | the ground's heights, a vertex a pixel | 16-bit grey |
| `normals.png` | the ground's normals | 16-bit RGB: the sector file's codes widened by repeating their bits: x and y 1000 + 1000 x (11 bits), z 1000 z (10 bits); a normal map's look, up is (0.49, 0.49, 0.98) |
| `offsets.png` | the ground vertices' sideways displacements | 8-bit RGB: R = 128 + x, G = 128 + y (1/252 of a cell a step), B 128 |
| `water.png` | the water surface's heights | 16-bit grey (0 in a sector without water) |
| `water_normals.png` | the water's normals | as normals.png |
| `tiles.png` | the ground's tiles, a tile a pixel | 8-bit RGB: R the texture's low byte, G its high byte, B the quarter turns x 85 (0, 85, 170, 255) |
| `water_tiles.png` | the water's tiles | as tiles.png |
| `water_materials.png` | the water's material per tile | 8-bit grey: the `[[material]]` index, 255 none |
| `seams.toml` | where two sectors' copies of a shared vertex differ (below) | |

- Grids: the map is `sectors` x / y sectors of 32 x 32 cells; a cell is the vertices' spacing and one world unit
  (zone 1: 6 x 8 sectors, 192 x 256 units). The vertex layers are (32 x + 1) x (32 y + 1) pixels, the tile layers
  16 x by 16 y (a tile is 2 x 2 cells); the PNG's first row is y = 0 (world (0, 0) at the image's top left, x to the
  right, y down). A sector's own vertices are the grid's 33 x 33 from
  (32 sx, 32 sy).
- Heights: a pixel's world height is value x height_range / 65534 (zone 1: 21.95 units at 65534). Painting heights in
  an editor gives what it shows. Normals: when heights.png comes from a place above normals.png (a mod's heights.png
  over the game's map, or one painted into a converted folder whose normals.png was removed), the normals are worked out
  from the heights, each vertex's the mean of its four faces (displacements counted); so a modder paints heights alone.
  The shipped normals are not the heights' (made by another tool from finer data), so they are kept in normals.png.
- Seams: each sector holds its own copy of the vertices on its edges. The grid takes the earlier sector's (lower y,
  then lower x; the one the height queries read), and `seams.toml` lists where a later sector's copy differs, by what
  it adds (`{sector, vertex, height, offset, normal}`, `ground` and `water`). Vanilla's 38 maps have 100, Rebirth's 88
  (its mods too) 2,015. Seams from a place below a layer are left out of it: painted heights meet at the edges,
  worked-out normals are the same in each sector.
- Water: the water grid takes the first copy among the sectors with water; a sector without water has none of its
  water layers (its water tiles and materials none). `water_sectors` says which sectors have water: 19 of Rebirth's
  sectors have water tiles but no water material.
- Merging, each file from the first place that has it, no lower than the original's (the first place with a map file);
  each PNG replaces its layer whole, `map.toml` merges by key over the original's header (a list whole). A new size in
  `map.toml` starts every layer of the other size again (flat, dry, no turn). A folder without an original needs
  `map.toml` (named by its `name`, else the folder's).
- Rebirth's Maps (mod_folders.cpp `SelectMaps`): a mod whose `Maps\` has `zone1\` reads the mod's `Maps\zone1.mpr` in
  place of the game's, so a mod that paints heights alone ships the archive (or its folder) with them, as the test mod
  does.
- The AI region graph follows the ground (docs/notes/ai.md, "Built graphs"): a zone whose map comes from the mod's
  `Maps\` and its .mob from the game's has its graph built at the zone's load (the showcase island, a mod's painted
  heights); ei-convert's folders sit beside their .mob and read its graph, so a converted install plays as the binary
  one.
- Not held (no loader reads them): each material's warp speed and reserved bytes, the water vertices' displacements; a
  sector flag byte 1 (water vertices without water tiles: none ship) comes back as 3.

### ei-convert's maps

ei-convert writes each `.mpr` (and each unpacked `Maps\<zone>\` holding a map file) as its folder
(tools/convert/convert_maps.cpp) and lists the map file and the sectors as converted (the archive left holds the rest).
Each round trip: the folder's files assembled as the game assembles them (the only place), the map file and every
sector made from them and compared byte for byte with the original, the unread bytes above zeroed.

Counts (2026-11-10, `ei-convert --check`, every round trip holding):

| | Maps | Sectors (with water) | Seams | Originals | Folders | Assembled in |
|---|---|---|---|---|---|---|
| Vanilla | 38 | 2,504 (747) | 100 | 30.5 MB | 18.7 MB | 0.19 s (the slowest zone19, 0.018 s) |
| Rebirth (its mods too) | 88 | 6,141 (1,949) | 2,015 | 75.8 MB | 44.2 MB | 0.45 s (the slowest zone1, 0.018 s) |
| Stub data | 126 | 8,645 (2,696) | 2,115 | 106.3 MB | 62.8 MB | 0.64 s |

Loading: a zone's folder is assembled at its archive's first file read, in about 20 ms (the 16-bit PNGs' decode), so
maps add little to a converted install's load (its textures cost the time: "Textures as PNG", "Loading cost").

### Checks

- `ei_unit_check`'s map checks (tests/unit/map_folder.cpp): the PNG writer's 16-bit grey and 8-bit RGB read back
  (`ReadRgba`, `ReadRgba16`); both against the pixels the writer was given (fixed ones for a palette PNG with tRNS),
  every channel count at 8 and 16 bits, two
  segmented images (8-bit RGB, 16-bit grey and alpha) and one whose segment offset is wrong (read whole by Wuffs), and
  a cut PNG refused; a made-up map with water and seams through its files and back byte for byte; heights.png over the
  original (worked-out normals, the rest the original's, its seams' heights and normals left out); flat normals up.
- `mod_map_heights` (both installs; tools/make_mod_s19d.py heights): a mod with zone 1's archive and a heights.png
  raising a plateau of 60000 (20.1 units) where the ground is at about 1; `ground` reads the landscape's and the AI
  map's height there and 20 cells away (the archive's), and the trace has the PNG read.
- `ei-convert --check` on both installs (every map's round trip holding), and on the stub data in the Linux suites'
  workflow.
- `work.py test <ID> --converted --golden` (and `--rebirth-only`): the converted installs have every map as a folder.

## Map objects as TOML

A `.mob`'s chunks but its scripts can be TOML (MF9, user 2026-11-10): `Maps\zone1.mob.toml` beside `Maps\zone1.mob`.
Every `.mob` still loads. `WorldServer::LoadMapObjects` (world_zone.cpp) asks `mobtoml::Find` (src/game/map/mob_toml.h)
for the TOML, looked for as a record file's (`FindRecordFile` without reading the original: each layer of the path map
top first, down to the first that has the .mob); the top one that parses is the whole of the objects, the weather and
the player table (no merge: a mod's file holds every record, as a camera path's). The scripts are still read from the
`.mob` (`MobFile::ReadChunks(false)`: its script chunks alone), and so are the AI graph (`ReadAIGraph`) and the main
script (`LoadZoneScripts`); with a TOML the `.mob` may be missing (no scripts then, and the graph built at the load:
"Zones from a mod"). The `.lua` the TOML's `script`
key names runs in place of the scripts when it is there ("Scripts in Lua"). Both readers build one
`mapspec::MobContents` (map_spec.h: the records, the weather, the player table, the relations flag, the scripts, the
TOML's `script` key), which `mapspec::LoadMob` applies as the zone loader did.

```toml
# A .mob's map objects (docs/notes/mods.md, "Map objects as TOML"); its script stays in the .mob.
script = "zone1.lua"
applies_relations = true

[[weather]]
wind = [0.0, 1.0, 0.0]
...
[players]
hostile = [[0, 3], [3, 0]]

[[object]]
kind = "unit"
id = 12
...
```

- `[[object]]`: the records in the file's order (the order they are filed by id; the zone loader walks `byId`, whose
  order is the insertions', so the order is behaviour). `kind`: `object`, `unit`, `flame`, `trap`, `lever`,
  `light`, `sound`, `particle` (the chunks B001, BBBB0000, BBBF0000, BBAB0000, BBAC0000, AA01, CC01, DD01). Then
  `id` and each field that differs from the kind's defaults (the record's constructor: a missing key is the default).
  Each kind's fields are those its chunks set, listed once in mob_toml.cpp (`Fields(record, visit)`), which the
  writer, the reader and ei-convert's comparison walk:
  - every object kind: `id`, `name`, `model`, `texture`, `primary_texture`, `skin_texture`, `parent_iname`,
    `class`, `position`, `rotation` (`[w, x, y, z]`), `scale` (`[x, y, z]`), `side` (the player's number, or its
    name when `[players] names` gives it one: the reader takes `[players]` first), `player`, `shadow`,
    `visible` (true / false), `parts`, `comment`;
  - flame: `volume`, `offset`, `sound`, `effect`; trap: `spell`, `unread_sides` (the loader sets the sides from the player
    table), `period`, `single_target`, `area` (`{ x, y, radius }` circles), `points` (`[x, y]`); lever: `lock`
    (`{ mask, id, level }`, the mask by name: "Readable values"), `state`, `state_count`, `cycling`,
    `direct_use_blocked`, `keeps_map_flag`;
  - unit: `prototype`, `apply_stats`, the item lists `carried`, `quick_items`, `spells`, `weapons`, `armour`;
    `[object.stats]` (the statistics chunk BBBB0004 by field: `hit_points`, `speed`, ...; `mode_speeds`,
    `damage_weights`, `perception`, `conspicuity` and `attributes` as the units.udb tables of their names; its unused
    bytes `unread_...` only when not zero), `[[object.behaviour]]` (up to five: `type`, `alarm`,
    `alert_mode` by name, `alarm_kind`, `enabled`, `guard`, `guard_radius`, `call_radius`, `loop`,
    `always_updated`, `unread`), each with its `[[object.behaviour.patrol]]` (`position`, `waypoints` of
    `{ position, time, look_wait, stop_first }`);
  - light: `id`, `name`, `position`, `colour`, `radius`, `shadows`, `comment`; sound: `id`, `name`, `position`,
    `sounds`, `volume`, `radius`, `min_interval`, `max_interval`, `ambient`, `unread` (chunk CC0E), `comment`;
    particle: `id`, `name`, `position`, `effect`, `scale`, `comment`. A flame's `effect` (TOML only, default
    CampFire) and a particle's `effect` (the .mob's type read by its name, `ParticleTypeName`) name effects; the
    server takes their ids (`EffectTypeNamed`) for the torch's light kind and the EffectParticle's type.
- `[[weather]]` (chunk ABD0, each applied in order): `wind`, `wind_strength`, `time`; its two unused values are
  not kept. `[players]` (chunk DDDDDDD1) only what differs from `ZoneDiplomacy`'s defaults ("Player-n"; neutral,
  allied with itself): `names = { 3 = "Golems" }` by player number, `allied`, `neutral`, `hostile` as lists of
  `[player, other]` pairs (DiplomacyRelation 0..2; the shipped data has no other relation). Left out when nothing
  differs (no map of vanilla or Lost in Astral names its players).
  `applies_relations`: the last of the flag chunks C000 / D000 (false: the table only tells traps their targets).
- Strings are UTF-8 both ways (the chunk reader converts the .mob's 1251); floats the shortest text that reads back.
  A record of an unknown kind, an unknown key or a value its field does not take is logged ("warning: map objects:
  <file> line n: ...") and left out.
- `class` (the chunk's parent id) is the object's class (`MapObjectType` by name, record_names.h: `hero`, `creature`,
  `animal` units, `tree`, `building`, `stone`, `stuff`, `torch`, `magic_trap`, `lever`), the factory's type in `WorldServer::CreateObject`. `mapspec::LoadMob`
  (mob_reader.cpp, `ClassFits`) checks each record's class against its kind (units 50-52; flames 58; traps 59; levers
  60; objects 53-56 or a unit's class: the main menu's unmoco2 is an object record of class 51) before it makes the
  object, and a record that does not fit is logged ("warning: map objects: <zone> record <id>: class <n> does not fit its
  kind; the record is left out") and skipped (MF10a; without the check the null object was used and
  the zone load crashed: MF10's hand-written pig without a class, 2026-11-10). It is there, not in the readers
  (mob_toml.cpp is also ei-convert's and knows no factory) nor in `WorldServer::CreateObject` (which has four make
  sites and no zone name), and covers the .mob reader too; every shipped record of both installs fits.
- A record id the file repeats: the later record takes the earlier's place in `byId` (the original leaked the earlier
  one); the TOML writes the later record at its own place. ei-convert compares the walks, so a file where that moved
  the order would fail its check (Lost in Astral's files repeat two ids; their walks hold).

### ei-convert's map objects

ei-convert writes each `.mob` of an install (any folder: the mods' too) as `<path>.mob.toml`
(tools/convert/convert_mobs.cpp), naming `<stem>.lua` as its script when it has one, and does not list the `.mob` as
converted: a converted copy keeps it for its scripts and AI graph. Each round trip: the .mob read by the game's
reader, written, read back by `mobtoml::Read`, and compared: the records in `byId`'s walk, every field by its bytes
(`mobtoml::FieldImages`: floats by their bits, the five behaviour records, the statistics), the weather, the player
table, the flag.

Counts (2026-11-10, `ei-convert --check`, every round trip holding):

| | Files | Records | Repeated ids | .mob | TOML | Read back in |
|---|---|---|---|---|---|---|
| Vanilla | 99 | 52,670 (43,655 objects, 7,953 units, 408 levers, 212 particles, 209 lights, 133 flames, 87 sounds, 13 traps) | 0 | 60.0 MB | 25.4 MB | 0.6 s |
| Rebirth's maps | 99 | 52,822 (43,789 objects, 7,954 units, 225 lights, ...) | 0 | 60.0 MB | 25.5 MB | 0.6 s |
| AllHeroes | 11 | 4,011 (3,481 objects, 274 units, ...) | 0 | 4.6 MB | 1.6 MB | 0.04 s |
| Lost in Astral | 130 | 90,403 (80,134 objects, 7,216 units, 1,617 levers, ...) | 2 | 89.2 MB | 34.8 MB | 0.8 s |

Zone 1's TOML (777 records, 12,824 lines, 0.3 MB) reads in about 10 ms.

### Checks

- `ei-convert --check` on both installs (every `.mob`'s round trip holding), and on the stub data in the Linux suites'
  workflow.
- `work.py test <ID> --converted --golden` (and `--rebirth-only`): every zone's objects come from its TOML.
- `mod_map_objects` (both installs; tools/make_mod_s19d.py mobs): a mod whose only file but its config is
  `Maps\zone1.mob.toml`, the game's zone1.mob as ei-convert writes it (a converted install's own file, else made at
  run time by the build's ei-convert from a copy of the install's .mob; nothing is committed) with the pig 1947 moved to
  (50, 138) and a pig `MF9aPig` (id 2009) added at (50, 142), beside the party's start. The mod has no `.mob`, so the
  scripts come from the game's: in a new game both pigs stand near their places (`GetObject(id)`, `GetX`, `GetY`; the
  capture shows them by the hero), another pig and the script's units keep theirs, and the trace has the mod's TOML
  and the game's `.mob` read; two more records at the file's end (a unit without a class, one of a tree's class, ids
  2010 and 2011) are left out with their warnings (`expectlog`). A map unit is found by its id (`GetObject`): `GetObjectByName` found none of the
  pigs by the names the TOML gives.
- Still binary in a mod: the `.mob`'s AI graph chunk (0x31415926, `ReadAIGraph`) comes from the game's `.mob` (or
  none when no layer has one; then, or when the mod's map stands over the game's .mob, it is built from the ground:
  docs/notes/ai.md, "Built graphs"); a mod that only moves objects keeps the game's graph, which the TOML does
  not carry. Its scripts
  can be a `.lua` ("Scripts in Lua").
## Zones from a mod

The zone table (`MapDatabase`, map_database.cpp: the allods, the zones with their exits, the quests' global map places)
is texts.res's `map.txt` and `map-LMP.txt`, read once at `StartGame` (`LoadAll`) and for a quest's folder
(`GameFlow::RegisterQuests`: `<folder>\map.txt`). A mod adds zones, exits and quests to it, or changes the game's, with
`Res\texts\map.toml` (MF13a, user 2026-11-11), the common TOML records ("Records as TOML": `FindRecord("map")` in
texts.res's place, each layer's file merged bottom first, `replace = true` the whole table but the quest folders'):
each `[[allod]]`, `[[zone]]` and `[[quest]]` merges into the record of its `name` (matched without case, as the game's
`FindMap`), or is added. The texts' records are kept as read, and the lists are made again from them after each text
file, the map.toml files merged over them all (`MapDatabase::Merge`; the files parsed once, at `LoadAll`, which alone
logs what they refuse): a quest folder's map.txt, read later, is merged over as map.txt is, so a mod changes its
zones by name too (MF13d2b: `net_zone_missing_map` gives z3q2 only its `mob`). Until the folder is read such a record
is a zone of its own, with no map. A zone's `[[zone.exit]]` with `number` n (1-based, map.txt's `#exit n`) merges into
its n-th exit; one without, or past the last, is added (target "none" until it names one). The keys are those of map.txt's
lines, one `Fields(record, visit)` per record (`name`, `allod`, `kind`, `map`, `mob`, `camera`, `size`, `minimap`,
`objectives_map`, `figure`, `position`, `weather`, `sky`, `cage`, `restrict`; an exit's `target`, `entrance`, `deploy`,
`remove`, `angle`, `pass_time`, `view`, `deploy_angle`, its angles in degrees). A zone given another allod moves from
the old allod's list to the new one's; a new zone joins its allod's. A record that does not read is logged ("warning:
map: <file> line n: ...") and left out whole. The guide's "Zones" has the format.

The texts' reader changed with it: map.txt's `#position` line and a `#quest`'s places are kept on the records
(`MapRecord::position`, `MapQuestRecord`) rather than looked up again in the text, so the global map
(`ui_global_map.cpp`: the party's arrow, a way's, a quest's) and the objectives screen's places
(`ui_objectives.cpp`, `LoadQuestPlaces`) read a mod's zones as the game's. Two differences from the old lookups, both
in data no golden reached: a quest with no places line (vanilla q46ak, q47ck, q52k, q64h; ten in Lost in Astral) has no
places, where the old reader took the next header as one place at (0, 0); and a multiplayer base zone (map-LMP.txt's
bz1mpg ...) has its own position on the global map, where the lookup read map.txt only and gave (0, 0, 0).

A new zone needs no `.mpr` (a map folder: "Maps as folders") and no `.mob` (`NewZone` takes a `.mob.toml` alone:
`MapFileMissing` of both). With neither a `.mob` graph chunk nor a `.grp`, `NewZone` builds the region graph once
the objects stand (docs/notes/ai.md, "Built graphs"), and a save keeps it against zeros, so a load never builds.

### Checks

- `mod_showcase_zone` (vanilla): the showcase mod's zone, reached by walking: a new game on the first island, the hero
  walks onto gz1g's merged exit 3 east of the landing and is ordered onto it, the leave question, and the party arrives
  on the island in its exit 1's deploy rectangle (the island's script, the quest's variable, the golem standing); back
  through the island's exit 1 into gz1g's exit 3 deploy rectangle, and in again from the island's save (the golem and
  the variable kept, the graph from the save).
- The showcase scenarios ("The showcase mod") enter the island with the harness's `zone showcase 0` once the new
  game's battle screen is up.

## Scripts in Lua

A `.mob`'s script runs from a `.lua` when one is there (L8, user 2026-11-10; the modder's side: docs/modding.md,
"Scripts"); without one the .mob's script is converted at its load (lua-scripting.md, "Texts at runtime"). Both run on
the script's Lua scheduler; the runtime, the scheduler and the saves are lua-scripting.md's.

- **Which file.** `mobtoml::FindScriptLua` (mob_toml.cpp): the name the `.mob.toml`'s `script` key gives (the TOML
  `mobtoml::Find` used; `MobContents::scriptFile`), else the .mob's name with `.lua`, in the .mob's folder of each layer
  of the path map, top first, down to the first layer that has the `.mob` (MF3's rule for a record's TOML): a mod that
  ships its own `.mob` takes the `.lua` from its own layer only, and a converted install's `Maps\zone1.lua` stands for
  its own zone1.mob. Looked for at every load of a .mob's script into the mission script: a zone's (`LoadMapObjects`
  then `mapspec::LoadMob`, and `WorldServer::LoadZoneScripts` for a brief zone, which reloads the script after it,
  reading the TOML's key with `mobtoml::ScriptFile`) and an AddMob'd one. A TOML that names a file no layer has (every
  ei-convert TOML names `<stem>.lua`) runs the .mob's script.
- **Script::LoadLua** (script_lua_mission.cpp). The .mob's text is parsed (`Script::Parse`: the parser's declarations
  and `fileCount`, later texts' names) and converted (the scheduler's `names`: the Lua names those texts refer to), and
  the `.lua` is loaded in its place into the script's scheduler (`Script::Runner`; an AddMob'd .mob's `.lua` goes into
  the zone's running script beside the texts before it), a UTF-8 byte order mark dropped, its chunk `Maps\<file>`
  (`mobtoml::ScriptLuaName`), its WorldScript started. A file that does not load is error 18 with Lua's
  message (the caller's `Script error: zone1.mob line 0: Maps\zone1.lua:<line>: ...`). A `.mob` text the game's
  parser stops in keeps that error (the caller's report, as before) and leaves the script as the parser does: not
  loaded (no ticks) and the WorldScript not started, so a converted install's `.lua` of it (what came before the
  error) plays as the game (Lost in Astral's bz23k.mob and zone10hunter.mob); a mod's `.lua` for such a `.mob` needs a
  `.mob` that parses. Every later text (an AddMob'd
  mob without a `.lua`, QFinish's quests, execs files) goes through Script::Load to the scheduler as L7's runtime
  conversion.
- **The scheduler.** `Script::Tick` is the scheduler's tick (a Reset inside it drops the scheduler after it:
  `retired`), `ResetZone` the scheduler's, `Reset` drops it (the next load makes a new one). The world's reads of the
  script's globals go through `Script::GlobalGroup` (the Heroes group the server gives its players, world_server.cpp)
  and `Script::GlobalValues` (Rebirth's `listgvar`, the digest): the Lua values under the parser's names (the names
  map), then the globals only a Lua GlobalVars declared.
- **Saves** (lua-scripting.md, "Saves"): the old format's fields hold the scheduler's state as far as they go, the
  Eris state follows the areas; a save keeps running the code it was saved with, whatever the zone's files say now.
- **The record** (L8a, so that a converted install's goldens are the normal install's): the digest's script record
  (harness_digest_image.cpp, `ReadRuns`) and tools/savefile.py describe the script in the game's executor's terms
  (`Script::InstanceViews` / `RunViews`, script.h `ScriptRunView`):
  - the globals by the parser's names with their Lua values, the loops' counters too (L8c: the bindings set the
    counter global a converted loop names as the game's loops do; lua-scripting.md "Loop counters");
  - the instances by definition name, kill flag and values (typed as the definition's parameters);
  - the runs by definition and rule (a WorldScript's: no name, -1), ticks, sleep start, instance and values (a
    scheduler's run keeps its definition, rule and starting values for this: `Run::values`, persisted with it);
    no block id or command index, which Lua has not;
  - in the saves, no program (ours writes no blocks; a definition's pairs are its rules' numbers, which name a run's
    rule), `loaded` without the 2, and the Lua state left out (rule `lua`: Eris writes tables in the order of Lua's
    string hashes, whose seed changes every run).
- **The sandbox** (script_lua.cpp, every ScriptLua): no io, os, debug, package / require, dofile / loadfile; no
  `math.random` / `math.randomseed` (C's rand() is a draw outside the game's: a script draws through `Random`, the
  game's generator, saved with the game); `print` writes a console line; `load` text only (bytecode could break the
  state); `pairs` walks the keys sorted (numbers, strings by bytes, false, true, then the rest in no set order; a
  `__pairs` first), since Lua's own order follows the seeded string hashes.

### Checks

- `ei-convert --lua-check` (the build checks): the sandbox (each removed name, `load` refusing bytecode, the `pairs`
  order, `__pairs`) and a mission from a `.lua` through SaveState and LoadState into another script (the coroutine's
  local count going on the same in both), a `.lua` that does not load as error 18.
- `mod_lua_plain` (vanilla; tools/make_lua_mod.py plain): a mod whose only file but its config is `Maps\zone1.lua`,
  ei-convert's conversion of the install's zone1.mob, through a new game, a save and its load. Its twin `mod_lua_none`
  is the same mod without the file (the .mob converted at its load). Their goldens are the same (L8a, 2026-11-10): the
  digests' 12 records, the loaded save's dump, the console and the decoded saves (`golden.py explain <none run> <plain
  run>` says "the same").
- `mod_lua_changed` (vanilla; make_lua_mod.py changed): the conversion with the mod's lines: two console lines (the
  second the `pairs` order), a definition of the mod's that prints when a count reaches 3 and then fails (`Script
  error: Maps\zone1.lua:<line>: L8 mod: a failing run`), and the WorldScript counting in a local variable into the game
  variable l8.count; after a save and its load the count goes on from its saved value (a restarted coroutine would
  count from 1).
- A converted install (`work.py test <ID> --converted --golden --rebirth`) runs every zone's mission script from its
  ei-convert `.lua`. With the record above and the counters set (L8c, 2026-11-10): 447 runs pass, 381 goldens the
  same, 0 differ (rebirth_flags' `listgvar` line prints `VSS#i#val : object = <CUnit:...>` on Lua too; before L8c it
  printed null there and its goldens differed). Before L8a 186 of 233 and
  129 of 145 differed in the script record and the saves' `commands` alone. Lost in Astral's bz23k.mob and
  zone10hunter.mob, which the parser stops in, keep their error and their WorldScript unstarted on Lua as in the game.
- `mod_lua_addmob` (vanilla; make_trap_mob.py makes `l8b_addmob.mob`, one of zone 1's objects renumbered and no
  script, make_lua_mod.py addmob its `.lua` beside it in the saves folder): `AddMob("..\saves\l8b_addmob.mob")` into
  zone 1's running script (the `.lua` prints `heroes 1, Ogre kept`: two of the zone's globals with their values), the
  `.lua`'s definition and its counting WorldScript go on through a save and its load. On a converted install the
  golden is the same.
- `mod_lua_named` (vanilla; make_lua_mod.py named): `Maps\zone1.mob.toml` (ei-convert's) with `script =
  "zone1_named.lua"` beside both `zone1_named.lua` and `zone1.lua`: the key's file runs (its line on the console),
  zone1.lua's line never comes.
- `mod_lua_broken` (vanilla; make_lua_mod.py broken): a `Maps\zone1.lua` that does not load: `Script error: zone1.mob
  line 0: Maps\zone1.lua:3: <name> or '...' expected near <eof>`, and the game goes on with the zone's script empty;
  the zone's globals are not declared in Lua, so the engine's text adding the party to Heroes fails too (`Script
  error: text2:4: AddObject: argument 1 is no group (nil)`).
- `mod_example` (both installs): the example mod (docs/modding.md) has a `Maps\zone1.lua`, ei-convert's with the mod's
  lines (`examples/example_mod_additions/Maps/zone1.lua`: a console line and a definition whose rule greets the party
  when the leader is near the mod's pig); the digest's records up to the greeting are those it had with the .mob's
  script.
- The four with `--converted`: their goldens the same (2026-11-10).

## Models as glTF

A figure model can be a glTF 2.0 file (MF6, user 2026-11-09). Every original still loads; a `<name>.gltf` or
`<name>.glb` found where the figure's files would be (each place of "Loose files", top first: a folder's file or an
archive's entry) stands for them there (`MappedResource::ModelFile`, resource.cpp). At each place, before its own file, a
request for a `.mod`, `.bon`, `.lnk`, `.fig` or `.anm` ("Animations as glTF") asks the glTF models of that place whose name begins the file's stem
(the longest first): the glTF is read once (src/engine/models/figure_gltf.h, `FromGltf`, on cgltf), made into the files
the game would have read (`Files`), and the one asked for is answered from them, shared like a loose file. So both
loaders (the figure manager's `<name>.mod`/`.bon`, the link loader's set or its `<name>.lnk` and `<name><part>.fig` /
`.bon`) and the item models' `<name>.fig` see the files as before. A `.gltf` comes before a `.glb` of its name; its
buffers (`"uri"`, relative, percent-decoded; data URIs too) are read from the same place. A glTF cgltf cannot read is
logged ("warning: models: ...; not read") and the original is read; what the reader left out is logged too
("warning: models: <file>: <part>: ...").

- The figure: the scene's nodes are the parts, named as the parts are; the link list is the node tree (a child's
  parent its node's parent). A node's mesh is the part's record, one primitive (several are merged, the first's
  material the part's). The node's translation is the part's bone (version 0); the node's rotation and scale are not
  applied (logged).
- The mesh: a glTF vertex per triangle corner. POSITION is corner 0 of the FIG8 morph cube; the other 7 corners are
  morph targets named (mesh `extras.targetNames`, as Blender writes them) `s`, `t`, `st`, `u`, `su`, `tu`, `stu` (the
  corner's coefficient bits, docs/notes/models.md "Link figures"); the older record's 6 versions `s`, `t`, `u`, `su`,
  `tu`. A target's POSITION is the corner's displacement from corner 0. NORMAL and TEXCOORD_0 as the record has them;
  indices as the record's (none: every vertex once, in order).
- Materials: one per material kind and texture kind, named `"<kind> <texture kind>"` ("wood 8", "metal 2"; a kind
  without a file of its own by its number, "25 6"), `extras.ei = {material, kind}`. A part without one is stone, kind 1.
  The figure files name no texture: the game picks the texture by the unit's or object's database record and the
  texture kind, so the glTF has no images.
- Axes: glTF (x, y, z) = game (x, z, -y): a rotation, no mirror (the game is right-handed with z up; the triangles keep
  their winding, counter-clockwise from the outside as the normals say). Blender's importer turns glTF's y up into its
  z up, so Blender shows the game's own coordinates, the right way up. The bounds and bones in `extras` are in the
  game's axes.
- `extras.ei` (ei-convert writes them; the loader takes them only when they agree with the glTF's data):
  - mesh: `format` (`FIG8`, `FIG` with its `magic`, `old`), `unused` (the header's last dword, when not 0), `bounds`
    (`center`, `min`, `max`, `radius` per version), `slots` (an accessor: each vertex's position, normal and texture
    slot, u16), the slots no vertex gives exactly as `positions` / `normals` / `texcoords` patches (a slot accessor and
    a value accessor: the packets' padding lanes, unused slots, a position whose versions a float displacement does not
    give back (about 4% of the shipped values), a normal whose w is not 1 or that is not of unit length: NORMAL holds
    a unit one, as glTF requires, straight up for a zero one), `words` (an accessor; only when they are not the derived
    ones; -1: none), `indices: false` (a record without), `skipped` (the older record's block, a buffer view).
  - node: `bone` (all versions), `linked: false` (a part outside the link list: the unlinked armour pieces), `parent`
    (a parent that is no node).
  - scene: `layout` (`set` or `loose`), `link: false` (a loose figure without a .lnk, a set whose .mod has no link
    list), `bones: false` (a set without a .bon archive), `files` (archive entries and files a glTF cannot hold, carried
    as they are: Lost in Astral's stray link entries in its .mod files, records that do not parse, a .bon that is no
    archive; name, `mod` / `bon` / `folder`, a buffer view).
- Without the extras (another tool's file, or one that changed the mesh so that they no longer agree: a slot's
  vertices with different values, a patch more than the sum's rounding away), the record is built from the glTF's data:
  the slots numbered by value in vertex order (padding lanes copies of the first position and of the last normal, as
  the shipped records), words derived (one per normal slot: its slot in the high half, in the low half the slot or the
  last used one, as every shipped record but 28 of Rebirth's), the bounds fitted as `ClipBounds::Fit` fits them; a FIG8
  record unless the mesh's `format` says otherwise (missing targets: copies of corner 0); a node without `bone` gets 8
  copies of its translation.

### ei-convert's models

ei-convert writes each archive's figure files as `<archive folder>\<figure>.glb` (tools/convert/convert_models.cpp) and
lists them as converted: a set per `.mod` (with its `.bon`); a loose figure per `.lnk`, with the `.fig` and `.bon`
files whose name its name begins (the longest); a `.fig` or `.bon` of no `.lnk` its own figure. A `.lnk` beside a `.mod`
of its name (vanilla's ingm3, which the game never reads: both loaders take the `.mod`) and its parts' files are left as
they are (listed). Each round trip: the files made from the .glb as the game makes them, compared with the originals
(an archive entry by entry, a link list by its names, every record and bone byte for byte).

Counts (2026-11-10, `ei-convert --check`, every round trip holding): vanilla 870 figures (454 sets, 416 loose), 3,651
parts (3,583 FIG8 records, 55 older ones), 1 file carried as it is, 217,252 corners on 154,692 slots, 253,142 triangles;
24.2 MB of originals as 45.7 MB of .glb (a vertex per corner, 8 positions each), read back and made into files in
0.35 s. Rebirth (its mods too): 978 figures (483 sets, 495 loose), 4,639 parts (4,569 FIG8, 55 older), 46 files carried
as they are (Lost in Astral's stray entries, a .bon that is no archive), 306,364 corners on 223,208 slots, 365,451
triangles; 34.6 MB as 62.3 MB, 0.42 s. Loading costs little: the read and the files' making take well under a
millisecond a figure (a converted install's load: "Textures as PNG", "Loading cost").

### Checks

- `ei_unit_check`'s figure checks (tests/unit/figure_gltf.cpp): a set and a loose figure of every record kind (FIG8
  with corners a float displacement cannot hold, FIG, the older record with its skipped block, no indices; padding,
  unused slots, a normal's w, zero and non-unit normals, words of their own, a prism bone, an unlinked part, a part
  without a record, an entry of another kind) come back as the same files; a hand-made .gltf with an external buffer
  and a data URI and no extras is turned to the game's axes, numbered by value, its bounds fitted; lib/json.h.
- `mod_gltf_animation` (below) loads the hero's figure (unhuma) as ei-convert writes it from a mod: the trace has the
  glTF read for unhuma.mod and unhuma.bon (H74 dropped mod_gltf_model, which also scaled it: the same code).
- `ei-convert --check` on both installs, and on the stub data in the Linux suites' workflow.
- `work.py test <ID> --converted --golden` (and `--rebirth-only`): the converted installs have every figure as a .glb.
- The Khronos glTF Validator (the npm package gltf-validator 2.0.0-dev.3.10, run with node on a few exported .glb:
  the hero, trees, rocks, a loose figure): no errors or warnings; its infos are the TEXCOORD_0 it calls unused (no
  material has a texture). Blender was not at hand.

## Worn models

A weapon in hand and the armour worn are parts of every unit figure that can wear them (MF13c): ShowWeapon loads
`WeaponModelParts` (`rh3.club05`, the bow's seven), ArmourPutOn and the unit's first figure `<bone>.armor<nn>`, each by
`Object3D::LoadPart` (the piece from the figure set) and the skeleton's bone of the part's name (`AttachMesh`), which
the set's link list makes. A new item number has no piece and no bone. Its worn model, a glTF named as the item's
model without material and variant (`<code>_<nn>`, `figure_gltf::IsWornModel`: letters, `_`, digits; no shipped
figure or converted file has such a name), holds the parts under root nodes named as the bones they hang from.

The parts join the figure sets at the file level (one path for vanilla and mod parts, nothing in the game's code
changes): `MappedResource::FindFile` answers a `.mod` or `.bon` request through `WornSetFile`, which (once per name,
`wornSets`) reads the set's own `.mod` and `.bon` as FindFile would (a glTF's made file, a folder's, an archive's entry)
and joins every worn model of its sources (`JoinedSet`; the top source's of a name): `figure_gltf::JoinWorn` adds each
worn link whose parent the set links (or just gained) and whose part it does not link, the part in lower case (the
skeleton and the parts match names with their case; the game asks in lower case) under the set's spelling of the
parent, its record to the `.mod` and its offsets to the `.bon` (when the set has one), in place of an entry of the
name. A set nothing joins is read as before (vanilla and Rebirth: no worn model, nothing changes). The joined set is
the same set for every reader: both figure caches (the figure manager's and the link loader's), the skeleton
(`LoadSkeletonLinks`: a bone for the part from the unit's creation), `ArmourPutOn`'s `FindEntry`, the bounds
(`SetUpAnimatedFigure` boxes the new bone too) and the remorph. The `.anm` is not touched: the joined bone has no track
and hangs still from its parent, as the game's clubs hang from `rh3.axe00`.

Texture: the game's weapons and helmets take the unit's second texture (kind 2): the weapon's picture in its left 0.3
(the game's hammers are opaque only there), the helmet's in the middle; NameSkinTextures composes the helmet's
then the weapon's. So a worn weapon's picture is clear outside its strip (the showcase hammer's: 38 of 128 pixels).

Gaps: a worn part is not scaled by the unit's build unless its glTF has the morph targets; the weapon's point
(`baseclubNN`, `WeaponPoint`) is not read for any weapon today (it asks `WeaponBaseFigure` with the packed id).

Checks:
- `ei_unit_check`'s worn checks (tests/unit/figure_gltf.cpp): the names; a club and the tip under it join a set with
  the hand bone (lower case, the set's parent spelling, record and bones), a part the set has keeps its own, a root it
  lacks and a set without the bone join nothing, a second join changes nothing.
- `mod_showcase_worn` (both installs): the hero wears no `rh3.club09` until it takes the hammer, then one
  (`unitpart`, harness.md); the capture `worn`; the trace has `hm_09.glb` read.

## Animations as glTF

A set's animations can be glTF animations (MF7, user 2026-11-09). Every `.anm` still loads; the set's glTF ("Models as
glTF") stands for its `<name>.anm` too when it has animations (or `extras.ei.anm`): `ModelFile` answers the `.anm` from
the files made of the glTF (src/engine/models/figure_gltf_animation.cpp makes the archive the game would have read), so
the figure manager's `FindModel` / `FindAnimation` and the link loader's figure sets read it as before. A glTF without
animations does not stand for the `.anm`: the one below it is read (a mod's model alone keeps the game's animations).
A layer's glTF is read as that layer's own file (`ReadSource` through `platform::ReadLayerFile`): a read through the
path map took the mod's glTF of the name in the game's place, so Lost in Astral's hero (its `unhuma.mod` and `.bon`,
the game's `unhuma.anm`) had no animations on a converted Rebirth install and every Lost in Astral golden differed
(its footprints, the client units). The animations are in the figure's own .glb, so Blender imports a unit with them: each `.anm` entry is a glTF
animation of the name the game gives it (`cidle01`, `uattack02`), its channels the parts' nodes (named as the parts).

- Keys and time: one key a world tick (55 ms at the game's normal speed): key k at k x 0.055 s, LINEAR. A channel of
  other times (another tool's: Blender writes its own frames) is sampled at every tick up to its last key with glTF's
  interpolation (STEP, LINEAR with the game's `Quaternion::Slerp` for rotations, CUBICSPLINE); a key within 0.1 ms of a
  tick is that tick's. How far the game plays is the `.adb`'s frame count, so a longer animation needs its `.adb`
  record's `frames` too ("Records as TOML").
- Rotations: the game turns a child by its key after its parent's turn (`FigureBone::Place`: the part's rotation is
  its key times its parent's, in the figure's frame), a glTF node by its own rotation before its parent's. So a node's
  rotation is its key carried into its parent's frame, `L = P^-1 T P` (P the parent's rotation at that key: the keys'
  product up the tree, a shorter track's last key past its end, the identity for a part without a track), and the
  loader turns it back, `T = P L P^-1`. A root's is its key. Axes as the models': (x, y, z, w) = the game's (x, z, -y)
  and w. On the hero (`cidle01`, `crun01`) the glTF's world rotations are the game's within 1e-6. A key not of unit
  length would be normalised in the glTF (the extras keep it; no shipped key is more than 0.00001 off).
- Positions: a root's track positions are its node's translation channel (they place the figure). A child's the game
  never reads (its bone places it: the node's translation), so they have no channel (the shipped ones are not offsets
  from the parent: the hero's `crun01` moves `bd` 3.5 away from `hp`).
- Vertex frames (222 tracks of 13 figures: wings' membranes, faces, hair; each frame an offset of every position slot)
  are in the extras alone. Morph target weights could hold them only as a target per frame on the mesh, and an
  animated `weights` channel sets every target, the `s`, `t`, `u` corners too, which the game takes from the unit's
  stats. Blender does not show them.
- `extras.ei` of an animation: `tracks` (every track in the archive's order: `name`, the key counts `rotations` and
  `positions`, `frames` and `slots` of its vertex frames), and the accessors `rotations` (every track's keys as the file
  holds them, w first, one track's after another), `positions`, `vertices`. A track's rotations are the extras' when
  its node has no rotation channel or the channel agrees (its keys at the ticks, each within 1e-5 of the rotation the
  extras' key makes, or of its negation), else the channel's, sampled and turned back; a root's positions likewise;
  a child's positions and the vertex frames are the extras'. The parents are decided first: a child's check uses its
  parent's keys as they came out. A track of no node (546 vanilla, 562 Rebirth: parts the figure does not have) is in
  the extras alone; a node's channel no track of the extras names makes a track of the node's name. A glTF that lost
  the extras (Blender's export, `mod_gltf_animation`'s idle) gets the channels' rotations and roots' positions and no
  vertex frames.
- Carried as they are (the scene's `files`, `"in": "anm"`): an `.anm` entry that is no archive of tracks that read,
  or whose tracks have a name twice, and an animation whose glTF animation would have no channel (tracks only of no
  node or without keys); none shipped. The scene's `extras.ei.anm`: the set has an `.anm`. The made `.anm` lists its
  animations by name (the glTF holds those it carries apart).
- Left out (logged): a glTF animation's scale and morph weight channels, an animation without a name.

### ei-convert's animations

A set is its `.mod`, `.bon` and `.anm` (tools/convert/convert_models.cpp); an `.anm` without a `.mod` of its name is
left as it is (none shipped). Each round trip compares the `.anm` made from the .glb with the original entry by entry,
each animation track by track, byte for byte.

Counts (2026-11-10, `ei-convert --check`, every round trip holding): vanilla 51 `.anm`, 969 animations, 22,311 tracks
(546 of no part, 222 with vertex frames), 1,171,646 rotation keys and as many positions, 598,051 frame vertices;
Rebirth (its mods too) 55, 1,007 and 22,825 (562, 222), 1,177,091 keys, 598,051 frame vertices. The .glb files with
them: 111.3 MB for 65.4 MB of originals vanilla (45.7 MB for 24.2 MB without the animations), 128.2 MB for 75.9 MB
Rebirth (62.3 MB for 34.6 MB): every rotation is there twice (the node's and the file's) and the times are shared by
count. Read back and made into files in 0.38 s and 0.46 s (0.35 s and 0.42 s without). In the game the hero's
files are made from its 21.8 MB .glb (179 animations) in about 66 ms, and every model made in
`mod_lost_in_astral_cz_ingos` on converted Rebirth took 0.96 s together (625 makes; the whole load: "Textures as
PNG", "Loading cost").

### Checks

- `ei_unit_check`'s animation checks (tests/unit/figure_gltf.cpp): a set's animations (a root's and children's keys, a
  child with fewer keys than its parent, keys of no length and twice the unit length, vertex frames, a track of no part
  and an unlinked part's, an animation of no channel, one without tracks, an entry that is no archive) come back as
  the same files; a hand-made .gltf with an animation and no extras (keys two ticks apart, STEP, a scale channel) is
  sampled at the ticks, its root's middle key slerped and its child's keys turned after the root's.
- `mod_gltf_animation` (both installs; tools/make_mod_s19d.py anim): the hero's figure as ei-convert writes it with
  its idle `cidle18` twice as slow (its keys' times doubled, its extras dropped) and an `unhuma.adb.toml` giving it 210
  frames (the game's 105). The figure manager's track of `hp` has 211 keys (the game's 106 at the even ticks, slerps
  between), the other idles' as they were (`cidle02`: 96), the hero standing plays `cidle18` past its frame 105
  (`objplayed cidle18 past 105`: to 207 vanilla, 195 Rebirth, in 120 frames), the trace has the glTF read for `unhuma.anm`.
- `ei-convert --check` on both installs, and on the stub data in the Linux suites' workflow.
- `work.py test <ID> --converted --golden` (and `--rebirth-only`): the converted installs have every `.anm` in its .glb.
- The Khronos glTF Validator (gltf-validator 2.0.0-dev.3.10 with node) on the hero (179 animations), unmowi (vertex
  frames), unmodg and unanhoco: no errors or warnings; its infos are the extras' accessors and TEXCOORD_0, which it
  calls unused.

## A mod's config.reg

The game's registry format (src/engine/data/registry.h), or `config.reg.toml` ("Records as TOML"). Sections in Rebirth's
mods:
- `[MOD]`: Title, Author, Version, Date_DMY, URL, AuthorEmail, GameSaves, Single, Multi, ForceShowTutorial: the
  starter's list (the menu's list, MOD3). The four switches are `true` / `false` in config.reg.toml ("Readable
  values").
- `[RES]`: archive name = the mod's archive(s) (string or array), `-` to drop the game's; read by ResOverride
  (mod_folders.cpp) from `ModPath()`, which is the Mod folder. Rebirth's starter's own `AddonPath` (its registry) is not
  read.
- `[CONFIG]`: file name = the mod's file (Lost in Astral: `ai.reg = config\ai.reg`, `movie.ini = config\movie.ini`,
  `music.reg = res\music.reg`, the lights `.ini` files, keyboard.ini, addrbook.ini, tutorial.ini; IronMod:
  `ai.reg`). In every shipped mod each value is where the game folder has the file of that name (`config\`, the
  resources' `res\`), so the mod's layer gives the mod's file; the section itself is not read.
- `[DEV] Plugin`: a DLL that patches game.exe (Lost in Astral's fixshops.plg, IronMod's fixfslpatei.plg), which the
  starter loads; never loaded by our exe. The ones that matter are code, on while the mod names them ("The mods'
  plugins").

Rebirth's own Maps, CameraPatch, MoviesPatch and StreamPatch look in `ModPath()` too; with the layer they find the
same files.

## The mods' plugins

A `.plg` is a DLL the starter loads into game.exe (1.06) when the mod's config.reg names it; it writes bytes into the
code. Read with a disassembler, never loaded. `src/engine/mods/mod_plugins.cpp` reads the mod's config.reg (`ModPath()`) at
each question, so a plugin's change is on exactly while the chosen mod names it.

- Lost in Astral's `fixshops.plg` (`modplugins::FourShopClasses`, used by party_manager.cpp) makes two changes:
  - a new game (`PartyManagerNewGame`) builds the trader class templates as a network game does (`Build(1)`: classes
    1, 3, 4 and 5, each selling items and spells with the default weights) instead of the campaign's `Build(0)`
    (five classes: 1 items only, 2 spells only, 5 zero weights). The mod's bases name classes 1 (OrcC, OrcPyr), 3
    (Trader), 4 (YCF, Zaku) and 5 (Stark), never 2: with the campaign's five its class 1 traders sold no spells and
    class 5 nothing.
  - `PartyManager::Load` builds them again after reading the save: the saved templates and their stock are dropped,
    so a loaded game restocks at the next camp visit.
  Checked by every Lost in Astral scenario's autosave (four templates).
- IronMod's `fixfslpatei.plg`: not supported (below).

### IronMod's fixfslpatei.plg (MOD1b)

How Rebirth loads a plugin (addon.dll): it reads `[DEV] Plugin` from the mod's config.reg whenever the value is not
empty, loads the DLL, calls its `EntryBeforePatch` before addon's own patches and `EntryAfterPatch` after them.
fixfslpatei.plg ("Game patcher plugin for addon.dll" 1.0.0.0, read with Ghidra 12.1.4 headless) is a generic patcher,
not IronMod's own code: its `EntryBeforePatch` makes game.exe's .text (0x401000, 0x33A000 bytes) and .rdata (0x73B000,
0x48000 bytes) writable, reads `patches.txt` from the plugin's own folder (the mod's), and for each line
`<file offset> = <hex bytes>` (`#` starts a comment, spaces are dropped) turns the file offset into an address through
game.exe's section table and copies the bytes there; then it protects the sections again and flushes the instruction
cache. IronMod's patches.txt (mod 1.0.8) has 21 lines, all on strings in game.exe's .data:

- `config\keyboard.ini` (0x787C04) becomes `config\kbdinput.ini`: the key bindings file, read at the interface's
  start and written at its shutdown. Rebirth ships `Config\kbdinput.ini`: its keyboard.ini without the `F5 quicksave`
  and `F8 quickload` lines.
- `saveq` (0x7933AC) becomes `save:`: the one string every quick-save site shares (16 references: the battle screen's,
  the briefing's and the global map's keys, and the save list's titles and reserved names). On Windows `saves\save:`
  cannot be made, so the quick load finds no folder and does nothing, and the quick save's `GameFlow::SaveGame` cannot
  open scenario.sav and starts the main menu's zone: in Rebirth the quick-save key leaves the game for the main menu
  (the play since the last save lost).
- `Activated!` (0x792388) becomes `NoCheating`: only the cheat word's reply changes; the word still unlocks the cheat
  commands and the script lines.
- Eighteen names in the script keyword table's strings (0x78D010 to 0x78D680, `kScriptKeywords`) get a letter or more
  made '0' (GiveMoney becomes GiveM0ne0): SetPlayer, SetDiplomacy, RemoveUnitFromServer, KillUnit, InflictDamage,
  GodMode, GiveUnitSpell, GiveUnitQuestItem, GiveStrength, GiveSkill, GiveQuestItem, GiveMoney, GiveItem,
  GiveIntelligence, GiveDexterity, CastSpellUnit, CastSpellPoint, AddMob. Addon's AddNewScriptCommands copies the
  game's table entries (the name pointers) into its own, so every parse sees the renamed names: Rebirth with IronMod
  knows none of the eighteen, in the console's lines and in the zones' own scripts alike. The game's maps call them in
  57 of their .mob files (zone2.mob's traps call InflictDamage from line 71, its quests GiveQuestItem and
  RemoveUnitFromServer), so there each zone's scripts stop loading at the first such call.

Purpose, taken together: IronMod is a no-cheating, no-quick-save mode. It drops F5/F8 from the bindings, breaks the
quick-save slot's folder name, renames the cheat word's reply and takes the cheat-like script commands away (in Rebirth
from every script, which also stops the game's own zone scripts that call them).

Not supported (user 2026-11-05): our exe does not carry fixfslpatei.plg; IronMod played from our exe runs with its
data but none of these four changes. MOD1b carried them as code and that code was taken out at its merge; the reading
above stays for when the mod matters.

Lost in Astral's fixshops.plg read the same way agrees with MOD2a's reading: it writes a call (`E8` and a rel32 into
the plugin) over 0x4A84F3, the call of the list clear 0x4A9650 near the end of `PartyManager::Load`, to a stub that
makes that call and then `TemplateTable::Build(this + 0x38, 1)`; and the byte 0x4A88AC, the `push 0` before
`PartyManagerNewGame`'s Build call, becomes 1.

## Checks

- `tests/unit/path_layers.cpp`: full paths, the mod's and the game's files for reads and writes, archives and root
  files left out, the saves' folder, merged searches; the game folder is reached through a symbolic link where the
  system makes one (Linux, macOS).
- `mod_lost_in_astral` (Rebirth's data): with Lost in Astral the main menu's zone (ZoneMainMenuNew) and the new game's
  (gz1g, the mod's zonezero) and `config\ai.reg` are the mod's, and the autosave goes to
  `harness_saves\LostInAstral\savea`. On the vanilla data the folder is missing: logged, the game without a mod.
- `mod_heroes` (both installs; tools/make_mp_hero.py `--mod`): a hero prepared in the mod's `mp\` and one in the game's;
  the hero screen (it logs `heroes: <n> listed <files>`) lists only the mod's under the mod, then after a switch to no
  mod only the game's.
- Every other scenario has no mod: the overlay's map with two layers gives the paths MapPath gave before (the goldens
  the same).

## Switching mods

The main menu changes the mod without a restart (MOD3; the user chose a switch in the process, 2026-11-05).
`app::RequestModSwitch(setting)` (the setting as `[game] Mod`'s, empty for none) is taken only in the main menu (its
window on top, no flow request pending) with no network session; at the main loop's next pass (`TakeModSwitch`, before
anything else of the pass) the game stops and starts again with the new mod. The menu's list of mods is below.

### The menu's list (MOD3b)

The main menu has a drop-down list (`UiDropList`, ui-native.md "The drop-down list") at its bottom left, layout
(16, 566)-(256, 582), the native interface's own control, no GDI. It is in the main menu rather than the options window
because the options window is on top of the menu when it is open and a switch is taken only with the menu's window on top
(`ModSwitchAllowed`), and because the mod decides the menu's own zone and music, so the choice belongs where the
change shows. `MainMenu::AddModList` makes it at every `CreateScreen` (a switch makes the menu again) from
`modlist::Scan(app::GameFolder(), platform::ModFolder())`:
- the entries: "No mod" (the game's own files), then each folder or .zip of the game folder's `starter\Mods` with a
  `config.reg` (`platform::ModRoot`: at its root or in its one top folder; "Zip mods"), by name, titled by `[MOD] Title` (the folder's name when it has none; a mod for one kind of game, `Single` 1
  and `Multi` 0 or the reverse, has " (single player)" or " (multiplayer)" after it: Lost in Astral and the other
  Rebirth mods are single player; the shipped "Standard" is Nival's title "Без модов", a mod folder of its own with its
  own saves, not the same as "No mod"). The mod played now that is not in `starter\Mods` is added, titled by its folder.
- no list (and nothing drawn) when no mod is found and none is played: the vanilla look is unchanged.
- the entry of the mod played is the chosen one. A pick (`UiDropList::TakeChanged` in `MainMenu::Update`) calls
  `app::RequestModSwitch(entry.setting)`; the switch writes `[game] Mod` to the settings and `settings::Save` puts it in
  `ei_settings.ini` at once. The setting is the folder relative to the exe's folder when it is below it
  (`starter\Mods\LostInAstral` for an exe dropped into the game folder), else in full (the harness's runs: the exe is in
  the build folder); "No mod" writes an empty `Mod`. A refused request (a network session) puts the list back.
- while the list is open `MainMenu::OnEvent` sends every event to it (the stones under it and Esc do nothing).

Single / Multi only label the entries: a mod is not kept from a multiplayer game by them (the switch itself is offline
only).

### Start and stop

GameMain's game half is two functions that the program's start, its exit and a switch all run:
- `StartGame(openDisplay)`: `GameFlow::Restart` (the zone table, the quest folders and quest text files forgotten, the
  databases "database.res"), `g_logicSettings.Load` (the defaults, then config\ai.reg), both worlds' `Create`, the
  camera's `Init`, the saves, mp (with a mod `saves\<mod>\mp`, made again after each switch) and textures folders, `OpenSound`, `LoadDatabases` (each database emptied, then
  loaded), `g_mapDatabase.LoadAll` (emptied first), then `openDisplay` (the program's start only: the disk-space check,
  the window, the renderer, its gamma and the start movies), the sun (`SceneRenderer::AddDefaultLight`: a light of the
  client world's light factory, so made with each world, no longer by `CreateRenderer`), `SetUpGraphics`,
  `SetUpCursors`, `g_interface.Create` and `StartFirstMap`. The window is made after the files (before: after the
  worlds); the device is still taken last.
- `StopGame()`: the composed textures, `g_interface.Shutdown` (tutorial.ini and keyboard.ini written, the help topics
  forgotten), `g_cursor.Shutdown`, the scene renderer's sun, the device's stage textures and material let go (the
  renderer keeps no pointer into a world: the sun's release after the lights were gone crashed), both worlds' `Destroy`.
  The exit then sets the gamma back, destroys the renderer and saves the options.

A switch, between the two: `app::UseMod` (`platform::SwitchMod`: the old `Mod` is never deleted, other threads may
still map a path through it), then `modfolders::Reset` (the mod's archives closed, its folder, `[RES]` lists and Maps\
listing read again at their next use, the maps folder the game's; after the switch, so a thread that asked the layer in
between, the music stream, does not keep the old mod), the settings' `Mod` written, the loop's clock as at the start.
The renderer, the window, the audio device (a new `SoundManager` closes and opens the output) and the settings stay.

### The loading screen (MOD3c)

A switch draws the game's own loading picture, the progress movie a level's load draws (`ShowLoadingMovie`, progres.bik
from the mod's `Movies\` with Rebirth's MoviesPatch), instead of the menu's last frame standing still: `LoadingStep`
(app/main_loop.cpp) before the stop (step 0, while the worlds still draw), then in `StartGame(..., showLoading)` after the
sound is open (1), the databases (2), the map list (3), the world's graphics (4) and the interface (5); the first
zone's own load steps (8 to 11) follow. Nothing can be drawn between the stop and the new worlds (the draw reads them),
so that part (the stop, `Reset`, the files of the new mod's settings) shows the last picture. Each step opens the movie,
presents its frame and closes it again: an open movie holds a stream of the audio device, which the stop and `OpenSound`
close and open (a movie kept across them crashed in its destructor). `ShowLoadingMovie` takes the window's messages
first (`platform::PumpEvents`: the events stay queued, the system's messages are answered), so a long reload is not
marked hung; the harness's backend does nothing there. The log says `mod: loading screen step N drawn` and, at the end,
`mod: the reload took N ms`: about 125 ms on the test machine (Rebirth's install too), which is the gap in the music (the
menu's zone starts it again). The harness cannot capture a frame inside a pass (its capture hook is in the frame step),
so `mod_switch` and `mod_menu_list` check the log lines (`expectlog`).

Made safe to run again: `WorldClient::Destroy` empties both grids, the dynamic objects, the frame's entries and the
exits (Create starts from none; the destructor freed the second grid's objects before); the lightning noise is read at
every `LightningManager::Create` (the client world's one); `UseResources` keeps its last choice in
`GameFlow::networkResources`; `Interface3D::Shutdown` clears the fade pointer; `WorldServer::Destroy` makes its object
maps fresh (a cleared `lib::HashMap` keeps the buckets it grew to, and the order of its walk with them: on Rebirth's
data the menu's units thought in another order after a switch); `StartGame` sets `g_random` and `g_clientRandom` back to
unseeded, so `World::Create` seeds them (and the CRT's rand) as at the program's start.

### What start-up still does once

`main` (main.cpp): the game folder, the settings, `StartVideo` (the one-instance check, SDL's video, the renderer's device
and pipelines on a thread), EI_Plugin's values. `GameMain`: the static initialisers' work (net and code tables, the
weather noise: computed, no files), the command line, `g_startFolder`, the options and the screen layout, then
`StartGame` with the display. The AutoExec lines (the settings and 2utoexec.cfg in the root) are read at each new game;
no mod changes them.

### The globals by owner

About 480 `g_` names in 450 definitions (render 264, game 180, platform 17, audio 15, lib 4, main.cpp 1); the
other mutable state is the function statics and a few scratch buffers. Keep: stays as it is. Reset: emptied or set
back. Reload: made again from the new mod's files. The list to check when a switch shows the old mod's data.

| Owner | Globals | Switch | Why |
|---|---|---|---|
| Platform: window, events, input, instance lock, log (platform_sdl, other_game, log, main.cpp, present_window, app/app.cpp's confinement and resolution changes) | ~22 | keep | no file of a mod |
| Path map (path_layers.cpp `g_mod`) | 1 | reload | `platform::SwitchMod` |
| Renderer (src/render: device, pipelines, frame, caches of textures, meshes, terrain pages, fonts, shadows, water, weather) | 264 | keep | caches key on game objects and forget them: `ForgetHolder` when a texture holder is freed, meshes and terrain pages checked for staleness and dropped after 600 frames unused |
| The game's draw glue (renderer_scene/sprites/strip/lines, device_draw, figure_part_draw, world_client_draw/lights/shadow_casters/view, effects_weather, particle_manager, lightning's sprites, drawn_motion, camera, camera_trace) | ~50 | keep | per-frame scratch and the drawn view; the scene renderer's sun and the device's bindings let go by `StopGame` |
| Audio device (audio.cpp) | 15 | keep | a new `SoundManager` closes and opens the output again |
| App and loop (app/main_loop.cpp, app/harness_hooks.h, app/movies.cpp, `g_camera`, screen_layout, ui_cursor.h sizes) | ~27 | keep | the camera `Init` again and attached to the new client world; the loop's clock reset |
| Settings, options, EI_Plugin's values, console, crt, random generators | ~19 | keep | `g_random` / `g_clientRandom` unseeded by `StartGame`, seeded again by `World::Create` |
| `g_logicSettings` (config\ai.reg) | 1 | reload | `Load` starts from the defaults |
| Databases (spell_db, item_db, quest_database, ground_print, acks_db, unit_db, the lever table in objects/lever.cpp) and `g_mapDatabase` | 9 | reload | `LoadDatabases` empties each; `LoadAll` empties the map list |
| Mod-folder layer (`g_mapsInMod` and mod_folders.cpp's state) | 1 + 2 | reload | `modfolders::Reset` |
| Worlds (`g_clientWorld`, `g_serverWorld`: every manager and its archives, texts, sounds, music, the lightning noise) | 2 | reload | `Destroy` then `Create` and `SetUpGraphics` |
| Game flow and world state (app/game_flow.cpp: `g_gameFlow`, `g_zoneTable`, `g_saveStream`, the stamps; app/game_flow_briefings.cpp's quest folders and files; world_weather, world.h; party_manager, party, player_client, player_server_handlers) | ~15 | reset | `GameFlow::Restart`; counters may keep counting |
| Interface (`g_interface`, `g_keyActions`, `g_menuRects`, `g_cursor`, texture_load's `g_composed`) | 5 | reload | `Shutdown` / `Create`, the cursor's `Shutdown` / `SetUpCursors`, `ForgetComposedTextures`; menus.reg read again by the figure manager |
| Interface scratch (ui_surface's list, ui_main_menu, ui_objectives, ui_interface, ui_screens.h, `g_keyNames`) | ~6 | keep | |
| AI pools and tables (ai_block_search, ai_events, ai_footprint, ai_path_smooth, ai_refresh, ai_unit_shape), weather noise, fog tiles, animation blends | ~13 | keep | computed, or loaded for each zone |
| Network (net_classes, core/coder, net_messages, net_second_master, net_session, net_server_players, world_guard) | ~25 | keep | the switch is offline only |

### Checks

- `tests/unit/path_layers.cpp`: a switch to no mod, a refused folder, and back.
- `mod_switch` (both installs; the log's loading screen steps 0, 1 and 5 drawn and the reload's line): in the main menu a switch to Lost in Astral (Rebirth's data: the mod's
  ZoneMainMenuNew read; the vanilla data has no such folder, logged, the game started again without a mod), then to
  none, then a new game with a dump. `mod_switch_plain` is the same new game from the start: the two dumps must be equal
  (`python tools/golden.py diff` of the two runs' `dump_newgame.txt`), so starting twice gives the state of one start.
- `mod_menu_list` (both installs; settings-file check on each): on Rebirth's data the list's entries, the list open (a
  frame to look at), a pick of Lost in Astral (the log's `mod: switching to` and `its saves in saves\LostInAstral`),
  the list showing "Lost in Astral (single player)" in the new menu, a pick of "No mod", and Lost in Astral again; the
  run's `ei_settings.ini` after the exit (`settingsfile writable`) is compared with `settings/mod_menu_list.rebirth.ini`
  (`Mod = {install}\starter\Mods\LostInAstral`; the vanilla file has no `Mod`: that data has no list).
- The asan build's suite (harness.md, "AddressSanitizer") for the freed archives and managers.

Risks: a mod-dependent global missed shows the old mod's data after a switch (the table above; the start-twice check
catches state, not stale files); a fatal error in the new mod's files (a missing music.reg) ends the game as it would at
the start.

## The showcase mod

`examples/showcase_mod/` (MF13, user 2026-11-10): a mod made only of our own files, so it lives in the public repo and
releases ship it ready-made (MF12 adds the Mods folder; the package copies the folder whole into `Mods\showcase_mod\`).
An exit from the first island leads to its island: its ground, a stone golem of its own and two smaller shards of it
guarding the path, one quest that gives the mod's spell, Chain Spark, and whose reward is the mod's hammer and boots.
Hand-written: config, texts, databases, the golem's animation list, the map objects, the scripts and the effects. Made
by `tools/make_showcase_mod.py --write` (procedural pixels and boxes from one seeded generator per file, no game file
read; `--check` compares pixels and glTF data, not bytes, since zlib's output may differ between versions): the map
folder's PNGs and `map.toml`, the ground page and minimap pictures, the golem's skin and `.glb`, the item models and
textures, the shards' skin, the spell's pictures and its effects' texture. About 0.5 MB. LICENSE.txt: the files are
ours, under the repo's MIT license.

Each part, its format, and what the formats could not do (each gap a ticket):

| Part | Files | Format | Gap |
|---|---|---|---|
| The zone | `Res\texts\map.toml` (the zone "showcase" on allod gipat, its two exits, and gz1g's merged exit 3 to it), `Maps\showcase\` (map.toml, heights.png, tiles.png), `Maps\showcase.mob.toml`, `Maps\showcase.lua`; `Res\textures\showcase000.png` (the ground page), `showcasemap.png`, `showcasequest.png` (minimap, objectives map); texts' `zone showcase` | zone table TOML, map folder, map objects TOML, Lua, PNG | None (MF13a): a zone of its own beside the game's ("Zones from a mod"). gz1g's exit 3 (63, 134)-(69, 142), east of the landing's ruins, leads to it; the party arrives at the island's exit 1 (49, 133)-(59, 143), which leads back; exit 2 (181, 108)-(193, 120) leads to the base bz1g. No `.mob` and no `.grp`: the AI region graph is built at the load |
| The enemy | `Res\figures\scgolem.glb` (15 boxes, 13 animations), `Res\database\scgolem.adb.toml`, `units.udb.toml` (`[[model]]` "Showcase Golem", `[[unit]]` "ShowcaseGolem"), `Res\textures\scgolem00.png` (the placement's `parent_iname`) | glTF set, records TOML, databases TOML, PNG | None. No sounds: `sound_folder` empty, the animations' `sounds` empty; its footsteps name the game's stone steps. The shards (`[[unit]]` "ShowcaseShard", 4002 and 4003 by the golem) are the golem's model at scale 0.3 in their own skin (`Res\textures\scshard00.png`) |
| The weapon | items.idb.toml `[[weapon]]` "showcase hammer" (club number 9), `Res\figures\initwehm9.glb` (inventory model, loose figure, part `weapon`), `Res\figures\hm_09.glb` (its worn model: `rh3.club09` under `rh3.axe00`, "Worn models"), `Res\redress\unhumahm_09.gr.0.png` (the weapons' strip of the hero's second texture) | databases TOML, glTF loose and worn, PNG | None (MF13c) |
| The armour | `[[armor]]` "showcase boots" (boots number 20), `initarbt20.glb`, `Res\redress\unhumabt_20.le.0.png` (a layer over the hero's 256 x 256 skin: opaque where the feet and shins are) | as the weapon | None: boots, gloves, shirts and trousers are skin layers, no part. Helmets, body armour and leggings take a worn model as the weapon does (none shipped) |
| The spell | spells.sdb.toml `[[spell]]` "spark" (Chain Spark, `script = "chain_spark.lua"`, icon 101), `Res\spells\chain_spark.lua`, `Res\effects\ChainSpark.effect.toml`, `ChainSparkHit.effect.toml`, `ChainSparkShock.effect.toml`, `ChainSparkCast.effect.toml` (the record's `cast_look`), `Res\textures\chainspark.png` (the effects' 16 pictures), `spell0101.png`, `prototype0101.png`, texts' `SPELL spark` | databases TOML, Lua, data effects, PNG | None (MF17e): a scripted spell (items_spells.md, "Scripted spells"). The spark (a missile carrying ChainSpark) strikes, then an arc reaches the nearest enemy within the radius not struck yet and the spark leaps along it 10 ticks later (a timer), each strike 0.6 of the one before (12, 7.2, 4.32), each leaving its victim shocked 40 ticks (the spell's own active effect: OnStats slows as the game's slow, the record's `look` ChainSparkShock) and starting ChainSparkHit on it, up to `target_count`; the last chain's strikes go into the game variables `chain_spark.*`. No sounds of its own: the game's lightning sound |
| The buff | spells.sdb.toml `[[spell]]` "ember" (Ember Brand, `script = "ember_brand.lua"`, `duration` 120, `damage` 5, `look` the game's SphereFire), `Res\spells\ember_brand.lua` | databases TOML, Lua | None (MF17h3, MF17h1a): a curse, the spell's own active effect. OnStats subtracts its value from Strength (a stat no game buff touches); OnEffectTick burns 2 fire every 20 ticks left (`effect:Damage`, no caster); OnEffectApplied / OnEffectEnded say so on the console; the burns and the end into the game variables `ember_brand.*`. No item carries it: `CastSpellUnit("ember", ...)` |
| The quest | `Maps\showcase.lua` (sets `q.showcase.showcase_golem` and its objective `.1`, gives the spell container `spell container[spark]`, watches the golem, `QuestComplete`, `GiveItem`), `quests.qdb.toml` (its experience), texts' `quest showcase_golem` | Lua, databases TOML, texts TOML | None |

Notes from making it:
- A map folder with no `.mpr`: the mod's `Maps\showcase\` makes Rebirth's Maps rule (`SelectMaps`) read the zone's map
  from the mod. The map is named `Showcase` (map.toml `name`), so its ground pages are `showcase000..`. Until MF13a
  the island replaced zone 1 and kept zone1.mob's graph (a mod could not add a zone).
- A map page's tiles count from the page's bottom row up (texture 0 is the bottom-left 64 x 64), and the game draws a
  tile's inner 48 x 48 pixels (8 kept from each edge), so tiles that meet seamlessly repeat their pattern every 48.
- A figure's root is placed by its animations' root translations: the golem's hips are 1.15 above its origin (the
  hero's 0.95); the first try had them at 0 and the golem stood buried to its waist.
- Units a `.mob.toml` places have no script name (`GetObjectByName`, the harness's names): the scenario and the script
  find the golem by its id, and the harness's `objtrace start #<id>` follows a unit by id.
- Inventory models are drawn at their own size (about 2 x 2 fills a slot), unlit (`ItemScreen::DrawItem`: flat
  colour), in the worn piece's texture with a smaller picture at the bottom left of 256 x 256 (`ItemFigureTexture`),
  seen from +z; weapons lie diagonally, head at the top left, and turn about that diagonal (docs/modding.md, "Inventory
  models"). The first boxes (MF13) sampled outside the hammer's picture (white) and drew the boots as lit-less planks;
  MF13e paints each face's light into the textures: the hammer's strip holds HANDLE ... IRON, and the boots' layer
  holds a front view (FRONT, x 200..222 of rows 144..192) and a foot from above (FOOT_TOP, x 208..221 of rows 48..96)
  where no part of unhuma.glb takes the skin (its legs lie there seen from the side, soles at rows 59 and 159).
- A `.mob.toml` placement's `texture` is the figure (the shards' is "scgolem"), its `parent_iname` the skin: a
  placement naming a figure that does not exist ends the game ("Can't load skeleton file").
- Guards wander within their `guard_radius` until they see an enemy: by the time a new game's battle screen is up the
  golem and the shards stood up to 2.5 from their posts. The shards keep a radius of 2, so the Chain Spark's 5 still
  reaches from one to the next.
- A spell given as `spell container[<code>]` goes on a hero in the camp's magic page only with the school's skill
  (`skill` in the record: the school's skill plus intelligence over 25, `HeroCanUseSpell`) and the mana (its `mana`
  within the hero's, about 50 at a new game's start). A new game's hero has no elemental skill, so Chain Spark's
  record has `skill = 0` (MF17e1): the quest's spell is usable on the island at once, and its runes' skills still
  raise the need, as the game's spells' do; the quest giving the skill instead would hand out a skill the game sells
  for experience. A hero's own auto-attack and the
  golem's guard end a scripted chain early when they kill its next victim first: the scenario clears the units' AI
  modes (UMClear).

### Checks

- `mod_showcase` (both installs; tools/make_showcase_mod.py copies the folder into the run's saves): a new game, then
  the island (`zone showcase 0`; the script's line, the quest's variable 1), the hero (god mode) walks to the golem,
  which runs to it and attacks (`objplayed crun`, `uattack01` from the glTF), and breaks it in the fight; the quest is
  done (variable 2, the message log's lines), the party has the hammer at granite's damage (`modweapon showcase hammer
  14.4 21.6`); captures of the fight, the broken golem and the camp's inventory with both items; the trace has each kind
  of file read from the mod (map.toml, the map folder, the `.mob.toml` and `.lua`, the textures). The shards join the
  fight and fall.
- `mod_showcase_spell` (both installs): the quest's spell container put on the new game's hero, its skills and
  attributes as they start, in the camp (the mod's picture read), the hero casts Chain Spark at the golem from the HUD; three strikes, 12 / 7.2 / 4.32 in the game variables
  (and so in the digest), golem, shard 4002, shard 4003; saved after the first strike and loaded: the chain goes on
  from the save to the same strikes. Captures: the spark in flight, the first strike and its arc, the leaps.
- `mod_showcase_buff` (vanilla): a console cast of Ember Brand at shard 4002: its strength lower while it lasts and
  back after, five burns (100, 80, 60, 40, 20 ticks left) that take hit points, "expired"; saved after the second burn
  and loaded: the same five burns and end from the save's effect alone.
- `mod_showcase_long_walk` (vanilla): the island's graph built (`expectlog ai graph: built for 64 x 48 blocks`); the
  hero walks from the landing past the golem's post over the hills to (130, 80), about 96 units; saved on the way
  and loaded: the hero walks there again on the save's graph.

## Mods in multiplayer

Our peers compare their mods when a client joins (MF16): the lobby's login, acceptance and refusal carry a mark with
the mod's title and content hash; a client whose mod is not the host's is refused with a box naming both, and the
host's players get a line (network.md, "Mods", for the wire, the refusals and why original clients are refused by a
server that plays a mod).

- **The title**: config.reg's (or config.reg.toml's) `[MOD] Title`, else the mod's name (its folder's last part); empty
  without a mod. `modidentity::Played` makes it with the hash at the first call for the mod's folder and keeps both for
  the program (a mod changes only in the main menu, never in a network session); the log names them with the times
  ("mod: \"Showcase mod\", content hash ... over N files of M MB (made in S s, waited W s)").
- **The content hash** (`modhash::ContentHash`): FNV-1a 64 over the counted files, ordered by their lower-case paths
  below the mod's folder, each its path, a zero byte, its size and its CRC-32. Counted (`modhash::Counted`) are the files
  that decide the game's course: config.reg / config.reg.toml (the archives it names, the plugins), everything below
  `Maps\` (maps, map folders, .mob and .mob.toml, scripts as .lua, quest files), `Config\`'s registries (`*.reg`,
  `*.reg.toml`: ai.reg) and below `Res\` the database and texts archives and their loose folders (`database*`,
  `texts*`: records, databases, the zone table map.txt). Not counted: what only looks or sounds (textures, figures and
  their animations, redress, menus, sounds, speech, music, movies, cameras, streams: the world is the server's, so a
  peer's own copy of these changes what it shows, not the game), the per-user `.ini` files the game writes into
  `Config\` (tutorial.ini, addrbook.ini) and the notes. The data effects (`Res\effects\`, effects.md "Data
  effects") count: their `[server]` block decides when the world's effect objects end. So do the spell scripts
  (`Res\spells\`, items_spells.md "Scripted spells"), which the server runs.
- **Cheap**: a zip mod's files are not unpacked, their CRC-32 is the zip's own (`platform::LayerFileCrc32`, the zip's
  list of files), so a mod hashes the same as a folder or as its zip, and Rebirth's LostInAstral.zip costs only its
  list. A folder's counted files are read once, on a thread of its own begun by the main menu's Multiplayer button
  (`modidentity::Prepare`), so the hero screen's time covers it; `Played` waits for it, or hashes on the spot when
  nothing began it. Lost in Astral's folder (226 files, 157.5 MB, nearly all maps): 0.41 s with the files cached, 4.3 s
  from a cold disk; the showcase mod 0.01 s.
- Both sides compare title and hash: another version of a mod (same title, other files) is refused as another mod is,
  with its own text.

### Checks

- `tests/unit/mod_hash.cpp` (ei_unit_check, the POSIX port check): the counted paths, a folder and a zip of it hashing
  the same, an uncounted file's change keeping the hash and a counted one's changing it.
- `net_pair_mod_same` (showcase on both: admitted, the acceptance names the host's mod), `net_pair_mod_differ` (the
  peer's copy one Lua line longer: refused with the version box; then the host as the original game, whose unmarked
  acceptance the modded peer does not take), `net_pair_mod_none` (the peer without a mod refused, and as an original
  client refused too), `net_pair_mod_list` (the peer without a mod lists the host's mod from its info answer, the row
  marked as one it cannot join; then the host as the original game, joinable), `net_pair_mod_effect` (a data effect
  started by the host's server shown on the peer; effects.md, "In multiplayer and saves"), `net_pair_mod_spell` (a
  scripted spell the host's server casts, its bolt and its blow, the target's hit points, in the peer's client;
  items_spells.md, "Scripted spells");
  `tools/make_mod_pair.py` copies each game's mod (the peer's into the run folder's `peer\harness_saves`; `spell`: the
  showcase with that spell added).

## Open questions

- Other per-user files still shared by every mod (MOD13's audit, nothing in them can break a game): `config\addrbook.ini`
  (the connection screen's addresses), `config\keyboard.ini`, `config\tutorial.ini` (help topics seen): a write goes to
  the game's `config\` unless the mod ships the file (a zip mod's go to `saves\<mod>\files\`), and `textures\` (the
  user's loose .bmp textures over the archived ones: meant for every mod). Say so if a mod should have its own.

- `[MOD] Single` / `Multi`: the menu's list shows them as a label only; whether Rebirth's starter hides or refuses a
  mod in the other kind of game is not known.

## Lost in Astral played (MOD2)

What the expansion's data asks of the game that the base game's never did, and how each is checked:
- The main menu's camera: the mod's `Camera\mainmenu.cam` places it by a first key whose rotation is 1.16 long (the
  game's own file has unit rotations). The game draws it as it is; the drawn view between ticks once undid it by its
  length squared and zoomed the menu in and out on every frame (renderer.md section 14, "Rotations of other lengths";
  scenario `mod_lost_in_astral_menu_camera`).
- Its campaign: map.txt (texts.res) names the allods Jigran (the start), Suslanger, Gipat (gipat2) and Ingos, and the
  game zones gz1g (zonezero), gz1h..gz5h, gz7g..gz16g, gz21k..gz26k, gz31j..gz36j with their caves, bases (bz*) and
  cut-scene bases (cz*). Every keyword its map.txt uses (`#cage`, `#restrict`, `#deployangle`, `#camera`, ...) the
  map database reads; every function its scripts call is in the keyword table (the game's and Rebirth's
  AddNewScriptCommands); its database.res (units, items, quests, levers, spells, perks, acks) stands in for the
  game's file by file (rebirth.md, "Resources and paths").
- The opening (scenario `mod_lost_in_astral_start`): the new game in gz1g, the camp bz1r and its briefing (the mod's
  texts and speech), the quest q1r with its areas, a chest lever, the portal lever that takes the quest item
  TeleportKey00 (a lock by id: the key from the quick item panel; a plain click does not open it), QuestComplete, the
  movie teleprt1.bik from the mod's Movies\ and the change to the next allod's base bz1h.
- Every zone of the campaign is entered and its scripts run for two seconds (`mod_lost_in_astral_suslanger`, `_gipat`,
  `_ingos`, `_jigran`). A zone's mission script that does not load is reported on the console (`Script error:
  <zone>.mob line <n>: <error>`; the original said nothing and played on without the scripts after the error):
  bz23k's stops at line 267, a call of the never declared script `BuyHaburuMain#2#1#0` (the mod's error; the scripts
  before it stay).
- The cut-scene bases (cz*) move story people ("merc2" and others) who join the party on the way: entered without
  them, their briefings never start. Played from the story's state (`mod_lost_in_astral_cz_suslanger`, `_cz_ingos`
  and `_cz_ingos_end`, `_cz_jigran`): the party the story has there, made by the script calls the mod's zones use (`CreateParty`,
  `AddUnitToParty(0, "<party>::<member>", "<unit>")`, `SetCurrentParty`, `RedeployParty`; a member's unit is named
  by the member: "merc2" whatever its prototype). The parties: FPrison (Hero1, merc2) on Suslanger, Gipat (Hero3,
  merc2g as "merc2") on Gipat and Ingos, Jigran1 (Hero3 alone, `CopyStats` from Gipat's hero) on Jigran, Jigran3
  for cz4j; cz2j and cz3j make Jigran2 and Jigran5 themselves. Each base's briefing, read to the end, leaves for the
  next zone (`LeaveToZone` from its briefing check, which the harness's `talk` sees as the base left); cz3j's leads to
  gz35j, where merc2 dies in the acid column as the story has it.
- The gz1d2 lift (MOD9, R64; zone1dun2.mob, the third map, Suslanger's caves): the party enters on an upper floor (z
  about 33.5) and goes on in the lower cave (z about 4); a lift between them, the static stbr6 2240358, sits in a pit
  under a fixed landing (stbr6 1014204, its deck at 33.5, covering the lift's centre and half its deck). The script
  moves the lift with `SetCP` by `GetZ` (the cell's level, the landing's deck, plus the object's z: 33.5254 + z, a
  constant the script's offsets assume) between z -0.3 (the pit's floor) and 28.654 (level with the landing): the call
  lever 1369841 on the landing brings it up once (Lift#01 ends itself), the lever on its deck 1357456 takes it down,
  338779 (also on the deck) up again; each is enabled only at its end of the way. In the AI map a bridge object stands
  on the ground saved under the bridges (`CollectFootprint`), the pit's floor here, so the lift's deck cells follow it
  and a unit standing on the part of the deck outside the landing rides with it (GetZ from 33.5 to 4.2); the cells
  the landing covers stay at its height. The trap was the pick: the original's (00565E40) sorts the objects under the
  cursor by their origins, and from half the headings round (the user's save: 309) the lift's origin is nearer than
  the lever's while its deck covers the lever's whole rectangle, so a click on the lever was a walk onto the deck;
  turning the camera round made the lever clickable there. Ours sorts them by the depth of each one's surface drawn
  under the cursor (R64, the user's choice; renderer.md section 3, "Picking"), so the lever drawn on the deck wins
  from every heading, with no special case for platforms. Scenario `mod_lost_in_astral_elevator` (the user's save,
  tests/saves/lia_elevator, without its picture): the lever clicked from the save's camera, the ride down, the walk
  on in the lower cave. A lever needs exactly one party member selected, as everywhere.
- The prison party FPrison (Hero1, merc2: the mod's prototypes name no weapon) is unarmed from gz1d2 on, until a weapon
  is equipped. The original's key for an unarmed unit finds no body search animation, so the original plays none; ours
  plays the melee weapons' search there (MOD12, the user's choice; combat.md, "Animation keys";
  `mod_lost_in_astral_unarmed_search` on the user's save, tests/saves/lia_prison).
- The user's save in gz1h saved paused (tests/saves/lia_astral_paused: its info, mission, scenario and visuals only)
  is loaded by `mod_lost_in_astral_first_pose` (R65; renderer.md section 14, "Animation blends"): no blend from a
  rest pose on the load's first frames, nor in the main menu's after Esc > main menu.
- cz3h and cz4h have no mob in the mod or the game and are reached by nothing: cz3h's exit is commented out in
  zone2's script and cz4h's (zone2's VCheck#6#1) waits for `q24h` equal to 1 and to 0 at once.
- The later quests (`mod_lost_in_astral_gipat_quests`, `_gipat_necropolis`): gz11g's q11g (areas), gz12g's q27g (the
  broken palantir and three key levers), gz13g's q17g (a chest's key on three key levers, opened from the quick
  panel as a player does) and gz14g's q28g (a lever's golem centre key on two key levers), each to its end and the
  briefing or exit it opens. A key lever's lock (the lever record's id) is the quest item's index in items.idb
  (TeleportKey00 2, AmbushKey00 38, GolemCenterKey00 39, PalantirBroken00 44). Every key lever is opened from the
  quick panel's key, as a player does.
- A teleport for a quest's area (`SetCPFast`) can put the hero on a cell he cannot stand on (inside the stone around
  gz12g's palantir lever 4523 at (110, 119), beside gz14g's lever 42 at (160, 94)): the cell is not passable for his
  size, so `FindPath` from there finds a way only to a spot its local search reaches (lever 42's) and none further: a
  use order's spots all fail (`FindUseSpot`: NoPath, the order dropped) or a spotless lever's way (1712) is to stand
  still. A player never stands there, so this is the scenario's doing, not the game's
  or the mod's (MOD2b): put the hero where the walk can start ((110.2, 116.7) still sets q27g.2). A lever's use spots
  can also be on the far side of a wall: gz14g's gate lever 57 (radius 5.6) has one spot, west of it at (177.9, 46.0),
  reached from outside the enclosure.
- The ending (`mod_lost_in_astral_ending`): gz36j (zonefinal.mob) from cz4j, its four levers of q22j, the last enemy
  (object 666666, which the zone heals until the step q22j.5 is done) killed, and `LeaveToZone(0, "EndofGame", ...)`:
  the campaign's end mode, the credits screen ("titles").
- Leftovers of the game's map.txt in the mod's, which no exit or script reaches: gz17g names the game's zone17.mob, one
  of whose units carries `necrolabkey00`, an item the mod's database lacks (Rebirth's FixCreateItem gives no item, now
  logged as Rebirth's addon.log did, `GiveItem: can't find prototype ...`; the unit record adds no carried item for
  it, where both games crashed); bz15g and mov1 name mobs (bz15g.mob, 1mov.mob) that neither the mod nor the game has,
  so they load nowhere (entering one leaves the world broken in both games).
- The mod's `fixshops.plg` (its shops' patch) is code ("The mods' plugins").
- On Linux CI the stub data keeps the Rebirth mods whole (linux.md, "The stub data"): starter\Mods\LostInAstral's
  Camera, Config, Maps, Movies, Res, Stream, config.reg, fixshops.plg and readme.txt are all in the pack, so the
  mod's scenarios run there as on Windows.
- "Very dangerous" enemies from the start (H63): the mod's NPC record "Human Hero" has experience 1 (the base game's
  271), so the leader's level is round(ln(1/200) / ln(1.61)) = -11 against 1 for the first map's plain monsters
  (zonezero Rat, Scrab, Skeleton (8 HP), Toad; Succub 13, Fighters 15-20); the panel's gap is 12 (consider_more12) and
  the ack fires over 5. Our `Unit::Level`, `UnitClientSpecific::Level` and the panel arithmetic read the same fields as
  the original (the NPC experience goes into the stats, `Pack` rounds it into the params the client level reads); this
  is the mod's data. The hero's experience grows with play, so the gap closes.
