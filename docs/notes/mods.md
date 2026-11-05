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
| `src/main.cpp` | reads `[game] Mod` after the Rebirth profile, calls `app::UseMod` (app_main.cpp: the folder in full, `platform::SwitchMod`, the log) |
| `src/game/app_main.cpp` | `StartGame` / `StopGame` (the game's half of the start and the stop), `app::RequestModSwitch` and the switch (`TakeModSwitch`) |
| `src/game/mod_list.h`, `.cpp` | the main menu's list of mods: `Scan` (starter\Mods\*\config.reg), `IndexOf`, `SettingOf` |
| `src/game/ui_main_menu.cpp` | `MainMenu::AddModList` (the list's drop-down control) and the pick's `RequestModSwitch` (in `Update`) |
| `src/game/mod_folders.cpp` | Rebirth's mod-folder patches (rebirth.md, "Resources and paths"): `ModPath()` is the Mod folder, else `[rebirth] AddonPath`; `Reset` at a switch |
| `src/game/mod_plugins.h`, `.cpp` | the mods' plugins carried as code ("The mods' plugins"): `modplugins::FourShopClasses` |
| `tests/verify/harness.cpp` | `Layers` / `MapPath` / `MapSearches`: the run's overlay as the map's top layer |
| `tests/unit/path_layers.cpp` | the unit check (ei_unit_check, the POSIX port check) |
| `tests/scenarios/mod_lost_in_astral.txt` | a new game in Lost in Astral (Rebirth's data) |
| `tests/scenarios/mod_switch.txt`, `mod_switch_plain.txt` | a switch to Lost in Astral and back, then a new game; the same new game from the start |
| `tests/scenarios/mod_menu_list.txt` | the menu's list picked through the UI, and the settings file written |

## Choosing a mod

`ei_settings.ini`, `[game] Mod = starter\Mods\LostInAstral`: the mod's folder relative to the exe's folder (the game
folder for a dropped-in exe; on macOS the app's MacOS folder, so give it in full there), or in full. Read at the start,
after the Rebirth profile (the settings' first use writes the profile's defaults); a switch of mods writes it. A folder that is not there
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
folder); the snapshot (config/harness/registry.reg) marks `Mod` absent otherwise. After a `waitmenu` or
`waitwindow` line, `mod <folder>` (or `mod none`) is a step instead: a switch of mods in the main menu
(`app::RequestModSwitch`, refused elsewhere: the run fails), the next step once the switch is made; the `read mod` /
`wrote mod` checks then look in that folder. The overlay's map is made once for each mod the run plays (`Layers`).

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
- `[DEV] Plugin`: a DLL that patches game.exe (Lost in Astral's fixshops.plg, IronMod's fixfslpatei.plg), which the
  starter loads; never loaded by our exe. The ones that matter are code, on while the mod names them ("The mods'
  plugins").

Rebirth's own Maps, CameraPatch, MoviesPatch and StreamPatch look in `ModPath()` too; with the layer they find the
same files.

## The mods' plugins

A `.plg` is a DLL the starter loads into game.exe (1.06) when the mod's config.reg names it; it writes bytes into the
code. Read with a disassembler, never loaded. `src/game/mod_plugins.cpp` reads the mod's config.reg (`ModPath()`) at
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
  files left out, the saves' folder, merged searches.
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
- the entries: "No mod" (the game's own files), then each folder of the game folder's `starter\Mods` with a `config.reg`,
  by folder name, titled by `[MOD] Title` (the folder's name when it has none; a mod for one kind of game, `Single` 1
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

A switch, between the two: `modfolders::Reset` (the mod's archives closed, its folder, `[RES]` lists and Maps\ listing
read again at their next use, the maps folder the game's), `app::UseMod` (`platform::SwitchMod`: the old `Mod` is never
deleted, other threads may still map a path through it), the settings' `Mod` written, the loop's clock as at the start.
The renderer, the window, the audio device (a new `SoundManager` closes and opens the output) and the settings stay.

### The loading screen (MOD3c)

A switch draws the game's own loading picture, the progress movie a level's load draws (`ShowLoadingMovie`, progres.bik
from the mod's `Movies\` with Rebirth's MoviesPatch), instead of the menu's last frame standing still: `LoadingStep`
(app_main.cpp) before the stop (step 0, while the worlds still draw), then in `StartGame(..., showLoading)` after the
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
and pipelines on a thread), the Rebirth profile. `GameMain`: the static initialisers' work (net and code tables, the
weather noise: computed, no files), the command line, `g_startFolder`, the options and the screen layout, then
`StartGame` with the display. The AutoExec lines (the settings and 2utoexec.cfg in the root) are read at each new game;
no mod changes them.

### The globals by owner

About 480 `g_` names in 450 definitions (render 264, game 180, platform 17, audio 15, lib 4, main.cpp 1); the
other mutable state is the function statics and a few scratch buffers. Keep: stays as it is. Reset: emptied or set
back. Reload: made again from the new mod's files. The list to check when a switch shows the old mod's data.

| Owner | Globals | Switch | Why |
|---|---|---|---|
| Platform: window, events, input, instance lock, log (platform_sdl, other_game, log, main.cpp, present_window, app_confine, app_resolution) | ~22 | keep | no file of a mod |
| Path map (path_layers.cpp `g_mod`) | 1 | reload | `platform::SwitchMod` |
| Renderer (src/render: device, pipelines, frame, caches of textures, meshes, terrain pages, fonts, shadows, water, weather) | 264 | keep | caches key on game objects and forget them: `ForgetHolder` when a texture holder is freed, meshes and terrain pages checked for staleness and dropped after 600 frames unused |
| The game's draw glue (renderer_scene/sprites/strip/lines, device_draw, figure_part_draw, world_client_draw/lights/shadow_casters/view, effects_weather, particle_manager, lightning's sprites, drawn_motion, camera, camera_trace) | ~50 | keep | per-frame scratch and the drawn view; the scene renderer's sun and the device's bindings let go by `StopGame` |
| Audio device (audio.cpp) | 15 | keep | a new `SoundManager` closes and opens the output again |
| App and loop (app_main, app_frame, main_loop.h, bink_video, `g_camera`, screen_layout, ui_cursor.h sizes) | ~27 | keep | the camera `Init` again and attached to the new client world; the loop's clock reset |
| Settings, options, Rebirth profile and flags, console, crt, random generators | ~19 | keep | `g_random` / `g_clientRandom` unseeded by `StartGame`, seeded again by `World::Create` |
| `g_logicSettings` (config\ai.reg) | 1 | reload | `Load` starts from the defaults |
| Databases (database_globals, unit_db, lever_table) and `g_mapDatabase` | 9 | reload | `LoadDatabases` empties each; `LoadAll` empties the map list |
| Mod-folder layer (`g_mapsInMod` and mod_folders.cpp's state) | 1 + 2 | reload | `modfolders::Reset` |
| Worlds (`g_clientWorld`, `g_serverWorld`: every manager and its archives, texts, sounds, music, the lightning noise) | 2 | reload | `Destroy` then `Create` and `SetUpGraphics` |
| Game flow and world state (world_game: `g_gameFlow`, `g_zoneTable`, `g_saveStream`, the stamps; world_briefing's quest folders and files; world_weather, world.h; party_manager, party, player_client, player_server_handlers) | ~15 | reset | `GameFlow::Restart`; counters may keep counting |
| Interface (`g_interface`, `g_keyActions`, `g_menuRects`, `g_cursor`, texture_load's `g_composed`) | 5 | reload | `Shutdown` / `Create`, the cursor's `Shutdown` / `SetUpCursors`, `ForgetComposedTextures`; menus.reg read again by the figure manager |
| Interface scratch (ui_surface's list, ui_main_menu, ui_objectives, ui_interface, ui_screens.h, `g_keyNames`) | ~6 | keep | |
| AI pools and tables (ai_block_search, ai_events, ai_footprint, ai_path_smooth, ai_refresh, ai_unit_shape), weather noise, fog tiles, animation blends | ~13 | keep | computed, or loaded for each zone |
| Network (net_classes, net_coder, net_messages, net_second_master, net_session, net_server_players, world_guard) | ~25 | keep | the switch is offline only |

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
- cz3h and cz4h have no mob in the mod or the game and are reached by nothing: cz3h's exit is commented out in
  zone2's script and cz4h's (zone2's VCheck#6#1) waits for `q24h` equal to 1 and to 0 at once.
- The later quests (`mod_lost_in_astral_gipat_quests`, `_gipat_necropolis`): gz11g's q11g (areas), gz12g's q27g (the
  broken palantir and three key levers), gz13g's q17g (a chest's key on three key levers, opened from the quick
  panel as a player does) and gz14g's q28g (a lever's golem centre key on two key levers), each to its end and the
  briefing or exit it opens. A key lever's lock (the lever record's id) is the quest item's index in items.idb
  (TeleportKey00 2, AmbushKey00 38, GolemCenterKey00 39, PalantirBroken00 44). Not matched: the key levers of gz12g
  (4523 to 4525) and gz14g (1712, 57) do not open from the quick panel's key: the order is taken (the panel's mode
  back to 0) but the hero stops about 2 units short (gz12g) or does not move (gz14g), where gz13g's and bz1r's open;
  the scenarios switch them by `SwitchLeverState` (MOD2b).
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
