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
| `src/game/models/texture_png.h`, `.cpp`; `third_party/stb/` | textures as PNG and back ("Textures as PNG"): `ToPng`, `FromPng`, `ReadLevel`; stb_image, stb_image_write, stb_dxt |
| `src/game/data/resource.cpp` | `MappedResource::PngTexture`: an .mmp's PNG made into it where it is found |
| `tools/convert/convert_textures.h`, `.cpp` | ei-convert's textures: each as PNG, its round trip measured |
| `tests/unit/texture_png.cpp` | the unit check (ei_unit_check, the POSIX port check) |
| `tests/scenarios/mod_png_texture.txt` | a mod's PNG terrain pages seen on the ground |

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
- The game folder's `saves\` is `saves` (the folder made at the first write). A path already in the saves' folder or
  the written top layer is not mapped again.
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

The harness's map (harness.cpp, `Layers`): the run's overlay (`harness_saves`) on top (owns `mp\` and the "hidefile"
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
| database.res's `.adb` | `Res\database\<name>.adb.toml` | name, start, length, end, `[[animation]]` (name, index, key, flags, chance, frames, stride, arrow, steps, moment, sound_frame, sounds) | the header's keys given; an animation replaces the first of its name, or is appended |
| `acks.db` (database.res) | `Res\database\acks.db.toml` | `[[unit]]`, `[[model]]`, `[[extra]]`: name and the speech lists by ack kind (AckKind in snake case: `selected`, `move`, ..., `bored` with `environment`, `scenario` the named ones), each speech `{ sound, chance, text, name }` | a record replaces the first of its name in its table, or is appended |
| `prints.db` (database.res, databaselmp.res) | `Res\database\prints.db.toml` | `[[blood]]`, `[[footprint]]`, `[[scorch]]`: name, `dry` / `wet` = `{ alpha, life, lifetime }` (a footprint's `unread`: chunk 10) | as acks.db (a table's index is the ground's surface class) |
| texts.res's, textslmp.res's entries | `Res\texts\texts.toml`, `Res\textslmp\texts.toml`: one per archive | `"<entry name>" = "<text>"` (quoted keys, multi-line text as `"""`, CR kept as `\r`) | per entry: a folder's own file of the name, then its texts.toml, in the archive's order of places; so a mod's texts.toml holds only the strings it changes |
| `Camera\<name>.cam` | `Camera\<name>.cam.toml` | `[[key]]`: time, position `[x, y, z]`, rotation `[w, x, y, z]` | the top TOML is the whole path |

Merging (`FindRecordFile` / `MappedResource::FindRecord`, resource.h; `MergeTomls`): the TOML files of every place above
the first place that has the original, and that place's own, are read; the bottom one is merged first over the original,
so the top one (the mod's) wins. A TOML file with no original anywhere builds the records alone. One that does not parse
is logged ("warning: records: ...") and left out; an unknown key is logged with the file. A merge never removes a
record: a mod replaces one, or adds one. A TOML file with the top-level key `replace = true` is the whole file: the
search ends there, nothing below it (its place's original, lower TOML files, the game's file) is read, as a mod's
original hides the game's file. ei-convert writes it in every record file it converts, so a converted copy plays as
the original: Lost in Astral's own `.adb`, `ai.reg` and the like hide the game's rather than merging over them (without
it mod_lost_in_astral_start's units differed). A mod's file of only the records it changes leaves it out.

What stays binary: the quest zones' `quest.reg` (a `.mq`'s, read as bytes for the briefings: MF9), the other databases
(`.idb`, `.sdb`, `.udb`, ...: MF4-), the maps.

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
  that changes "string quit" and an `acks.db.toml` that replaces Haburu's record; the interface's text and the acks
  database the units speak from have the mod's (`modtext`, `modack`).
- `ei-convert --check` on both installs (all round trips hold), and on the stub data in the Linux suites' workflow.
- `work.py test <ID> --converted --golden --rebirth`: every golden the same.

## Textures as PNG

A texture can be a PNG (MF4, user 2026-11-09). Every .mmp still loads; `<name>.png` found where `<name>.mmp` would be
(each place of "Loose files", top first: a folder's file or an archive's entry) stands for it there: at each place the
PNG is asked before the .mmp, so a mod's PNG hides the game's .mmp and a converted copy's PNG its removed one
(`MappedResource::PngTexture`, resource.cpp). The PNG is made into the .mmp file the game would have read
(src/game/models/texture_png.h, `FromPng`): its header, its extra block, every level. So every reader of an .mmp sees one
as before: the texture loads, the item and inventory pictures, the cursors, the composed skins' layers, the terrain's
`HasFile`. A made file is shared like a loose one (by its name, freed at its last close). A PNG that stb_image cannot
read, or whose settings name a format it does not make, is logged ("warning: textures: ...") and the .mmp is read.

- Settings: what is not pixels (the format, the header's channel masks, the level count, the extra block) is in the
  PNG's `tEXt` chunk "ei-texture", TOML: `format = "DXT1"`, `channels = "1555"`, `levels = 5` (not for PNT3),
  `extra = "<hex>"` (only when there is one; no shipped archive texture has one). ei-convert writes it. A PNG without
  the chunk (an image editor drops it) takes them from the .mmp it stands for: in its own place or the first below it.
  A PNG with neither is a new texture: A8R8G8B8, every level down to 1 x 1. A PNG of another size than the .mmp keeps
  the .mmp's settings, its levels at most its own full chain.
- The PNG: any kind stb_image reads (palette, grey, grey and alpha, RGB, RGBA; 16 bits a channel taken to 8 by the
  high byte; gamma and colour profiles not applied), as 8-bit RGBA with straight alpha. ei-convert writes RGB when
  every texel is opaque, else RGBA, deflated by miniz.
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
| PNT3 | 2,838 / 0 / one level / 1.3 s | 3,511 / 0, 499 texels in 44 files clamped (colour above alpha) / one level / 2.3 s |
| DXT1 | 617 / 35,597 of 93.1 M, by 3 / 14.13, 5.75 (13 with shipped holes) / 5.8 s | 990 / 170.6 M of 1,363 M, by 33, worst file's mean 0.28 / 14.11, 18.64 (2) / 50.7 s |
| DXT3 | 15 / 3,125 of 0.48 M, by 3 / 17.45 (arrow01), 2.05 | 10 / 17,335 of 1.44 M, by 8 / 3.55, 0.51 |
| DXT5 | none | 58 / 1.72 M of 40.8 M, by 18, worst mean 0.15 / 10.38, 7.34 / 1.5 s |

The shipped DXT blocks of vanilla nearly all come back exactly (their ends are among their texels). Rebirth's large
DXT1 and DXT5 textures were compressed by a tool whose ends are not, so most of their blocks are stb_dxt's again: close
(the worst file's mean 0.28 a channel) but not the same, and slower to make (50 s for Rebirth's 990 DXT1 files in one
thread: a converted Rebirth install loads its large textures visibly slower). The stub data (`build\stub`, the Linux
workflow's) gives the same kinds of counts, every round trip holding.

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
  camera's `Init`, the saves, mp and textures folders, `OpenSound`, `LoadDatabases` (each database emptied, then
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
