# Mods

Rebirth's mods (its install's `starter\Mods\<mod>`: Lost in Astral, IronMod, AllHeroes, Standard) played from our exe:
the settings choose a mod folder, whose files then stand over the game folder's. Tickets MOD1 (the overlay), MOD2
(Lost in Astral played through), MOD3 (the menu's choice, without a restart).

## Files

| File | What |
|---|---|
| `src/platform/path_layers.h`, `.cpp` | the layered path map (`PathLayers`, `PathLayer`), `FullGamePath`, `PathBelow`; the mod (`platform::UseMod`, `ModFolder`, `CurrentMod`) |
| `src/platform/seams.cpp` | the game's `SystemPath` / `SystemSearches`: the mod's map, the path itself without a mod |
| `src/platform/files.cpp` | `FolderEntries`: each layer's search (the backends' `SearchFolder`) merged |
| `src/main.cpp` | reads `[game] Mod` after the Rebirth profile, calls `platform::UseMod` |
| `src/game/mod_folders.cpp` | Rebirth's mod-folder patches (rebirth.md, "Resources and paths"): `ModPath()` is the Mod folder, else `[rebirth] AddonPath` |
| `tests/verify/harness.cpp` | `Layers` / `MapPath` / `MapSearches`: the run's overlay as the map's top layer |
| `tests/unit/path_layers.cpp` | the unit check (ei_unit_check, the POSIX port check) |
| `tests/scenarios/mod_lost_in_astral.txt` | a new game in Lost in Astral (Rebirth's data) |

## Choosing a mod

`ei_settings.ini`, `[game] Mod = starter\Mods\LostInAstral`: the mod's folder relative to the exe's folder (the game
folder for a dropped-in exe; on macOS the app's MacOS folder, so give it in full there), or in full. Read once at the
start, after the Rebirth profile (the settings' first use writes the profile's defaults). A folder that is not there
is logged ("mod: no folder ...") and the game runs without a mod. The log names the mod and its saves' folder.

## The layered path map

Every file and folder call of the platform takes its path from `SystemPath(path, use)` (platform.md, "Files and the
harness's overlay"); with a mod, or in a harness run, that is a `PathLayers` map:
- `bases`: the game folder in full (the harness adds the settings' data folder when it differs). A path is made full
  lexically (`FullGamePath`: the current folder for a relative one, `.` and `..` folded, `/` as `\`), and only paths
  below a base are mapped; every other path is the system's as it is.
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

The saves of a mod are `<game folder>\saves\<mod folder's name>\` (the game's `saves\save*` search does not list it
unless the name starts with "save"), never mixed with the game's. `[MOD] GameSaves` is not read: every mod keeps its
own.

### The harness

The harness's map (harness.cpp, `Layers`): the run's overlay (`harness_saves`) on top (owns `mp\` and the "hidefile"
files; writes go there, copied up unless a log or over 16 MB), the pinned key bindings and address book
(harness_settings.h) below it, the mod's layer under those; the saves `harness_saves\` (with a mod
`harness_saves\<mod>\`). Around the map MapPath keeps its own rules: `config\movie.ini` and recorded play's movies
read as missing, `config\tutorial.ini` in `harness_saves\`, a write left in an install refused, the traces. The
scenario line `mod <folder>` answers `Mod` in the run's settings file with the folder in full (relative to the game
folder); the snapshot (config/harness/registry.reg) marks `Mod` absent otherwise.

## A mod's config.reg

The game's registry format (src/game/registry.h). Sections in Rebirth's mods:
- `[MOD]`: Title, Author, Version, Date_DMY, URL, AuthorEmail, GameSaves, Single, Multi, ForceShowTutorial: the
  starter's list (the menu's list, MOD3).
- `[RES]`: archive name = the mod's archive(s) (string or array), `-` to drop the game's; read by ResOverride
  (mod_folders.cpp) from `ModPath()`, which is the Mod folder.
- `[CONFIG]`: file name = the mod's file (Lost in Astral: `ai.reg = config\ai.reg`, `movie.ini = config\movie.ini`,
  `music.reg = res\music.reg`, the lights `.ini` files, keyboard.ini, addrbook.ini, tutorial.ini; IronMod:
  `ai.reg`). In every shipped mod each value is where the game folder has the file of that name (`config\`, the
  resources' `res\`), so the mod's layer gives the mod's file; the section itself is not read.
- `[DEV] Plugin`: a DLL that patches game.exe (Lost in Astral's fixshops.plg, IronMod's fixfslpatei.plg); not loaded.

Rebirth's own Maps, CameraPatch, MoviesPatch and StreamPatch look in `ModPath()` too; with the layer they find the
same files.

## Checks

- `tests/unit/path_layers.cpp`: full paths, the mod's and the game's files for reads and writes, archives and root
  files left out, the saves' folder, merged searches.
- `mod_lost_in_astral` (Rebirth's data): with Lost in Astral the main menu's zone (ZoneMainMenuNew) and the new game's
  (gz1g, the mod's zonezero) and `config\ai.reg` are the mod's, and the autosave goes to
  `harness_saves\LostInAstral\savea`. On the vanilla data the folder is missing: logged, the game without a mod.
- Every other scenario has no mod: the overlay's map with two layers gives the paths MapPath gave before (the goldens
  the same).

## Open questions

- The stub data (linux.md, "The stub data") has no `starter\Mods`: `mod_lost_in_astral` needs the mod's files read in
  its run.
- The `.plg` plugins: what fixshops.plg changes in the shops.
- `[MOD] Single` / `Multi`: a mod for one kind of game only (the menu's list, MOD3).

## Lost in Astral played (MOD2)

What the expansion's data asks of the game that the base game's never did, and how each is checked:
- The main menu's camera: the mod's `Camera\mainmenu.cam` places it by a first key whose rotation is 1.16 long (the
  game's own file has unit rotations). The game draws it as it is; the drawn view between ticks once undid it by its
  length squared and zoomed the menu in and out on every frame (renderer.md section 14, "Rotations of other lengths";
  scenario `mod_lost_in_astral_menu_camera`).
