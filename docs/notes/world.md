# World server, world client and game flow

Files: `world.h` (layouts), `game_flow.h`, `saved_parties.h`, `save_file.h`, `frame_lists.h`, `world_*.cpp` (server, client, tick, zone / map, save, sync, units, game flow in
`app/game_flow.cpp`, quests, party, briefing, guard, weather, loot, spells), `figure_mesh*.cpp`, `party_manager.cpp`,
`class_templates.cpp`. Scenarios: `world`, `world_client`, `world_load` (new game, eight `AddUnitToServer` wolves, one name
added twice, three `RemoveObjectFromServer`, `load savea`, one more spawn), `party_records`, `sandbox_*`.

## Structures

- `WorldServer` and `WorldClient` share the `World` base: the player list, FindObject (and a version restricted to
  units), IsNight (time of day before 6 or from 22 h), the ground and surface heights (the server from its coarse grid,
  the client from the terrain; slots 29 / 30 combine platforms with them), `worldHandle`, the update count and
  countdowns. Virtual slots are named in world.h (map_objects.md has the low-confidence ones).
- Globals are declared in app/singletons.h (the game server and multiplayer client session, the game clock,
  the server tick, the game flow, the item and effect managers, the briefing texts).
- `Player::dirty` (`PlayerDirty`): weather-like settings, time, a 0..1 value, countdowns; the named float variables
  ("gtime" ...); units; the party.
- `MapObjectIdMap` / `EffectIdMap` = `hash_map<unsigned, T*>` plus the highest key. Server maps: units to update (inserting sets the unit's
  sleep counter to `((unit >> 5) & 15) + 0x80`), every object by id, map objects saved with the zone, non-unit map
  objects, named objects, timed objects (spells / effects stamped with the tick). `RemoveObjectFromServer` erases from
  all of them. Shared objects: `sharedObjects` ({id, player, value, objects}, from a multiplayer client's corpse loot)
  and a list of {id, player index (-1 = everyone)}.
- Server state: alarms, the `SectorMap` (`WakeUnitsAround` uses its own `SpiralCache`), the game result, a flag that
  enables the first command queue, paused, the game kind (1 single player, 3 no range test, 4 network), the .mob / map
  name, `aiMap` (with its `events`). Zone exits: a rectangle, a rotation, a target ("none" = no exit), an entrance.
- Client: `session` (a `Player`; its connection is the PlayerClient, `Client()`), text-key heroes, every object, objects
  answering IsSynchronised, screen entries (id, RECT), `ticksRun` (Segment counts it), `tickFraction`. Sector grids
  (objects and a second one): cells of 16 x 16 world units (cell = FISTP shifted right by 4 as unsigned, so negatives land
  outside), each a list of counted object pointers. Platforms (`PlatformSet`): `SurfaceHeight` projects the point onto
  each plane along z and keeps the highest z (margin 0.5), `Contains` tests (x, y) (margin 0.1), `AddBox` keeps only
  faces with normal z >= 0.8. The bird is the EffectMoshka (effects.md).
- Ownership: each world owns its managers (and the client its terrain, sounds, texts, music, sky, numbers and bird) as
  `std::unique_ptr`s, made one way in `Create` (`make_unique`, then `Init`) and deleted by `Destroy` in a fixed order:
  an object's teardown reaches managers deleted after its own (the timed effects and the terrain stop their sounds,
  the music its stream, the client halves release their textures and materials). A `reset()` empties the pointer
  before the delete, so an object must not reach its own manager through the world while that manager is deleted
  (`ParticleSource::Manager` returns its owner for that reason). The managers' counts are not kept: nothing releases
  a manager.
- Time: the day is 24 hours; time of day = fmod(tick * secondsPerTick + base, 24); a speed s gives secondsPerTick = 1 /
  s / 15 (0: stopped, also for NaN; negative: the setting). The client's lighting (WorldClient::UpdateLighting) takes it
  at the drawn frame, the tick plus `tickFraction`, so the sun turns a little every frame (R25a). Wind: direction and
  strength (ChangeWind clamps it).

## Server update path

`Update` (once per world tick): drop disconnected players; for every party unit `WakeUnitsAround(unit position,
max(perception * factor, minimum) * 2 + 20)`; accept new players; unless paused each player's tick; rebuild the "Heroes"
script group; each player's `SyncPlayer` (0 disconnects).

`Tick`: `++tick`; every 64 ticks (tick & 63 == 3) `RollWeather` then `UpdateWeather` (rain, lightning via `ShowBolt`: a script line on the
mission script's queue, one time in four only a thunder sound); timed objects (one returning 0 from `RunTick` is released
and its id erased after the walk); the AI event time wheel; countdowns (any running sets the countdown dirty bit for
every player); missiles; script command queues (the world's first only while enabled or `force`, the mission script's
always); `UpdateUnits`. `AddToUpdateMap` is the map insertion of `WakeUnitsAround` / `InvokeAlarm`.

**Per-player sync** (`SyncPlayer`): zone (`SendZone`), weather, time, wind, countdowns when dirty; then the map objects
the player should see (camera range, own units, named objects, the units its units can hear: max(perception * factor,
minimum) * k + c via the sector map) and the timed objects whose reach covers the camera, compared with what it was sent.
Object sets (`world_object_sets.cpp`) are sorted by id (world/object_order.h); `DiffObjectSets` gives new objects a reference
and releases gone ones (added / kept / removed).

**The view range** (`world_sync.cpp`): 100 units in the ground plane around the player's `lastPosition`, the camera
position the client sends every update (message 0x40), or the deploy point a zone start sends with the camera placement.
The main menu sees its whole zone (`SeesWholeZone`: every map and timed object, wherever the camera is): its camera is the
client's own path (`MainMenu.cam`), started by the menu screen that the server's game state message opens, so the
server learns it an update after that message. At start-up the client's first camera (the launch position 30, 30)
covers the menu's objects; after a game it was still over the game's map, the menu's dynamic objects (the signpost,
the troll, the fire) came an update after the menu was shown, and at 120 frames a second its first eight frames had the
map and its lights without them (R61; scenario `render_menu_return`). The menu's static objects come with the zone
(`SendFreshObjects`) whatever the range.

## Zone loading, saving, loading, game flow

- **State machine** (`GameFlow`, game_flow.h: state, request target, request party, multiplayer zone, zone switch started):
  every request first runs `TeardownZone` (views reset, lists, `.mob` list, script state, `ClearMapObjects`, missiles,
  timed objects). Kind 3 (new game) empties saves\current (`ClearSaveFolder`) and starts zone1; kind 2 loads. Requests
  acted on by the flow's update: 2 load, 3, 4 go to a zone (also BeginMultiplayerGame), 5 / 6 with a party copy.
- **NewZone**: checks map / .mob paths (the missing-file texts), shows the loading movie, reads the AI graph, clears
  alarms, then `LoadMap` loads the terrain when the map changed and either tears the zone down (same .mob) or
  `ResetZone`; marks every player dirty; then `LoadMapObjects` for the .mob and `SendFreshObjects` for a new zone (it
  moves the objects answering IsSynchronised 0 out of the map-object maps). It reloads only when the map or .mob name
  changed. `ResetState` adds map name, diplomacy, command queues, AI map and state fields.
- **Weather across a load** (differs from the original on purpose, user 2026-11-05; R57d): a fresh zone starts clear
  (LoadMap sets `weather` 0), a save's load (a slot, a revisited zone from saves\current, a multiplayer zone) keeps
  the `weather` LoadSave read. The original cleared it three times over: LoadMap for every load, SetClimate (any
  weather stopped; StartZone and StartNetworkZone set the zone's climate after the load) and BattleScreen::CreateScreen
  (`SetWeather(0, 0)`, after every load). Here LoadMap clears it only for a fresh zone, SetClimate only when the
  climate changes (a revisit sets the climate the save had) and the battle screen leaves it alone. LoadMap sets
  `weatherStrength` (the fade the weather message carries) to 0 for every zone, so the client's rain / snow layer is
  at the zone's weather at once; LoadSave's SetTime runs UpdateWeather, which sets `weatherFactor` (0.9 in rain or
  snow, the units' sight) from it (no roll: below). Checked by render_weather_save and
  zone_visuals (`weatherstate`).
- **The weather's rolls are not saved.** UpdateRain rolls on `g_random` (seeded from the system at every start, not
  saved) once its delay (`WorldServer::weatherDelay`, the original's global 007C2DFC: 10 after a change, counting down
  every check) runs out: rain starts 1 in 101, stops 1 in 31. Only Tick rolls (RollWeather, every 64 ticks); the
  original rolled in every UpdateWeather, so SetTime and SetTimeSpeed rolled too (differs on purpose, user
  2026-11-08): a load ran three checks before its first frame (LoadSave's SetTimeSpeed and SetTime, StartZone's
  SetTime from "gtime") and a save in rain came back clear about 1 load in 11; a script's FixWorldTime or SetTime
  threw the dice early. A script's `SetWeather` sets the delay as a roll's change does (kWeatherChangeDelay, 10
  checks, about half a minute), so the asked weather holds that long before a roll may end it (differs on purpose:
  the original's scripted rain could stop at the next check). A slot keeps the delay in visuals.sav (renderer.md, "What a save keeps of the picture",
  WeatherDelay; a save without it starts at 0); a revisit keeps the session's. So a load opens in the saved weather,
  and the next roll comes when the saved game's would have (its outcome is `g_random`'s).
- Zone start: `AIEventTable::Setup` sizes the grid 1 - ftol(size x -0.125) each way, zeroes the cells and copies
  "normal.dat" of aiinfo.res. `BuildZoneTable` reads the maps folder's *.mq files (allod 0 / 1 / 2 by three name lists,
  else 3). `LoadZoneQuests`: the zone's "<zone>.mq" of the maps folder; a running server shares the file's bytes, the
  briefing texts add it as a source.
- `LoadMapObjects` opens a root chunk on the full path and `MobFile::Load` reads the objects (`chunk_stream.cpp`);
  `LoadMobFile` (script AddMob, multiplayer zone loader) loads extra .mob files once per zone; `SetZoneExits` copies the
  zone record's exits skipping empty targets. `AddUnitToServer` (script; the name's text key is the id) fills a unit
  record builder and hands it to `CreateUnits`, which removes an existing object with that id, builds the creation
  message (the .mob record `mapspec::UnitSpec`), gives heroes figure parts ("hr" -> "hr.NN"), places units from the
  placement array or a line along the heading, creates them (per kind ObjectSpec, UnitSpec, FlameSpec, TrapSpec,
  LeverSpec, SoundSpec, LightSpec, ParticleSpec) and sets up AI, free cell, controller, items, spells and the
  "WorldScript( AddObject( Heroes, ... ) )" line. Every unit it makes is on side 0, the hero's, with side 0's hostile
  mask: no such unit is hostile to the party until `SetPlayer(unit, <side>)` (the scenarios use 6), so the HUD's
  map draws it neutral and the use at once (Ctrl and a spell key, `WorldClient::NearestVisibleUnit`) passes it over.
- **A record's texture names.** The creation message's primary and secondary textures are the record's texts 3 and 4,
  which only a save fills: a record made by `AddMember` (AddUnitToServer, AddUnitToParty) has them empty, as the
  original's had (its record constructor 0065F5F0 sets every text empty, FillFromPrototype 006621F0 writes texts 2 and
  6). A hero's skins are named "mo<id>p" / "mo<id>s" on the client; any other unit's texture is named "" and found
  nowhere, so its figure draws untextured (white lit: pale in the light), the original's look. No map script of either
  install calls AddUnitToServer: the missions add units at run time with AddMob (21 calls: an extra .mob whose unit
  records name their textures, B007 / B008) or reveal placed hidden ones; zone4hadagans' AddUnitToParty("DrPlen",
  "DragonkinGreenF3") made such a record, so DrPlen came back untextured in the next zone (the original's bug, fixed:
  R49a). AddUnitToParty now has `WorldServer::TakeLookFromUnit` copy the map unit's names (the server companion's
  parentIname and skinTexture) into the record; the record's save and load carry them (`party_join_skin`: a placed
  wolf joins, the party is saved and loaded, the zone changes, `unitskins` checks the client's names). A mercenary's
  hire (`world_quests.cpp`, `AddMember` with the unit's prototype) still leaves them empty. The scenarios' AddUnitToServer spawns lack that step; the harness's
  `skin` step gives them the textures the maps place their kind with (render_unit_skin; a generated .mob through
  AddMob, as tools/make_trap_mob.py does for traps, would be the game's own way but moves the goldens).
- **Saving** (`SaveGame`; autosave "savea" on zone entry when the option is on): saves\current files are copied into
  saves\<slot>, scenario.sav gets the global state (header 0xDEAD), mission.sav the world (header 0x102, zone kinds 0 and
  2); prints "Saved <slot>". `SaveFileWrite` compresses with the LZ coder, checks by decompressing (mismatch: a 0 flag and
  the raw data; checksum above version 0x6B `h ^= dword; h *= (h & 0xFC) | 3`, flag byte above 0x71). The text in saves is
  in the asset code page (standalone.md, "UTF-8"). `SaveWorld` writes names, state fields (alarms, tick, time, game
  kind), the AI-graph state as a diff against the zone's file, map object identities, managers' objects and the map
  objects, changed objects, timed ids, missiles, the time wheel, the first command queue, diplomacy, countdowns, counted
  items, every player (party records, object and id lists), item records, .mob files, shared objects. `LoadSave` reads it
  back with format versions (0x4C time speed as float, 0x4E countdowns, 0x55 players' id lists, 0x59 map object names,
  0x60 .mob files, 0x6E shared objects, 0x74 one more field), then moves units that cannot leave their cell (text-key
  units, id > 1e8, to a free cell near them, others removed); `LoadSavedMapObjects` registers each object (a unit's six
  model dwords from its prototype, parts marked changed, woken). `PrepareSave`: every living unit refills its
  controller's slots. **Loading**: scenario.sav, files copied back, then the zone or mission.sav and `SetZoneExits`;
  refused only in two multiplayer modes; a missing scenario.sav sends the game to the main menu.
- **EnterZone**: plays the pending dcity movie, restores a visited zone (zs.<zone>) from saves\current and lets the world
  catch up (one Tick per elapsed gtime unit since zt.<zone>, at most 1000, waking units within 70 of the entrance), else
  loads it fresh (NewZone, gz6g frozen / thawed map, time 4); then scripts and exits. `LeaveZone` the way out;
  `StartNetworkZone` the network counterpart.
- **Multiplayer start**: `BeginMultiplayerGame` (start zone bz1mpg..bz4mpg by game type; a lone outside party gets unit id
  0x3B9AF5CE and "z.MPGame1"), `UpdateMultiplayerVariable` (each network step), `CloseSessions`, `StartServer`,
  `PickZones` (same order and random draws as the original). The sessions are `net::GameClient()` / `GameServer()`
  (net/net_session.h).
- **Party between zones**: `CollectParty` / `StoreParty` on leaving, `Redeploy` on arrival: `RestorePlayerItems` gives
  back fresh copies of the carried items (all on a client; in single player the quest items of this zone,
  `TakeZoneQuestItems`), units are created at the target exit's centre (a lost game goes back to exit 0) facing away
  from the map centre unless the exit has a heading; the first command queue runs 5x.
  A zone that cannot be entered (EnterZone 0: its map or objects missing, NewZone refusing them after the old zone was
  torn down, or its saves\current file gone) goes to the main menu with the warning "Zone <name> cannot be entered", as
  a zone map.txt lacks does (StartZone); playing on left the battle screen over no world, reading the party's units
  (MF13d2: gz21g, whose zonemichel files no install has). A network zone the same (StartNetworkZone 0, the base torn
  down: `GameFlow::NetworkZoneNotEntered`): the same warning, then flow state 7, which closes both sessions
  (CloseSessions) and loads the main menu; from the flow's request 6 at once (ProcessRequest runs again), from
  SetNetworkZone (a server without a local client, inside ServerSession::SetMap) at the flow's next request, as a
  session released there would return into freed memory. A hosted game used to play on with no world (MF13d2a:
  net_zone_missing_map, a mod's map.toml naming objects for z3q1-z3q3 that no install has).
- **Briefings**: the client's speaker is the second word of the first "#show" line of section "briefing <name>" that is
  not "Hero"; the variable is "b.<speaker>.<name>" or "b.elder.<name>"; `MarkBriefing` / `ForgetBriefing` set / remove it
  (loading the zone's quests first for "<zone>_N" names); `OnVariableChanged` passes "q." variables to the quest log.
  Server: `CompleteBriefing` -> `ApplyMercBriefing` (b.mercN.n8 cleared, n10 / n2 the mercenary leaves (n10 also removes
  its unit), n1 / n3 hired: amerc_N = 2 and added to the party).
- **Player data guard** (from WinMain on (de)activation): with `plain` 0 the players' gold, party records and, on the
  server, units' controller stats are XORed with a new random key, with 1 XORed back.

## Zone state block

`WorldClient::LoadZone`: the flow acknowledges the server, sounds stop, then from `block` the tick, the map and .mob
names, the exits, the cave flag and the unit looks (`unit_looks.h`); the local server makes sure it has the zone; the
ground prints, ResetZoneObjects, ClearMaps, the items and spells and the idle particle sources go; the terrain loads the
map (a name without a drive from the maps folder; models.md, "Terrain") with the allod of the local player's zone, the
sector grids take its size (cells of 16), the sky, lights table, exit markers, lighting and music follow the cave flag,
the local server's pending messages are appended to the view's, the unit skins are built and the view resumes. Loading
movie steps 8-11, then closed. Returns 1, or the result of the fatal error when the map does not load. The client keeps
the `MusicSystem` and the allod's name in lower case.

## World client frame

Draw path `Render` -> `DrawPass` (every terrain sector and every object sector, the clip tests choose; the game took 100
units around the camera, Rebirth 5376). `Render`: the camera's view matrix replaces the view stack's top, then sky,
terrain first pass, `DrawPass`, terrain second pass, effects / particles / lightning / ribbons, pops, flying hit points.
`DrawPass` clears eleven frame lists (`FrameObjects(list)`: objects whose model stays loaded; visible lists by class:
0x52, units 0x50, 0x5D, other, 0x54; shadow lists), walks the sector grid calling `CollectVisible` (units are queued on
the deferred list on the first pass, then it runs for that list and for the text-key heroes), sorts the loaded list by
model use clearing flag 0x400 on the first max(10, n/8), sorts the visible lists by model (`object_order::
DrawBefore`) and draws them; the shadow lists' objects, in no order, only cast in the shadow map and fit their shadow
spheres (`DrawPartsShadow`). `CollectVisible`: heroes with a text-key id are skipped unless `withScripted`; the object
matrix is pushed and the bounding volume clip-tested (2 = outside); in view: flag 0x20, the view-space position (3x3
part of the view matrix only), class and touched-model lists; out of view: flag cleared and, when the shadow volume is
in view, a pending figure change applied and the object goes on the shadow list. The view-space depth starts at zero
(the original's NaN heap leftovers made `Less(x, x)` true and crashed its unguarded partition).

The first pass is `CollectCells` (R37f2): the same verdicts a cell at a time. Every object is placed (its drawn pose,
steps and light) and keeps its world boxes (the clip volume, the shadow volume, the casters' sphere) until its pose,
place, bounds or scale change; a cell's union of boxes decides all of its objects when it is wholly out of or inside the
view's planes by a margin, an object's own box next, and only the undecided ones take the exact `ClipTest`. The cells'
caster spheres it files (`CasterCell`, frame_lists.h) let the casters' walk outside the view pass over a cell none of whose
spheres can reach the shadow map. The visible lists sort on 64-bit keys (`object_order::SortForDraw`: the texture's and
figure's ranks, the depth's bits) to the permutation `DrawBefore`'s sort gives.

**Picking**: `PickGround` / `PickSurface` invert the view for pixel (x, y) giving the ray x = a z + b, y = c z + d,
iterate z = height(x, y) from half the terrain's height range up to 100 times until |dz| <= 0.01, then up to 100
bisection steps; x, y clamp at 0. `PickSurface` uses the platforms, `PickGround` `Terrain::HeightAt`. `Pick` (by bounds or by parts)
sort the screen entries with `DepthLess`.

**Sorts**: `lib::IntroSort` (lib/intro_sort.h) reproduces STLport (median of three, unguarded Hoare partition, recursion
on the right part, heap sort at the depth limit, insertion sort of the first 16 then unguarded): objects with equal keys
keep its order.

**Client maps**: `AddObject` (an already listed id is only referenced and released; the grid listing is removed before it
is appended), `RemoveObject`, `ClearMaps`, `CreatePartyUnits` (per record the figure's part names "hd", "bd", "hp", "hr" ->
"hr.%.2d" from the prototype, "lh1".."rl3"; the hero created as type 0x32; a failed figure ends the call; then skin,
stats, position, items, player, AddObject), `ObjectsInRange` (the camera's; box clamped to the terrain), `Setup` /
`Teardown`: plain methods, not the virtual Create / Destroy. `UnitSet::Write` walks its three hash maps in the original's
order; `ReadUnitLooks` feeds `unit_skins.cpp`. `SectorGrid::Move` removes every listing from the old cell, stores the
position, appends a counted copy into the new cell; out-of-grid cells are skipped. `MarkWithinRadius` marks when `_hypot`
<= radius x scale (NaN marks).

## Figure meshes

`figure_mesh.h/.cpp`: the game-side figure part mesh (classes 0x41-0x44 of the figure factory).
- `FigureMesh` owns all its arrays; the figure manager's types 0x41 and 0x42 are plain `FigureMesh`es (loose and
  attached parts); 0x43 (`UiMesh`) and 0x44 (`SkyObject`) derive from it and share its loaders. Fields: flags
  (0x20 loaded), `MeshBuffers` (pointer / count pairs the renderer draws from), vertex offsets, colour arrays (one per
  normal), header words, centre, box, radius, ClipBounds, vertex packets, normal packets, corners, corner indices (3 per
  triangle), texture coordinates. `VertexPacket`: x[4], y[4], z[4], w[4] = 1.0.
- The `fig` block in figure_mesh.h holds the record format: `kFigMagic`, the version counts, the offsets derived from
  `sizeof(FigHeader)` and `fig::BoundsBytes`, and `MorphCube` / `MorphPrism`, the one morph (bounds, link figure
  vertices, bone cages).
- The loader picks FIG data ("FIG" + version byte) or the older format by the first dword. FIG8 holds 8 versions of
  every bound and vertex block, the corners of a trilinear morph by the figure's three coefficients (models.md, "Link
  figures"); the old format has 6 versions and every array preceded by its count.
- Its two callers (the figure manager's NewFigure for the UI's 3D meshes, and the sky dome) pass s = t = u = 0, so the
  loaders take the first version of each value. The morph with the original's per-component roundings is left only in
  `MorphFigureBounds` / `MorphFigureBounds6` (the figure loader's part bounds with the unit's real weights).
- SkyObject's own class id is 0x20; 0x44 is the figure factory's class number.

## Party records and class templates

`party_manager.cpp`, `class_templates.cpp`; scenario `party_records.txt`; every new game reaches both (parties reset, then
the templates). The party manager (script.md) holds the party records (counted pointers to records: a party and a name),
two strings and the class templates.
- `ClassTemplatesBuild(flag)` releases the templates and builds new ones: flag set (multiplayer) classes 1, 3, 4, 5;
  flag clear classes 1-5, the first with one byte flag 0, the second with the other, the fifth with all weights zero. A
  template: class, two byte flags, nine float weights (1, .5, .2, .1, 1, .5, .2, .1, .2), then `ClassTemplateFill`.
- `ClassTemplateFill`: the item database lists weapon, armor, quick item and loot names; each is looked up (the by-name
  lookups insert a null record for unknown names) and a record whose class mask has bit (class - 1) goes into the
  template's "allowed" stock (items_spells.md, "Trader stock"). The spell database lists spells and modifiers; allowed
  ones go to both stocks. Each add appends unless the record is already there.
- Records: `Reset` erases every record, empties the strings, appends a new unnamed record whose party gets the member
  "Hero" / "Human Hero", zone "gz1g" and "gtime" = 6; `Add` names it; `Remove`; `Find` (the party of the first record with
  the name by `_mbscmp`, else of the last unnamed one). `PartyMapLoad` / `PartyMapStore` work on hash_map<int, Party>
  (bucket = key % count unsigned): Load resets the target for key 0, else copies the stored party or returns false; Store
  copies into map[key]. Kept: `Remove` advances its index after an erase, so a matching record moved into the erased slot
  is skipped.

## The AI map's load

`AIMap::Load` in two steps (ai.md, "The map archive"). `LoadTerrain`: the archive's .mp description gives the height scale
and both climb tables (1023 costs each: up to 40 degrees for the large classes, 60 at the flat cost for class 0),
tileDesc.reg of aiinfo.res the sixteen terrain types, the three grids sized 64 x 64 cells per sector, every sector file
stamped into them. `AttachTerrain`: open marks, cost tables, water depths, searches, region map, events, sector map, the
refresh. A vertex's altitude unit is 1 / 65534 as a float (37800100, 2^-16 + 2^-31); 1 / 65535 turned about one cell in
500 over a rounding edge. Rule: compare every float constant of a port with the original's bits, and write inexact
constants as exact fractions or bit patterns with a static_assert on the bits. The unit shape cache releases every shape
as its owner at exit: a fault there shows as a crash on quit.

## Original bugs kept

- The original called the same field `sessionKind` (1 single player, 3 no range test, 4 network) and stored the campaign
  mode in it (1 battle, 2 menu, 3 base, 4 edge, 8 end). Here it is `WorldServer::zoneMode` (`ZoneMode`, world.h):
  a network zone runs in Battle, so nothing sets 4 for a network game, and `RestorePlayerItems` and `CollectParty`
  (which compare with the original's 4) skip the items of an edge zone (`ZoneMode::Edge`): a party redeployed or
  collected there neither stores nor gets back its quest items. A fix would treat edge zones like the others.
  The game's state (`WorldServer::gameState`, `GameState`: playing, network, main menu) is a separate field; the
  digests keep their old names "gameResult" and "sessionKind" for the two numbers. `Player::sessionKind` is the
  player's own state number (a copy of `zoneMode` at login, later the interface screen the server sent), still an int.

## Known points

- save_file.h's SaveFileRead parameters `minimum` / `maximum` are swapped in meaning (ReadHeader takes newest, oldest).
- The torch's slot 16 takes `float` (flame->volume), so a signalling NaN could be quietened.
