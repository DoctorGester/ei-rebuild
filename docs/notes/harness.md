# The harness

How to run scenarios, the scenario steps and what a run does. How changes are checked (goldens, WideScreen
references) is in `standalone-verification.md`.

## Running a scenario

`harness.cmd tests\scenarios\<name>.txt` runs one scenario on `ei-rebuild.exe` with the vanilla install's data
(`run_all.py <name> --vanilla-only --retries 0`) and exits with 0 when it passes. The report
(`build\verify\runs\<name>-standalone\harness_report.txt`) holds everything the game console printed, and any failed
checks. Scenarios live in `tests/scenarios/`; the steps are listed at the top of `tests/verify/harness.cpp`.

    harness.cmd tests\scenarios\batch_combat.txt

`python tools\run_all.py` runs every scenario on the vanilla install's data, several games at once (`--jobs N`,
default 16; the whole suite, 190 runs, takes about 75 s and the Rebirth set, 43, about 30 s since H74); workers run it through `python tools/work.py test <ID> [run_all
args]` (work-board.md). Each run writes to its own folder, `build\verify\runs\<scenario>-<install>` (`EI_OUT_DIR`): its
`ei-rebuild.log`, report and `harness_saves`, emptied first. A folder that cannot be emptied (a file in it still open) fails the run; a run that passes in digest mode
(`EI_DIGEST`, `--golden`) without a `digest.txt` fails too, and a digest the game cannot create fails its report (H54; a rerun by name that empties a suite's run folder before the suite's comparison waits in the scheduler, work-board.md). Installs: `standalone` (the vanilla data) and
`standalone-rebirth` (the Rebirth data and goldens; only on demand: `--rebirth` adds the Rebirth set,
`--rebirth-only` runs it alone, `--all-rebirth` every scenario). The Rebirth set is config/rebirth_scenarios.csv's
marks, the scenarios with Rebirth-specific steps and every scenario with a Rebirth golden (run_all.rebirth_scenarios),
so main's `--rebirth-only --golden` after each merge compares every Rebirth golden there is: one outside the set went
stale unseen after golden-moving merges (H51a found 66 of 102 behind C84b, H47c, C81 and C84d). A scenario gets a
Rebirth golden only to be checked on Rebirth's data after every merge; 102 runs take about a minute on 16 slots. A
line `# installs: vanilla` or `# installs: rebirth` keeps a scenario's runs to that install, named or not, and decides
its place in the Rebirth set (H74: Rebirth runs that reach no code and check nothing their vanilla twin does not, and
vanilla runs of scenarios whose steps are all Rebirth's); `--widescreen` plays its scenarios on Rebirth's data
whatever the line says. A line `# suite: named` leaves a scenario out of the suite but lets it run by name (the CI
gate `boot`, `smoke`, `--widescreen`'s `ui_widescreen_dialogue`), as the `screenshots_*` ones. The
Linux runner (linux.md) plays only the marked and stepped part (`rebirth_scenarios(goldens=False)`). The installs' folders are `EI_GAME_VANILLA` and `EI_GAME_REBIRTH` (default
`F:\games\EvilIslands`, `F:\games\Evil Islands (Rebirth)`; run_all.install_dir, also the tools that read
game.exe, tools/exe_path.py); a `work.py test` batch carries the submitter's two, so a suite on the stub data
(linux.md, "The stub data") goes through the scheduler too. `--unpacked` sets both to the installs' unpacked copies
(every archive replaced by its folder of loose files; mods.md, "Unpacked installs"), `--converted` to those copies'
converted copies (the record files as TOML; mods.md, "Converted installs"). `EI_BUILD_DIR` points run_all at
another verify build.

Tiers (H74). `--smoke` plays `config/smoke_scenarios.csv`'s runs (a scenario and an install per line), the few that
reach the most code between them: `tools/suite_coverage.py --smoke-file` picks them greedily from a coverage pass's
per-run functions (below, "Source coverage"): 60 runs (50 vanilla, 10 Rebirth; about 8 game-minutes of the suite's
25) reach 99% of the functions and 97% of the edge flags the whole suite reaches (2026-11-11). `work.py test`
without names plays the smoke tier for every ticket but MAIN; `--full` (run_all's own default) the whole suite, which
the merge runs always play (`work.py test MAIN --golden` and `--rebirth-only --golden`). `--converted` through `work.py
test` without names plays only when a file under `run_all.CONVERTED_PATHS` (the converter, the install copies, the
loaders of the converted formats, `assets/`) changed against `converted_base()`: a branch's merge base with main, or on
main the commit before the merge. Merged scenarios (`batch_*.txt`, and `render_batch` / `camera_batch`, whose names
keep the Linux workflow's render_ and camera_ filter on their sections; H74) play several former scenarios as sections
in one game: each section after the first undoes what the ones before set (`frametime 0`, `speed 0`, `unfreeze`), goes
back to the main menu (Esc unless the escape menu is up already, its exit, Enter) and starts its own new game (or, for
the menu ones, starts at the main menu); a `# ---- section <name>` line keeps the old name, which the notes still use,
and its `reject` lines start with the section (sections that allow script errors come first). The smoke list of
2026-11-11 counts a merged run as reaching what its sections reached as scenarios; the next coverage pass
(`coverage.py run`, then `suite_coverage.py --smoke-file config/smoke_scenarios.csv`) measures them.

How a run starts (run_all.launch, tools/game_process.py): the exe itself, with the working directory and `EI_GAME_ROOT`
the install, `EI_HARNESS` the scenario, `EI_OUT_DIR` the run folder and `EI_INSTALL_ROOTS` both installs. The game prints into the run folder's
`launch.txt`, followed by run_all's own lines (`run_all: ...`). The games run in a job object on Windows (a session of
their own with PR_SET_PDEATHSIG on Linux), so none outlives run_all.

The scheduler that `work.py test` feeds is the main checkout's code: it plays a worktree's build with main's run_all. A
branch that changes how run_all starts a game is checked with its own `python tools\run_all.py` (machine slots still
apply), not through the scheduler, until it is merged.

**Screenshots** (D7): the README's and the release notes' pictures (`docs/screenshots`) come from the
`tests/scenarios/screenshots_*.txt` scenarios, which no suite plays (run_all's `SCREENSHOT_PREFIX`; the CI workflows
leave them out too) and which have no goldens: 3840x2160, `widescreen 1`, `msaa 8`, `anisotropy 16`, the side panels
slid out where the HUD is not the subject (`click 793 15`, `click 7 15`), the camera placed by `camera`. Run them one
game per `work.py test` call (`work.py test <ID> --vanilla-only screenshots_town`, then the next; the scheduler
ignores `--jobs` and starts a batch's runs at once): a dozen 4K games with 8 samples at once hung the GPU
(DXGI_ERROR_DEVICE_HUNG, "the shadow map's pass could not begin", or a crash after it) in most of them; one at a time
they pass. Each frame is then halved to 1920x1080 (Catmull-Rom) and written as a PNG, its channels rounded to 7, 6 or
5 bits where 8 make a file over about 1.5 MB (the game's textures hide the steps; D7's PNGs took 5 to 7), and looked at
before it is kept. Lower camera pitches over the sea show the water plane's far edge (screenshots_water keeps 32
degrees). Text is smoothed in harness runs as in play, so the captures show the game's text as players see it.

A scenario line `# prepare: <tool> [args]` runs `python tools\<tool> <harness_saves dir> [args]` before the game starts
(`tools/prepare_scenario.py`): the game sees those files under its `saves\` (e.g. `magic_traps.txt` and
`tools/make_trap_mob.py`).

## Scenario steps

- `newgame` starts a new single-player game (the hero on zone1); `load <slot>` loads a save.
- `console <command>` types into the game's console. `console thingamabob` unlocks scripting; then `@<command>` runs a
  world script command (e.g. `@AddUnitToServer(...)`, `@Attack(...)`) and `#<expression>` prints a script expression.
- `check <expression>` requires the expression to print nonzero; `expect`/`reject <text>` look for console output;
  `allow <text>` exempts lines containing <text> from `reject`.
- `waitfor <seconds> <expression>` waits until the expression is nonzero (fails after <seconds>); prefer it to fixed
  `wait`s. `waitline <seconds> <text>` waits until the console has printed <text> (e.g. `Saved savea`).
- Waits on the interface rather than the clock (each fails the run after <seconds>): `waitmenu [seconds]` (default 30)
  waits until the main menu is up and taking input, and every scenario starts with it. `waitwindow <seconds> <window>`
  waits until that window is on top, no game request (new game, load) is pending, and
  the save/load or options window has finished sliding in, for two frames in a row. Names: `menu battle briefing map
  camp view trade escmenu saveload options objectives help msgbox difficulty heroes titles mpconnect masterbox`. `ifwindow <window> <step>` runs a step only while that window is
  on top (else skipped, logged): a box that Windows keeps up and Linux closes by itself. A new
  game's battle screen comes seconds after the leader has HP, so input for it waits on `waitwindow 15 battle` first.
  `waitunit <seconds> <name>` waits until `clickunit` would find the unit on screen. `frames <n>` lets the game run <n>
  frames (input posted before it has been handled after one), with a real-time limit of 10 s plus 100 ms a frame: past
  it the step fails and the scenario goes on early, so every later step lands on other world updates and the digest
  differs from there on (a slow machine's cut, not a behaviour difference; linux.md, H56). Plain `wait <ms>` is left for "let the world run this
  long". On the server world: `waitidle <seconds> [frames]` waits until the leader has stood idle for that many frames
  (default 10), `walkto <seconds> <x> <y> [frames]` walks the leader to a map point (lookat, a ground double click,
  then idle for that many frames, default 20; clicked again when a fight stopped it short), `waitdead <seconds>
  <id>...` until those units are dead, `waitaward <seconds> [received]` until the leader's experience or money changes (from an
  earlier `awardfrom`; `net_pair_quest.txt`; `served`: instead until the game server's queued awards, each kept 60 s, are
  gone, `net_pair_leave.txt`). `hit <target> <attacker> <amount>` deals a server world unit a blow from another
  (`leader`, `#<id>` or a script name; attacker `none` for none) through Unit::ReceiveHit, so the blow has a side and
  works in a network game, where `@` commands do nothing (`net_host_death.txt`, `unit_hit_turning.txt`). `giveitem
  <player> <item>` gives the server world's player (its id: 0 the host's own, 1 a pair's peer once it is in the game
  zone; the multiplayer bases are each game's own) a new item by its database name, as a script's GiveItem
  (`net_pair_database.txt`). `serverscript <command>` runs a world script command on the server world
  (`WorldScript(<command>)`), also in a network game where the console's `@` lines do nothing; a script error fails
  the step (`net_pair_mod_effect.txt`). On the base screen: `waitdialogue <seconds> <state>` waits for its
  dialogue state (1 a person's topics open: `clickregion` picks one), `talk <seconds>` clicks through the dialogue's
  phrases and rewards until it is over, or until the base is left for a battle zone (a cut-scene base whose briefing
  ends in LeaveToZone: `mod_lost_in_astral_cz_ingos.txt`; `net_pair_leave.txt`).
- `key <key>` and `click/rclick/move <x> <y>` send keyboard and mouse input to the game window (coordinates in the
  800x600 interface layout); Esc and mouse clicks reach the interface. `clickregion/rclickregion/dblclickregion/
  moveregion <n>` do the same at the centre of region `<n>` of the window on top (region numbers as the screen's code
  uses them; ui.md); they also move the harness's cursor (what the game polls each frame: the
  tooltips, the hover) to that point, as `click`/`move` do, so `moveregion` hovers a tooltip (`ui_tooltip_switch`,
  `ui_gallery_menus` help_tip); a capture taken after a region click shows the hover of the button clicked, and a window
  resize keeps the cursor's pixel (`render_resize_windows` moves off the page first). `hold <mods>` / `release <mods>` press and release Ctrl / Shift / Alt (`ctrl shift alt`), and the
  same words after a key or mouse step's arguments (`click 400 560 shift`, `key 1 ctrl`) hold them around that step
  (`camera_limits.txt`). `clickunit <name> [l|r|move|dbl] [mods]` clicks the unit with that script name where the
  game's own picking finds it this frame (`dbl` sends only a double click's second half); `clickunit #<id>` picks a map
  object by id (a lever, `levers_key.txt`). `clickground <x> <y> [click|dblclick|rclick|move]` does that mouse step
  where the game's ground pick finds map point (x, y) (`net_pair_companion.txt`). `layout <mouse step> <x> <y>` aims at the
  800x600 layout's point in the window's centred 4:3 part (the plain mouse steps spread their points over the whole
  window); `checksave ... centre` takes the save picture's 4:3 part in a wider window.
- `camera <x> <y> [<heading> [<distance> [<pitch>]]]` places the view camera to look at map point (x, y) at once
  (R17c4; Camera::SetFocus, SetHeading, ApplyView through image::PlaceCamera, in WinMain's loop): it follows no unit
  and stops its pan; heading in degrees about z (0 the camera's north, 90 screen right = +y), distance in units,
  pitch in degrees, each kept when left out; `camera` alone logs the pose. It aims a frame capture by map point on any
  install and window size, where `lookat`'s minimap clicks (`minimap` numbers per install, kept for `walkto`'s player
  input) did not hold: on Rebirth's zone1 at 1440x1080 they moved the camera erratically.
  `camrange pitch|distance|x|y|ground <least> <most>` requires the camera's pitch (degrees), distance, focus x / y or
  its near plane's lowest height above the ground (the drawn view's, 9 x 9 points of the plane worked out from the
  projection's sizes apart from the camera code) within the range and logs its pose (`camera_limits.txt`).
  `climate <n>` sets the server world's climate (WorldServer::SetClimate: 0 none, 1 rain, 2 snow, 3 storms; a change
  of climate clears the weather). A dry capture scene says `climate 0`: `@SetWeather("clear")` only stops the rain now, and on
  zone1 (climate rain) UpdateRain rolls one in 101 every 64 ticks to start it again, which Rebirth's random stream hit
  during a capture scene's views (`weathercheck 0 0` before each view checks the ground dry).
- `screen <id>` asks the server for interface screen `<id>` as the global map's buttons do (4 the global map, 5 the
  camp); the camp's Esc returns to the game. `screen 5 <n>` opens the camp with constructor / trader class `<n>` as a
  base dialogue's "constrN" answer does (1, 3, 4, 5 after a new game; `ui_camp_deals.txt`). `campstock <n>
  scroll|prototype <spell code> <least>` requires that many scrolls (or prototype loot items) of the spell in trader
  class `<n>`'s magic stock, made when its camp opened, and logs the stock (`mod_spell_hero`).
- `zone <name> [exit]` leaves the running zone as its exits do for map.txt zone `<name>` (`gz2g`, `bz1g`, ...), the
  party arriving at that zone's exit `<exit>` (from 0: map.txt's `#exit n` minus 1); `waitzone <seconds> <name>` waits
  until it is loaded. Starting a zone from the main menu crashes the game, so scenarios start with `newgame` and change
  zones from zone1 (`zone_gz2g_traps.txt`; the zone list in ui.md).
- `speed <ms>` sets the real time per world update (game default 55); on the virtual clock it speeds nothing up.
- `capture <name>` writes the next presented frame to `frames\<name>.bmp` in the run's folder; `capture <name> <count>
  [freeze] [after <text> | on <window>]` a burst of consecutive frames (renderer.md, "Frame capture"); `freeze` / `unfreeze`
  hold the game's clocks and camera around captures; `samecapture <a> <b> [<tolerance> [<fraction> [<left> <top>
  <right> <bottom>]]]` requires two captures to match (within the rectangle when given, as diffcapture's; R58),
  `diffcapture <a> <b> <least> <most> [<tolerance> [<left> <top> <right> <bottom>]]` that they differ in that share of the pixels; `printlight <with> <without>
  <least> <most>` bounds the brightness of what one capture draws over the other (the ground prints, `decals off`)
  over that of what it covers (render_shadows_decals); `blink faces|open|close` loads the
  units' faces or shows every face's open / closed state (render_blink). `skin <unit> <first> [<second>]` gives a
  spawned unit's client figure the textures a map placement names (AddUnitToServer names none: untextured, world.md),
  `unitskins <unit> <first> <second>` requires the client unit's texture names (party_join_skin),
  `groundprints [<least> <most> blood, footprints [, scorch]]` logs the client's ground prints and checks their counts
  (ground_prints: the save's prints back after its load),
  `unitcolour <frame> <a> <b> <least> <most>` compares two units' mean colours in a frozen capture (render_unit_skin;
  the crowds of render_occlusion, render_lighting and render_stats are skinned). `python tools\compare_frames.py A B` compares
  two captures or two runs' frames with a tolerance (`--mask` restricts it to where two other frames differ). A
  finished run's frames are stored packed (`<name>.bmp.z`, tools/frame_store.py). `render <spec>` sets the renderer's
  options for the run (renderer.md).
- `peer <name>` makes a pair (`tests/verify/harness_pair.h`, `net_pair_kick.txt`): run_all starts a second game on
  `tests\scenarios\peer\<name>.txt` beside this one, the two in lockstep on a network of their own (the second joins the
  first's hosted game over virtual UDP ports); `peersignal <tag>` / `peerwait <seconds> <tag>` order the two scenarios.
  The peer's report and logs are in the run folder's `peer\`; a pair takes two of run_all's slots. `netcut` cuts one
  game's network (`net_pair_leave.txt`); `hidefile <path>` makes an install file read as missing in that game until it
  writes the file itself. A file a `# prepare:` tool writes into `harness_saves\<install path>` is read in place of the
  install's by the scenario's own game (the overlay), not by its peer: `net_pair_database.txt`'s host plays with a
  network item database of one item more (`tools/make_extra_item_db.py`). The peer's own overlay is the run folder's
  `peer\harness_saves`, which a prepare tool may fill too (`tools/make_mod_pair.py`: a mod for each game). `netmod`
  prints the mods the lobby compares (network.md, "Mods"), `netlist` the client's listed servers with the mod each
  one's info answer named and whether this game may join it; `originalnet` makes the game's lobby act as the original's
  (no mod mark, any mod admitted).
- `secondmaster <host[:port]>` sets NewMaster's second master server for the run, over the settings file's
  `[network]` SecondMaster (rebirth.md, "Settings"). A game's player limit and pause mode are the host's create-server choices:
  a scenario drags the player slider (`layout ldown` / `move` / `lup`, `net_pair_portraits`) or clicks the pause
  mode's region (29) with the new game's row selected; `checklimit <n>` requires the client session's player limit
  (`ClientSession::PlayerLimit`), `checkpaused 0|1` the server world's pause flag. Companions (network.md): the
  create-server screen's region 30 turns them on; `companions <n>` requires server player 0's party records, `companionlist <n>` the companions picked in the base
  (`SavedParties::companions`), `askcompanion <hero>` sends a mid-game add of that saved hero (the server refuses it),
  `heroxp <name> [more]` reads the saved hero's file from the disk and logs its experience (`more`: above the last
  heroxp of that hero); the `hit` step's units take `companion` (server player 0's first) and, as a target, `enemy`
  (the living unit nearest the attacker that is hostile to it; `net_pair_companion`). `folderprofile` checks the drop-in start-up (harness_folder_profile.h,
  `dropin_profile.txt`).
- Mod folders (S19d; standalone.md, "Mod folders"): `mod <folder>` answers the settings' `[game] Mod` with a mod folder
  of the install (relative to the game folder, `starter\Mods\LostInAstral`), in full, or `harness_saves\<folder>` for
  a mod the scenario's `# prepare:` tool wrote into the run's overlay (mods.md); after a `waitmenu` or `waitwindow` line, `mod <folder>|none` switches the mod in the main menu instead (mods.md, "Switching mods"). `read` / `wrote` `mod|game <path> yes|no` and `waitread <seconds> mod|game <path>`
  ask which files the game touched or wrote (`harness_modtrace.h`; noted only in a scenario that uses them, the wait is in
  real seconds because a thread opens the file); `modfile`, `modlist`, `modarchive` and `mapfile exists` ask Rebirth's patches
  directly; `playstream <file>.mp3` asks the music stream for a track. The stream's steps run on the virtual clock (a
  harness run opens no sound device), so the wait takes the same world updates every run and the digest records them
  (S19d1: four runs agreed record for record); with `music <volume>` and a device the halves free in real time and would not.
- Data effects (effects.md, "Data effects"): `effect <name> set <input> <value | x y z | enum value>`, `effect <name>
  fire <event>` drive the client's running sources of that effect through the inputs game code uses
  (`effectdata::SetInput` / `Fire`); `effect <name> value <input> <expected>` requires an input's value (an enum's as its
  index), `effect <name> check <least particles> [<stage>]` requires a source with that many particles, in that stage
  (`harness_objects_image.cpp`'s `DataEffectStep`; `mod_effect`). `effectviewer <command>` opens and drives the
  effect viewer (effects.md, "The effect viewer"; the commands in harness.cpp's list, `tools::effect_viewer::Command`;
  `folder out [<name>]` saves into the run's output folder; `effect_viewer`). `clientbolts <least> [<most>]` requires that many
  lightning bolts (EffectLightning) in this game's client world, a peer's too, and logs their ends
  (`net_pair_mod_spell`); `clienthurt <least> [<most>]` that many living client units below their maximum hit points
  (the packed vitals the server sent), each logged with its id and hit points (`net_pair_mod_spell`'s peer: the blow
  of the host's spell).
- `resolution <WxH|desktop>` and `widescreen 0|1` (or `EI_RESOLUTION` / `EI_WIDESCREEN`) set the run's window; without
  them runs play in a window of 960x720 on the vanilla data and 1280x720 on Rebirth's (each takes its goldens' shape;
  FullScreen 0 unless the scenario says `fullscreen 1`).
  `chat 1|0` opens and closes the chat line. Every other setting a run reads comes from the committed snapshot
  `config/harness/` (`tests/verify/harness_settings.h`), never the user's own.
- `recordinput [live]` records the run as a scenario, `recorded` marks one, `at <frame> <step>` / `at <frame> event
  ...` are its lines, `realinput ...` is input made as SDL's own ("Recorded play" below).

## The virtual clock

Harness runs use a virtual clock (`tests/verify/harness_clock.h`): each main-loop frame is five world updates, the main
thread's sleeps and timed waits take no real time, sound ends come on virtual time, the date and the random seeds are
fixed, and frames present without waiting for vsync. The fixed date, 2001-03-26 12:00, comes with a fixed zone, UTC+2
(the process's `TZ` is set to `EET-2`, so the log's times of day are in it too): the main menu's time of day and a
save's time (info.sav) are then the same on a machine in any zone. A scenario that repeats takes the same course on every run (a
golden is recorded only from runs that agree). The harness's waits count virtual time, each with a real-time safety
limit (times `EI_TIME_SCALE`, default 1: a factor for slow runners, also on a frames step's and the scenario's limits; linux.md,
"The CI job"). A `realtime` line puts a scenario on the real clock; `EI_HARNESS_CLOCK` (or `run_all.py --clock`) overrides:
`virtual` or `real`, plus `clocktrace` (every clock read and wait by function, caller and thread, in `ei-rebuild.log`)
and `randtrace` (every random draw with its frame and caller, in `randtrace.txt`; `tools/randtrace.py` finds where two
runs part). The heap moves its blocks in every run, so a golden never follows heap addresses. The trace tools
(`EI_SAVETRACE`, `EI_WATCH_*`) are in standalone-verification.md.

## Digest and goldens

The game-state digest (`tests/verify/harness_digest.h`): `digest <N>` (or `EI_DIGEST=<N>`) writes the world's state
(units, positions, vitals, effects, AI state, generators, players) every N world updates to the run's `digest.txt`,
`dump <name>` all of it once; `python tools\golden.py diff <run A> <run B>` names the first world update and field where
two runs differ, and `golden.py record` / `compare` keep and check golden runs in `tests/golden/` (recorded on the
runner by the Suites workflow's `record` input; standalone-verification.md, "Recording goldens"). `python tools\work.py
test <ID> --golden` compares every run that has a golden with it, its saves too, decoded field by field (`python
tools\savefile.py show|diff|rules`).

Unit checks (`tests/unit`, `ei_unit_check.exe`, run by build.cmd): the settings file's format and lib::HashMap's walk
order, without the game.

Timing runs go through `work.py test ID --quiet ...` (H64: each game alone, no build beside it; before it, R37f1's
build measured 9.1 ms in R37e3's runs and 11.7 ms in its own on a busy machine). `EI_PROFILE=1` samples the main thread into the run's `profile.txt` (`python tools\profile_report.py <run
folder>`: time per function between harness steps; names from the x64 `.map`; the log's stamps move to the run's zone,
EET-2, once the harness sets TZ, and the report takes them back by whole hours). The stack is scanned, not walked, so a
stretch's "total" list holds stale return addresses (free_base, string constructors at 80%): read "self" first. The sampler
stops in an exit function (after the game's, before the runtime's end) and closes the file; one still sampling into
the runtime's end ended runs with exit code E06D7363 after the quit (H70a).

Save and load cost (PF4, `zone_gz19h_save_load`: the Portal zone gz19h, zone19, the largest map and .mob; wall time
from the log, 2026-11-04, vanilla / Rebirth data): entering the zone 0.94 / 1.04 s, the F5 save 0.11 s (about 70% in
the LZ coder's `CoderState::Compress`, mission.sav about 250 KB packed), F8 or `load` 0.35 / 0.39 s (`LoadSave`:
`LoadSavedMapObjects` with the figures' skeleton links, the AI map's `RefreshArea` / `CellMark`). Fast in our build;
the original's slow saves on this zone are not reproduced.

## Recorded play

Real play becomes a scenario with a golden (`tests/verify/harness_record.h`, `tools/recorded.py`).

**To record a session by hand:** `harness.cmd record <name>` (Rebirth's data: `harness.cmd record <name> rebirth`; the
suites play every scenario on the vanilla data). It builds the verify build and starts it on
`tests/scenarios/record/live.txt` in a 960x720 window that is shown and takes the keyboard and mouse, from the main
menu on (no sound; no movies: below). Play; close the window, or Exit from the main menu, to end it. The session
becomes `tests/scenarios/rec_<name>.txt`. A session past 3000 frames (about 48 s of play; its replay takes about 25 s)
is cut by the game into parts, `rec_<name>_1.txt`, `rec_<name>_2.txt`, ... ("Parts" below); a single part longer than 3750
frames still draws a warning from `recorded.py`. Then replay it twice and record its golden like any new scenario
(standalone-verification.md, "Recording goldens"; each part on its own):

    python tools\work.py test <ID> rec_<name> --golden --vanilla-only      (twice, golden.py snapshot after each)
    python tools\golden.py record --install vanilla --runs <pass1> <pass2> --reason "..." rec_<name>

**What a recording holds.** A scenario line `recordinput` makes its run write `recorded.txt` in the run folder: the
marker `recorded`, the scenario's lines read before the game starts (window, settings, flags, `# prepare:`), then an
`at <frame> ...` line for everything that reached the game, at the harness's frame count: each platform event the
harness queued (`at 14 event down left 564 396 1 c`: keys with code, scan code, modifiers, `raw` when the harness adds
no character; text; mouse moves, buttons, wheel), the mouse's motion (`event motion`), the harness's mouse position
(`event cursor`, or `c` on the event that put it there), and the steps that act on the game themselves (`newgame`,
`load`, `console`, `zone`, `screen`, `focus`, `windowsize`, `frametime`, `quit`, ...). The input steps (`key`, `click`,
`clickunit`, `walkto`'s clicks, ...) leave only their events, the waits and checks nothing; any other step fails the
recording (record::Classify). On replay each `at` line runs in the pass after its frame (the steps run once a frame on
the virtual clock), so the game takes the same input at the same frames: a replay's digest equals its recording run's
(`golden.py diff` of the two). An `at` line whose frame has passed fails the run (the replay has parted). A recorded
scenario's game may quit by itself after its last line (the player's Exit), which passes.

**Live play** (`recordinput live`, live.txt): the backend takes SDL's keyboard and mouse events through the game's own
translation (`src/platform/sdl/sdl_input.h`, the same as platform_sdl.cpp's) and holds them until the pass after the
next frame, where they are queued and recorded (a frame's mouse moves merged into the last); the window is shown,
activated and not resizable, the cursor held for real (relative mode, as the game's backend). Frames are 16 ms of game
time (`frametime 16`) held to the wall clock, so the game plays at its speed; focus changes reach nothing (the game
stays active, as in every harness run). `realinput key|text|move|click|rclick` makes the same SDL events in a hidden
run (tests/scenarios/record/real_input.txt).

**Parts (H44a).** A replay has a run's 60 s, so a long recording is cut into parts, each starting from a save the
recording made. `recordinput live cut <frames>` (live.txt) or the step `recordcut` (a driven recording,
tests/scenarios/record/cut.txt) cuts: the run presses F5 (the quick save, slot `saveq`; `waitexpect Saved saveq`), the
part's file ends with a `quit`, and the run loads the save at once (`load saveq`, `waitwindow 30 battle`, `origin`) while
the next file, `recorded_2.txt`, begins as its replay will run: `# prepare: recorded_save.py {previous} saveq`,
`waitmenu`, the kept settings (`frametime`, `speed`, `windowsize`, `screen`, `shadows`, `focus`: the last value each
had), those three load steps, then its `at` lines. They count from the `origin` step (the frame where the loaded game
settled: `record::Frame()` is the harness's frame less that origin), so a part's lines do not depend on how many frames
the menu or the load took. `origin` also seeds the generators again (image::ReseedRandom: a save holds no game generator, and the client's,
kept beside the save, has drawn in the frames between the load and the origin, which a replay that begins at a load
draws otherwise than the recording did), in the recording and in the replay alike. The automatic cut waits until the part has `cut` frames, the battle screen takes input and
the player holds no key or button; the player's input between the cut and the origin is dropped. `recorded.py take <run>
<name>` makes `rec_<name>_1.txt`, `rec_<name>_2.txt`, ... (`{previous}` becomes the part before). Saves are made by the
game from its data, so none is committed: `tools/recorded_save.py` (the prepare tool) plays the part before once per
verify build (its own prepare line first, so a chain plays each part once), keeps the slot in
`build\verify\recorded_saves\<scenario>\` (key: the exe, the install and the chain's scenario files; one run at a time
makes a save) and copies it into the run's `saves\`. Each part is replayed and gets its golden on its own; the first
run after a build also plays the part before (up to a run's 60 s more, outside the part's own time), in the slot run_all
holds for the run being prepared (`EI_PREPARE_IN_SLOT`, set by `prepare_scenario.prepare(in_slot=True)`; never a game beyond
the machine's 16); called by hand the tool waits for a slot of its own. What a cut does
not carry: console state (an unlocked script console), the camera and anything else a save does not hold; the driven
check (H44a: cut.txt in 2 parts; cut_auto.txt, `recordinput cut 100`, in 3)
found a part's replay equal to its recording from the cut on in every record's players, objects, timed, missiles, events,
script and client hashes (not in the world hash, which holds the update counter, nor in `crt.rand`, whose draws only
client code makes).

**Movies**: a recording and a recorded scenario's run play none. PlayMovie (the new game's intro, the exit's, a
script's) times its frames by the wall clock in a loop where the steps do not run, so the player's Esc would reach
nothing and the replay would wait for the whole movie: their files read as missing there (harness.cpp PlayedMovie);
the loading screen's movie (app::g_loadingMovieName) is drawn by the load's progress and stays. Other runs play them;
on the virtual clock each frame as soon as the one before it (`SetPlayedMoviesPaced(false)`,
H70: Lost in Astral's teleport movie in cz4k's briefing held mod_lost_in_astral_cz_ingos 9 s of wall time). The movie's
loop moves no game clock either way, so the frames shown and the game after them are the same.

The recordings in the suite and their sources (`python tools\recorded.py take <run folder> <name>` turns a run's
recording into `rec_<name>.txt`): `rec_menu_newgame` (the main menu's clicks into a new game, a walk, camera keys and
wheel, the escape menu), `rec_save_load` (a save and its load through the escape menu's windows), `rec_cut_auto_1` (the first part of a
recording cut in parts, above), recorded from `tests/scenarios/record/*.txt` (not in the suite; run by name,
`record/<name>`). `record/real_input.txt` (`realinput` keys, text and clicks, the console, `focus off` / `on`) and
`record/cut.txt` (the driven cut) have no recording in the suite (H74: the other runs reach the same code): record them
again to check those paths. A change that moves a recording's game re-records its golden as any
other; when the menus or the windows' timing change, the recording may miss what it clicked: record it again from
its source.

## AddressSanitizer

`build.cmd asan` is the verify build with `/fsanitize=address` (every target; the runtime DLL,
clang_rt.asan_dynamic-x86_64.dll, copied beside them) into `build\asan`; the product builds never have it. Run the suite
on it with `set EI_BUILD_DIR=<checkout>\build\asan` then `python tools\work.py test <ID> --golden` (any run_all
arguments). The exe sends the reports to `asan.<pid>` in the run's folder (tests/verify/image/asan_report.cpp;
`ASAN_OPTIONS` still works for other options): the first heap error (use after free, double free, overflow of a heap,
stack or global block, a new / delete mismatch) ends the game with its report, symbolised from the PDB, and run_all fails
the run with the error and its first frames. For a flaky heap crash this finds the cause in one run: a freed block stays
poisoned in the quarantine, so the write that only sometimes hit live memory always reports. The whole suite takes
about 6 to 8 minutes at `--jobs 4`; the scenarios that wait on real time (frame counts, `render_stats`, `camera_smooth`,
`ui_compare`, the cursor and device-removed renders, the long pairs) run out of time there: judge those by
their `asan.*` files, not their result.

## Source coverage

`python tools\coverage.py run <ticket>` builds `build.cmd coverage`, plays the image and Rebirth suites on it (`work.py
test <ticket> --full --rebirth --golden`, with `EI_BUILD_DIR` at `build\coverage`; more run_all arguments pass through) and
writes `build\coverage\report.md` (totals, a table per area with gameplay first, the ranked gaps, the biggest unreached
functions, every file), the HTML view `build\coverage\html\index.html` (a page per file, its lines marked) and
`coverage.json`. `python tools\coverage.py report` rewrites them from the runs already there. Not part of the merge
routine: run it when wanted.
- **The build** (CMakeLists.txt's EI_COVERAGE): the verify build's ei-rebuild.exe (only that target; no build checks)
  with src/ compiled with MSVC's `/fsanitize-coverage=inline-bool-flag` (a flag byte set on each instrumented edge) and
  `/Gh` (each function calls `_penter` once at its start: tests/verify/harness_coverage_entry.asm marks the function's
  16-byte slot of the image, then turns the call into a 2-byte jump over it, so only a function's first entry costs),
  at `/Ob1` and with `/OPT:NOREF` (functions nothing calls stay, as unreached). The harness writes the run's flags and
  marked slots to `coverage.dat` in the run folder at each report and at exit (tests/verify/harness_coverage.cpp); a
  pair's peer writes its own into `peer\`.
- **Why MSVC's and not clang's:** the Build Tools here have no clang-cl (only clang-format and clang-tidy under
  VC\Tools\Llvm), so llvm-cov's source-based coverage would need the "C++ Clang tools" component or an LLVM install;
  MSVC's options need nothing and keep the compiler the goldens come from. Their limits, found with small test files:
  `/fsanitize-coverage=pc-table` (the edges' addresses) and `no-prune` crash the compiler (C1001) on most of our files;
  `func` and `edge` exclude each other (the last one wins); the default edge mode prunes so hard that a function whose
  path was entry, one loop pass and the exit sets no flag, so entries come from `/Gh` instead.
- **`_penter` keeps the flags:** MSVC places the call inside the prologue, sometimes between a `test` and the `cmov`
  that reads it (terrain_sector.cpp's BuildRecords took the wrong layer count and crashed every run until `_penter`
  saved them with pushfq).
- **The report** (tools/coverage.py): dumpbin's disassembly of the exe gives each instrumented function (a label whose
  code calls `_penter`), its basic blocks (cut at jumps, returns and jump targets) and which block each flag's `mov byte
  ptr [flag],1` is in; dbghelp gives the PDB's line records. A function is reached when its slot was marked; a line when
  its function was and some of its code is outside the blocks whose flags stayed clear. Blocks without a flag count with
  their function, so the line numbers are an upper bound and the function numbers exact. Only src/ counts, by the part of
  the PDB's path from `src/` on (the compiler cache's objects name the checkout that compiled them first); functions
  marked inline are inlined at `/Ob1` and count in their callers. `static.pickle` keeps the exe's analysis.
- **Per scenario (H74):** `coverage.py scenarios` (also the end of `run`) writes `build\coverage\scenarios.json`: per
  run folder (a pair's peer folded in, retried attempts left out) the functions it reached and its set flags.
  `python tools\suite_coverage.py` reads it with the scenarios, the goldens and the runs' times and writes
  `build\h74\report.md`: the runs that reach no function the others miss and check nothing of their own (drop
  candidates), the families of scenarios with the same start that could share a run, a smoke set (the most functions
  per run) and the counts before and after; its docstring says how each is decided. The mod scenarios' prepare tools
  run `EI_BUILD_DIR`'s `ei-convert.exe`, which the coverage build does not make: `run` copies the checkout's verify
  (else release) one into `build\coverage`, so build one of those first.
- **Measured (H43, 2026-11-04):** the 258 runs (270 games with the pairs' peers) took 68 s, as on the verify
  build (the `_penter` patch), with every golden the same (252 the same, 6 without one); functions 7973 of 12292
  reached (64.9%), lines 66862 of 76533 (87.4%). The report itself takes under 10 s (dumpbin and dbghelp; then
  `static.pickle`). The mixer (src/audio) is never reached: harness runs open no sound device; the unit check
  `tests/unit/audio_mix.cpp` drives it on an offline output (sound.md, "The mixer").
- **False gaps:** a lambda that a template calls (`LoadList(.., [](..){..})`, `ForWorldObjects`) is inlined at `/Ob1`; its
  out-of-line copy is never entered, so the report lists it as an unreached function while its lines run
  (script_load.cpp's `LoadInstances` lambda, script_addon_units.cpp's `GetObjectsOnMap` lambda).
- **Gap scenarios (H43c, H43d):** `zone_return` (RestoreZone: gz1g, gz2g and back), `save_flight` (F5 with a spell arrow and a
  fire wall's sound in flight, F8), `script_file` (`execs`: a mission script file with `#define`s, effects and a sleeping
  script saved and loaded; GameOverMsg, AddMobEx, RedeployParty, GetObjectsOnMap), `gvar_hosted` (a prepared hero with
  `gvar.` variables hosts a game: the AddonStringMap), `ambient_sounds` (tools/make_ambient_mob.py: no zone of the game has
  an ambient sound). The map specs' `Save`
  methods and the `Chunk` writers they alone called are deleted (H43c2): the .mob writer is the editor's.
- **Gap scenarios (H43g, H43h):** pairs `net_pair_lobby` (the peer asks the host's address in the connection screen's
  address box: ClientBase::Ask; the same button on an asked row forgets it; Esc saves the address book, the next visit
  reads it; a double click on the row joins), `net_pair_password` (a password on the hosted game: a wrong one is refused
  with the refusal box, the right one joins; Connect's own first join goes out with no password and is refused unseen); single
  games `ui_base_hide` (Smith's topic M6: HideSpeaker), `ui_base_objectives` (TAB on the base screen),
  `ui_hero_rename`
  (MpHeroScreen::Rename; the edit field is short, so the suffix is cut) and `ui_camp_spells` (trader class 3 has the spell
  trade page; class 1 shows the item page). Not reachable by play (H43g1): GiveBriefingRewards (`CompleteBriefing` returns
  early in every network game, and the briefing records exist only after hosting, when the quit to the menu has dropped
  the text sources), the message clones and copy constructors. Quest zones of a game hosted alone (H43g2; the console's script lines do nothing in a network game, so
  the zone's own script has to run, and only z6q3.mob and z7q1.mob of the offered zones call GSSetVarMax): `net_zone_vars` (a
  prepared strong hero; z6q2 taken and rejected after 25 frames, which offers z6q3 on both installs; one long walk
  to BoarCage, where the first trigger raises the server's variables: ServerSession::RaiseVariable) and `net_zone_leader`
  (z7q1 taken and rejected after 30 frames, which offers z7q3, whose first trigger calls GetLeader: LeaderId). The offer
  follows the game's draws, which move with the frames before the reject (z6 after 0 / 10 frames: z6q1 on vanilla; z7
  after 0: z7q2 on vanilla), so the numbers hold only because every run takes the same steps; the fourth topic of
  a first offer is z7q1, whose trigger needs the hero about 190 units from its start (not walked). Not reachable by play:
  GSDelVar (ServerSession::RemoveVariable): the only uncommented calls are in zone13.mob and zone2.mob behind
  variables of the single-player story that a multiplayer game never sets.
- **Gap scenarios (H43c1, H43e1):** `net_host_death` (a hosted game's hero killed by a `hit` step in the game zone:
  LeaderDied's multiplayer branch, AddSharedObject, StopSpeech on the host's own client; the zone left, saved and
  entered again: SharedObject::Load), `unit_hit_turning` (a blow while the hero turns: HitWhileTurning),
  `unit_ai_healer` (bz2g's healing mercenary renumbered by make_trap_mob.py: only a unit whose id is no text key gets
  spell options, and no zone has one with a heal or protection, so IsNearPath runs on no shipped data). No caller at
  all: TrapObject (no record kind makes one; kept, units.md "Unreached in the shipped game"). The map specs' Apply,
  Remove and Refresh had no caller and are gone (H43j).

## What a run leaves alone

Harness runs need no clicks and leave the install alone (standalone-verification.md, "No run writes into an install"):
startup movies are skipped, the tutorial option is off, saves and `config\tutorial.ini` go to
`build\verify\harness_saves`, every other file the game writes under its folder goes to a copy-on-write overlay there
(reads prefer the overlay copy), settings are not written; run_all lists the installs' files before a suite and after
(install_watch.py) and fails the runs that overlap a change. Every game hosts and joins on a UDP port of its own instead of
8888 (but a pair's two games; `tests/verify/harness_net.h`) and binds on 127.0.0.1 only (the report's `NET:` line,
run_all's `network:` summary). The user's real mouse never reaches the game (`tests/verify/harness_input.h`, the
report's `MOUSE:` line; the harness's posted input and its GetCursorPos are the game's only mouse). For parallel runs the
harness ignores other game windows, keeps the game active when it loses focus, and opens data files for shared reading.
On Windows harness games never take the foreground and every window stays out of sight (the game window concealed before
it is first shown, any other window of the process concealed when the watch finds it; `EI_HARNESS_SHOW_WINDOW=1` shows
them to watch a run); the report's `WINDOW:` line and run_all's `game windows:` summary check it. On Linux the window is
shown on the game's display; run_all starts one Xvfb for the suite (`start_headless_display`: a free number from :99 down, a 3840x2160 screen
(the goldens' desktop: the `resolution desktop` scenarios, ui_widescreen_*, play differently on a smaller one),
every game's DISPLAY pointed at it, WAYLAND_DISPLAY unset, stopped when run_all ends), so a local run opens nothing on the
desktop; `EI_SHOW_WINDOWS=1` leaves the games on the session's display to watch them. Harness runs open no sound device (sound.md).

**The game window's size** (H62b). A windowed harness game's client area is always the size asked for (the resolution,
`windowsize`, the options' `SetClientSize`), even where it and the borders do not fit in the display's usable bounds:
centred there when it fits, else at their top left and past their right and bottom (harness_platform.cpp's
`PlaceClient`; the game's own backend makes it smaller instead). A smaller window would show the 960x720 frame scaled
into it, and every point given in window pixels would land elsewhere in the frame: a recorded scenario's clicks
(rec_save_load's Save button missed, so no save1 and the game stayed paused) and `lookat`'s minimap clicks
(render_water's camera a little off, one object seen a record sooner). That made render_water differ on GitHub's
Windows runner (1024x768) and both differ on the macOS one, not Metal: both differences come back exactly on a desktop
with the usable area cut to 1024x674. When the window is shown, a size the system changed (AppKit may fit a resizable
window taller than the screen) is set again. The log says it: `harness: the game window's client area WxH made|shown|set
(asked WxH; the display's usable area WxH)`. Each step and each replayed input event is logged with the frame the digest
counts (`harness: frame N: ...`), and so is each pause and resume of the game (the client world's).

## The harness's code

tests/verify: harness.cpp (the steps, the overlay, the report) and its parts; image/ (what reads the game's objects);
platform/ (the platform backend: platform.h's functions the harness's builds take in place of the game's,
harness_platform.cpp on SDL and game_answers.cpp for the calls a run answers; platform.md, "The harness's answers"); and
system/ (the harness's own system calls, harness_system.h with system_win32.cpp and system_posix.cpp, the sockets'
helpers, and Windows' sampling profiler and debug-register watch). CMakeLists.txt builds the `_win32` or `_posix` files
for its system. The harness patches nothing of the system: no import slot, hot patch or registry answer (nothing in the
program reads the registry; the settings are a file, harness_settings.h). The harness builds on Linux
(`-DEI_HARNESS=ON`; linux.md).

## Hangs, crashes and timeouts

- A game that hangs on exit is ended 3 s after the scenario quits (where its threads were: `exit_hang.txt` in the run
  folder). Exit hangs come and go with the machine's load and are only a note. An empty `exit_hang.txt`, or one that
  lists only threads at `ntdll!RtlUserThreadStart` with no frames, means the process was still ending in the kernel.
- A run that does not pass (a TIMEOUT above all) still has its digest and saves compared with its golden, and they
  differ where it stopped; `golden.py compare` and run_all's golden check put "the run did not pass (<its failures>)"
  first. H70: twice main's `--rebirth-only --golden` said "145 the same, 1 differ" with
  `LostInAstral/savea/visuals.sav` first (the clouds' drift 166.48 against 0.0675, the client generator, the
  template's moneyKey, a party record missing): mod_lost_in_astral_cz_ingos had timed out twice, and its savea was
  still the new game's autosave instead of gz31j's at the end. Nothing in the save depends on load or timing; the game
  did: six cut-scene bases (ten zone loads and a movie) took 24 s alone and 40 to 62 s with the Lost in Astral runs,
  the suite's longest, started together on the 16 slots (main-thread CPU 22 s alone, 26 to 31 s there, plus GPU waits:
  submissions 0.7 ms each alone, 7 to 10 ms there). It is two games now (`_cz_ingos`, `_cz_ingos_end`), and the
  harness's movies do not wait for real time ("Recorded play", Movies). A scenario should take under about a third of
  its limit alone.
- The quit is checked for a game that ended by itself after its scenario wrote `harness_quit` (PL22, run_all.exit_failures): an exit code other than 0 fails the run (`exit code C0000409` was an abort at exit, a joinable thread destroyed), and so do the game's own exit stages over `EXIT_SLOW_S` (2 s: the log's `exit: the quit request` to `exit: the exit functions done`; the stamps between them are the game stopped, the renderer destroyed and the settings saved). Those stages take about 30 to 100 ms, mostly the audio device's close. The time from the quit to the process's end is only a note (`game ended N s after the scenario quit`): a real quit ends the process 0.1 to 0.3 s after the request, but 16 games at once take 2 to 3 s and more in the system's teardown of the GPU memory (the 3 s hang note above).
- The game's output and run_all's lines for the run go to its `launch.txt`; a game that ends while its report still
  says `RESULT: RUNNING` gets a failure with its exit code (an INT3's named as such), and a harness that has not written
  its report 30 s after the start ends the run without one. A scenario's `# settings-file:` line is checked after the
  game has ended (run_all.check_settings_file). A `# arguments: <words>` line starts the exe with those arguments
  (run_all.scenario_arguments; effect_viewer_standalone.txt's `--effects Portal`).
- When the game crashes, the stack is in `ei-rebuild.log`; run_all prints it by function names under the run's line
  (outermost first) and `python tools\resolve_crash.py <run folder>` prints the named stack (`--raw`: every logged word).
- A run that exceeds its time limit logs where the main thread waits. No game outlives its limit: the watchdog thread in
  the game ends it at the scenario's `timeout` (60 s unless the scenario says more); if that thread cannot run, run_all
  ends the game 20 s later (and the game runs in a job object that dies with run_all, children included). run_all
  reports both as `TIMEOUT` (and says when the system could not end the process).
  `tests\scenarios\watchdog_*.txt` check this (`# expect: TIMEOUT`); `tools/test_run_all_timeout.py` checks run_all's
  side with a Python stand-in for the game.

## The Suites workflow

`.github/workflows/suites.yml` builds and plays the goldens on the self-hosted runner (Kirill's machine, labels
`self-hosted`, `ei-game`), which has the game installs and a GPU. Cloud sessions have no compiler or game data and test
through it.
- **Trigger**: by hand only (Actions > Suites > Run workflow, or the API) with `ref` and `suites` = `full` | `quick`
  (both build verify and release; quick plays the image suite, full also the Rebirth suite). Pushes stopped starting it
  on 2026-11-04: the runner is not always up and pushed runs queued for days. Debug and asan are built only by the coordinator's
  routine (every few merges; asan with a suite run on it). Never on pull_request. Pushes get the Linux suites (suites-linux.yml) instead.
- **Reading results**: the run's summary page has each suite's goldens block; a failing build prints its last 40 log
  lines in the step log. From a session: `gh run list --workflow suites.yml`, `gh run view <id> --log-failed`, or the REST
  API (`/repos/DoctorGester/ei-rebuild/actions/runs/<id>/jobs`, then `/actions/jobs/<id>/logs`). To wait for a run, poll
  `https://api.github.com/repos/DoctorGester/ei-rebuild/actions/workflows/suites.yml/runs` from a background command.
- **Where two builds part** (`diff_base`, with optional `scenarios`, `resolution`, `flags`, `records`, `skip`): plays the
  scenarios on both installs with the branch's verify build and with the base ref's and prints, for each run that
  differs, the first differing digest record field by field, the console lines, the failures and the window size. Use it
  before re-recording anything: every changed golden needs a cause.
- **Recording goldens** (`record` = both, vanilla or rebirth, optionally `scenarios` and `reason`; on a branch, not
  main): two passes that must agree, `tools/golden.py record`, and the goldens pushed to the branch as a commit of their
  own. That push starts no run, so dispatch a run on the branch after it.
- **Limits**: one run at a time (concurrency group `ei-game`); games share the machine's 16 slots with local work
  (through its scheduler); the runner is only up while Kirill is logged in.
- **Without the runner** a session can run the Python tools' own tests (`python -m unittest` in `tools/`) and the text
  checks that read only the source: `tools/check_cleanup.py`, `tools/comments.py check`, `tools/tickets.py`,
  `tools/data_seam.py --check`, `tools/named_data.py --check`, `tools/check_platform.py`,
  `tools/gen_settings_doc.py --check`. Linux builds there too (linux.md).
- A change that should not alter behaviour keeps every golden the same; an intended change re-records the affected
  goldens and says why in the commit.

## The Windows CI job

`.github/workflows/suites-windows.yml` ("Windows", H61; on pushes and pull requests like Linux, and by hand) is a smoke job like the macOS one below, on GitHub's
windows-latest runner (4 vCPU, 16 GB, no GPU), on SDL_GPU's Direct3D 12 driver with WARP, the software adapter. It was a
full suite (both suites over eight jobs, H61 to H62g) until the user cut it (2026-11-06): WARP draws a frame in 0.3 to
0.8 s, the shards took 70 to 140 minutes, most runs needed a time scale of 8 and a long skip list, and a few pictures
differ from the desktop's.
- **Builds**: `build-release` and `build-verify` in parallel, each `build.cmd` of its name (the runner's Visual Studio,
  found by vswhere and given as EI_VSROOT; no sccache: EI_COMPILER_CACHE=0; the shader toolchain's build folder cached),
  with the warnings and build checks included. The verify job uploads `build/verify/ei-rebuild.exe` and its `.map` (so
  run_all names a crashed run's stack; the stub data's game.exe is empty, which resolve_crash.py leaves out); a failed
  build check with the exe linked is a warning, so the smoke still runs. The input `builds_only` stops there.
- **Smoke**: the stub data fetched and unpacked as in the Linux and macOS jobs (same variable and secrets), vanilla only.
  `boot` alone first as a gate (the `gpu:` lines and logs are printed when it does not pass); then the `scenarios` input
  or `SMOKE_LIST` (the macOS list less render_stats: boot, smoke, settings_defaults, batch_menus, ui_screens, ui_hud,
  batch_items_saves, rec_save_load, batch_scripts, world_load, zone_gz3g, render_scene, render_present_shot,
  render_water, render_shadows; H74 merged and dropped some of the names the timings below are for), one game at a time, `--vanilla-only --golden`, EI_TIME_SCALE 8. All of them passed
  on the runner with none of their goldens differing (run 42124917682: 5 to 197 s each, about 15 minutes together; render_stats
  alone 616 s); the first run of the smoke job itself (42149539900) passed: builds 9 minutes, the smoke job 18 (17 games
  and the gate, all the same). The summary has the goldens block and the device line; `tools/ci_details.py` prints the runs that did
  not pass or differ (as the macOS job's "Differences in detail" below). The verdict fails the job on GOLDENS DIFFER,
  "not passed:" or no "GOLDENS THE SAME" line. For other scenarios, name them in `scenarios`.
- **WARP**: SDL's D3D12 driver takes the high-performance adapter (`EnumAdapterByGpuPreference`), which on the GPU-less
  runner is the Basic Render Driver, i.e. WARP. SDL has no switch for it, so third_party/SDL/README.txt lists ours:
  the hint (so the environment variable) `EI_GPU_D3D12_WARP=1` picks `IDXGIFactory4::EnumWarpAdapter` in the driver's
  support check and in the device. The workflow sets it everywhere; on a desktop with a GPU it runs the same path
  locally (`gpu: direct3d12 device (Microsoft Basic Render Driver; driver 10.0.19041.5794)` in the run's log tells it
  took effect and which WARP drew: the runner's is 10.0.26100.33648; `work.py test` carries every EI_* variable to the
  games).
- **Why it is slow** (H62d): the frame is WARP's work, not a stall of ours. The report's `TIMING: gpu:` line
  (standalone-verification.md, "Run time") has the main thread waiting for the swap chain texture for all but a few ms
  of each frame (on the runner 330 to 830 ms a frame; submissions 4 to 5 ms, download waits a few seconds a run, no
  pipeline made late), and the process's CPU is WARP's: a frame of the base bz1g costs about 1.8 CPU-seconds, one of
  render_weather_shelter about 3, on the desktop as on the runner, so 4 vCPUs draw it in 0.5 to 1 s. Split by pass
  (a scratch probe that submitted and waited after each pipeline's draws, locally on WARP): the terrain's and the
  meshes' draws in the frame target, the 4096 x 4096 sun map's casters, its cells and range, the occlusion and the
  water's copies, in that order; the terrain shader cut to its texture still cost half its time and the sun filter's
  taps a sixth, so no one shader part is the cause. One game at a time the 166 runs that passed on both took a median 2.4
  times as long as on Linux's lavapipe (run 42016421240 against 42040984380), hence EI_TIME_SCALE 8 against Linux's 3.
- **Microsoft's WARP was slower** (H62g, tried and dropped): NuGet's Microsoft.Direct3D.WARP 1.0.21
  (`build/native/bin/x64/d3d10warp.dll` placed next to `build/verify/ei-rebuild.exe`, which D3D12 loads instead of the
  in-box one; the run's `gpu:` line said `driver 1.0.21.0` against the in-box `driver 10.0.26100.33648`) took 1.61 times as
  long on the scenarios that passed in both (26374 s against 16374 s; median scenario 1.63 times), runs 42124922200
  and 42103335919 against 42124917682 on one commit, started together; the in-box run reproduced H62d's
  42085737374 (0.93 times, median 1.00). Its AVX2 codegen, wave size 8 and uniform jumps do not pay on 4 vCPUs. The
  download step was removed again; a later WARP version would need the package fetched by version and checksum
  (nuget.org lists the SHA512) next to the exe and a run of the same comparison.
- **A lost device** (H62f): WARP 10.0.26100 removed its device in bz1g's scenes (ui_base_dialogue, render_stats in the
  town, net_pair_database on Rebirth's): an access violation in its shader code reading 0xFFF0 bytes past a uniform
  buffer, the point shadows' arrays read at index -1 by lanes that do not use the answer (point_lights.hlsli's
  `point_index` keeps them in range now). SDL then crashed in D3D12_PushVertexUniformData; with the vendored SDL's
  change the run ends with "render: fatal: The renderer cannot present on the game window: the GPU device was lost:
  ..." (renderer.md, "No device-loss recovery"), and the harness's log holds the exception (`harness: exception
  C0000005 at <JIT address>`).
- **What the full runs showed** (2026-11-05/06, in-box WARP, scale 8, for whoever reopens it): the scenarios that say
  `resolution desktop` (the ui_widescreen ones, the net pair / zone ones with it) cannot get the 3840x2160 desktop the
  goldens were recorded on; camera_drag_rate, camera_smooth and camera_spin, render_weather and render_weather_units,
  render_drawn_lights, render_drawn_motion, render_weather_panel, ui_minimap_edges, unit_ai_bridge and
  zone_gz15h_tornado_client ran past their real-time limits or took 37 to 55 minutes; the seven render_device_lost_*
  ended with "the game did not crash" (`losedevice`'s RemoveDevice does not reach SDL's device with the WARP hint:
  open, H62h dropped with the full suite); render_load_town (unitcolour) and render_resize_windows (samecapture)
  differed in every run.


## The macOS CI job

`.github/workflows/suites-macos.yml` ("macOS", H62; on pushes and pull requests like Linux, and by hand with a `scenarios` input) is a smoke run on GitHub's
macos-latest runner (Apple silicon VM, 3 cores, 7 GB; whether it has a Metal device is what the first run answers). One
job, never Release, nothing published or uploaded:
- **Build**: the harness build as macos.md describes (`cmake -DEI_HARNESS=ON`, RelWithDebInfo, arm64, Ninja; pip's cmake
  3.31.6 and ninja, DXC from the LunarG Vulkan SDK under release.yml's cache keys, the shader toolchain's build folder
  cached, ccache under `.ccache` keyed by the commit). No ctest (its checks are built without the harness; release.yml's
  macos job runs them).
- **Data**: the stub set fetched and unpacked as in the Linux and Windows jobs (same variable `EI_STUB_DATA_REPO`, secrets
  `EI_STUB_DATA_TOKEN` / `EI_STUB_DATA_KEY`), vanilla only.
- **Gate**: `boot` alone first. When it does not pass (no Metal device, or it cannot draw) the step prints the `gpu:`
  lines, the run's launch.txt and ei-rebuild.log, and the job stops: that is the answer then.
- **Smoke**: `SMOKE_LIST` (boot, smoke, settings_defaults, batch_menus, ui_screens, ui_hud, batch_items_saves,
  rec_save_load, batch_scripts, world_load, zone_gz3g, render_scene, render_present_shot, render_water, render_shadows,
  render_stats; seconds each on a desktop), one game at a time, `--vanilla-only --golden`, `EI_TIME_SCALE` 3. The summary has the
  goldens block, the GPU device line and each run's `TIMING: median frame` (harness_report.txt). Goldens that differ on
  Metal are listed, not excused: no check is widened for it.
- **Differences in detail** (always runs; `tools/ci_details.py`): for each scenario that did not pass or differs (run
  folder `runs/<name>-standalone`), the report's failures, `golden.py compare --records 3 --fields 30` (the first
  differing digest records with their world update, frame and tick, and the fields); the log lines on the game window's
  client area (made, shown, set, with the size asked for and the display's usable area; `windowsize`; the resolution),
  the present mode ("render: presenting ... asked for mode N, got M"), the device, focus and the cursor confinement;
  then the log from 15 frames before the first differing record's frame to 2 after it, between the frame-stamped lines
  (each step, each replayed input event, each pause and resume; a failed run: its last 20), a crash's stack by function
  names, and its median frame. The harness
  drops every system event, so a focus or occlusion event cannot pause a game there; a `world paused 1` in a digest is the
  game's own pause (the menu). Metal has no Mailbox, so the harness's present mode falls back to Immediate (it fell back
  to Vsync before H62a: every frame 16.7 ms).
- **Verdict**: fails on GOLDENS DIFFER, "not passed:", or no "GOLDENS THE SAME" line (bash -e ignores a failed `! grep`
  that is not the step's last command, so H62's first run passed with a difference printed).
- **Folder rule**: PL19a's, with `EI_GAME_ROOT` unset: the harness-build app (no `EI_HARNESS`, so a player's start) copied
  into the data folder, then elsewhere with `game_folder.txt` naming the data; each must log `ei-rebuild started in`
  the data folder. The picker is not reached.
