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
| `src/game/mods/mod_list.h`, `.cpp` | the main menu's list of mods: `Scan` (starter\Mods\*\config.reg), `IndexOf`, `SettingOf` |
| `src/game/ui/ui_main_menu.cpp` | `MainMenu::AddModList` (the list's drop-down control) and the pick's `RequestModSwitch` (in `Update`) |
| `src/game/mods/mod_folders.cpp` | Rebirth's mod-folder patches (rebirth.md, "Resources and paths"), whenever a mod is played: `ModPath()` is the Mod folder, `Active()` whether there is one; `Reset` at a switch |
| `src/game/mods/mod_plugins.h`, `.cpp` | the mods' plugins carried as code ("The mods' plugins"): `modplugins::FourShopClasses` |
| `tests/verify/harness.cpp` | `Layers` / `MapPath` / `MapSearches`: the run's overlay as the map's top layer |
| `tests/unit/path_layers.cpp` | the unit check (ei_unit_check, the POSIX port check) |
| `tests/scenarios/mod_lost_in_astral.txt` | a new game in Lost in Astral (Rebirth's data) |
| `tests/scenarios/mod_switch.txt`, `mod_switch_plain.txt` | a switch to Lost in Astral and back, then a new game; the same new game from the start |
| `tests/scenarios/mod_menu_list.txt` | the menu's list picked through the UI, and the settings file written |
| `src/game/data/resource.h`, `.cpp` | the archives and the loose files over them ("Loose files"): `MappedResource::Sources`, `LooseFolderOf`, `ResourceExists` |
| `tools/unpack_install.py` | an install's archives unpacked into their folders; run_all.py `--unpacked` |
| `tests/scenarios/mod_loose_files.txt` | loose files of a mod over its archives and the game's |
| `src/platform/zip_folders.h`, `.cpp` | a .zip read as a folder ("Zip mods"): `InZip`, `ZipFile`, `SearchZip`, on miniz (third_party/miniz) |
| `tests/scenarios/mod_zip_archives.txt`, `mod_zip_loose_files.txt`, `mod_zip_menu.txt` | zip mods: rebirth_mod_archives' and mod_loose_files' mods zipped, the menu's list finding one |
| `src/lib/toml.h`, `.cpp`; `third_party/tomlc17/` | TOML read (tomlc17) and written ("Records as TOML") |
| `src/game/data/resource.h`, `.cpp` | `RecordFile`, `FindRecordFile`, `MappedResource::FindRecord` / `FindText`, `MergeTomls` |
| `tools/convert/convert.cpp` | ei-convert: an install's records as TOML, each round trip checked |
| `tools/convert_install.py` | an unpacked copy converted; run_all.py `--converted` |
| `tests/scenarios/mod_toml_records.txt` | a mod's TOML text and record seen by the game |
| `src/game/data/database_toml.h`, `.cpp` | the game databases as TOML ("Databases as TOML"): field lists, `WriteRecords`, `MergeRecords` |
| `tools/convert/convert_databases.h`, `.cpp` | ei-convert's databases: each round trip, compared by the records' own serialisers |
| `tests/scenarios/mod_toml_databases.txt` | a mod's items.idb.toml: a weapon changed and one added, given in a new game |

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

`textures.res : foo.mmp` is looked for in this order (`MappedResource::Sources`, src/game/data/resource.cpp):
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
- The zone table lists the quest zones (`IsQuestFile`, map/folder.h): each `.mq`, and each `Maps\<zone>\` folder holding
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
folder alike. `mod_menu_list`'s expected settings file names the registry snapshot's `AddonPath` as it is (not the
run's install folder: the snapshot pins it).

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
- `mod_zip_archives` (either install; make_mod_s19d.py archives --zip s19d_mod.zip): rebirth_mod_archives' mod as a zip
  of files: the same answers and the same goldens as rebirth_mod_archives (its console, digest and saves files equal).
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
| database.res's `.adb` | `Res\database\<name>.adb.toml` | name, start, length, end, `[[animation]]` (name, index, key, flags, chance, frames, stride, arrow, steps, moment, sound_frame, sounds) | the header's keys given; an animation's fields given over the first of its name that the file has not merged yet, or it is appended ("Merging by field") |
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

The game databases (units.udb, items.idb, ...) can be TOML too: "Databases as TOML". What stays binary: the quest
zones' `quest.reg` (a `.mq`'s, read as bytes for the briefings: MF9), the maps.

### Merging by field

Every record file of these tables (acks.db, prints.db, the .adb animations, and the game databases of "Databases as
TOML") merges one way (user 2026-11-10; the shared code is `dbtoml::MergeRecords`, src/game/data/database_toml.h): a TOML
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
- Unsigned flags and masks are written in hexadecimal (`class_mask = 0x1`); a perk's required stats are floats
  (`required_strength`, held as their bits), its worth exponent signed.

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

## A mod's config.reg

The game's registry format (src/game/data/registry.h), or `config.reg.toml` ("Records as TOML"). Sections in Rebirth's
mods:
- `[MOD]`: Title, Author, Version, Date_DMY, URL, AuthorEmail, GameSaves, Single, Multi, ForceShowTutorial: the
  starter's list (the menu's list, MOD3).
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
code. Read with a disassembler, never loaded. `src/game/mods/mod_plugins.cpp` reads the mod's config.reg (`ModPath()`) at
each question, so a plugin's change is on exactly while the chosen mod names it.

- Lost in Astral's `fixshops.plg` (`modplugins::FourShopClasses`, used by party_manager.cpp) makes two changes:
  - a new game (`PartyManagerNewGame`) builds the trader class templates as a network game does (`Build(1)`: classes
    1, 3, 4 and 5, each selling items and spells with the default weights) instead of the campaign's `Build(0)`
    (five classes: 1 items only, 2 spells only, 5 zero weights). The mod's bases name classes 1 (OrcC, OrcPyr), 3
    (Trader), 4 (YCF, Zaku) and 5 (Stark), never 2: with the campaign's five its class 1 traders sold no spells and
    class 5 nothing.
  - `PartyManager::Load` builds them again after reading the save: the saved templates and their stock are dropped,
    so a loaded game restocks at the next camp visit.
  Checked by `mod_lost_in_astral_shops` (the camp of classes 1 and 5, a quick save, its load, the camp again, a
  second quick save: the saves golden holds the four templates and the stock made after the load) and by every
  Lost in Astral scenario's autosave (four templates).
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
  mod only the game's. `rebirth_mod_zone_table` hosts a game with a mod: its hero is the mod's (`--mod s19d_mod`).
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
  them, their briefings never start. Played from the story's state (`mod_lost_in_astral_cz_suslanger`, `_cz_ingos`,
  `_cz_jigran`): the party the story has there, made by the script calls the mod's zones use (`CreateParty`,
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
