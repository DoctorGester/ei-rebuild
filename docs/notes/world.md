# World server, world client and game flow (0x00560000-0x005A9000)

Files: `world.h` (layouts), `world_*.cpp` (server, client, tick, zone / map, save, sync, units, game flow in `world_game.cpp`,
quests, party, briefing, guard, weather, loot, spells), `figure_mesh*.cpp`, `party_manager.cpp`, `class_templates.cpp`.
Verify: `tests/verify/verify_world*.cpp`, `verify_figure_mesh.cpp`, `verify_class_templates.cpp`, `verify_party_records.cpp`.
Scenarios: `world`, `world_client`, `world_load` (new game, eight `AddUnitToServer` wolves, one name added twice, three
`RemoveObjectFromServer`, `load savea`, one more spawn), `party_records`, `sandbox_*`.

## Structures

Layouts are in `world.h` (named members with offset asserts; virtual slots named, see map_objects.md for the low-confidence
ones); what the notes add is the meaning of fields it still calls `unknown*`, the slots and the globals.

- **CWorldServer** (vtable 0073F928, 42 slots, global 007AFF60) and **CWorldClient** (0073F41C, 43 slots, global 007AF5B8)
  share a base whose methods live at 0055E000-0055FFFF: slot 20 (0055EB50) the player list, slot 40 FindObject (server
  00593AA0, client 0055FAE0), 0055FB00 FindObject restricted to units (class id 0x50), 0055FC10 IsNight (tick +0x64 x
  seconds per tick +0x20 + base +0x1C modulo the day length: before 6 or from 22 h). Slot 27 ground height (server 00594220
  from its coarse grid at +0x280C, client 00563F70 from the terrain), slot 28 surface height (client 00564070 =
  `TerrainOwner::SurfaceHeight`; TerrainOwner is WorldClient), slots 29 / 30 (005618E0 / 00561930) combine platforms with
  ground / surface height. World +0x5C is `RefPtr worldHandle`; +0x68 / +0x6C / +0x70 (updateCount, unknown6C_,
  countdowns) are shared by both worlds.
- Globals: 0079B5D8 multiplayer client session, 0079B5DC game server (+4 bit 31 = inactive), 007AF61C / 007AFCA8 game clock,
  007AFFC4 server tick, 007AFE20 the game flow, 007AFEA4 / 007AFEA5 interface / client glue, 007AFFB4 item manager, 007AFFA8
  effect manager, 007AF788 briefing texts. Named in world_data.h / game_flow_data.h.
- Player list: reference-counted, walked with 00404920 (begin; takes a count) / 00404960 (next). `Player::dirty`
  (+0x130): bits 2 / 4 / 8 weather-like settings, time, a 0..1 value set by 0058EE20; 0x20 countdowns; +0xC4 are the named
  float variables ("gtime" ...); +0x40 units; +0x70 the party (Party).
- `ObjectIdMap` = STLport `hash_map<unsigned, T*>` plus the highest key (+0x14). Server maps: +0x104 units to update
  (inserting sets unit +0x174 to `((unit >> 5) & 15) + 0x80`), +0x12D70 every object by id, +0x12DA0 map objects saved with
  the zone, +0x12DB8 non-unit map objects, +0x12DD0 named objects, +0x12DE8 timed objects (spells / effects stamped with the
  tick at +0x94). `RemoveObjectFromServer` erases from +0x12D70, +0x12DD0, +0x12D88, +0x12DA0, +0x12DB8. Shared objects:
  `sharedObjects` +0x12E60 (an MFC list of {id, player, value, objects}, from a multiplayer client's corpse loot 0066F4C0)
  and +0x12E64 ({id, player index (-1 = everyone)}).
- Server state: alarms +0x8C (read at 007AFFEC), +0x294 `SectorMap` (005C0930 returns sector (x, y)'s unit list;
  `WakeUnitsAround` uses its own `SpiralCache` 007AFF08), +0x138 game result, +0x160 enables the first command queue, +0x6C
  paused, +0x12D64 game kind (1 single player, 3 no range test, 4 network), +0x12D68 / +0x12D6C .mob / map name. Zone exits
  are 0x3C-byte records (rectangle +0x14, rotation +0x24, target CString +0x28 "none" = no exit, entrance +0x2C).
- Client: session +0x20C (a `Player`, `WorldClient::session`; +0x20 its connection, the local view, a PlayerClient: `Client()`; view +0x1C local
  Player), +0xB4 text-key heroes, +0xB8 every object, +0xBC objects answering slot 29, +0xF0 screen entries (id, RECT),
  ticksRun +0x6EC (Segment counts it), tickFraction +0x6F0. Sector grids (+0x120 objects, +0x150 second; slot 41 005618A0
  / 42 005618C0 move an object in them): cells of 16 x 16 world units (cell = FISTP shifted right by 4 as unsigned, so
  negatives land outside), each an STLport list of counted object pointers; the grid at 007AF6D8 (moved by 0067BEF0 /
  0067CEF0) holds hundreds of entries per cell near effects. Platforms (+0x1D4 `PlatformSet`, 0x4C-byte records):
  `SurfaceHeight` projects the point onto each plane along z and keeps the highest z (margin 0.5), `Contains` tests (x, y)
  (margin 0.1), `AddBox` keeps only faces with normal z >= 0.8. Segment's light grey levels walk Light objects (0068D730).
- Time: the day is 24 hours; time of day = fmod(tick * secondsPerTick + base, 24); a speed s gives secondsPerTick = 1 / s / 15
  (0: stopped, also for NaN; negative: the setting 0079BE5C). Wind: direction +0x24, strength +0x30 (ChangeWind clamps it).

## Server update path

`Update` (slot 6, 0058DFF0, once per world tick): drop disconnected players; for every party unit `WakeUnitsAround(unit +
0x1C, max(perception * factor, minimum) * 2 + 20)`; accept new players (0058DAF0); unless +0x6C is set each player's
00665DE0 and `Tick(0)`; each player's 0066E130; rebuild the "Heroes" script group; each player's `SyncPlayer` 0058B750 (0
disconnects).

`Tick` (0058DCD0): `++tick` (+0x64); every 64 ticks (tick & 63 == 3) `UpdateWeather` 0058CCD0 (rain, lightning via
`ShowBolt` 00578E30: a script line on the queue 007C29CC, one time in four only a thunder sound); timed objects +0x12DE8 (an
object returning 0 from slot 6 `RunTick` is released and its id erased after the walk); time wheel 005C9530 (+0x2838, 1024 x
16); countdowns 006D44E0 (any running sets dirty bit 0x20 for every player); missiles (+0x12E28, MFC list of 0xA8-byte
objects from 0058E670; 0067E6D0 moves one, 0 unlinks and frees); script command queues (+0x12874 only while +0x160 is set or
`force`, +0x12A6C always; 004BF000); `UpdateUnits` 0058D670. The inline map insertions of `WakeUnitsAround` / `InvokeAlarm`
are `AddToUpdateMap`; `Tick`'s local `list<unsigned>` reproduces the original's node pool traffic (head allocated before the
walk, one 12-byte node per finished object, hash nodes freed in the erase pass, the head freed last).

**Per-player sync** (`SyncPlayer`): zone (`SendZone` 0058C890), weather, time, wind, countdowns when dirty; then the map
objects the player should see (camera range from 0073F91C, own units, named objects, the units its units can hear:
max(perception * factor, minimum) * 0073F910 + 0073BF34 via the sector map) and the timed objects whose reach covers the
camera, compared with what it was sent (0047B1F0) in function statics 007AFF18-007AFF48. Object sets are aligned pointer
arrays (`world_object_sets.cpp`: 0047ADF0 sort-unique, the original's by address through a byte-wise radix sort of four passes
of 0047B120, ours by id (address_order.h), `DiffObjectSets`
0047B1F0: new objects get a reference, gone ones are released through slot 4; the original names the outputs added / gone /
same, `world_sync.cpp` added / kept / removed).

## Zone loading, saving, loading, game flow

- **State machine** 0057C700 (request at 007AFE20): every request first runs `TeardownZone` 005957E0 (views reset, +0x12E64
  list, `.mob` list, script state, `ClearMapObjects`, missiles, timed objects, 0055F890). Kind 3 (new game) empties
  saves\current (`ClearSaveFolder` 0057AAB0) and starts zone1 through 0057C460; kind 2 (load) calls 0057ABA0. Requests acted
  on by the flow's update 005800C0: 2 load (005816D0), 3 (005816C0), 4 go to a zone (00581700, also BeginMultiplayerGame), 5
  / 6 with a party copy (00581730 / 00581760). GameFlow (world.h): state +0, requestTarget +4, requestParty +8,
  multiplayerZone +0x7C, zoneSwitchStarted +0x80 (original offsets).
- **NewZone** 00595480 (Rebirth, not rewritten): checks map / .mob paths, reads the AI graph (00596140), clears alarms, then
  `LoadMap` 00595C70 loads the terrain when the map changed (005B43C0) and either tears the zone down (same .mob) or
  `ResetZone` 0058B250; marks every player dirty; then `LoadMapObjects` 00596030 for the .mob and `SendFreshObjects` 005951A0
  for a new zone (it moves the objects answering 0 to slot 29 out of the map-object maps). 00595B50 reloads only when the
  map or .mob name changed. `ResetState` 0058B600 adds map name, diplomacy, command queues, AI map and state fields.
- `LoadMapObjects` 00596030 opens a root chunk (vtable 0073FAD4, flags 0x8020) on the full path (00595BA0) and 00481660 reads
  the objects (`chunk_stream.cpp`); `LoadMobFile` 00595AB0 (script AddMob, multiplayer zone loader) loads extra .mob files
  once per zone (byte 007C554C set meanwhile); `SetZoneExits` 005915E0 copies the zone record's exits (map database +0x48)
  into +0xEC skipping empty targets. `AddUnitToServer` 005910A0 (script command 0x88; the name's text key 00461360 is the
  id) fills a 0x70-byte unit record builder (00661050 / 006628D0 / 006611D0; its 0x784-byte record at +0x30) and hands it in
  a CArray (vtable 0073FAA4) to `CreateUnits` 0058EF20, which removes an existing object with that id, builds the
  0x324-byte creation message (0046C010; the .mob record `mapspec::UnitSpec`), gives heroes figure parts ("hr" -> "hr.NN"),
  places units from the placement array or a line along the heading, creates them (005927C0; per kind ObjectSpec (1, 5-9),
  UnitSpec (5), FlameSpec (6), TrapSpec (7), LeverSpec (8), SoundSpec (3), LightSpec (2), ParticleSpec (4)) and sets up AI,
  free cell, controller, items, spells and the "WorldScript( AddObject( Heroes, ... ) )" line.
- **Saving** (`SaveGame` 0057B3B0; autosave "savea" on zone entry when the option at 0079BE7C is on, 006360F0 -> 006570E0 ->
  00581610): saves\current files are copied into saves\<slot>, scenario.sav gets the global state (header 0xDEAD),
  mission.sav the world (header 0x102, zone kinds 0 and 2); prints "Saved <slot>". All paths are the game folder plus
  "saves\", so the harness redirection covers them; `load savea` after `newgame` loads the autosave. `SaveFileWrite` 00579B80
  compresses with the LZ coder, checks by decompressing (mismatch: a 0 flag and the raw data; checksum above version 0x6B
  `h ^= dword; h *= (h & 0xFC) | 3`, flag byte above 0x71); `SaveFileRead` 00579830. `SaveWorld` 00597BD0 writes names, state
  fields (+0x24, +0x30, +0x84, alarms, tick, time, game kind), the AI-graph state as a diff against the zone's file (00596140
  base, 005B1380 current, 004A4DA0), map object identities (recreated by manager40's slot 23), managers' objects (slot 10)
  and `SaveMapObjects` 00596730, changed objects (00404860), timed ids, missiles, time wheel, first command queue,
  diplomacy, countdowns, counted items, every player (party records 00661C10, object and id lists), item records, .mob
  files, shared objects. `LoadSave` 00596E70 reads it back with format versions (0x4C time speed as float, 0x4E countdowns,
  0x55 players' id lists, 0x59 map object names, 0x60 .mob files, 0x6E shared objects, 0x74 +0x13C), then moves units that
  cannot leave their cell (text-key units, id > 1e8, to a free cell near them, others removed); `LoadSavedMapObjects`
  00596900 registers each object (copies a unit's six model dwords at +0x4B0 from its prototype, marks parts changed, wakes
  it). `PrepareSave` 005956E0: every living unit refills its controller's slots. **Loading** (0057ABA0): scenario.sav, files
  copied back, then the zone (0057C460) or mission.sav (00579E60 -> `LoadSave`) and `SetZoneExits`; refused only in
  multiplayer modes 2 / 3 of 007B0098; a missing scenario.sav sends the game to the main menu.
- **EnterZone** 0057A060 (from 0057C460): plays the pending dcity movie, restores a visited zone (zs.<zone>) from
  saves\current and lets the world catch up (one Tick per elapsed gtime unit since zt.<zone>, at most 1000, waking units
  within 70 of the entrance), else loads it fresh (NewZone, gz6g frozen / thawed map, time 4); then scripts and exits.
  `LeaveZone` 0057A780 the way out; `StartNetworkZone` 0057BDF0 the network counterpart.
- **Multiplayer start**: `BeginMultiplayerGame` 0057F280 (start zone bz1mpg..bz4mpg by game type via 0057E6B0; a lone
  outside party gets unit id 0x3B9AF5CE and "z.MPGame1"), `UpdateMultiplayerVariable` 0057F190 (each network step),
  `CloseSessions` 00581660 (sessions' refcount is the low word at +4), `StartServer` 0057E740, `PickZones` 0057F850 (groups in
  game-heap arrays instead of STLport lists; same order and random draws). The sessions are net::GameClient() /
  GameServer() (net_classes.h): the client's +0x2600 is player (a LoginMessage: state +0x28 the game state request,
  version +8, host +0x24), +0x25EC info (HostMessage), +0x25F4 map, +0x25F8 zones / +0x25FC variables, +0x261C
  gameType; the server's +0x25A0 hostInfo.
- **Party between zones**: `CollectParty` 00590A40 / `StoreParty` 0058FD90 on leaving, `Redeploy` 00590700 on arrival:
  `RestorePlayerItems` 00590460 gives back fresh copies (`CopyItem` 00594CE0 = world slot 24) of the carried items (all on a
  client; in single player the quest items of this zone, `TakeZoneQuestItems` 005901E0), units are created at the target
  exit's centre (player +0xDC picks the exit of zone +0xD8; a lost game goes back to exit 0) facing away from the map centre
  (FPATAN) unless the exit has a heading; the first command queue runs 5x.
- **Briefings**: the client's speaker is the second word of the first "#show" line of section "briefing <name>" that is not
  "Hero" (0057FC90); the variable is "b.<speaker>.<name>" or "b.elder.<name>" (0057FDE0); `MarkBriefing` 0057FF40 /
  `ForgetBriefing` 00580040 set / remove it (loading the zone's quests first for "<zone>_N" names, 005809D0);
  `OnVariableChanged` 00580840 passes "q." variables to the quest log. Server: `CompleteBriefing` 00592090 ->
  `ApplyMercBriefing` 00591DE0 (b.mercN.n8 cleared, n10 / n2 the mercenary leaves (n10 also removes its unit), n1 / n3
  hired: amerc_N = 2 and added to the party).
- **Player data guard** 005790E0 (from WinMain on (de)activation): with `plain` 0 the players' gold, party records (0x704
  bytes from +0x20 of each 0x784 record) and, on the server, units' controller stats (0x704 bytes from +0x1E0) are XORed
  with a new random key (007AFECC), with 1 XORed back; 005791C0 one player list.

## World client frame

Draw path `Render` 00569E40 -> `DrawPass` 0056AFE0 (rewritten with Rebirth's LandscapeDrawRadius / ObjectsDrawRadius behind
flags; slot 6 `Segment` 005641F0 with DisableExitOnCrashReport). `Render`: the camera's view matrix (004F0EF0 of 0079B2A8)
replaces the view stack's top, then sky (flag 0x1000), terrain first pass (0x100, draw distance 100.0), `DrawPass`, terrain
second pass, effects / particles / lightning / ribbons (0x400), pops, flying hit points (0x2000). `DrawPass` clears eleven
global `vector<CEngineObject*>` (`FrameObjects(list)`, 007AF508 + 0x10 * list: 007AF508 objects whose model stays loaded;
visible lists 007AF568 (class 0x52), 578 (units 0x50), 588 (0x5D), 598 (other), 5A8 (0x54); shadow lists 007AF518 .. 558),
walks the sector grid calling `CollectVisible` 0056A990 (units are queued on the deferred list 007AF5B4 on the first pass,
then it runs for that list and for +0xB4), sorts 007AF508 by model use clearing flag 0x400 on the first max(10, n/8), sorts
visible and shadow lists by model (00574300; ours `address_order::DrawBefore`) and draws them. `CollectVisible`: type 0x32 heroes with a text-key id are
skipped unless `withScripted`; the object matrix (+0x54, from the quaternion by 004D9AA0) is pushed on the device's object
stack and the bounding volume (+0x124) tested with 004F16A0 (2 = outside); in view: flag 0x20, the view-space position at
+0x28 (3x3 part of the view matrix only), class and touched-model lists; out of view: flag cleared and, when the shadow
volume is in view (0051E300), a pending figure change applied (0050D450) and the object goes on the shadow list. The view
matrix is the top of the device's view stack (007AF068: +0x5C matrices, +0x60 depth); `DrawPass` does not check the depth.

**Picking**: `PickGround` 0056BF30 / `PickSurface` 0056C350 invert the view for pixel (x, y) giving the ray x = a z + b, y =
c z + d, iterate z = height(x, y) from half the terrain's height range (+0x114) up to 100 times until |dz| <= 0.01, then up to
100 bisection steps; x, y clamp at 0; an empty view stack returns the vector at 007AFDB0. `PickSurface` uses slot 29
(platforms), `PickGround` `Terrain::HeightAt`. Callers: the interface (00638EF0 / 00639EF0), 005EB940. `PickAt` / `PickAt2`
(00565790 / 005659D0) sort the screen entries with `DepthLess`. The first height result stays unrounded (forwarder to
006C8680 declared `double`).

**Sorts**: `lib::IntroSort` (lib/intro_sort.h) reproduces STLport (median of three, unguarded Hoare partition, recursion on
the right part, heap sort at the depth limit (00577FC0 / 00577730, 00577E90 / 005782F0), insertion sort of the first 16
then unguarded): 005740D0 / 00574390 by model (comparator 00574300), 00573D50 / 00573F10 by model use; objects with equal
keys keep its order. **Rare Rebirth crash** (`docs/open-questions.md`): 00574300's last test
is "less or unordered", so an object with a NaN view depth (+0x30) makes `Less(x, x)` true and STLport's unguarded partition
walks off the vector; the rewrite behaves exactly like the original, crash included (the verify logs "not orderable" and
sends NaN ranges to the original alone). The NaN depths were heap leftovers: the constructor leaves +0x28 and objects
listed only for their shadow before they were ever in view sort on it; it now starts at zero in ours and, through a
harness site, in the original (S17a, docs/notes/standalone.md).

**Client maps**: `AddObject` 005647E0 (an already listed id is only referenced and released; the grid listing is removed
before it is appended), `RemoveObject` 00564BE0, `ClearMaps` 00564E70, `CreatePartyUnits` 00566F70 (per record the figure's
part names "hd", "bd", "hp", "hr" -> "hr.%.2d" from the prototype in 007AF364, "lh1".."rl3"; manager slot 23 creates the
hero (type 0x32); a failed figure ends the call; then skin, stats, position (+0x724), items (+0x730 weapons, +0x740 quick,
+0x750 armour), player, AddObject), `ObjectsInRange` 0056DAF0 (the camera's 0040E4C0; box clamped to the terrain with
FCOMP-mask selects and `_ftol`), `Setup` 00561E50 (slot 2) / `Teardown` 00563290 (slot 3): plain methods, not the virtual
`Create` / `Destroy` (a vcall thunk loops forever); Setup follows the original constructor call by call (the terrain's
0xAFC-byte constructor, vtable 0073F6A8, is inlined; every player gets a reference never dropped). `UnitSet::Write` 00559E60
walks its three hash_maps with the original iterator helpers so the order matches; `ReadUnitLooks` 0055A190 (ECX stream, EDX
the 0x3C-record vector, stack the 0x24-record vector) feeds `unit_skins.cpp`. `SectorGrid::Move` removes every listing from
the old cell (counted copy), stores the position, push_backs a counted copy into the new cell; out-of-grid cells are skipped.
`MarkWithinRadius` 00571D50 marks when `_hypot` <= radius (+0x94) x scale (NaN marks).

## Figure meshes (0x0059F000-0x005AC200)

`figure_mesh.h/.cpp`. The 0x005A0000-0x005B0000 range holds the game-side figure part mesh (classes 0x41-0x44 of the factory
005AA8E0) and the figure loader (005AC010), not AI code.
- `FigureMesh` (the base "CFigure", the layout of class 0x42: 0x220 bytes). Class 0x42 is `PlainFigureMesh`; 0x43
  (`UiMesh`, "CI3DFigure") and 0x44 (`SkyObject`) derive from it and share its loaders; class 0x41 (`SharedFigureMesh`,
  0x1E0 bytes, the same layout up to +0x1E0) has its own loaders. Real virtual classes since C19 batch 6 (standalone.md,
  "Compiler-made vtables"). A TransformObject, +0x08 flags (0x20
  loaded), +0x9C MeshBuffers (pointer / count pairs the renderer draws from), +0x128 vertex offsets, +0x138 / +0x148 colour
  arrays (one per normal), +0x158 / +0x15C header words, +0x160 centre, +0x16C / +0x178 box, +0x184 radius, +0x188
  ClipBounds, +0x1C0 vertex packets, +0x1D0 normal packets, +0x1E0 corners, +0x200 corner indices (3 per triangle), +0x210
  texture coordinates. Every loader inlines its `AlignedArray` SetSize. `VertexPacket` (0x40): x[4], y[4], z[4], w[4] = 1.0.
- Slot 19 (005A2490 / 005A4CF0) picks slot 20 (FIG data: "FIG" + version byte) or 21 (older format) by the first dword. FIG8
  holds 8 versions of every bound and vertex block, the corners of a trilinear morph by the figure's three coefficients
  (s, t, u): `lerp(lerp(lerp(v0,v1,s), lerp(v2,v3,s), t), lerp(lerp(v4,v5,s), lerp(v6,v7,s), t), u)`; the old format has 6
  versions and every array preceded by its count.
- Slot 19's two callers (the figure manager's NewFigure for the UI's 3D meshes, and the sky dome) pass s = t = u = 0, so
  `LoadFromMemory` has no weights and the loaders take the first version of each value. The original's zero-weight blend
  gives the same bits for finite data except that a -0.0 may come out +0.0; nothing compares the sign of these zeros.
- The morph itself, with the original's per-component roundings (VC6 register allocation; patterns from a symbolic x87
  trace), is left only in `MorphFigureBounds` / `MorphFigureBounds6`: the figure loader's part bounds
  (`FigureManager::LoadFigureBounds`) with the unit's real weights.
- No scenario of its own: every zone load reaches the FIG loaders (the sky dome); the old-format loaders and
  `MorphFigureBounds6` run only for figures without a FIG header (possibly none in the data).

## Party records and class templates (0x004A5BD0-0x004A7E90)

`party_manager.cpp`, `class_templates.cpp`; scenario `party_records.txt`; every new game reaches both (004A88A0 / 004A88C0:
parties reset, then the templates). The object at 007AA5A0 (the party manager, script.md) holds at +0x24 the party records
(STLport vector of counted pointers to 0x7C-byte records: vtable 0073C360, count at +4, a party at +8 built by 00661050, name
CString at +0x78), two CStrings (+0x30 / +0x34) and at +0x38 the class templates (counted pointers to 0x28C-byte templates,
vtable 0073D0C4).
- `ClassTemplatesBuild` 004A7630 (thiscall, byte flag) releases the templates and builds new ones: flag set (004A88C0, from
  0057F280) classes 1, 3, 4, 5; flag clear (004A88A0) classes 1-5, the first with byte +0x0D = 0, the second with +0x0C = 0,
  the fifth with all weights zero. A template: new(0x28C), dword +4 = 0 before the constructor 004A6580, class at +8, bytes
  +0x0C / +0x0D, nine float weights +0x10..+0x30 (1, .5, .2, .1, 1, .5, .2, .1, .2), then `ClassTemplateFill` 004A7080, then
  push_back through a temporary counted pointer.
- `ClassTemplateFill`: the item database (template +0x3C, slot 18) lists weapon, armor, quick item and fourth-kind names; each
  is looked up (weapons 004AB720 in 007C86F8, armors 004AB820 in 007C8718 by char*; quick items 004ABBD0 in 007C8744 and the
  fourth kind 004ABAA0 in 007C86E4 by operator[] on an STLport key, which inserts a null record for unknown names) and a
  record whose class mask (+0x64; +0x4C for the fourth kind) has bit (class - 1) goes into the template's lists from +0x11C
  (adders 00699080 armors, 006990E0 weapons, 00699140 quick items, 00699260 fourth kind with counts 0x14 / 0x32). The spell
  database (+0xAC, slot 22) lists spells (00689570) and modifiers (00689610); `SpellCodeByName` 006893F0 /
  `SpellModifierCodeByName` 006894B0 key the maps 007C851C / 007C853C; allowed ones go to +0x1D4 and +0x11C (006991A0 /
  00699200). Each add appends unless the record is already there.
- Records: `Reset` 004A5BD0 erases every record, empties the strings, appends a new unnamed record whose party gets the member
  "Hero" / "Human Hero" (006628D0), "gz1g" at party +0x68, 0 at +0x6C and "gtime" = 6 (00460BC0 on party +0x54); `Add`
  004A5E90 (constructs a record inline: vtable 0073BB68, 00661050 on +8, vtable 0073C360) names it; `Remove` 004A5FA0; `Find`
  004A5E10 (the party of the first record with the name by `_mbscmp`, else of the last unnamed one). `PartyMapLoad` 004A8790
  / `PartyMapStore` 004A8800 work on hash_map<int, Party> (buckets at +4 / +8; node next / key / 0x70-byte party at +8; bucket
  = key % count unsigned): Load resets the target for key 0 (00661700(1, 0)), else copies the stored party (00661300) or
  returns false; Store copies into map[key] (004A91B0 inserts). Also `ClassTemplatesFind` 004A74F0,
  `ClassTemplatesQuestCompleted` 004A88E0, `PartyNextId` 004A8910, `PartyManagerNewGame` / `NewGameFour` 004A88A0 / 004A88C0,
  `BeginRedeploy` / `EndRedeploy` 004A8890 / 004A8880 (pass their argument to 004A6050 / 004A6360).
- Verify: all ours only except `Fill` (the original on a scratch template, the six vectors compared) and `Find` /
  `ClassTemplatesFind`. Kept: `Remove` advances its index after an erase, so a matching record moved into the erased slot is
  skipped; `operator new`'s result is written through (dword +4) before its null test in both constructors. Not rewritten:
  004A5870 / 004A5700 (static constructor / exit destructor) and the STLport instantiations at 004A8B70-004AFFF0 (library).

## Rewritten

Every rewritten function carries its address in its source comment (grep the address); the per-function tables that used to
be here are gone. Where things live: `world_server.cpp` (lookups, setters, object-map iterators), `world_server.cpp`
(server Setup / Teardown, slots 2 / 3; `hoover` +0x140 is a Hoover), `world_server.cpp` (Tick, Update, AcceptNewPlayers),
`world_save.cpp` / `world_map.cpp` / `world_create.cpp` (zone load and save, NewZone, EnsureZone, FindUnit),
`world_quests.cpp`, `world_party.cpp`, `party.cpp`, `world_time.cpp`, `world_game.cpp` (GameFlow), `world_glue.cpp` (the small glue objects), `world_briefing.cpp`,
`world_map.cpp`, `world_savefile.cpp`, `world_create.cpp`, `world_deleting_destructors.cpp`,
`world_client*.cpp` (frame, maps, setup, lights, draw, view, session), `figure_mesh*.cpp`,
`figure_manager.cpp`, `figure_part_draw.cpp`, the AI grids in `ai_block_search.cpp` / `ai_unit_shape.cpp` / `ai_map_load.cpp` /
`ai_path_smooth.cpp` / `ai_unit_shape.cpp` / `ai_fog_grid.cpp` / `ai_path_save.cpp` / `sector_map.cpp`,
`bink_video.cpp`, `console_commands.cpp` (`generateacks`), `logic_settings.h`, `cursor_setup.cpp`.

Facts a worker needs that the code comments do not say:

- Rebirth-patched (rewrite guide rule 9): MapsFolder 0055E990, MapPath 00595BA0, MapFileExists 005812F0, WriteMapFile 005813A0,
  FigureManager::CreateWithArchives 005AA510, AIEventTable::Setup 005C9310, WorldServer::NewZone 00595480,
  GameFlow::BuildZoneTable 0057F560, LoadZoneQuests 005809D0, WorldClient::LoadZone 00567AB0, AIMap::Load 005B43C0, the
  Bink loading movie close 005DA3C0.
- Zone start: `AIEventTable::Setup` sizes the grid 1 - ftol(size x -0.125) each way, zeroes the cells and copies "normal.dat"
  of aiinfo.res to 007C43C4. `NewZone` shows the loading movie, progress 0 / 6 / 7, the missing-file texts 00790AD8 /
  00790AAC, and reaches the AI graph and the map through the forwarders. `BuildZoneTable` fills 007AFDE0 from the maps
  folder's *.mq files (allod 0 / 1 / 2 by the lists 00790270 / 00790298 / 007902C0, else 3).
- `LoadZoneQuests` (stdcall, AL): the zone's "<zone>.mq" of the maps folder; a running server shares the file's bytes through
  a growing GameBuffer (the file read whole: `platform::ReadFile`), the path joins the quest files 007AFDD0 once and the
  briefing texts 007AF788 add it as a source (`GameBuffer::BeginWrite` 00580EE0 lives in game_buffer.cpp).
- `WorldClient::LoadZone`: the zone state block (tick, map and .mob names, exits, cave flag, unit looks, `unit_looks.h`);
  the old zone's prints, objects, maps, items and spells freed; the terrain's map through `LandscapeLoadMapOriginal` 006C68B0
  with the allod of the local player's zone; sectors of 16 cells, sky, lights table, exit markers, lighting, music, unit
  skins; loading movie steps 8-11. +0x1A8 is the `MusicSystem`, +0x184 the allod's name in lower case (00700EFE is
  MakeLower).
- `AIMap::Load` in two steps. `LoadTerrain`: the archive's .mp description gives the height scale and both climb tables
  (1023 costs each: up to 40 degrees for the large classes, 60 at the flat cost for class 0), tileDesc.reg of aiinfo.res the
  sixteen terrain types, the three grids sized 64 x 64 cells per sector, every sector file stamped into them.
  `AttachTerrain`: open marks, cost tables, water depths, searches, region map, events, sector map, the refresh.
  `ClimbTable` is `CArray<int>`, the terrain types `AlignedArray<AITerrainType>` (+0x104 cost, +0x114 open-cell
  marks), the grids have `SearchGrid` accessors, +0 is `loaded`.
- The AI's tables that game.exe's static initialisers build are built on first use in ours (C50a1): the smoother's 81
  transition tables (ai_path_smooth.cpp SmoothTables; game.exe's 007C3058 by 005C42B0, BuildSmoothEdges 005C3690 with the
  state lists 005C3840 / 005C3A00, SmoothTable::Build 005C3E10, MakeSmoothStep 005C4050 with cost factors 0x400 / 0x5A8 /
  0x479 / 0x46E / 0x648 / 0x200 / 0) and the sector spiral (sector_spiral.cpp SectorSpiralTables; game.exe's 005BF8F0:
  441 offsets of a 21 x 21 square sorted by sector distance with the game's qsort, a length per distinct distance, last
  radius 1e30, RadiusTree::Build 005BF5F0). The path vectors' @PathTables record holds game.exe's tables; the replay
  compares ours with them. The shape cache `UnitShapeCache` (game.exe's an STLport hash_map of handle to shape, ours a
  std::unordered_map by height, core.md C14q) releases every shape as its owner at exit: a fault there shows as a crash
  on quit. "AIMap::Load exact" must pass on every map (X31 below); a failure prints the first cell whose ground level
  differs and the height scale.
- Not hooked because they are not functions: the initialiser stubs 005C41C0 / 005C41F0 / 005C42A0 / 005BF8E0 / 005B31C0 /
  005B31E0, the tables' element constructor 005C4210 (`SmoothTable::Construct` is its body), the spiral's sort comparison
  005BFC10 (ours is passed to the game's qsort). Static initialisers before the hooks: 0057F480, 005809A0, 00581070.
- Candidates: 006C68B0 (the landscape's map load, `LandscapeLoadMapOriginal`); the event table's users in unit_ai.
- Forwarders kept at their addresses so the callees' wrappers see the calls: `LandscapeLoadMapOriginal` 006C68B0
  (originals_world.h); in world_client_maps.cpp for LoadZone 0057F150, 004E5750, 0055A190, 00578730, 00498630, 00692F00,
  006C9290, 00567500, 006B4E30, 00667B40, 00568800, 0066B300; in ai_map_load.cpp the event table's Setup 005C9310; one shared
  forwarder for MappedResource::OpenResource 00671730 (originals_engine.h); `FlowLoadQuestsOriginal`, `ClientLoadZone`
  (player_client.cpp), `AIMapLoad` (world_map.cpp).
- In the overlap with the units area and left to it (units.md / combat.md): 00545FD0, 005461E0, 00546200, 0054E5D0, 00558840.
- Compiler-generated lifetime code is in library.csv: the WorldServer constructor 00589ED0, destructor 0058AAD0, member
  destructors 0058A140 / 0058A450, World's 0055EC80, the database globals' constructors, destructors and resets
  0057CBE0-0057E510 (18), WorldClient's 005612F0 / 00561B00 (its destructor calls our `Teardown`). Left: the coder classes
  00580DD0-00580EE0 (vtables 0073F8C8 / 0073F8A4, used by 005809D0; script.md "Buffered file streams").

## Cleanup left and unverified points

- Declarations found in cleanup: MapObjectCompanion slot 17 IsDead, CMapObjectVirtuals slot 40 KeyId, world_effect.h
  Effect's slots 18-20, Terrain::heightRange, WeaponRecord bonuses / damageTypes; script "danger" is
  PlayerServer::danger, GetAIClass reads Unit::sizeClass, GameTime's fraction is tickFraction; WorldServer
  sentObjects (+0x12D88) and zoneHeader (+0x12E00, a MemoryStream built by 0041DD70 in 00589ED0, called through a
  CStream* so the slot dispatch stays), MainScript() (the Script at +0x12874; globals holds "Heroes"); Player link
  (+0x1C) / connection (+0x20); Projectile slot 0 DeletingDestructor and its 16-bit reference count; ObjectPtrListVtable()
  0073F530. script_exec's callees are call-only `...Calls` facades.
- Left: world_game.cpp's `At<int>(World(), 0x5C)` (World::worldHandle: on the server the pending connection source
  world_server.cpp steps (slot 6 the lobby's TakeNew, the last VCall in AcceptNewPlayers) and hands to 0058DAF0;
  ClientSetPlayer 00566CE0, declared with an int, passes it to 004782E0 as a link): needs a class and the forwarder retyped
  (app.cpp shares it); its two `__asm` helpers (_ftol, 24.0), `TextOpsAt`, the literal-address accessors (GameMode, Campaign,
  ...) that belong in game_flow_data.h; world_quests' GameFtol `__asm`; world_client_draw.cpp's six x87 `__asm` helpers;
  world_client_view.cpp's own draw-queue view (007AF508, a local DeferredUnits()) that could use world.h `FrameObjects` /
  `g_deferredUnits`.
- 00564AF0 (`Object3D::PartBox`) runs on a platform object (AddObject passes it in ECX; it reads Object3D::parts +0xD0 /
  +0xD4 and the always empty second part array +0xE0), not on the client. The client's +0x1F0 list is the platform set's face list (PlatformSet
  +0x1D4: an engine object, then an CList<Platform> at +0x1C; 00573000 carves 0x54-byte nodes; AddFace is AddHead).
- bird (+0x1B0) is the EffectMoshka SummonBird makes. Unverified: the +0xA0 array's elements are engine
  objects (00570000 calls their slot 4). WorldServer's +0x170..+0x2838 is its AIMap (`aiMap`, its `events` at +0x26C8 the AI event table; sizeof 0x12704):
  the old aiLayer_ (+0x2468, 005B1380's argument) is its region map, occluderReplanTime_ (+0x2804) its footprintTick (the
  world tick StampFootprint stores), the grid at +0x2808 its cells. world.h's SaveFileRead parameters `minimum` / `maximum` are swapped
  in meaning (ReadHeader takes newest, oldest; LoadMission passes 00790260, 00790268).
- Behaviour risks kept: the torch's slot 16 takes `float` (flame->volume) where the old code pushed the dword, so a
  signalling NaN could be quietened; Setup writes the handle's refs / flags as three stores (one dword in the original).
  Called by name instead of through their hooked address (their wrappers no longer see these calls): PartBox,
  PlatformSet::AddBox, 004031B0, 00404410 (BaseManager::FreeSpares), 00404920, 00404960, 0040D3B0, 00417DF0, 00413250,
  00413270 / 00418BE0, 004A53F0; world_savefile.cpp's coder (0042FCA0 / 00430450 / 00430960 / 0041D930) and
  ByteStream::ReadHeader / WriteHeader; AddSharedObject's ServerSession::FindPlayer (0041A2E0). None is Rebirth-patched.

## Lessons from two checks

- LoadSky 00562F70: SkyObject's own class id is 0x20; 0x44 is the figure factory's class number. UnitTextureRecords
  005683D0: the body list is [0] body, [1] empty, [2..7] items[1..6] (the loop at 00568441 starts at record offset 0xC + 0xC).
- "AIMap::Load exact" failed on every map (35 ground levels one unit off) because a vertex's altitude unit 0073FD84 is
  37800100 (1 / 65534 as a float, 2^-16 + 2^-31) and the port had 1 / 65535 (37800080) without having read the exe: the
  relative error of 1.5e-5 turns about one cell in 500 over a rounding edge. The height maths separates what the original
  stores to a float from what it keeps on the FPU stack (the first vertex's height, the diagonal's quarter, the third
  cell's water surface), so it is the same at 24 and at 53 bits. Rules: compare every float constant of a port with the
  exe's bits; and under /fp:fast a `constexpr float` initialised with an inexact constant expression (`1.0f / 65534.0f`)
  is folded at double precision where it meets a double, so write such constants as exact fractions or bit patterns with a
  static_assert on the bits.
## Zone state block

Patched by Rebirth, so hooked in the vanilla launch only: the player's client reaches it through
its address. The zone's state block: the flow acknowledges the server, sounds stop, then from
`block` the tick, the map and .mob names, the exits, the cave flag and the unit looks; the local
server makes sure it has the zone; the ground prints, ResetZoneObjects, ClearMaps, the items and
spells and the idle particle sources go; the terrain loads the map (a name without a drive from
the maps folder) with the allod of the local player's zone, the sector grids take its size (cells
of 16), the sky, lights table, exit markers, lighting and music follow the cave flag, the local
server's pending messages are appended to the view's, the unit skins are built and the view
resumes. Loading movie steps 8-11, then closed. Returns 1, or the result of the fatal error when
the map does not load.
