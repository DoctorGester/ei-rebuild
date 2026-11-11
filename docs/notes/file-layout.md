# File layout

src/game's folders and file names: the plan (C168, 2026-11-05, with the user's decisions of the same day: "The
user's decisions" below). Until the moves land, src/game is the flat folder this file describes under "Today". The
map of every file is `tools/layout_map.txt`; `python tools/layout.py check` checks it against src/game (and that each
line's file is where the map's state allows: flat, moved, folded, or not made yet), `listing` writes the generated
part at the end of this file, `where <old name>` answers where a file of the flat tree goes and whether it moved,
`sizes` counts what each move batch rewrites, `apply <batch>` moves a batch (`--dry-run` first) and `rewrite`
brings a branch's includes and references to where the files are now.

## Today (measured 2026-11-05)

- src/game is one flat folder: 533 files (357 .cpp, 175 .h, one .inc), 132,865 lines. The rest of src: render 73
  files (37 sources, shaders, fonts), platform 31 (in sdl/, win32/, posix/ already), lib 30, video 11, audio 2.
- Files by first word: ui 94, world 40, unit 34 (5 of them unit_ai), net 27, ai 27, script 21, map 21 (map_object 16),
  combat 17, player 14, item 10, renderer 8, core 8, spell 7, particle 7, text 6, terrain 6, resource 6, object 6,
  mod 6, figure 6, app 6, party 5, link 5, game 5, console 5, camera 5; 69 more words with 1-4 files each.
- Twelve files pass 1,000 lines (ui_camp.cpp 1,744, ui_battle.cpp 1,669, ui_hud_side.cpp 1,489, particle_casting.cpp
  1,329, world_game.cpp 1,270, sound_manager.cpp 1,124, world.h 1,121, unit_ai.cpp 1,092, particle_blasts.cpp 1,065,
  script_exec.cpp 1,059, unit.h 1,053, script.h 1,005); the plan splits none of them (each is one subject).
- Includes (before C168a): 3,465 game includes were bare (`#include "unit.h"`, found beside the includer), 899 name
  `lib/`, 101 `platform/`, 82 `render/`; the harness names `game/<file>.h` (89). C168a rooted every game include at
  src (`#include "game/unit.h"`; 3,485 lines with the unit checks' `../../src/game/` and src/game's `../platform/`).

### Thin files

144 files are under 80 lines: 51 .cpp and 93 .h. Most of the headers are a small class's header beside its .cpp
(ui_spin.h, mod_list.h) or a shared data header (ack_kind.h, save_versions.h, frame_lists.h); they stay. The thin .cpp
files, read one by one:

| File | Lines | What it holds | Verdict |
|---|---|---|---|
| object_id_map.cpp | 7 | EffectIdMap::Set | fold: world/world_server.cpp (the id maps) |
| game_folders.cpp | 8 | three folder globals | fold: app/app.cpp |
| core_refcount.cpp | 13 | RefObject::Clone, Shutdown | fold: core/ref.cpp |
| database_globals.cpp | 14 | five database globals | split: each to its database's file |
| link_loader_create.cpp | 15 | LinkFigureLoader's constructor | fold: models/link_loader.cpp |
| ui_font.cpp | 23 | PointFont, TextFontOf, TextExtent | keep: ui/ui_font.cpp beside ui_font.h |
| options_construct.cpp | 27 | GameOptions' constructor and global | fold: data/options.cpp |
| player_sources.cpp | 27 | the two PlayerSource::TakeNew overrides | keep: players/player_source.cpp beside player_source.h |
| app_confine.cpp | 30 | UpdateCursorConfinement | fold: app/app.cpp (window events) |
| ui_person_texts.cpp | 30 | LoadPersonTexts | fold: ui/ui_briefing.cpp |
| misc_400.cpp | 31 | ClientSpecificManager (its comment lists three more pieces that are gone) | fold: objects/side_managers.cpp |
| ai_water_patch.cpp | 33 | AIMap::StampWaterPatch | fold: ai/ai_unit_layer.cpp |
| mod_plugins.cpp | 33 | FourShopClasses | keep: mods/mod_plugins.cpp |
| counted_ref.cpp | 34 | Ref<T> loaded by id, the unit lists | keep, renamed core/ref.cpp |
| text_helpers.cpp | 34 | TextTokenizer, RemoveCarriageReturns | fold: core/text_split.cpp |
| map_object_kinds.cpp | 35 | PlainMapObject's four virtuals | fold: objects/map_object.cpp |
| combat_spell_sight.cpp | 37 | SpellHasLineOfSight | fold: combat/combat_sight.cpp |
| ai_map_construct.cpp | 38 | AIMap's constructor and Reset | fold: ai/ai_map.cpp |
| ui_region_tips.cpp | 38 | UiRegion's tooltip setters | fold: ui/ui_window.cpp (UiRegion's members) |
| script_cleanup.cpp | 39 | Script's runtime resets | fold: script/script_exec_run.cpp |
| net_game_info.cpp | 48 | GameInfoRecord's Write / Read | fold: net/net_lobby.cpp |
| ai_fog_grid.cpp | 49 | FogGrid (effects.h) | fold: effects/world_effect_visibility.cpp, its one user |
| quest_records.cpp | 50 | the quest records' Serialize | fold: world/quest_database.cpp |
| server_specific_manager.cpp | 51 | ServerSpecificManager and TorchObjectSpec | split: objects/side_managers.cpp, objects/torch.cpp |
| engine_object.cpp | 52 | EngineObject's members | keep |
| manager_context.cpp | 53 | ManagerContext's members | keep |
| global_map_clouds.cpp | 56 | GlobalMapClouds | keep |
| figure_bones.cpp | 57 | FigureBone::Place | fold: models/animation_bones.cpp (FigureBone's other members) |
| menu_rects.cpp | 57 | FigureManager::LoadMenuRects | fold: models/figure_manager.cpp |
| game_buffer_strings.cpp | 58 | GameBuffer's and BufferCursor's strings | fold: core/buffer.cpp |
| ui_container.cpp | 58 | UiContainer's dispatch | keep: ui/ui_container.cpp (gains UiContainer's draw) |
| cursor_setup.cpp | 59 | the cursor's set-up and its 25 shapes | fold: ui/ui_cursor.cpp |
| combat_step_lists.cpp | 60 | AnimationSteps' members | fold: combat/combat_steps.cpp |
| lever_table.cpp | 61 | the lever records' table | fold: objects/lever.cpp |
| resource_manager.cpp | 62 | ResourceManager | keep |
| address_order.cpp | 63 | DrawBefore | keep, renamed world/object_order.cpp |
| app_resolution.cpp | 63 | the resolution changes | fold: app/app.cpp (window events) |
| text_split.cpp | 66 | Split, Join, ReplaceChar | keep: core/text_split.cpp |
| ui_spin.cpp | 66 | the spin control | keep |
| world_guard.cpp | 68 | the player data guard | keep: world/world_guard.cpp (its own subject) |
| class_template_stock.cpp | 69 | ClassTemplate's lifetime and stock generators | fold: players/class_templates.cpp |
| logic_settings.cpp | 69 | LogicSettings::Load | keep |
| ui_text.cpp | 69 | Interface3D's text layout | fold: ui/ui_interface.cpp |
| weather_noise.cpp | 72 | the weather noise curves | keep |
| player_manager.cpp | 73 | PlayerManager | keep |
| unit_equipment.cpp | 73 | a new unit's starting equipment | fold: units/unit_create.cpp |
| camera_trace.cpp | 76 | the camera mouse turn trace | keep: ui/camera_trace.cpp |
| object_tables.cpp | 77 | text keys and NamedFloatTable | split: data/text_manager.cpp, core/named_floats.cpp |
| app_frame.cpp | 78 | WinMain's frame step | fold: app/main_loop.cpp |
| exit_area.cpp | 78 | ZoneExit's members | fold: map/map_database.cpp |

The thin headers that fold: angles.h (one constant) into core/core_maths.h; ai_motion.h (OrderOf) into ai/ai_motion.h;
ai_unit_layer.h into ai/unit_shape.h; view_matrices.h into scene/pipeline.h; device_buffers.h into scene/device.h;
combat_plan.h into combat/combat_internal.h; net_session.h into net/net_session.h; text_helpers.h into core/text_split.h;
resource_stream.h into data/resource.h; map_object_kinds.h into objects/map_object.h; torch_client_spec.h into
objects/torch.h; trap_object.h into objects/traps.h. Thicker splits of one class or subject fold too (ai_occluders,
ai_bridges, ai_path_save, unit_armour_layers, unit_figure_queries, unit_lever, combat_order_setters,
net_connection_life, world_effect_sequence, resource_stream.cpp, hoover_loot, ui_equip_panels, ui_console_window):
55 folds in all, each with its reason in the listing.

### Address-named and grab-bag files

| File | What it really holds | Where it goes |
|---|---|---|
| misc_400.cpp | ClientSpecificManager only ("small pieces of the early code" by the exe's range) | objects/side_managers.cpp with ServerSpecificManager; ClientSpecificManager's class leaves base_manager.h for objects/side_managers.h |
| hash_tables_400.h | the id tables' map helpers (namespace hash400) | core/id_tables.h, namespace idtable |
| object_tables.h / .cpp | "small lookup tables found among the object code": text keys, the map database's records, NamedFloatTable, the key names | map/map_database.h (the records), core/named_floats.{h,cpp}, data/text_manager (text keys), app/key_bindings.h (UiKeyName, g_keyActions, FindCommandByKey) |
| object.h | Hoover only | world/hoover.h; hoover_loot.cpp into world/world_loot.cpp |
| model.h / model.cpp | the transform pipeline, the render presets, the scene renderer, the link loader, prints.db's records | scene/scene_renderer.h, scene/pipeline.{h,cpp}, scene/render_helper.h, models/link_loader.{h,cpp}, terrain/ground_print.h |
| model_db.cpp | prints.db's and acks.db's loads, WaterTweens, ReadPrefixedName | terrain/ground_print.cpp, units/acks_db.cpp, terrain/water_tweens.cpp, models/figure_manager.cpp (its one user) |
| database_globals.cpp | five globals of five databases | each into its database's file |
| world_rules.cpp | the local player's queries (GameTime, LocalPlayerData, PlayerZeroVariable ...) and DepthLess | world/local_player.cpp; DepthLess into world/world_client_maps.cpp, its one user |
| ui_container_draws.cpp | draws by kind: UiContainer's, the briefing screen's, the menu frame's, the equipment panel's, ClearUiDepth | each into its window's file |
| server_specific_manager.{h,cpp} | the server side manager and the torch's spec | objects/side_managers.*, objects/torch.* |
| spell.h | spells, and Light / LightFactory | Light to scene/light.h beside light.cpp |
| texture.h | textures, and the device (TextureDevice, MeshArgs) | scene/device.h for the device |
| main_loop.h | the harness's hooks into the loop, not the loop | app/harness_hooks.h |
| game_singletons.h, game_folders.cpp | the game's globals | app/singletons.h; the folders into app/app.cpp |
| world_game.cpp, world_briefing.cpp | GameFlow's members (not World's) | app/game_flow.cpp, app/game_flow_briefings.cpp |
| plugin_fixes.h | named for EI_Plugin: the player faces' rules | players/faces.h |
| net_classes.h | the sessions' classes and the message base | net/net_session.h |
| address_order.{h,cpp} | orders without addresses | world/object_order.*, namespace object_order |
| bink_video.{h,cpp} | the game's movie players (src/video/bink_video.cpp is the decoder) | app/movies.* |

C168b and C168c split these on the flat tree (C168c model.h and texture.h): the new files are flat under their final
names (named_floats.{h,cpp}, water_tweens.cpp, light.h) and the code went into the files the table names by their flat
names (text keys into text_manager, the key tables into key_bindings, DepthLess into world_client_maps.cpp, the database
globals and prints.db's and acks.db's loads into their databases' files, the container draws into their windows' files
with the screens' shared draw as DrawScreen in ui_container.cpp, TorchObjectSpec into torch.h / torch_object.cpp).
misc_400.cpp is folded into server_specific_manager.cpp already (`layout.py check` takes a fold made before its owner
moved). object_tables.h (now the map records alone), world_rules.cpp (the local player's queries) and
server_specific_manager.* (both side managers) keep their names until their move batches rename them; hash_tables_400.h's
namespace is `idtable`, address_order.h's `object_order`.

C168c: model.h keeps SceneRenderer (and AmbientColour) until C168g renames it scene/scene_renderer.h; the transform
pipeline (with ClipBounds, FootprintPart, the matrix stacks, SpritePoint, ToClip, the viewport, projection and view
matrices) is pipeline.h, RenderPreset / RenderHelper render_helper.h, the link loader link_loader.h (its archive list
moved from model.cpp to link_loader.cpp), prints.db's records joined ground_print.h, and ReadPrefixedName is
figure_manager.cpp's own. model.cpp (the pipeline's clip test and matrices) keeps its name until C168g. TextureDevice,
MeshArgs, DrawFlag, texture stage 0's setups and DevicePresent left texture.h for device.h. view_matrices.h is folded
into pipeline.h and device_buffers.h into device.h already. Each of model.h's and texture.h's includers names the
headers it uses; a header that only points at SceneRenderer, Texture or TextureDevice declares it.

C168d moved core/, data/, mods/ and saves/ (53 moves, 7 folds: core_refcount.cpp and game_buffer_strings.cpp,
angles.h, text_helpers.{h,cpp}, resource_stream.{h,cpp}); the other folders' files are still flat, and their includes
name the flat path until their batch. Each fold is the two files' text one after the other with one include list.

C168e moved app/, console/, sound/, world/, players/ and map/ (102 moves, 11 folds into 9 files: app/app.cpp takes
app_confine, app_resolution and game_folders; main_loop.cpp app_frame; options.cpp options_construct; console_window.cpp
ui_console_window; world_server.cpp object_id_map; world_loot.cpp hoover_loot; quest_database.cpp quest_records.cpp;
class_templates.cpp class_template_stock; map_database.cpp exit_area; 1,376 paths and 61 renamed names rewritten). A fold
reads as one file: one include list, one anonymous namespace (a name both had, kLineHeight in the console window, is one
definition), each former file a section marked `// --- Name ---`. `layout.py rewrite` leaves the arguments of
`layout.game_paths(...)` alone: they are flat names the map resolves (check_cleanup.py's exemptions). Prose that names a
file by a stem without extension (app_main, world_game, app_frame) is not rewritten: C168e's were read and changed by
hand; bink_video.* in the notes names either the game's movie player (app/movies.*) or the decoder in src/video, and the
decoder's references stay.

C168f moved ai/, combat/, objects/ and units/ (90 moves, 20 folds into 18 files, 1,048 paths and 61 renamed names
rewritten): ai_map.cpp takes ai_map_construct; ai_footprint.cpp ai_occluders and ai_bridges; ai_unit_layer.cpp
ai_water_patch; unit_shape.h ai_unit_layer.h (the occupancy layers' helpers); ai_path_state.cpp ai_path_save; combat's
combat_internal.h combat_plan.h; combat_orders.cpp combat_order_setters; combat_sight.cpp combat_spell_sight;
combat_steps.cpp combat_step_lists; objects/map_object.{h,cpp} map_object_kinds (the plain classes); lever.cpp
unit_lever and lever_table; torch.h torch_client_spec; traps.h trap_object; units/unit.cpp unit_figure_queries;
unit_create.cpp unit_equipment; unit_stats.cpp unit_armour_layers. ai/ai_motion.h is the old unit_motion.h (the
trajectory) with ai_motion.h folded in: its tail included ai_map.h, which includes unit.h, which includes the
trajectory, so OrderOf (MotionOrder of a PathOrder) moved into ai_map.h after PathOrder. A fold of a header
is one include list at the top, with the second file's classes and its forward declarations after the first's.
The include lines the rewrite left twice (a file that included both of a fold's files) are once.

C168g moved items/, spells/, effects/, terrain/, models/ and scene/ (102 moves, 5 folds into 5 files, 1,082 paths and
10 renamed names rewritten): effects/world_effect.cpp takes world_effect_sequence (EffectSoundSeq's Start and Unpack,
its choosers joining the one anonymous namespace); effects/world_effect_visibility.cpp ai_fog_grid (FogGrid, with
kNoOrigin); models/figure_manager.cpp menu_rects (LoadMenuRects, g_menuRects, kRects; the class name and the figure
factory's Create share its one namespace); models/animation_bones.cpp figure_bones (FigureBone::Place); models/
link_loader.cpp link_loader_create (the constructor). model.h is scene/scene_renderer.h and model.cpp
scene/pipeline.cpp. No fold was of a header, so none made an include cycle. Names the batch kept (device_draw.cpp,
renderer_scene.cpp) stay bare in comments and notes.

C168h moved script/, net/ and ui/ (135 moves, 9 folds into 9 files, 1,338 paths and 35 renamed names rewritten): the
last batch, so no file is flat in src/game any more. script/script_exec_run.cpp takes script_cleanup (Script's resets;
its include list joins the file's); net/net_connection.cpp net_connection_life (Connection's state and tick: the
second anonymous namespace's Age and kRttKeep join the first); net/net_lobby.cpp net_game_info (GameInfoRecord, with
Sanitize); ui/ui_interface.cpp ui_text (the text layout); ui/ui_window.cpp ui_region_tips (kTipText joins the
namespace); ui/ui_cursor.cpp cursor_setup (ShapeFiles and kShapes join the file's helpers, which are now one
anonymous namespace at the top); ui/ui_briefing.cpp ui_person_texts; ui/ui_hero_panels.cpp ui_equip_panels (the three
anonymous namespaces are one). net/net_session.h took the pump's header (Pump and the two Init...Pointer
declarations) as a last section inside namespace net (the folded text had included net_session.h again), and
three files that included it twice (game_flow.cpp, main_loop.cpp, net_session.cpp) have it once. A cycle check over
every src/game header found only ai/sector_map.h and ai/ai_map.h, which were one by design before (the AI map embeds
the sector map; sector_map.h ends by including ai_map.h). The plumbing C168a kept for flat files is gone: CMake's glob
of `src/game/*.cpp`, and coverage.py's name patterns (GAME_AREAS; a file's area is its folder's).

`*_internal.h` (combat, spell effects, world effects) and `script_parse.h` are private headers of one family: they
stay (combat_plan.h joins combat's). ui_float.h (FloatFromBits for constants written as raw bits, Truncate) moves as
ui/ui_float.h; its raw-bit constants are a cleanup of their own (they should be float literals), not a move.

## The rules

- **Folders by subsystem**, chosen from what the code is: a class's members live in its subsystem's folder, whatever
  their file's first word said (world_game.cpp is GameFlow's, so app/; ai_fog_grid.cpp is the visibility effect's, so
  effects/). One level under src/game; no folder takes a name of src's (lib, render, platform, audio, video, game).
- **Names keep their folder word** (user 2026-11-05): a file moves under its name (ui/ui_battle.cpp,
  net/net_lobby.cpp, units/unit_stats.cpp, ai/unit_ai.cpp), so every name stays unique in src/game and a bare
  `ui_battle.cpp` in a comment or a note still says which file. A file is renamed only to say what it holds (the
  grab-bag table above, and a file named for its partner: player_sources.cpp is players/player_source.cpp beside
  player_source.h); every such line in the map gives its reason; `layout.py check` refuses one without.
- **References:** a renamed or folded file's references name its new path (`app/game_flow.cpp`; `layout.py rewrite`
  does it); a kept name may stay bare in comments and notes. Paths name the folder: `src/game/world/world_save.cpp`,
  `#include "game/world/world_save.h"`.
- **Includes are rooted at src:** `#include "game/units/unit.h"`, as `lib/`, `render/` and `platform/` already are and
  as the harness names game headers; the same in the folder itself. No include path is added; every target already
  has `src`.
- **One class or one subject per file; a split only when a file is genuinely large or the subject is separate.** A
  class split across thin files folds (ai_map_construct.cpp's AIMap constructor joins ai/ai_map.cpp); a thin file with a
  subject of its own and a header of its own stays (world/world_guard.cpp, ui/ui_spin.cpp). Large files with one subject stay
  whole (ui/ui_camp.cpp).
- **A fold keeps the code as it is:** the two files' text one after the other, their includes merged, two anonymous
  namespaces made one (a name defined in both is one definition or two names). Nothing else changes in a fold.

## The engine library

`ei_engine` (CMakeLists.txt, MF15k1) is the part of the code a program without the game links: the effect viewer's own
exe (MF15k4) after the particle core's host interface (MF15k2) and a figure drawn without a unit (MF15k3). It is
`src/lib` and `src/game`'s `core/`, `data/`, `models/`, `scene/` and `mods/` folders, and the particle core in
`effects/` (its `particle_*` files: the sources, their manager and set-up, the data effects' records, expressions and
runtime, the built-in effects); `ei_game` is the rest of src/game with src/audio and src/video. Both are object
libraries; a program that links ei_game links ei_engine too (an object library's objects reach only the targets that
link it themselves).

- **The engine includes no game header.** `tools/engine_layers.py` (a build check) fails on an include from an
  engine file of any other src/game file; the engine may include its own files, src/render, src/platform, third_party
  and the system's. `tools/engine_layers.txt` lists the includes still crossing (the back-references left); the list
  only shrinks: a new crossing fails, and so does a listed one that is gone (`engine_layers.py --write` rewrites the
  list, keeping each kept line's comment).
- Files are in the engine's folders or the game's by what they need: the settings and options are data/'s (the
  renderer and the camera read them), as are maps as folders and the .MP header; the maps folder's functions are
  mods/maps_folder.h's; the peers' mod identity is net/'s; the particle core's calls into the world are
  `particle_host.h`'s functions, which the game defines (effects/effects_particles.cpp).
- The check reads includes only. Code of the engine that calls a function defined in ei_game links only because both
  are in the same programs today; such calls are the seams MF15k2 turns into a host interface.

## The target tree

| Folder | Files | What |
|---|---|---|
| `core/` | 27 | the engine's object model (EngineObject, BaseManager, ManagerContext), counted references, id tables, byte streams, buffers and the LZ coder, maths, transforms, random, UTF-8 text, text splitting, named floats |
| `data/` | 13 | resource archives and their manager, registry and .ini files, the chunk archive, the databases' plumbing, the text manager, the settings file and the game options, maps as folders and the .MP header |
| `mods/` | 6 | Rebirth's mod layer: mod folders, the menu's mod list, the mods' plugins, the maps folder |
| `saves/` | 7 | the save file container, save versions, save pictures, the client's kept picture |
| `app/` | 25 | WinMain's loop, the window, game flow (with Rebirth's kept "gvar." variables), options, settings, key bindings, movies, the globals |
| `console/` | 5 | the developer console, its commands and window |
| `sound/` | 3 | the sound manager and music |
| `world/` | 35 | World, WorldServer, WorldClient: zones, ticks, sync, saves of the world, quests, loot, diplomacy, client maps and draw lists |
| `players/` | 23 | Player, PlayerServer / PlayerClient (messages, handlers, sync), the player manager, parties, class templates |
| `map/` | 11 | zone files (.mob specs, the reader, chunked files), the map database and its exits, the .MP header, the maps folder |
| `ai/` | 28 | the AI map, footprints, unit layers, searches, regions, paths, motion, events, the sector map; the brain and its modes |
| `combat/` | 13 | the server unit controller (UnitServer): orders, steps, sight, ticks, saves |
| `objects/` | 22 | MapObject and Object3D with their halves, side managers, net state, levers, torches, traps |
| `units/` | 27 | Unit: creation, records and databases, statistics and vitals, hits, items, skins, the client controller, speech (acks) |
| `items/` | 10 | items, their database, manager, names and trader stock |
| `spells/` | 7 | spells, their database and manager, spell effects |
| `effects/` | 23 | particles (their core, `particle_*`, is the engine's), the world's effect objects, lightning, weather, the visibility fog |
| `terrain/` | 12 | the landscape, sectors, ambient sounds, ground prints and decals, water levels |
| `models/` | 20 | figures, figure meshes and animation, link meshes and their loader, textures, materials, bitmaps |
| `scene/` | 30 | the game's draw side over src/render: scene renderer, transform pipeline, device draws, the old renderer's draws, surfaces, camera, lights, sky, drawn motion |
| `script/` | 20 | the mission script engine and Rebirth's script commands |
| `net/` | 21 | sessions, socket driver, connections, links, messages, delta replication, lobby, game server and client |
| `ui/` | 94 | the interface: manager, windows and controls, cursor, layout, menus, screens, battle HUD, briefing, camp and global map, hero screens, item panels |

482 files after the moves (533 today, 55 folded, 4 split files gone, 8 new from splits), in 23 folders.

The rest of src:
- `src/render` (37 sources, all but seven named `render_*`): names keep their folder word, so it has nothing to
  change (C168j dropped).
- `src/lib`: Quaternion's out-of-line members are in src/lib/vecmath.cpp beside lib/vecmath.h (C168b; they were in
  core_maths.cpp and projection.cpp). Nothing else.
- `src/platform` is by backend already; `src/audio` and `src/video` are fine (the game's bink_video.* becomes
  app/movies.*, so the two files of that name go). `tests/verify` (the harness) stays flat: out of scope.

## The batches

Each batch is its own ticket, builds verify and release with every build check at 0, runs the image and Rebirth
suites with `--golden` (every golden the same: nothing in a batch changes behaviour; a fold that changes a golden is
read, not re-recorded: two files' code in one object can inline differently under /fp:fast) and the unit checks, and
updates the notes' and tools' file references. Every move batch is a script run (`git mv`, the include and reference
rewrite, CMake) plus the folds by hand; the folds are a concatenation each. Sizes from `python tools/layout.py
sizes` on 2026-11-05 ("other references": comments, notes, tools, tests, config; done tickets are history and stay).

| Batch | What | Size | Model |
|---|---|---|---|
| C168a | The mover, `tools/layout.py apply <batch>` (`git mv`, each fold concatenated onto its owner, then the rewrite; `--dry-run`) and `rewrite` (idempotent: every game include rooted at src, any include or reference of a moved or folded file to its path now, the baselines' rows summed for folds, CMake lists without repeats); CMake's game glob to `src/game/*.cpp` and `src/game/*/*.cpp`; coverage.py's areas by folder; check_cleanup.py's path lists and families by the map; every game include rooted at src (`#include "game/unit.h"`) with no move yet | ~3,470 include lines in ~530 files, ~10 tools | Opus |
| C168b | The grab-bag splits and meaning renames that change code: object_tables.{h,cpp}, world_rules.cpp, misc_400.cpp with server_specific_manager.*, model_db.cpp, database_globals.cpp, ui_container_draws.cpp, Light out of spell.h, hash400 -> idtable, address_order -> object_order, Quaternion's members to src/lib/vecmath.cpp; new files flat under their final names | ~20 files, ~60 includers | Opus |
| C168c | model.h split (74 includers: each gets the headers it uses) and TextureDevice / MeshArgs out of texture.h with device_buffers.h and view_matrices.h | ~100 files | Opus |
| C168d | Moves: core, data, mods, saves (60 files, 7 folds) | 628 include lines, 350 references | Sonnet |
| C168e | Moves: app, console, sound, world, players, map (113 files, 11 folds) | 981, 705 | Sonnet |
| C168f | Moves: ai, combat, objects, units (111 files, 21 folds) | 700, 658 | Sonnet |
| C168g | Moves: items, spells, effects, terrain, models, scene (104 files, 7 folds) | 667, 692 | Sonnet |
| C168h | Moves: script, net, ui (145 files, 9 folds) | 602, 1,275 | Sonnet |
| C168i | Only the misplaced classes (user 2026-11-05), each into a header of its own: MapObjectManager and MapObjectType out of unit_create.h (objects/map_object_manager.h), Player out of player_messages.h (players/player.h), the spell records, tables and database out of item_db.h (spells/spell_db.h; the record table template stays in item_db.h), CreditsScreen out of ui_screens.h with its members from four files in ui/ui_credits.cpp. The subject splits of ui_camp.h, ui_screens.h and effects.h are dropped (cosmetic) | ~80 files (includes) | Opus |

How a move batch runs (the tool is C168a's; `python tools/test_layout.py` tests it on a tree of its own):
1. `python tools/layout.py check` (0 problems), then `python tools/layout.py apply C168x --dry-run`: the moves, the
   folds and what the rewrite would change. It refuses while the map does not match src/game or a file of the batch
   is not made yet (its split batch, C168b or C168c, first).
2. `python tools/layout.py apply C168x`: `git mv` of each move; each fold's text in place of its owner's include of it
   (the owner uses it) or after the owner's text, a header's `#pragma once` dropped, the folded file `git rm`ed; then
   the rewrite.
3. By hand: each fold tidied (one include list, one anonymous namespace, the file comment), the batch's old stems
   grepped in docs and comments, `layout.py listing`, the builds.

The rewrite (`layout.py rewrite`, also run by `apply`) changes, for every file whose line says it moved (OLD gone, NEW
there): a game include not rooted at src (bare, `../`) to `game/...`; `src/game/OLD` and `game/OLD` to the path now in
src, tests, tools, docs, config, CMakeLists.txt and .github (not the tickets, the goldens or the plan itself); a
renamed or folded file's bare name in prose to its new path (not a name a file outside src/game has too: bink_video.*
is printed and left to a reading); the rows of config/cleanup_baseline.csv and config/comment_baseline.csv (a fold's
summed into its owner's); a CMake command's repeated src/game source. A kept name stays bare, it is still unique.
The tools that name game files ask the map where they are now (`layout.now`, `layout.game_paths`): check_cleanup.py's
exemption lists and its families (by the flat name), gen_settings_doc.py, named_data.py; coverage.py gives a folder's
files its area.

Order and running work:
- C168a first: every later batch uses its tool, and its rooted includes are what lets a moved file include a flat
  one. C168b then C168c (C168c takes prints.db's records next to the load C168b moved).
- C168d-h touch the includes of most of src/game: they run back to back in a quiet window, each merged and pushed
  before the next starts, while the other workers stay off src/game (harness, renderer src/render, docs, tools are
  fine). A branch that was open across a batch merges main and runs `python tools/layout.py rewrite`, which moves its
  includes and references to the new paths; a conflict on an include line is resolved by taking both sides and
  rewriting.
- The order d-h: the base headers first (core, data: included everywhere, so the later batches' files already name
  them by their final paths), then the subsystems from the world outwards; ui last (the most files, the most
  references).
- Since C168h `tools/layout.py check` finds no flat file; the map stays as the old-name lookup (`where`) for old
  tickets, notes in other branches and memory.

Risks:
- A fold puts two files' anonymous namespaces into one object: a helper or constant both define (a `kNone`, a `Lit`)
  is one definition or two names; the build finds them.
- database_globals.cpp's five globals move into five objects: their construction order changes. They are plain
  containers built empty and nothing in a static initialiser reads them (checked in C168b); the goldens cover the
  rest.
- References in prose without an extension (`ui_camp`'s pages) are not rewritten by the tool: each batch greps its
  old stems in docs and comments after the run.
- Workers and notes say the old names for a while: `layout.py where` answers them.

## The user's decisions (2026-11-05)

- Names keep their folder word (ui/ui_battle.cpp, not ui/battle.cpp): unique names, and no reference to a file
  changes its name unless the file is renamed for meaning. The plan had dropped the word.
- ui/ stays one level (94 files); no ui/hud, ui/briefing, ui/camp.
- scene/ and models/ stay two folders: the draw side apart from the assets it draws.
- C168j (src/render) is dropped: with the folder word kept it has nothing to change. C168i does only the misplaced
  classes.

## The listing

<!-- layout.py listing: generated from tools/layout_map.txt, do not edit by hand -->

### The tree after the moves

| Folder | Files | Lines today | Move batch |
|---|---|---|---|
| `ai/` | 28 | 12552 | C168f |
| `app/` | 18 | 5308 | C168e |
| `combat/` | 13 | 6383 | C168f |
| `console/` | 5 | 2038 | C168e |
| `core/` | 28 | 3902 | C168d |
| `data/` | 25 | 8608 | C168d |
| `effects/` | 28 | 9530 | C168g |
| `items/` | 10 | 2958 | C168g |
| `map/` | 11 | 3139 | C168e |
| `models/` | 25 | 8220 | C168g |
| `mods/` | 9 | 802 | C168d |
| `net/` | 23 | 8423 | C168h |
| `objects/` | 23 | 8216 | C168f |
| `players/` | 24 | 5478 | C168e |
| `saves/` | 7 | 848 | C168d |
| `scene/` | 30 | 4393 | C168g |
| `script/` | 33 | 11590 | C168h |
| `sound/` | 3 | 1679 | C168e |
| `spells/` | 9 | 2214 | C168g |
| `terrain/` | 12 | 3560 | C168g |
| `ui/` | 97 | 33145 | C168h |
| `units/` | 27 | 9843 | C168f |
| `world/` | 35 | 10253 | C168e |
| all | 523 | 163082 | |

570 files today; 55 fold into an owner, 10 split by meaning, 12 new files come out of the splits, 36 moves rename for meaning.

### Every file

Op: blank a move (under the same name unless a reason is given), **fold** the content joins the destination, **split** divided by meaning (the destination keeps the main part), **new** a file a split creates.

#### `ai/` (C168f)

| Today | After | Op | Why |
|---|---|---|---|
| ai_alarm.h | ai/ai_alarm.h |  |  |
| ai_block_search.cpp | ai/ai_block_search.cpp |  |  |
| ai_events.cpp | ai/ai_events.cpp |  |  |
| ai_event_table.h | ai/ai_events.h |  | named for ai_events.cpp, its code |
| ai_footprint.cpp | ai/ai_footprint.cpp |  |  |
| ai_bridges.cpp | ai/ai_footprint.cpp | fold | a footprint's bridge decks (86 lines) |
| ai_occluders.cpp | ai/ai_footprint.cpp | fold | one footprint cell's occluder word (83 lines): the footprint stamps them |
| ai_local_search.cpp | ai/ai_local_search.cpp |  |  |
| ai_map.cpp | ai/ai_map.cpp |  |  |
| ai_map_construct.cpp | ai/ai_map.cpp | fold | AIMap's constructor and Reset (38 lines) |
| ai_map.h | ai/ai_map.h |  |  |
| ai_map_load.cpp | ai/ai_map_load.cpp |  |  |
| ai_motion.cpp | ai/ai_motion.cpp |  |  |
| unit_motion.h | ai/ai_motion.h |  | UnitMotion, whose code is ai_motion.cpp |
| ai_motion.h | ai/ai_motion.h | fold | OrderOf (11 lines) |
| ai_movement.cpp | ai/ai_movement.cpp |  |  |
| ai_path.cpp | ai/ai_path.cpp |  |  |
| ai_path_smooth.cpp | ai/ai_path_smooth.cpp |  |  |
| ai_path_state.cpp | ai/ai_path_state.cpp |  |  |
| ai_path_save.cpp | ai/ai_path_state.cpp | fold | a path in the save stream (80 lines) beside its network state: one subject |
| ai_refresh.cpp | ai/ai_refresh.cpp |  |  |
| ai_region_reach.cpp | ai/ai_region_reach.cpp |  |  |
| ai_unit_layer.cpp | ai/ai_unit_layer.cpp |  |  |
| ai_water_patch.cpp | ai/ai_unit_layer.cpp | fold | AIMap::StampWaterPatch (33 lines): a shape on the small-unit layer |
| ai_unit_shape.cpp | ai/ai_unit_shape.cpp |  |  |
| search_grid.h | ai/search_grid.h |  |  |
| sector_map.cpp | ai/sector_map.cpp |  |  |
| sector_map.h | ai/sector_map.h |  |  |
| unit_ai.cpp | ai/unit_ai.cpp |  |  |
| unit_ai.h | ai/unit_ai.h |  |  |
| unit_ai_modes.cpp | ai/unit_ai_modes.cpp |  |  |
| unit_ai_senses.cpp | ai/unit_ai_senses.cpp |  |  |
| unit_ai_targets.cpp | ai/unit_ai_targets.cpp |  |  |
| ai_shape_cache.h | ai/unit_shape.h |  | UnitShape and its cache: named for the shape |
| ai_unit_layer.h | ai/unit_shape.h | fold | the shape cache's creator and the block hash (17 lines) |

#### `app/` (C168e)

| Today | After | Op | Why |
|---|---|---|---|
| app.cpp | app/app.cpp |  |  |
| app_confine.cpp | app/app.cpp | fold | the cursor confinement (30 lines): a window event, declared in app.h |
| app_resolution.cpp | app/app.cpp | fold | resolution changes (63 lines): window events, declared in app.h |
| game_folders.cpp | app/app.cpp | fold | three folder globals (8 lines) |
| app.h | app/app.h |  |  |
| world_game.cpp | app/game_flow.cpp |  | GameFlow's members, not World's |
| game_flow.h | app/game_flow.h |  |  |
| world_briefing.cpp | app/game_flow_briefings.cpp |  | GameFlow's briefing variables |
| main_loop.h | app/harness_hooks.h |  | holds only the harness's entry points into the loop (InSite, HarnessHooks) |
| key_bindings.cpp | app/key_bindings.cpp |  |  |
| key_bindings.h | app/key_bindings.h |  | gains UiKeyName, g_keyActions and FindCommandByKey from object_tables.h (C168b) |
| logic_settings.cpp | app/logic_settings.cpp |  |  |
| logic_settings.h | app/logic_settings.h |  |  |
| app_main.cpp | app/main_loop.cpp |  | WinMain: start-up, main loop, shutdown |
| app_frame.cpp | app/main_loop.cpp | fold | WinMain's frame step (78 lines): part of the loop |
| bink_video.cpp | app/movies.cpp |  | with app/movies.h |
| bink_video.h | app/movies.h |  | the game's movie players; src/video/bink_video.cpp is the decoder of the same name |
| persistent_globals.cpp | app/persistent_globals.cpp |  | Rebirth's kept "gvar." variables; game flow clears the tables |
| present_window.cpp | app/present_window.cpp |  |  |
| present_window.h | app/present_window.h |  |  |
| game_singletons.h | app/singletons.h |  | the game's global objects; "game" says nothing inside src/game |
| tool_hooks.h | app/tool_hooks.h |  |  |

#### `combat/` (C168f)

| Today | After | Op | Why |
|---|---|---|---|
| combat.cpp | combat/combat.cpp |  |  |
| combat.h | combat/combat.h |  |  |
| combat_actions.cpp | combat/combat_actions.cpp |  |  |
| combat_internal.h | combat/combat_internal.h |  |  |
| combat_plan.h | combat/combat_internal.h | fold | the planner's structures (20 lines), shared by the same combat files |
| combat_motion.cpp | combat/combat_motion.cpp |  |  |
| combat_orders.cpp | combat/combat_orders.cpp |  |  |
| combat_order_setters.cpp | combat/combat_orders.cpp | fold | "the rest of the setters" (140 lines): one subject split at an arbitrary line |
| combat_planning.cpp | combat/combat_planning.cpp |  |  |
| combat_save.cpp | combat/combat_save.cpp |  |  |
| combat_sight.cpp | combat/combat_sight.cpp |  |  |
| combat_spell_sight.cpp | combat/combat_sight.cpp | fold | the spell's line of sight (37 lines) |
| combat_steps.cpp | combat/combat_steps.cpp |  |  |
| combat_step_lists.cpp | combat/combat_steps.cpp | fold | AnimationSteps' members (60 lines): the steps' own list |
| combat_taming.cpp | combat/combat_taming.cpp |  |  |
| combat_tick.cpp | combat/combat_tick.cpp |  |  |
| combat_update.cpp | combat/combat_update.cpp |  |  |

#### `console/` (C168e)

| Today | After | Op | Why |
|---|---|---|---|
| console.cpp | console/console.cpp |  |  |
| console.h | console/console.h |  |  |
| console_addon.cpp | console/console_addon.cpp |  |  |
| console_commands.cpp | console/console_commands.cpp |  |  |
| console_window.cpp | console/console_window.cpp |  |  |
| ui_console_window.cpp | console/console_window.cpp | fold | one class (ConsoleWindow) in two files: its text side and its surface |

#### `core/` (C168d)

| Today | After | Op | Why |
|---|---|---|---|
| asset_text.cpp | core/asset_text.cpp |  |  |
| asset_text.h | core/asset_text.h |  |  |
| base_manager.cpp | core/base_manager.cpp |  |  |
| base_manager.h | core/base_manager.h |  |  |
| game_buffer.cpp | core/buffer.cpp |  | GameBuffer / GrowingBuffer members |
| game_buffer_strings.cpp | core/buffer.cpp | fold | GameBuffer's and BufferCursor's string members (58 lines): same classes |
| net_coder.cpp | core/coder.cpp |  | with core/coder.h |
| net_coder.h | core/coder.h |  | the byte buffers, bit cursors and LZ coder of saves and messages alike (not network-only) |
| core_maths.cpp | core/core_maths.cpp |  |  |
| core_maths.h | core/core_maths.h |  |  |
| angles.h | core/core_maths.h | fold | one constant (kRadiansPerDegree) |
| core_refcount.h | core/core_refcount.h |  |  |
| core_streams.cpp | core/core_streams.cpp |  |  |
| core_streams.h | core/core_streams.h |  |  |
| core_transform.cpp | core/core_transform.cpp |  |  |
| core_transform.h | core/core_transform.h |  |  |
| engine_globals.h | core/engine_globals.h |  |  |
| engine_object.cpp | core/engine_object.cpp |  |  |
| engine_object.h | core/engine_object.h |  |  |
| hash_tables_400.h | core/id_tables.h |  | address-named: the id tables' map members (namespace idtable since C168b) |
| manager_context.cpp | core/manager_context.cpp |  |  |
| manager_context.h | core/manager_context.h |  |  |
| named_floats.cpp | core/named_floats.cpp | new | NamedFloatTable's members, out of object_tables.cpp |
| named_floats.h | core/named_floats.h | new | NamedFloatTable, out of object_tables.h |
| random.cpp | core/random.cpp |  |  |
| random.h | core/random.h |  |  |
| counted_ref.cpp | core/ref.cpp |  | the counted references' out-of-line code (Ref<T> by id, the unit lists): named for ref.h |
| core_refcount.cpp | core/ref.cpp | fold | RefObject's two members (13 lines) beside the references' save and load |
| ref.h | core/ref.h |  |  |
| text_split.cpp | core/text_split.cpp |  |  |
| text_helpers.cpp | core/text_split.cpp | fold | TextTokenizer's members (34 lines), with text_helpers.h |
| text_split.h | core/text_split.h |  |  |
| text_helpers.h | core/text_split.h | fold | TextTokenizer and RemoveCarriageReturns: cutting text, the subject of text_split |

#### `data/` (C168d)

| Today | After | Op | Why |
|---|---|---|---|
| chunk_archive.cpp | data/chunk_archive.cpp |  |  |
| chunk_archive.h | data/chunk_archive.h |  |  |
| database_file.h | data/database_file.h |  |  |
| database_globals.cpp | data/database_file.h | split | each database's global to its database's file: spells/spell_db.cpp, items/item_db.cpp, world/quest_database.cpp, terrain/ground_print.cpp, units/acks_db.cpp (C168b; the file goes) |
| database_toml.cpp | data/database_toml.cpp |  |  |
| database_toml.h | data/database_toml.h |  |  |
| ini_file.cpp | data/ini_file.cpp |  |  |
| ini_file.h | data/ini_file.h |  |  |
| map_file.h | data/map_file.h |  | kept whole: map/file.h would not say which file |
| map_folder.cpp | data/map_folder.cpp |  |  |
| map_folder.h | data/map_folder.h |  |  |
| options.cpp | data/options.cpp |  |  |
| options_construct.cpp | data/options.cpp | fold | GameOptions' constructor and global (27 lines) |
| options.h | data/options.h |  |  |
| record_names.h | data/record_names.h |  |  |
| registry.cpp | data/registry.cpp |  |  |
| registry.h | data/registry.h |  |  |
| resource.cpp | data/resource.cpp |  |  |
| resource_stream.cpp | data/resource.cpp | fold | MappedResource's name listing |
| resource.h | data/resource.h |  |  |
| resource_stream.h | data/resource.h | fold | "the rest of the resource code": WildcardMatch (8 lines) |
| resource_manager.cpp | data/resource_manager.cpp |  |  |
| resource_manager.h | data/resource_manager.h |  |  |
| settings.cpp | data/settings.cpp |  |  |
| settings.h | data/settings.h |  |  |
| settings_catalog.inc | data/settings_catalog.inc |  |  |
| settings_document.cpp | data/settings_document.cpp |  |  |
| text_manager.cpp | data/text_manager.cpp |  |  |
| object_tables.cpp | data/text_manager.cpp | split | text keys -> data/text_manager.cpp, NamedFloatTable -> core/named_floats.cpp (C168b; the file goes) |
| text_manager.h | data/text_manager.h |  |  |

#### `effects/` (C168g)

| Today | After | Op | Why |
|---|---|---|---|
| effects.h | effects/effects.h |  |  |
| effects_particles.cpp | effects/effects_particles.cpp |  |  |
| effects_weather.cpp | effects/effects_weather.cpp |  |  |
| embed_effects.cmake | effects/embed_effects.cmake |  |  |
| lightning.cpp | effects/lightning.cpp |  |  |
| lightning.h | effects/lightning.h |  |  |
| particle_create.cpp | effects/particle_create.cpp |  |  |
| particle_data.cpp | effects/particle_data.cpp |  |  |
| particle_data.h | effects/particle_data.h |  |  |
| particle_expr.cpp | effects/particle_expr.cpp |  |  |
| particle_expr.h | effects/particle_expr.h |  |  |
| particle_host.h | effects/particle_host.h |  |  |
| particle_manager.cpp | effects/particle_manager.cpp |  |  |
| particle_record.cpp | effects/particle_record.cpp |  |  |
| particle_record.h | effects/particle_record.h |  |  |
| particle_source.h | effects/particle_source.h |  |  |
| vis_fog_manager.cpp | effects/vis_fog_manager.cpp |  |  |
| weather_field.cpp | effects/weather_field.cpp |  |  |
| weather_field.h | effects/weather_field.h |  |  |
| weather_particles.cpp | effects/weather_particles.cpp |  |  |
| world_effect.cpp | effects/world_effect.cpp |  |  |
| world_effect_sequence.cpp | effects/world_effect.cpp | fold | EffectSoundSeq's Start and Unpack (80 lines): its other members are there |
| world_effect.h | effects/world_effect.h |  |  |
| world_effect_internal.h | effects/world_effect_internal.h |  |  |
| world_effect_missile.cpp | effects/world_effect_missile.cpp |  |  |
| world_effect_moshka.cpp | effects/world_effect_moshka.cpp |  |  |
| world_effect_tornado.cpp | effects/world_effect_tornado.cpp |  |  |
| world_effect_visibility.cpp | effects/world_effect_visibility.cpp |  |  |
| ai_fog_grid.cpp | effects/world_effect_visibility.cpp | fold | FogGrid (49 lines): its one user is EffectShowVisibility; not the AI's |
| world_effect_visual.cpp | effects/world_effect_visual.cpp |  |  |

#### `items/` (C168g)

| Today | After | Op | Why |
|---|---|---|---|
| item.cpp | items/item.cpp |  |  |
| item.h | items/item.h |  |  |
| item_db.cpp | items/item_db.cpp |  |  |
| item_db.h | items/item_db.h |  |  |
| item_manager.cpp | items/item_manager.cpp |  |  |
| item_manager.h | items/item_manager.h |  |  |
| item_names.cpp | items/item_names.cpp |  |  |
| item_names.h | items/item_names.h |  |  |
| item_stock.cpp | items/item_stock.cpp |  |  |
| item_stock.h | items/item_stock.h |  |  |

#### `map/` (C168e)

| Today | After | Op | Why |
|---|---|---|---|
| chunk_stream.cpp | map/chunk_stream.cpp |  |  |
| chunk_stream.h | map/chunk_stream.h |  | the chunked .mob files and editor specs |
| map_database.cpp | map/map_database.cpp |  |  |
| exit_area.cpp | map/map_database.cpp | fold | ZoneExit's members (78 lines): a map database record |
| object_tables.h | map/map_database.h | split | grab-bag: the map database's records stay (named for map_database.cpp); NamedFloatTable -> core/named_floats.h, text keys -> data/text_manager.h, key names -> app/key_bindings.h (C168b) |
| map_spec.cpp | map/map_spec.cpp |  |  |
| map_spec.h | map/map_spec.h |  |  |
| map_spec_unit.cpp | map/map_spec_unit.cpp |  |  |
| mob_reader.cpp | map/mob_reader.cpp |  |  |
| mob_script_decoder.h | map/mob_script_decoder.h |  |  |
| mob_toml.cpp | map/mob_toml.cpp |  |  |
| mob_toml.h | map/mob_toml.h |  |  |

#### `models/` (C168g)

| Today | After | Op | Why |
|---|---|---|---|
| map_object_animation.cpp | models/animation.cpp |  | with models/animation.h |
| map_object_animation.h | models/animation.h |  | figure animation tracks (AnimationTrack), not map objects |
| map_object_animation_bones.cpp | models/animation_bones.cpp |  | FigureAnimationState's skeleton and FigureBone |
| figure_bones.cpp | models/animation_bones.cpp | fold | FigureBone::Place (57 lines): FigureBone's other members are there |
| dib.cpp | models/dib.cpp |  |  |
| dib.h | models/dib.h |  |  |
| figure_gltf.cpp | models/figure_gltf.cpp |  |  |
| figure_gltf.h | models/figure_gltf.h |  |  |
| figure_gltf_animation.cpp | models/figure_gltf_animation.cpp |  |  |
| figure_gltf_glb.h | models/figure_gltf_glb.h |  |  |
| figure_manager.cpp | models/figure_manager.cpp |  |  |
| menu_rects.cpp | models/figure_manager.cpp | fold | FigureManager::LoadMenuRects (57 lines): a member of the manager |
| figure_manager.h | models/figure_manager.h |  |  |
| figure_mesh.cpp | models/figure_mesh.cpp |  |  |
| figure_mesh.h | models/figure_mesh.h |  |  |
| figure_part_draw.cpp | models/figure_part_draw.cpp |  |  |
| link_figure.cpp | models/link_figure.cpp |  |  |
| link_figure.h | models/link_figure.h |  |  |
| link_loader.cpp | models/link_loader.cpp |  |  |
| link_loader_create.cpp | models/link_loader.cpp | fold | LinkFigureLoader's constructor (15 lines) |
| link_loader.h | models/link_loader.h | new | LinkArchive, FigureModel, Morph, LinkFigureLoader out of model.h |
| material.cpp | models/material.cpp |  |  |
| material.h | models/material.h |  |  |
| texture.cpp | models/texture.cpp |  |  |
| texture.h | models/texture.h |  | TextureDevice and MeshArgs leave for scene/device.h (C168c) |
| texture_load.cpp | models/texture_load.cpp |  |  |
| texture_png.cpp | models/texture_png.cpp |  |  |
| texture_png.h | models/texture_png.h |  |  |

#### `mods/` (C168d)

| Today | After | Op | Why |
|---|---|---|---|
| world_map_files.h | mods/maps_folder.h |  | the maps folder and its files |
| mod_folders.cpp | mods/mod_folders.cpp |  |  |
| mod_folders.h | mods/mod_folders.h |  |  |
| mod_hash.cpp | mods/mod_hash.cpp |  |  |
| mod_hash.h | mods/mod_hash.h |  |  |
| mod_list.cpp | mods/mod_list.cpp |  |  |
| mod_list.h | mods/mod_list.h |  |  |
| mod_plugins.cpp | mods/mod_plugins.cpp |  |  |
| mod_plugins.h | mods/mod_plugins.h |  |  |

#### `net/` (C168h)

| Today | After | Op | Why |
|---|---|---|---|
| mod_identity.cpp | net/mod_identity.cpp |  |  |
| mod_identity.h | net/mod_identity.h |  | the mod a peer plays, told at login (MF15k1: out of the engine's mods/) |
| net_client.cpp | net/net_client.cpp |  |  |
| net_client_handlers.cpp | net/net_client_handlers.cpp |  |  |
| net_client_party.cpp | net/net_client_party.cpp |  |  |
| net_connection.cpp | net/net_connection.cpp |  |  |
| net_connection_life.cpp | net/net_connection.cpp | fold | Connection's state, reset and tick (98 lines): the same class |
| net_connection.h | net/net_connection.h |  |  |
| net_delta.cpp | net/net_delta.cpp |  |  |
| net_delta.h | net/net_delta.h |  |  |
| net_link.cpp | net/net_link.cpp |  |  |
| net_link.h | net/net_link.h |  |  |
| net_lobby.cpp | net/net_lobby.cpp |  |  |
| net_game_info.cpp | net/net_lobby.cpp | fold | the game announcement record (48 lines) the lobby sends |
| net_messages.cpp | net/net_messages.cpp |  |  |
| net_messages.h | net/net_messages.h |  |  |
| net_second_master.cpp | net/net_second_master.cpp |  |  |
| net_second_master.h | net/net_second_master.h |  |  |
| net_server.cpp | net/net_server.cpp |  |  |
| net_server_map.cpp | net/net_server_map.cpp |  |  |
| net_server_players.cpp | net/net_server_players.cpp |  |  |
| net_server_trade.cpp | net/net_server_trade.cpp |  |  |
| net_session.cpp | net/net_session.cpp |  |  |
| net_classes.h | net/net_session.h |  | "classes": the sessions' classes (SessionBase, ServerBase, ClientBase ...) and the message base |
| net_session.h | net/net_session.h | fold | the network pump's two functions (19 lines) |
| net_socket_driver.cpp | net/net_socket_driver.cpp |  |  |

#### `objects/` (C168f)

| Today | After | Op | Why |
|---|---|---|---|
| lever_object.cpp | objects/lever.cpp |  | Lever's members, named for lever.h |
| lever_table.cpp | objects/lever.cpp | fold | the lever records' table (62 lines), declared in lever.h |
| unit_lever.cpp | objects/lever.cpp | fold | Lever::SetState and UseSpots (113 lines): Lever's members |
| lever.h | objects/lever.h |  |  |
| lock_key.h | objects/lock_key.h |  |  |
| map_object.cpp | objects/map_object.cpp |  |  |
| map_object_kinds.cpp | objects/map_object.cpp | fold | their four virtuals (35 lines) |
| map_object.h | objects/map_object.h |  |  |
| map_object_kinds.h | objects/map_object.h | fold | PlainMapObject (26 lines): the member-less MapObject classes |
| map_object_client_effects.cpp | objects/map_object_client_effects.cpp |  |  |
| map_object_companion.cpp | objects/map_object_companion.cpp |  |  |
| map_object_draw.cpp | objects/map_object_draw.cpp |  |  |
| map_object_manager.cpp | objects/map_object_manager.cpp |  |  |
| map_object_manager.h | objects/map_object_manager.h | new | MapObjectManager and MapObjectType, out of unit_create.h (C168i) |
| map_object_shadow.cpp | objects/map_object_shadow.cpp |  |  |
| map_object_shadow.h | objects/map_object_shadow.h |  |  |
| net_state.cpp | objects/net_state.cpp |  | MapObject's and Unit's state writers (virtuals of the objects) |
| map_object_figures.cpp | objects/object3d.cpp |  | with objects/object3d.h |
| map_object_figures.h | objects/object3d.h |  | holds Object3D (and its halves' bases), the 3D object under MapObject |
| object3d_client.cpp | objects/object3d_client.cpp |  |  |
| map_object_parts.cpp | objects/object3d_parts.cpp |  | Object3D's parts |
| misc_400.cpp | objects/side_managers.cpp | fold | address-named: held only ClientSpecificManager, the client halves' factory (folded into server_specific_manager.cpp by C168b) |
| server_specific_manager.cpp | objects/side_managers.cpp | split | TorchObjectSpec -> objects/torch.cpp (C168b) |
| server_specific_manager.h | objects/side_managers.h | split | ServerSpecificManager beside ClientSpecificManager (from base_manager.h); TorchObjectSpec -> objects/torch.h (C168b) |
| torch_object.cpp | objects/torch.cpp |  | TorchObject's members, named for torch.h |
| torch.h | objects/torch.h |  |  |
| torch_client_spec.h | objects/torch.h | fold | the torch's client half (71 lines, header only) |
| magic_trap.cpp | objects/traps.cpp |  | with objects/traps.h |
| magic_trap.h | objects/traps.h |  | MagicTrap and TrapObject: TrapObject's code is in magic_trap.cpp already |
| trap_object.h | objects/traps.h | fold | TrapObject's class (51 lines) |

#### `players/` (C168e)

| Today | After | Op | Why |
|---|---|---|---|
| class_templates.cpp | players/class_templates.cpp |  |  |
| class_template_stock.cpp | players/class_templates.cpp | fold | ClassTemplate's constructor, destructor and stock generators (69 lines): one class |
| class_templates.h | players/class_templates.h |  |  |
| plugin_fixes.h | players/faces.h |  | named for its origin: holds the player faces' rules (sex by model name, the fallback prototype) |
| party.cpp | players/party.cpp |  |  |
| party.h | players/party.h |  |  |
| party_manager.cpp | players/party_manager.cpp |  |  |
| party_manager.h | players/party_manager.h |  |  |
| party_records.h | players/party_records.h |  |  |
| player.cpp | players/player.cpp |  |  |
| player.h | players/player.h | new | Player and its dirty bits, out of player_messages.h (C168i) |
| player_client.cpp | players/player_client.cpp |  |  |
| player_client.h | players/player_client.h |  |  |
| player_client_handlers.cpp | players/player_client_handlers.cpp |  |  |
| player_client_orders.cpp | players/player_client_orders.cpp |  |  |
| player_manager.cpp | players/player_manager.cpp |  |  |
| player_manager.h | players/player_manager.h |  |  |
| player_messages.cpp | players/player_messages.cpp |  |  |
| player_messages.h | players/player_messages.h |  |  |
| player_server_handlers.cpp | players/player_server_handlers.cpp |  |  |
| player_server_orders.cpp | players/player_server_orders.cpp |  |  |
| player_sources.cpp | players/player_source.cpp |  | named for its header |
| player_source.h | players/player_source.h |  |  |
| player_sync.cpp | players/player_sync.cpp |  |  |
| saved_parties.h | players/saved_parties.h |  |  |

#### `saves/` (C168d)

| Today | After | Op | Why |
|---|---|---|---|
| client_visuals.cpp | saves/client_visuals.cpp |  |  |
| client_visuals.h | saves/client_visuals.h |  |  |
| world_savefile.cpp | saves/save_file.cpp |  | the save file container (SaveFileWrite / Read); named for its header |
| save_file.h | saves/save_file.h |  | kept whole: saves/file.h would not say which file |
| save_picture.cpp | saves/save_picture.cpp |  |  |
| save_picture.h | saves/save_picture.h |  |  |
| save_versions.h | saves/save_versions.h |  |  |

#### `scene/` (C168g)

| Today | After | Op | Why |
|---|---|---|---|
| camera.cpp | scene/camera.cpp |  |  |
| camera.h | scene/camera.h |  |  |
| camera_object.cpp | scene/camera_object.cpp |  |  |
| clip.h | scene/clip.h |  |  |
| device_buffers.h | scene/device.h | fold | the device's two work vectors (18 lines) (folded into device.h by C168c) |
| device.h | scene/device.h | new | TextureDevice and MeshArgs, out of texture.h |
| device_draw.cpp | scene/device_draw.cpp |  |  |
| device_material.h | scene/device_material.h |  |  |
| device_mesh.cpp | scene/device_mesh.cpp |  |  |
| drawn_motion.cpp | scene/drawn_motion.cpp |  |  |
| drawn_motion.h | scene/drawn_motion.h |  |  |
| light.cpp | scene/light.cpp |  |  |
| light.h | scene/light.h | new | Light and LightFactory, out of spell.h |
| model.cpp | scene/pipeline.cpp | split | the pipeline's clip test and matrices; the link loader's archive list -> models/link_loader.cpp (C168c) |
| view_matrices.h | scene/pipeline.h | fold | ViewMatrix's declaration (16 lines); it is defined in projection.cpp (folded into pipeline.h by C168c) |
| pipeline.h | scene/pipeline.h | new | TransformPipeline, MatrixStack, ClipBounds, SpritePoint, out of model.h |
| projection.cpp | scene/projection.cpp |  |  |
| render_helper.cpp | scene/render_helper.cpp |  |  |
| render_helper.h | scene/render_helper.h | new | RenderPreset, RenderHelper, out of model.h |
| renderer_base.h | scene/renderer_base.h |  |  |
| renderer_init.cpp | scene/renderer_init.cpp |  |  |
| renderer_lines.cpp | scene/renderer_lines.cpp |  |  |
| renderer_lines.h | scene/renderer_lines.h |  |  |
| renderer_marker.cpp | scene/renderer_marker.cpp |  |  |
| renderer_scene.cpp | scene/renderer_scene.cpp |  |  |
| renderer_sprites.cpp | scene/renderer_sprites.cpp |  |  |
| renderer_strip.cpp | scene/renderer_strip.cpp |  |  |
| model.h | scene/scene_renderer.h | split | grab-bag: SceneRenderer stays; TransformPipeline -> scene/pipeline.h, RenderPreset / RenderHelper -> scene/render_helper.h, the link loader -> models/link_loader.h, prints.db -> terrain/ground_print.h (C168c) |
| sky_object.cpp | scene/sky_object.cpp |  |  |
| sky_object.h | scene/sky_object.h |  |  |
| surface.cpp | scene/surface.cpp |  |  |
| surface.h | scene/surface.h |  |  |

#### `script/` (C168h)

| Today | After | Op | Why |
|---|---|---|---|
| script.h | script/script.h |  |  |
| script_addon.cpp | script/script_addon.cpp |  |  |
| script_addon.h | script/script_addon.h |  |  |
| script_addon_commands.cpp | script/script_addon_commands.cpp |  |  |
| script_addon_handlers.h | script/script_addon_handlers.h |  |  |
| script_addon_math.cpp | script/script_addon_math.cpp |  |  |
| script_addon_units.cpp | script/script_addon_units.cpp |  |  |
| script_blocks.cpp | script/script_blocks.cpp |  |  |
| script_effects.cpp | script/script_effects.cpp |  |  |
| script_eval.cpp | script/script_eval.cpp |  |  |
| script_exec.cpp | script/script_exec.cpp |  |  |
| script_exec_helpers.cpp | script/script_exec_helpers.cpp |  |  |
| script_exec_run.cpp | script/script_exec_run.cpp |  |  |
| script_cleanup.cpp | script/script_exec_run.cpp | fold | Script's runtime resets (39 lines): instances and runs, beside where they start |
| script_group.cpp | script/script_group.cpp |  |  |
| script_ir.cpp | script/script_ir.cpp |  |  |
| script_ir.h | script/script_ir.h |  |  |
| script_load.cpp | script/script_load.cpp |  |  |
| script_lua.cpp | script/script_lua.cpp |  |  |
| script_lua.h | script/script_lua.h |  |  |
| script_lua_mission.cpp | script/script_lua_mission.cpp |  |  |
| script_lua_number.h | script/script_lua_number.h |  |  |
| script_lua_resume.cpp | script/script_lua_resume.cpp |  |  |
| script_lua_scheduler.cpp | script/script_lua_scheduler.cpp |  |  |
| script_lua_scheduler.h | script/script_lua_scheduler.h |  |  |
| script_lua_writer.cpp | script/script_lua_writer.cpp |  |  |
| script_lua_writer.h | script/script_lua_writer.h |  |  |
| script_parse.cpp | script/script_parse.cpp |  |  |
| script_parse.h | script/script_parse.h |  |  |
| script_parse_load.cpp | script/script_parse_load.cpp |  |  |
| script_save.cpp | script/script_save.cpp |  |  |
| script_spell_host.cpp | script/script_spell_host.cpp |  |  |
| script_spell_host.h | script/script_spell_host.h |  |  |
| script_vars.cpp | script/script_vars.cpp |  |  |

#### `sound/` (C168e)

| Today | After | Op | Why |
|---|---|---|---|
| sound.h | sound/sound.h |  |  |
| sound_manager.cpp | sound/sound_manager.cpp |  |  |
| sound_music.cpp | sound/sound_music.cpp |  |  |

#### `spells/` (C168g)

| Today | After | Op | Why |
|---|---|---|---|
| spell.cpp | spells/spell.cpp |  |  |
| spell.h | spells/spell.h |  | Light and LightFactory leave for scene/light.h (C168b) |
| spell_behaviour.cpp | spells/spell_behaviour.cpp |  |  |
| spell_db.cpp | spells/spell_db.cpp |  |  |
| spell_db.h | spells/spell_db.h | new | the spell records, tables and database, out of item_db.h (C168i) |
| spell_effect.cpp | spells/spell_effect.cpp |  |  |
| spell_effect_internal.h | spells/spell_effect_internal.h |  |  |
| spell_effect_update.cpp | spells/spell_effect_update.cpp |  |  |
| spell_manager.cpp | spells/spell_manager.cpp |  |  |

#### `terrain/` (C168g)

| Today | After | Op | Why |
|---|---|---|---|
| ground_decal.cpp | terrain/ground_decal.cpp |  |  |
| ground_decal.h | terrain/ground_decal.h |  |  |
| ground_print.cpp | terrain/ground_print.cpp |  | gains prints.db's load from model_db.cpp (C168b) |
| model_db.cpp | terrain/ground_print.cpp | split | grab-bag: prints.db -> terrain/ground_print.cpp, acks.db -> units/acks_db.cpp, WaterTweens -> terrain/water_tweens.cpp, ReadPrefixedName -> models/figure_manager.cpp, its one user (C168b; the file goes) |
| ground_print.h | terrain/ground_print.h |  | gains prints.db's records from model.h (C168c) |
| terrain.cpp | terrain/terrain.cpp |  |  |
| terrain.h | terrain/terrain.h |  |  |
| terrain_ambience.cpp | terrain/terrain_ambience.cpp |  |  |
| terrain_landscape.cpp | terrain/terrain_landscape.cpp |  |  |
| terrain_sector.cpp | terrain/terrain_sector.cpp |  |  |
| terrain_sector.h | terrain/terrain_sector.h |  |  |
| water_tweens.cpp | terrain/water_tweens.cpp | new | WaterTweens' members, out of model_db.cpp |
| water_tweens.h | terrain/water_tweens.h |  |  |

#### `ui/` (C168h)

| Today | After | Op | Why |
|---|---|---|---|
| camera_trace.cpp | ui/camera_trace.cpp |  |  |
| camera_trace.h | ui/camera_trace.h |  |  |
| global_map_clouds.cpp | ui/global_map_clouds.cpp |  |  |
| global_map_clouds.h | ui/global_map_clouds.h |  |  |
| screen_layout.cpp | ui/screen_layout.cpp |  |  |
| screen_layout.h | ui/screen_layout.h |  |  |
| ui.h | ui/ui.h |  |  |
| ui_battle.cpp | ui/ui_battle.cpp |  |  |
| ui_battle.h | ui/ui_battle.h |  |  |
| ui_briefing.cpp | ui/ui_briefing.cpp |  |  |
| ui_person_texts.cpp | ui/ui_briefing.cpp | fold | LoadPersonTexts (30 lines): the briefing screen reads them (the battle screen inline) |
| ui_briefing.h | ui/ui_briefing.h |  |  |
| ui_briefing_create.cpp | ui/ui_briefing_create.cpp |  |  |
| ui_briefing_dialogue.cpp | ui/ui_briefing_dialogue.cpp |  |  |
| ui_briefing_input.cpp | ui/ui_briefing_input.cpp |  |  |
| ui_briefing_phrases.cpp | ui/ui_briefing_phrases.cpp |  |  |
| ui_briefing_start.cpp | ui/ui_briefing_start.cpp |  |  |
| ui_camera_input.cpp | ui/ui_camera_input.cpp |  |  |
| ui_camera_keys.h | ui/ui_camera_input.h |  | CameraInput's header, named for its code |
| ui_camp.cpp | ui/ui_camp.cpp |  |  |
| ui_camp.h | ui/ui_camp.h |  |  |
| ui_chat_overlay.cpp | ui/ui_chat_overlay.cpp |  |  |
| ui_chat_overlay.h | ui/ui_chat_overlay.h |  |  |
| ui_chat_window.cpp | ui/ui_chat_window.cpp |  |  |
| ui_companions.cpp | ui/ui_companions.cpp |  |  |
| ui_companions.h | ui/ui_companions.h |  |  |
| ui_connect_list.cpp | ui/ui_connect_list.cpp |  |  |
| ui_connect_screen.cpp | ui/ui_connect_screen.cpp |  |  |
| ui_container.cpp | ui/ui_container.cpp |  |  |
| ui_container_draws.cpp | ui/ui_container.cpp | split | draws by kind: UiContainer's -> ui/ui_container.cpp, the briefing screen's -> ui/ui_briefing.cpp, the menu's -> ui/ui_main_menu.cpp, the equipment panel's -> ui/ui_hero_panels.cpp, ClearUiDepth -> ui/ui_interface.cpp (C168b; the file goes) |
| ui_create_screen.cpp | ui/ui_create_screen.cpp |  |  |
| ui_credits.cpp | ui/ui_credits.cpp |  |  |
| ui_credits.h | ui/ui_credits.h | new | CreditsScreen, its lines and scrolling surface, out of ui_screens.h (C168i) |
| ui_cursor.cpp | ui/ui_cursor.cpp |  |  |
| cursor_setup.cpp | ui/ui_cursor.cpp | fold | the cursor's set-up and its 25 shapes (59 lines) |
| ui_cursor.h | ui/ui_cursor.h |  |  |
| ui_drop_list.cpp | ui/ui_drop_list.cpp |  |  |
| ui_drop_list.h | ui/ui_drop_list.h |  |  |
| ui_edit_field.cpp | ui/ui_edit_field.cpp |  |  |
| ui_edit_field.h | ui/ui_edit_field.h |  |  |
| ui_exchange.cpp | ui/ui_exchange.cpp |  |  |
| ui_float.h | ui/ui_float.h |  |  |
| ui_flying_hit_points.cpp | ui/ui_flying_hit_points.cpp |  |  |
| ui_flying_hit_points.h | ui/ui_flying_hit_points.h |  |  |
| ui_font.cpp | ui/ui_font.cpp |  |  |
| ui_font.h | ui/ui_font.h |  |  |
| ui_global_map.cpp | ui/ui_global_map.cpp |  |  |
| ui_global_map_input.cpp | ui/ui_global_map_input.cpp |  |  |
| ui_global_map_paths.cpp | ui/ui_global_map_paths.cpp |  |  |
| ui_help_window.cpp | ui/ui_help_window.cpp |  |  |
| ui_help_window.h | ui/ui_help_window.h |  |  |
| ui_hero_panels.cpp | ui/ui_hero_panels.cpp |  |  |
| ui_equip_panels.cpp | ui/ui_hero_panels.cpp | fold | "the hero panels' small methods" (98 lines) |
| ui_hero_preview.cpp | ui/ui_hero_preview.cpp |  |  |
| ui_hero_screen.cpp | ui/ui_hero_screen.cpp |  |  |
| ui_hero_screen_click.cpp | ui/ui_hero_screen_click.cpp |  |  |
| ui_hero_screen_create.cpp | ui/ui_hero_screen_create.cpp |  |  |
| ui_hud.h | ui/ui_hud.h |  |  |
| ui_hud_corner.cpp | ui/ui_hud_corner.cpp |  |  |
| ui_hud_pieces.h | ui/ui_hud_pieces.h |  |  |
| ui_hud_portraits.cpp | ui/ui_hud_portraits.cpp |  |  |
| ui_hud_side.cpp | ui/ui_hud_side.cpp |  |  |
| ui_hud_slots.cpp | ui/ui_hud_slots.cpp |  |  |
| ui_interface.cpp | ui/ui_interface.cpp |  |  |
| ui_text.cpp | ui/ui_interface.cpp | fold | Interface3D's text layout (69 lines) |
| ui_interface_input.cpp | ui/ui_interface_input.cpp |  |  |
| ui_item_info.cpp | ui/ui_item_info.cpp |  |  |
| ui_item_info.h | ui/ui_item_info.h |  |  |
| ui_item_screen.cpp | ui/ui_item_screen.cpp |  |  |
| ui_item_strip.cpp | ui/ui_item_strip.cpp |  |  |
| ui_item_view.cpp | ui/ui_item_view.cpp |  |  |
| ui_layouts.cpp | ui/ui_layouts.cpp |  |  |
| ui_main_menu.cpp | ui/ui_main_menu.cpp |  |  |
| ui_menu_pieces.h | ui/ui_menu_pieces.h |  |  |
| ui_mesh.cpp | ui/ui_mesh.cpp |  |  |
| ui_mesh.h | ui/ui_mesh.h |  |  |
| ui_message_box.cpp | ui/ui_message_box.cpp |  |  |
| ui_message_box.h | ui/ui_message_box.h |  |  |
| ui_model.cpp | ui/ui_model.cpp |  |  |
| ui_objectives.cpp | ui/ui_objectives.cpp |  |  |
| ui_objectives.h | ui/ui_objectives.h |  |  |
| ui_options.cpp | ui/ui_options.cpp |  |  |
| ui_options.h | ui/ui_options.h |  |  |
| ui_panel_draws.cpp | ui/ui_panel_draws.cpp |  |  |
| ui_saveload.cpp | ui/ui_saveload.cpp |  |  |
| ui_saveload.h | ui/ui_saveload.h |  |  |
| ui_screens.cpp | ui/ui_screens.cpp |  |  |
| ui_screens.h | ui/ui_screens.h |  |  |
| ui_scroll_text.cpp | ui/ui_scroll_text.cpp |  |  |
| ui_scroll_text.h | ui/ui_scroll_text.h |  |  |
| ui_scrollbar.cpp | ui/ui_scrollbar.cpp |  |  |
| ui_scrollbar.h | ui/ui_scrollbar.h |  |  |
| ui_scrolling_surface.cpp | ui/ui_scrolling_surface.cpp |  |  |
| ui_spin.cpp | ui/ui_spin.cpp |  |  |
| ui_spin.h | ui/ui_spin.h |  |  |
| ui_stats.cpp | ui/ui_stats.cpp |  |  |
| ui_surface.cpp | ui/ui_surface.cpp |  |  |
| ui_tooltip.cpp | ui/ui_tooltip.cpp |  |  |
| ui_trade_panel.cpp | ui/ui_trade_panel.cpp |  |  |
| ui_view_screen.cpp | ui/ui_view_screen.cpp |  |  |
| ui_view_screen.h | ui/ui_view_screen.h |  |  |
| ui_window.cpp | ui/ui_window.cpp |  |  |
| ui_region_tips.cpp | ui/ui_window.cpp | fold | UiRegion's tooltip setters (38 lines): UiRegion's other members are there |

#### `units/` (C168f)

| Today | After | Op | Why |
|---|---|---|---|
| ack_kind.h | units/ack_kind.h |  |  |
| acks_db.cpp | units/acks_db.cpp |  | gains acks.db's load from model_db.cpp (C168b) |
| acks_db.h | units/acks_db.h |  |  |
| unit.cpp | units/unit.cpp |  |  |
| unit_figure_queries.cpp | units/unit.cpp | fold | Unit's figure queries (94 lines) |
| unit.h | units/unit.h |  |  |
| unit_client_effects.cpp | units/unit_client_effects.cpp |  |  |
| unit_client_models.cpp | units/unit_client_models.cpp |  |  |
| unit_client_models.h | units/unit_client_models.h |  |  |
| unit_client_update.cpp | units/unit_client_update.cpp |  |  |
| unit_controller.cpp | units/unit_controller.cpp |  |  |
| unit_create.cpp | units/unit_create.cpp |  |  |
| unit_equipment.cpp | units/unit_create.cpp | fold | a new unit's starting equipment (73 lines): a creation helper of the same header |
| unit_create.h | units/unit_create.h |  |  |
| unit_create_stats.cpp | units/unit_create_stats.cpp |  |  |
| unit_db.cpp | units/unit_db.cpp |  |  |
| unit_db.h | units/unit_db.h |  |  |
| unit_hits.cpp | units/unit_hits.cpp |  |  |
| unit_items.cpp | units/unit_items.cpp |  |  |
| unit_looks.h | units/unit_looks.h |  |  |
| unit_motion_tick.cpp | units/unit_motion_tick.cpp |  |  |
| unit_params.cpp | units/unit_params.cpp |  |  |
| unit_record.cpp | units/unit_record.cpp |  |  |
| unit_record.h | units/unit_record.h |  |  |
| unit_save.cpp | units/unit_save.cpp |  |  |
| unit_skins.cpp | units/unit_skins.cpp |  |  |
| unit_stats.cpp | units/unit_stats.cpp |  |  |
| unit_armour_layers.cpp | units/unit_stats.cpp | fold | UnitStats' armour layers (101 lines) |
| unit_update.cpp | units/unit_update.cpp |  |  |
| unit_vitals.cpp | units/unit_vitals.cpp |  |  |

#### `world/` (C168e)

| Today | After | Op | Why |
|---|---|---|---|
| object_diplomacy.cpp | world/diplomacy.cpp |  | with world/diplomacy.h |
| object_diplomacy.h | world/diplomacy.h |  | the diplomacy table between the players (world-wide, not an object) |
| frame_lists.h | world/frame_lists.h |  |  |
| object.h | world/hoover.h |  | grab-bag name: holds only Hoover |
| world_rules.cpp | world/local_player.cpp | split | "rules" holds the local player's queries (GameTime, LocalPlayerData ...); DepthLess -> world/world_client_maps.cpp, its only user (C168b) |
| address_order.cpp | world/object_order.cpp |  | with world/object_order.h |
| address_order.h | world/object_order.h |  | orders without addresses (namespace object_order since C168b) |
| quest_database.cpp | world/quest_database.cpp |  |  |
| quest_records.cpp | world/quest_database.cpp | fold | the quest database's records' Serialize (50 lines) |
| quest_records.h | world/quest_records.h |  |  |
| world.h | world/world.h |  |  |
| world_client.cpp | world/world_client.cpp |  |  |
| world_client_draw.cpp | world/world_client_draw.cpp |  |  |
| world_client_lights.cpp | world/world_client_lights.cpp |  |  |
| world_client_maps.cpp | world/world_client_maps.cpp |  |  |
| world_client_setup.cpp | world/world_client_setup.cpp |  |  |
| world_client_shadow_casters.cpp | world/world_client_shadow_casters.cpp |  |  |
| world_client_shadow_casters.h | world/world_client_shadow_casters.h |  |  |
| world_client_view.cpp | world/world_client_view.cpp |  |  |
| world_create.cpp | world/world_create.cpp |  |  |
| world_guard.cpp | world/world_guard.cpp |  |  |
| world_loot.cpp | world/world_loot.cpp |  |  |
| hoover_loot.cpp | world/world_loot.cpp | fold | Hoover::CollectLoot, which WorldServer::Loot calls |
| world_map.cpp | world/world_map.cpp |  |  |
| world_object_sets.cpp | world/world_object_sets.cpp |  |  |
| world_party.cpp | world/world_party.cpp |  |  |
| world_quests.cpp | world/world_quests.cpp |  |  |
| world_save.cpp | world/world_save.cpp |  |  |
| world_server.cpp | world/world_server.cpp |  |  |
| object_id_map.cpp | world/world_server.cpp | fold | EffectIdMap::Set (7 lines): the server file keeps the id maps |
| world_spells.cpp | world/world_spells.cpp |  |  |
| world_sync.cpp | world/world_sync.cpp |  |  |
| world_tick.cpp | world/world_tick.cpp |  |  |
| world_time.cpp | world/world_time.cpp |  |  |
| world_unit_set.cpp | world/world_unit_set.cpp |  |  |
| world_units.cpp | world/world_units.cpp |  |  |
| world_weather.cpp | world/world_weather.cpp |  |  |
| world_zone.cpp | world/world_zone.cpp |  |  |

<!-- layout.py listing end -->
