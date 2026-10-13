# World server, world client and game flow (0x00560000-0x005A9000)

Files: `world.h` (layouts), `world_*.cpp` (server, client, tick, zone / map, save, sync, units, game flow in `world_game.cpp`,
quests, party, briefing, guard, weather, loot, spells), `figure_mesh*.cpp`, `party_records.cpp`, `class_templates.cpp`.
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
  `TerrainOwner::SurfaceHeight`; TerrainOwner is CWorldClient), slots 29 / 30 (005618E0 / 00561930) combine platforms with
  ground / surface height. CWorld +0x5C is `RefPtr shared5C_`; +0x68 / +0x6C / +0x70 (updateCount_, unknown6C_,
  countdowns_) are shared by both worlds.
- Globals: 0079B5D8 multiplayer client session, 0079B5DC game server (+4 bit 31 = inactive), 007AF61C / 007AFCA8 game clock,
  007AFFC4 server tick, 007AFE20 the game flow, 007AFEA4 / 007AFEA5 interface / client glue, 007AFFB4 item manager, 007AFFA8
  effect manager, 007AF788 briefing texts. Named in world_data.h / game_flow_data.h.
- Player list: reference-counted, walked with 00404920 (begin; takes a count) / 00404960 (next). `WorldPlayer` dirty flags
  (+0x130): bits 2 / 4 / 8 weather-like settings, time, a 0..1 value set by 0058EE20; 0x20 countdowns; +0xC4 are the named
  float variables ("gtime" ...); +0x40 units; +0x70 the party (CParty).
- `ObjectIdMap` = STLport `hash_map<unsigned, T*>` plus the highest key (+0x14). Server maps: +0x104 units to update
  (inserting sets unit +0x174 to `((unit >> 5) & 15) + 0x80`), +0x12D70 every object by id, +0x12DA0 map objects saved with
  the zone, +0x12DB8 non-unit map objects, +0x12DD0 named objects, +0x12DE8 timed objects (spells / effects stamped with the
  tick at +0x94). `RemoveObjectFromServer` erases from +0x12D70, +0x12DD0, +0x12D88, +0x12DA0, +0x12DB8. Shared objects:
  `sharedObjects_` +0x12E60 (an MFC list of {id, player, value, objects}, from a multiplayer client's corpse loot 0066F4C0)
  and +0x12E64 ({id, player index (-1 = everyone)}).
- Server state: alarms +0x8C (read at 007AFFEC), +0x294 `CSectorMap` (005C0930 returns sector (x, y)'s unit list;
  `WakeUnitsAround` uses its own `SpiralCache` 007AFF08), +0x138 game result, +0x160 enables the first command queue, +0x6C
  paused, +0x12D64 game kind (1 single player, 3 no range test, 4 network), +0x12D68 / +0x12D6C .mob / map name. Zone exits
  are 0x3C-byte records (rectangle +0x14, rotation +0x24, target CString +0x28 "none" = no exit, entrance +0x2C).
- Client: session +0x20C (+0x20 the local view, `WorldSession::localView` = the CPlayerConnection; view +0x1C local
  WorldPlayer), +0xB4 text-key heroes, +0xB8 every object, +0xBC objects answering slot 29, +0xF0 screen entries (id, RECT),
  ticksRun_ +0x6EC (Segment counts it), tickFraction_ +0x6F0. Sector grids (+0x120 objects, +0x150 second; slot 41 005618A0
  / 42 005618C0 move an object in them): cells of 16 x 16 world units (cell = FISTP shifted right by 4 as unsigned, so
  negatives land outside), each an STLport list of counted object pointers; the grid at 007AF6D8 (moved by 0067BEF0 /
  0067CEF0) holds hundreds of entries per cell near effects. Platforms (+0x1D4 `PlatformSet`, 0x4C-byte records):
  `SurfaceHeight` projects the point onto each plane along z and keeps the highest z (margin 0.5), `Contains` tests (x, y)
  (margin 0.1), `AddBox` keeps only faces with normal z >= 0.8. Segment's light grey levels walk CLight objects (0068D730).
- Time: the day is 24 hours; time of day = fmod(tick * secondsPerTick + base, 24); a speed s gives secondsPerTick = 1 / s / 15
  (0: stopped, also for NaN; negative: the setting 0079BE5C). Wind: direction +0x24, strength +0x30 (ChangeWind clamps it).

## Server update path

`Update` (slot 6, 0058DFF0, once per world tick): drop disconnected players; for every party unit `WakeUnitsAround(unit +
0x1C, max(perception * factor, minimum) * 2 + 20)`; accept new players (0058DAF0); unless +0x6C is set each player's
00665DE0 and `Tick(0)`; each player's 0066E130; rebuild the "Heroes" script group; each player's `SyncPlayer` 0058B750 (0
disconnects).

`Tick` (0058DCD0): `++tick_` (+0x64); every 64 ticks (tick & 63 == 3) `UpdateWeather` 0058CCD0 (rain, lightning via
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
arrays (`world_object_sets.cpp`: 0047ADF0 sort-unique by a byte-wise radix sort of four passes of 0047B120, `DiffObjectSets`
0047B1F0: new objects get a reference, gone ones are released through slot 4; the original names the outputs added / gone /
same, `world_sync.cpp` added / kept / removed).

## Zone loading, saving, loading, game flow

- **State machine** 0057C700 (request at 007AFE20): every request first runs `TeardownZone` 005957E0 (views reset, +0x12E64
  list, `.mob` list, script state, `ClearMapObjects`, missiles, timed objects, 0055F890). Kind 3 (new game) empties
  saves\current (`ClearSaveFolder` 0057AAB0) and starts zone1 through 0057C460; kind 2 (load) calls 0057ABA0. Requests acted
  on by the flow's update 005800C0: 2 load (005816D0), 3 (005816C0), 4 go to a zone (00581700, also BeginMultiplayerGame), 5
  / 6 with a party copy (00581730 / 00581760). GameFlow (world.h): state_ +0, requestTarget_ +4, requestParty_ +8,
  requestArg_ +0x78, zone7C_ +0x7C, flag80_ +0x80.
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
  GameServer() (net_classes.h): the client's +0x2600 is player_ (a LoginMessage: state_ +0x28 the game state request,
  version_ +8, host_ +0x24), +0x25EC info_ (HostMessage), +0x25F4 map_, +0x25F8 zones_ / +0x25FC variables_, +0x261C
  gameType_; the server's +0x25A0 hostInfo_.
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
visible and shadow lists with `UnitOrderLess` and draws them. `CollectVisible`: type 0x32 heroes with a text-key id are
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

**Sorts**: `PtrSort` reproduces STLport (median of three, unguarded Hoare partition, recursion on the right part, heap sort
at the depth limit (00577FC0 / 00577730, 00577E90 / 005782F0), insertion sort of the first 16 then unguarded): 005740D0 /
00574390 by model (`UnitOrderLess`), 00573D50 / 00573F10 by model use. The original's recursive call lands on our verify
wrapper, so every recursion level is checked. **Rare Rebirth crash** (`docs/open-questions.md`): `UnitOrderLess`'s last test
is "less or unordered", so an object with a NaN view depth (+0x30) makes `Less(x, x)` true and STLport's unguarded partition
walks off the vector; the rewrite behaves exactly like the original, crash included (the verify logs "not orderable" and
sends NaN ranges to the original alone). Where the NaN depths come from is open.

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
- `CFigureMesh` (class 0x42, 0x220 bytes, vtable 0073C1C4; 0x43 (0073FC48) and 0x44 derive and share the loaders; class 0x41,
  0x1E0 bytes, vtable 0073FBF0, same layout up to +0x1E0 with its own loaders): a CTransformObject, +0x08 flags (0x20
  loaded), +0x9C MeshBuffers (pointer / count pairs the renderer draws from), +0x128 vertex offsets, +0x138 / +0x148 colour
  arrays (one per normal), +0x158 / +0x15C header words, +0x160 centre, +0x16C / +0x178 box, +0x184 radius, +0x188
  ClipBounds, +0x1C0 vertex packets, +0x1D0 normal packets, +0x1E0 corners, +0x200 corner indices (3 per triangle), +0x210
  texture coordinates. Every loader inlines its `AlignedArray` SetSize. `VertexPacket` (0x40): x[4], y[4], z[4], w[4] = 1.0.
- Slot 19 (005A2490 / 005A4CF0) picks slot 20 (FIG data: "FIG" + version byte) or 21 (older format) by the first dword. FIG8
  morphs trilinearly by the figure's three coefficients (s, t, u): `lerp(lerp(lerp(v0,v1,s), lerp(v2,v3,s), t),
  lerp(lerp(v4,v5,s), lerp(v6,v7,s), t), u)`; the old format has 6 corners and every array preceded by its count.
- Every loader rounds differently per component (VC6 register allocation; patterns from a symbolic x87 trace).
- No scenario of its own: figure meshes load whenever a unit or object is created on the client; the old-format loaders and
  `MorphFigureBounds6` run only for figures without a FIG header (possibly none in the data).

## Party records and class templates (0x004A5BD0-0x004A7E90)

`party_records.cpp`, `class_templates.cpp`; scenario `party_records.txt`; every new game reaches both (004A88A0 / 004A88C0:
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

| Group | Addresses and names | Verify |
|---|---|---|
| Server lookups | 00593AA0 FindObject, 00594180 GetPlayer, 00594220 GridHeight, 00591740 CountLivingMercs, 005949D0 IsObjectShared, 00594A30 CollectSharedObjects, 0058E4C0 GetZoneExit, 00578A90 LocalPlayerName | pure; outputs on a scratch copy |
| Server setters | 0058B6D0 MarkPlayersDirty, 0058CC70 SetUnknownE0, 0058CC80 SetUnknownDC, 0058EE00 SetUnknownE4E8, 005942C0 SetUnknown6C | `CompareSetter`: ours, snapshot (+0x6C / +0xDC..+0xE8, players' dirty flags), restore, original, compare |
| Server maps | 0058E520 AddTimedObject, 0058E5D0 RemoveTimedObject (80 callers), 0058D5D0 AddToUpdateMap, 005999E0 / 00599BD0 ObjectIdMap::Set | ours only |
| Server tick | 0058DCD0 Tick, 0058D3D0 WakeUnitsAround (the original's update-map entries recorded, the map put back), 00593FC0 InvokeAlarm, 0058DC90 SetGameResult, 0058DFF0 Update, 0058D280 UpdatePlayerTimes, 0058D670 UpdateUnits (+ TickSleepingEffects), 0058CCD0 UpdateWeather, 00578E30 ShowBolt, 0058E850 CastSpell, 0058E670 CreateMissile | ours only |
| Server zone, save | 005915E0 SetZoneExits, 005910A0 AddUnitToServer, 00596030 LoadMapObjects, 00595AB0 LoadMobFile (no scenario: zone1's scripts use no AddMob), 00596C60 ClearMapObjects, 00596730 SaveMapObjects, 00595C70 LoadMap, 0058B250 ResetZone, 0058B600 ResetState, 005957E0 TeardownZone, 005951A0 SendFreshObjects, 00596E70 LoadSave, 00596900 LoadSavedMapObjects, 005956E0 PrepareSave, 00596140 ReadAIGraph, 00596350 LoadZoneScripts | ours only; ReadAIGraph on a copy |
| Server save, sync | 00597BD0 SaveWorld (a scratch CMemoryStream first, the random generator restored: 00661C10 XORs gold with a fresh word), 00589CA0 LoadSharedObject, 005947E0 AddSharedObject, 0058B750 SyncPlayer, 0058C890 SendZone, 0047ADF0 sort-unique, 0047B120 radix pass, 0047B1F0 DiffObjectSets, 0047B770 / 0047B880 SetSize | SaveWorld / LoadSharedObject compared; rest ours only |
| Server units, party | 0058EF20 CreateUnits, 0058ECE0 UseQuickItem, 005927C0 CreateObject (FixAddMapObject), 005935C0 RemoveObject (FixRemoveMapObject), 00591800 UpdateMercBriefings, 00592090 CompleteBriefing, 00591DE0 ApplyMercBriefing, 005911C0 AwardExperience, 00590C70 SyncPartyRecords, 0058FD90 StoreParty, 00590A40 CollectParty, 00590700 Redeploy, 00590460 RestorePlayerItems, 005901E0 TakeZoneQuestItems, 00594CE0 CopyItem, 00594AB0 / 00594BC0 CreateItemNamed / CreateSpellNamed, 00594F30 AddCounted, 005942D0 Loot, 00593D60 LootDeadUnits, 0058DAF0 AcceptNewPlayers | ours only; TakeZoneQuestItems on a copy |
| Game flow | 0057B3B0 SaveGame, 00581610 SaveGameThunk, 0057A060 EnterZone, 0057BDF0 StartNetworkZone, 0057A780 LeaveZone, 0057AA80 ZoneNameInto, 0057AAB0 ClearSaveFolder, 00579B80 / 00579830 SaveFileWrite / Read, 00579E60 LoadMission, 005810A0 RegisterQuests, 005814C0 SessionMapNameInto, 0057F190 UpdateMultiplayerVariable, 0057E6B0 MultiplayerZoneInto, 0057F280 BeginMultiplayerGame, 0057F0F0 ReturnLocalParty, 0057F150 UpdateAndAcknowledge, 0057F390 RecreateClientSession, 00581660 CloseSessions, 00581630 RequestZone, 005816C0-00581760 request setters, 0057E740 StartServer, 0057EE10 SwitchMultiplayerZone, 0057F850 PickZones, 0057F820 NameInList, 0057FC90 / 0057FDE0 BriefingSpeakerInto / VariableInto, 0057FF40 MarkBriefing, 00580040 ForgetBriefing, 00580840 OnVariableChanged, 00580F10 UnloadQuestTexts, 0057FB60 RequestBriefing, 0057ABA0 LoadGame, 0057BD10 StartNewGame, 0057C700 ProcessRequest, 0057C460 StartZone, 0057EB10 AfterServerStart, 005800C0 UpdateFromServer, 00662FB0 SavedPartyList::Reload | ours only; string outputs compared |
| Time and wind | 0055FB30 CWorld::SetTime (31), 0055FB50 SetTimeSpeed (32), 0055FCB0 SetWind, 0058EEA0 / 0058EEE0 the server's SetTime / SetTimeSpeed, 0058EE20 ChangeWind (`world_time.cpp`) | SetTime / SetWind compared; every new game, the client's time message |
| Variable sort | 00589160 StampIntroSort, 00589240 StampInsertionSort (`world_stamp_sort.cpp`; the template in `stl_sort.h` shared with the draw-list sorts; UpdateFromServer calls them by address) | on a copy at every recursion level |
| Player guard | 005790E0 GuardPlayerData, 005791C0 GuardPlayers | ours only; involution checked |
| Client lookups | 00563F70 ClampedGroundHeight, 00560F00 PlatformSet::SurfaceHeight, 00561050 Contains, 00560E10 MatrixRows::Transform, 005640A0 NearestVisibleUnit, 00564AF0 CObject3D::PartBox, 005699F0 LocalView (69 callers), 00566EC0 IsLocalViewMode2, 00566EF0 IsLocalPlayerIdle, 00567440 FindExitAt, 005674C0 ExitCentre, 0056D9F0 EnvironmentFlags, 0057F440 IsMultiplayerClient (77 callers), 00578B40 GameTime, 00578BA0 LocalPlayerData (77), 00578B50 / 00578BE0 LocalPlayerVariable / PlayerZeroVariable, 00573A60 DepthLess, 00574300 UnitOrderLess, 00589580 EntryIdLess, 00573EE0 ModelUseLess | pure |
| Client frame | 0056A990 CollectVisible, 0056BF30 PickGround / 0056C350 PickSurface, sorts 005740D0 / 00574390 / 00573D50 / 00573F10, 00569E40 Render / 0056AFE0 DrawPass, 005641F0 Segment, 00595080 SelectLoadingMovie (FixProgressNames) | ours only; picks and sorts compared (ranges with a null element, no model or NaN depth go to the original alone) |
| Client maps | 00570860 / 00570BB0 SectorGrid::Move / Move2 (compared on the first 256 calls then every 32nd), 00571D50 MarkWithinRadius, 00571800 / 005718B0 CollectWindowsAt / At2, 0056FC60 PendingMapLookup, 0056E400 / 0056E440 RecordAt50 / AtB4, 005647E0 AddObject, 00564BE0 RemoveObject, 00564E70 ClearMaps, 005726B0 ReleaseIfIdentified, 00565790 / 005659D0 PickAt / PickAt2, 00566F70 CreatePartyUnits, 0056DAF0 ObjectsInRange, 00563290 Teardown, 00561E50 Setup, 00560650 AddFace / 00560820 AddBox (8 ULPs), 00559E60 UnitSet::Write, 0055A190 ReadUnitLooks | compared; AddObject .. Setup ours only |
| Exits, sky, skins, bird (A-W10) | 00567500 CreateExitMarkers, 005677F0 PartyInExit, 00561150 PlatformSet::SurfacePlane (the ground dust's plane), 00566C20 SummonBird, 00565380 OnQuestVariable (`world_client.cpp`), 005651A0 RefreshPlatforms, 0056D7E0 QueueSynchronised, 0056D7A0 DropShadows, 00565E40 UnitAt, 00566100 Deselect, 005630B0 ReferenceItemsAndSpells (`world_client_maps.cpp`), 005654F0 ObjectVariableName, 005652E0 ApplyObjectVariables (`world_client.cpp`), 00562F70 LoadSky (`world_client_setup.cpp`; CSkyObject slots 18 / 19 declared), 005683D0 UnitTextureRecords (`unit_skins.cpp`, called through its address) | PartyInExit / UnitTextureRecords / SurfacePlane / ObjectVariableName compared on scratch outputs, UnitAt after the original (its picks rebuild the hits); the others ours only (particle sources, figures, the bird: `ui_quest_notice` summons it; platform faces and the synchronised list: `levers`) |
| Startup, client set-up | 005CE0F0 LogicSettings::Load (007C53F8, `logic_settings.h`: [Logic], [RPG], [LMP], [Scenario], [DifficultyLevels]), 005DAEA0 SetUpCursors / 005DAE70 ResetCursors (`cursor_setup.cpp`), 00563710 SetUpGraphics (texture device, platforms, "numbers" digits +0x1B4, figure loader +0x34, sound counts), 00563930 LoadLights (tables `sunlight_` / `ambient_` / `sky_` at +0x22C / +0x3AC / +0x52C) | Load / LoadLights compared; rest ours only; every start |
| Client lighting | 00566360 UpdateLighting (Render, zone load, message 0x71: fog start, sun direction / colour, ambient, sky colour by hour; caves (`cave_` +0x6E0) hour 0 and a fixed sun; the terrain's sun 006C8ED0 once per hour), 00567680 UpdateExitMarkers (exit particle sources red when "z.<zone>" is 1) (`world_client_lights.cpp`) | D: original first (terrain hidden: its sun call is not idempotent), state put back, ours; colours / direction CheckNear, fog colour bytes within 1; markers bitwise |
| Figure meshes | 005A18E0 LoadBounds, 005A2500 LoadFig, 005A5C70 LoadFigShared, 005A3800 LoadOld, 005A4D60 LoadOldShared, 005AAE60 / 005AB9A0 MorphFigureBounds / 6, 0059FDF0 ExtendWorldBox, 005A4B30 / 005A4BF0 MirrorX / Y (ours, the original undoes it, ours again), 005A6E70 Resize, 005A6D40 RefreshBuffers, 005A0B10 SetOffsets, 005A0CD0 DrawOffset, 0059F0F0 CopyFrom, 0059F510 SetPose, 0059F710 / 0059F8F0 SaveToFile / LoadFromFile, 005ACE80 LoadMenuRects | on copies (colour arrays by shape only: the loaders size them without writing); the last ones ours only |
| A-W11 | 0058AE10 Setup / 0058B020 Teardown (server slots 2 / 3, `world_server_life.cpp`; WinMain at start-up and exit; +0x140 is `hoover_`, a CHoover), 005C8230 / 005C8370 AIPath::LoadOrder / SaveOrder (`ai_path_save.cpp`), 005C8B60 / 005C8BF0 / 005C8CC0 FogGrid Clear / Read / Write (`ai_fog_grid.cpp`), 005C06F0 CSectorMap::Init (`sector_map.cpp`; `sectors` is now `mfc::CArray<Sector, SectorElements>`, Sector has its virtual destructor), 00563190 CWorldClient::ReleaseItemsAndSpells (Teardown and the zone loads 00567A60 / 00567AB0), 005629B0 / 00562990 CTextManager::Destruct / DeletingDestruct (`text_sources.cpp`; the client's texts at exit) | SaveOrder / FogGrid::Write differential (stand-in stream); the rest ours only (every run; `world_load`; `spells_hero`) |
| A-W12 copies | 39 byte-identical copies (instruction streams compared with call targets resolved) registered beside their rewrites: hash400 ObjectsRemoveKey / ObjectsFind (00599A80 / 00599AE0), RefObjectDestructBase / RefValueDeletingDestructor (00562770, 0058FBE0 / 00562750, 0058FBC0), RefPtr::Destroy (00561A20, 005BE240), RefPtrRelease 0059B4B0, RefPtrNull (005683A0, 0058DFE0), ObjectHolderRelease (00598960-00598980), RefPtrCopyPlain 005BEF50, counted holder deleting destructors (0059DD90, 005DA270), ArrayFree1 (6), ClearArray 005AD870, NextParticle (0059A040, 0059A670, 005BD340), TextSourcesBegin / CopyTo (4 + 4), PatrolPoints destructors (005D95B0, 005D9D00), `CWorld::SetPaused` (was CWorldServer::SetUnknown6C; the client copy 00566350). Left: twins under 5 bytes and constant slots | the anchors' wrappers (own trampolines where they compare); 005AD870 / 00566350 in verify_world_range_copies.cpp / verify_world.cpp |
| A-W12 | 00565090 CWorldClient::ResetZoneObjects (+0xA0 emptied, second grid cleared with ClearWith, timed objects released, bird slot 18); `world_client_session.cpp`: 00566CE0 SetPlayer (ClientSetPlayer), 00566D30 PlayRecording, 00566D80 JoinGameClient, 00566E20 / 00566E70 PumpHasIncoming / PumpIncomingCount (00566CB0 is dead); 0055F890 CWorld::CloseSession (TeardownZone calls it directly now); `unit_ai_brain.cpp`: 005CD670 / 005CD800 AIReportArray SetSize / RemoveAt on the template (AIReportElements: 00558D10 Construct, 00558CE0's release as Destruct); `unit_ai.cpp`: 005D8D50 CorpsesPopHead; `figure_manager.cpp` (CFigureManager on CResourceManager): 005AC1A0 NewFigure, 005AC2F0 LoadPart, 005ACA60 LoadBoneAnimation, 005A2490 / 005A4CF0 CFigureMesh::LoadFromMemory (slot 19, byte-identical); 005A7160 FigureMeshDrawer::Animate (`figure_drawer.cpp`; +0xCC / +0xD0 are the texture offset); patrol mode 005D1930 / 005D1A40 Load / Save, 005D1140 Rebuild (slot 2), 005D0F90 Clone (slot 3), 005D9E40 / 005D9590 PatrolPointCopyAt | Animate, PatrolPointCopyAt, ConstructElements and Patrol Save compared; Patrol Load ours then our Save against the bytes read (version >= 0x44); the rest ours only with state checks (verify_world_client_session.cpp, verify_figure_manager.cpp, verify_figure_drawer.cpp, verify_unit_ai*.cpp) |
| A-W13 | `console_commands.cpp`: 005DFDA0 GenerateAcks (`console generateacks`: scenack.txt in the data folder, "#say <name>" and the text of every speech with a text, per record and kind map entry), 005DFC20 / 005DFCE0 WriteSpeechLines / WriteEnvironmentSpeechLines (through the file's WriteString slot, `console::CStdioFileVirtuals`); `world_glue.cpp` (new, with `world_glue.h`): 00578DB0 / 00578DC0 GameDataPathString / GameBasePathString; `unit_ai_looks.cpp`: 005D7970 AIMoveOrder::Assign (only the original fear mode calls it). Library (config/library.csv): 0055E1C0 / 0055E380 / 0055E510 (uninitialized_fill_n / copy of the look records), 00583060 (item record vector destructor), 0058C650 (two hash_maps, unwind only), the Terrain / particle manager / Sector destructors 00562B80 / 005627C0 / 005BFCD0 with their deleting destructors 00562B60 / 005627A0 / 0059E080 | GenerateAcks side by side (the file's length and hash after each run, emptied between); the line writers into stand-in files, lines compared, ours' replayed; the path getters pure; Assign ours only (scenario `console`) |
| A-W13 sectors | `sector_map.cpp` (`Sector` now has its members: the two object lists, `freeOccluder`, `occluders` as `mfc::CArray<SectorOccluder>`): 005BFE00 Sector::Clear, 005BFE70 NewOccluder, 005BFFD0 RemoveOccluder, 005C0060 FreeOccluder (ai_occluders.cpp uses them instead of its own record struct), 0059DF90 ConstructSectors (the sector array's ConstructElements), 005BF7D0 RadiusTree::Find (007C2FD8, the spiral's radius lookup: floats compared as integers). Library: 0058A960 (unwind-only member destructor), 005BF320 / 005C1C70 / 005C1C90 / 0059E260 (ConstructElements zero fills), 005C1440 / 005C1470 (CList Find / RemoveAt) | RemoveOccluder / FreeOccluder differential on the live pool (restored between), ConstructSectors against scratch sectors, Find pure; Clear / NewOccluder ours only (verify_ai_occluders.cpp; zone loads) |
| A-W13 AI map | `ai_grids.cpp` (new, `ai_grids.h`: `AllocateGrid` / `FreeGrid` over `SearchGrid<T>`): the grid template's copies 005B1470 / 005B14C0 / 005B1500 / 005B1680 (plain), 005B1550 (edge words -1), 005B15F0 (zeroed bytes), 005BD030 (cells), 005BD0B0 (occluder words: bit 8 set, bits 0-4 and 7 cleared, the rest as allocated), the destructor 005BD010 with 13 byte-identical copies (00598400 .. 00598B00, 005B1450, 005BCC30), the sized constructors 005BCBD0 / 005BCC50 / 005BCEE0 / 005BCF00 and 005AF5A0 (1 x 1); 005B3FF0 LocalSearch::Construct, 005B4110 CUnitLayer::Construct, 005B40F0 AIDirtyRect::Reset, 005BDD50 TerrainTypeAt, 005B3F40 / 005C9250 / 005C91F0 AIEventTable Construct / Clear / Destroy (`cells` is the grid block), 005B8620 CAIMap::BuildOpenMarks (`AITerrainType`: +0x104 cost, +0x114 open-cell marks; the per-class height it scales is dropped by the original too). `ai_unit_shape.cpp` (new; `CUnitShape` in ai_unit_layer.h): 005B3440 Construct (disc levels), 005B3540 Empty (slot 2), 005B34F0 Copy (slot 3), 005B33D0 CreateUnitShape (the shape cache's slot 0). Library: 005B1B30 / 005B1B50 / 005B3570 (deleting destructors), 005B1BE0, 005B1C70, 005B1CA0, 005BD1C0 (MFC), 005BCDE0, 005BD130, 005BD150, 005BEE40, 00582D80 (STLport). 005B31F0 / 005B32E0 are the shape cache's static constructor / exit destructor (007C2F70; with the static initialisers) | allocators and constructors against a probe the original fills (row layout, initialised elements; freed after); the unit shapes on copies / new objects, bytes compared; BuildOpenMarks, Reset, TerrainTypeAt compared; the frees and the event table ours only (verify_ai_grids.cpp, verify_ai_unit_shape.cpp; every start-up and map load) |
| A-W13 AI helpers | 005B30B0 RestoreBridgeCell (`ai_bridges.cpp`; the entry erased through 005BCE60), 005B1380 CRegionMap::SaveBlocks (`ai_region_reach.cpp`; world_save.cpp's AILayerSave), 005B22C0 AIPath::ToMotionOrder and 005B9BC0 CAIMap::ClearMarksAround (`ai_path_cells.cpp`), 0057FB90 FinishQuestMap (`world_glue.cpp`; script_exec's QuestAllowed: the game server's FinishMap for the name, 1 / 0; ECX unused). Library: 005AE820 / 005AE8E0 (STLport uninitialized copy / fill), 005AE030 (copy constructor) | SaveBlocks into a stand-in stream, ToMotionOrder twice into the same order (it overwrites it), ClearMarksAround on the whole marks grid for the first 64 calls: all compared; RestoreBridgeCell ours only against the entry read beforehand; FinishQuestMap compared without a live server, else ours only (multiplayer scripts only; no scenario reaches it) (verify_ai_helpers.cpp) |
| A-W13 order checks | `combat_tick.cpp`: 0054C8B0 CUnitServer::CanReachAndPlan (the too-high test `OutOfReachUp`, then CanPlanAction), 0054C960 CanReachAndPlanFrom (the same with the unit's position swapped for the point meanwhile); the AI's `UnitController::CheckOrder` / `CheckOrderAt` forwarders reach them. Library: 00577E90 (STLport make_heap of the model use sort), 00582030 (vector destructor), 0059A3C0 (CList destructor) | both compared with the random generator put back between the runs (verify_combat_sight.cpp; combat scenarios) |
| A-W13 flow and blocks | `world_game.cpp`: GameFlow::UseResources 0057DEE0 (the database follows the network state; ProcessRequest's inlined copy now calls it), SwitchResourceDatabases 0057CB50 (the four compiler-generated resets and loads through forwarders), StartClient 0057EA50, JoinServer 0057E970. `world_client_maps.cpp`: CWorldClient::LoadItemsAndSpells 00567A60 (state block 3), LoadNewItemsAndSpells 00567950 (block 5) and its reader LoadManagerObjects 005679A0. Library: 005896D0 / 00589610 (STLport uninitialized fill / copy), 005CD5B0 (CList::RemoveAt) | all ours only with results / state checked (verify_world_game.cpp, verify_world_client_maps.cpp); the blocks arrive with every zone state, the database switch and the client start only around multiplayer screens |
| A-W13 flow, glue, selection | `world_game.cpp`: GameFlow::Construct 00578460 / Destruct 005784D0 (the flow's static initialiser and atexit call them), SetNetworkZone 0057ED60, CurrentSaveSize 0057BC30. `world_glue.cpp`: RunScriptText 005785D0 (the command script 007C29CC; the error record copy 00456360 inlined), PlayerZeroVariables 00578C40. `world_client_maps.cpp`: CWorldClient::Select 00566030; `world_client_draw.cpp`: DrawLineLists 00569D60 (the hash map `lineLists_` +0x108). Library: 0056D400 / 0056D680 (compiler-generated destructors), 00562A80 (list constructor), 005782F0 (heap step), 005783B0 (vcall thunk), 0059A050 (CList::RemoveAt), 005894D0 (__median) | CurrentSaveSize against the original; the rest ours only with the state left checked (verify_world_game.cpp, verify_world.cpp, verify_world_client_maps.cpp). DrawLineLists runs every frame, Select with the battle selection; SetNetworkZone only in network games; 005939E0 and 00579FB0 have no callers (left) |
| A-W13 client lifetime | `world_client_maps.cpp`: the client world's two small managers' destructors (ClientListManager 00562EC0 / 00562EA0, ClientArrayManager 00562DF0 / 00562DD0; its Clear 004E2850 stays original). `effects_weather.cpp`: CRainSnow::DestructBody 005616C0 / DeletingDestruct 005617E0 (CRainSnow is now a CEngineObject with its work arrays named). `world_glue.cpp`: StoreStartFolder 0055EA10 (the CString 007AF454). | StoreStartFolder differential (text compared); the destructors ours only (lists empty, object and texture dropped, vtable back). Library rows: 005BE370, 00599CC0, 00577600, 00583A60, 0055FF90. |
| A-W13 picks, switches, lifetime | `world_client_maps.cpp`: CWorldClient::UnitAt2 00565D00, UnitsInBox 005656A0, PlatformSet destructor 00561A60 / 00561AE0, SaveListedObjects 0055E8A0. `world_client_draw.cpp`: the eight debug display switches (00566360..005665F0; `debugFlags_` at +0x88). `world_quests.cpp`: CWorldServer::TellParties 0058D340, TellPartiesGone 00593AF0, StorePlayerItems 00590160; `world_server.cpp`: RemoveAllUnits 005939E0 (no caller in the exe; Rebirth's binaries hold its address). `ai_grids.cpp`: AllocateEventGrid 005CD4B0, ResizeByteGrid 005CD3A0, CopyByteGrid 005CD410, the grid destructor copy 005CD490, CUnitLayer::Destruct 0058A8A0 / 0058A900; `sector_map.cpp`: the destructor 0058A700 / 0058A780; `text_sources.cpp`: TextSourcesDestruct 005628E0; `unit_ai_mode_lifetime.cpp`: FearMode::Clone 005D2880; `world_create.cpp`: CWorld::ConstructBase 0055EAB0. 00579FB0 is dead (no reference in the exe or Rebirth). 16 library rows. Differential: UnitAt2, UnitsInBox, SaveListedObjects, the switches, the three grid helpers, Clone, ConstructBase; the rest ours only. |
| A-W13 small world functions | `world_server.cpp`: ObjectMapIterator::First 0059AF20 / 0059AFF0, Next 0059AF80 / 0059B050 (an object map's values; byte-identical pairs). `world_time.cpp`: CWorld::NightLevel 0055FC50 (1 before 8, 0 until 20, then (hours - 20) / 4; ST0). `world_create.cpp`: CWorld::FindUnit 0055FB00. `world_game.cpp`: GameFlow::DropSessions 0057ECA0, EndSession 0057ED00. `world_client_maps.cpp`: CWorldClient::MarkFootprintsInSight 0056D740 (the +0x1BC list is the footprints), ApplyWind 00564190 (+0x6F4 is the CRainSnow), CentreOn 005662B0. `buffered_file.cpp`: BufferedStream::Construct 00580DD0, DestructBody 00580DF0 / 00580E40, BeginWrite 00580EE0, BufferedFile::DestructFile 00580E60 / 00580EC0. `party_items.cpp`: CParty::MakeCopyOf 0055E940 (slot 24 of the party and of CWorld's base and client vtables). `world_map.cpp`: CWorldServer::EnsureZone 00595B50 (NewZone stays the original). `world_party.cpp`: UseItemUp 00595020. Library: 005842C0, 00584340, 005776D0, 0059B7D0, 0059B8A0. Differential: the iterators, NightLevel (as float), FindUnit, MarkFootprintsInSight, Construct; the rest ours only. Functions under 5 bytes (the getters 0055EB20-0055EC50, 005795A0-00579680 and the like) cannot be hooked and stay. |
| A-W13 glue, heights, time | `world_glue.cpp` (checks in `verify_world_glue.cpp`): the rest of the glue 00578560-00578E20 and 005793E0, 38 small functions of the empty glue objects (server: construction variable, E4/E8, time of day, pass time, EnsureZone, 007C2D60, unit height, pause; camera: path stop / play / show / playing, recording start / stop; client: Render, sounds, ambience, music, FindUnit, party units, exits, leave / rejoin, local view and variables, flag 0x10, pause, map edge fade 0079BD34, texts, the single-player session start). Getters and lookups differential, the rest ours only. `world_client_maps.cpp`: CWorldClient::ExitTarget 00578A60 (the element's text directly, not through a copy), RemoveUnitsOf 00567400, SendFlag10 00566330, the height slots 29 / 30 005618E0 / 00561930, ShowNumber 0056CE40 (`numbers_` is the CFlyingHitPoints), LoadUnitFaces 0056DE80. `world_server.cpp`: the server's height slots 28-30 0058A390 / 0058A3D0 / 0058A3F0 (slot 27 now declared `double GridHeightAt(float, float)`). `world_time.cpp`: CWorld::IsNight 0055FC10, GetTime 0055FBD0. `world_create.cpp`: CWorld::FindMapObject 0055FAE0 (the client's slot 40). The camera path loader 0040ECD0 stays original (`CameraLoadPathOriginal`). Library: 00589930, 00589B00, 00589970, 00576E90, 00576ED0, 00577300, 00577330, 00577F90, 005849F0, 00584A40, 005755E0. |
| A-W13 figure meshes | `figure_mesh_life.cpp`: CFigureMesh::ConstructBase 005AABC0, Create 0059F210 (slot 2), Clear 0059F2B0 (slot 3 of 0073C224), ClearMesh 005A6C80 / ClearSharedMesh 005A6E10 (slot 3 of classes 0x42-0x44 / 0x41), DestructBase 0059F030, DestructMesh 005AAD00 / DestructSharedMesh 005AADC0 with their deleting destructors 005AACE0 / 005AADA0, SetDrawer 0059F390, SetOwner 0059F3D0; CFigureManager::Construct 005AA070 (the model map `models_` +0xBC, 100 buckets, vtable 0073FB88) and CreateFigureObject 005AAE00 (slot 23: a new figure of classes 0x41-0x44 gets a class 0x45 drawer from the context's slot 21). Create and Clear are called through their addresses. Library: 00589860, 0056D080 (compiler-generated destructors), 00583CB0 (vector<string>::erase) | Create and ConstructBase against the original on a scratch copy of the base part (0x1C0 bytes); the rest ours only with the state left checked (verify_figure_mesh_life.cpp; menus and every new game) |
| A-W14 small slots | `item_link.cpp`: CItemLink 0058FB60 / 0058FB90 / 0058FBB0; `unit_ai_mode_lifetime.cpp`: FearMode::Reset 005D28F0; `world_quests.cpp`: ClearPlayerItems 00591070 (the forwarder dropped); `world_client_maps.cpp`: PlatformSet::Clear 00561800; `world_sessions.cpp`: the client handle 005626B0 / 005626D0 / 00562700; `world_class_slots.cpp` (new, `world_class_slots.h`): the class name / id / getter slots of the world classes (41); five byte-identical copies 0058AAB0 (`ai_grids.cpp`), 005B3590 / 005A6F80 (`core_streams.cpp`), 0059AF10 / 0059AFE0 (`deleting_destructors_400.cpp`) | the name / id / getter slots, the links, FearMode::Reset, the handle's Shutdown, 005B3590 and the two clears differential (verify_world_class_slots.cpp and the files' own checks); ClearPlayerItems, PlatformSet::Clear, the handle's Clone / TakeNew, the four object slots 005618A0 / 005618C0 / 00562D70 / 00562DA0, 0058AAB0 and 005A6F80 ours only with the state left checked (start-up and every new game) |
| A-W14 lifetime | `world_deleting_destructors.cpp` (new, with its header): the scalar deleting destructors 00558CA0, 00558CC0, 00560590, 0059DD70, 005AA1A0, 005DA290, 005754E0, 00575510, 005768A0, 0059DDB0; `map_spec.cpp`: 0058EDD0 / 0058EDF0; `refptr_copies.cpp`: the unwind-only holders 005B84E0, 00598990, 005647C0, 00579350; `figure_drawer.cpp` (`CFigureClientSpecific` in link_figure.h): the drawer's lifetime 005A7010-005A7130; `figure_part_bounds.cpp`: 005A75D0; `misc_400.cpp`: the Construct73FB10 forwarder replaced. Library: 80 rows (config/library.csv) | the drawer's Construct / Destruct / DestructBase / Create / Destroy differential (verify_figure_drawer.cpp; every figure of a new game); the deleting destructors ours only with the vtable / state left checked (verify_world_deleting_destructors.cpp; objects die with every zone and game end); the unwind-only ones have no caller outside the exception tables |
| A-W14 AI leftovers | `ai_alarm.cpp` (`ai_alarm.h`): the alarm state 005AEA40 / 005AEA50 / 005AEA60 / 005AEC40 / 005AEC80; `unit_ai_modes.cpp`: the mode loads 005D1E60 / 005D2770 / 005D4CD0; `unit_ai_alarm.cpp`: HearAlarm 005D5940, ResetBehaviour 005D7C30; `unit_ai_hearing.cpp`: StepNoise 005C9770; `ai_map.cpp`: TurnObject 005BC180; `ai_motion.cpp`: the waypoint constructor 005C6E30; `ai_path_cells.cpp`: ConstructSteps 0054EAC0; `ai_grids.cpp`: CopyNodeGrid 005C4BA0 | the alarm state, ConstructSteps, CopyNodeGrid and the waypoint constructor differential; the mode loads ours only (a saved game with units in those modes: the save scenarios); HearAlarm, ResetBehaviour, StepNoise, TurnObject ours only with the state checked (combat scenarios). StepNoise multiplies in the order read from the exe; not confirmed by a run |
| A-W14 client and figures | `world_small_calls.cpp` (new): CWorldClient::NewTexture 005655D0, NewMaterial 005655F0, SetCamera 00565670, SetWorldValue 00566C10 (`worldValue_` +0x7C0, message 0x12), ReadWaterLevels 005683B0 (message 0x73), CWorldServer::SetWaterLevel 00593FA0; `unit_skins.cpp`: the texture record constructor 005685C0; `world_client_view.cpp`: ModelMaterialRank 005742F0; `figure_mesh.cpp` / `figure_mesh_life.cpp`: the base figure's slot 19 005A2480 (`flags_` bit 0x20), FitVertexBounds 005A37C0; `figure_manager.cpp`: FigureModel::Open 005A9EB0, CFigureManager::NewPart 005AC2B0 | SetCamera (every game start), SetWorldValue, the record constructor, the rank (pure), slot 19 and FitVertexBounds (item models in the interface) differential; NewTexture / NewMaterial (scene loads), Open (FindFigureSet), NewPart (every new game) ours only; ReadWaterLevels and SetWaterLevel ours only, no scenario (multiplayer client / a script call no scenario makes). SetWorldValue, the record constructor, the rank and slot 19 have no scenario either (their callers are rewritten or message-only) |
| A-W14 movies, maps folder | `bink_video.cpp`: CloseLoadingMovie 005DA3C0, CBinkVideo::Construct 005DB340, Destruct 005DB350, Close 005DBF70; `refptr_copies.cpp`: RefCountedDrop 005576B0. Rebirth-patched, so hooked in the vanilla launch only (rule 9; vanilla behaviour, the callers keep the forwarders at their addresses): `world_map_files.cpp` (new, `world_map_files.h`): MapsFolder 0055E990, MapPath 00595BA0, MapFileExists 005812F0, WriteMapFile 005813A0; `figure_manager.cpp`: CFigureManager::CreateWithArchives 005AA510 (slot 2: the text folder's figures.res and menus.res, the menu rectangles) | Construct, MapsFolder, MapPath (text by content; every zone load), MapFileExists (multiplayer client, no scenario) differential; the movie closing (the end of every zone load, the titles), RefCountedDrop, WriteMapFile (multiplayer client, no scenario), CreateWithArchives (every world's managers) ours only (verify_bink_video.cpp, verify_world_map_files.cpp) |
| A-W14 zone start (Rebirth-patched, vanilla only) | `ai_events.cpp`: AIEventTable::Setup 005C9310 (the grid 1 - ftol(size x -0.125) each way, cells zeroed, "normal.dat" of aiinfo.res copied to 007C43C4); `world_save.cpp`: CWorldServer::NewZone 00595480 (the loading movie, progress 0 / 6 / 7, the missing-file texts 00790AD8 / 00790AAC, the AI graph and the map through the forwarders at their addresses); `world_game.cpp`: GameFlow::BuildZoneTable 0057F560 (007AFDE0 from the maps folder's *.mq files: the allod 0 / 1 / 2 by the lists 00790270 / 00790298 / 007902C0, else 3) | Setup side by side on the table (the original first, its bytes noted and poisoned, then ours; CAIMap::Load at every zone load); NewZone and BuildZoneTable ours only with the state left checked (verify_world_map_files.cpp, verify_world_game.cpp; newgame and every scenario after it) |
| A-W15 zone loaders (Rebirth-patched, vanilla only) | `world_briefing.cpp`: LoadZoneQuests 005809D0 (stdcall, AL; the zone's "<zone>.mq" of the maps folder: a running server shares the file's bytes through a growing GameBuffer, the path joins the quest files 007AFDD0 once, the briefing texts 007AF788 add it as a source; `GameBuffer::BeginWrite` 00580EE0 moved there from BufferedStream). `world_client_maps.cpp`: CWorldClient::LoadZone 00567AB0 (the client's zone state block: tick, map and .mob names, exits, cave flag, unit looks; the old zone's prints, objects, maps, items and spells freed; the terrain's map through `LandscapeLoadMapOriginal` 006C68B0 with the allod of the local player's zone, grids of 16-cell sectors, sky, lights table, exit markers, lighting, music, unit skins; loading movie steps 8-11). The unit look records are in `unit_looks.h`, +0x1A8 is the `CMusicSystem`, +0x184 the allod's name in lower case (00700EFE is MakeLower). `ai_map_load.cpp` (new): CAIMap::Load 005B43C0 in two steps, `LoadTerrain` (the archive's .mp description gives the height scale and both climb tables, 1023 costs each: up to 40 degrees for the large classes, 60 at the flat cost for class 0; tileDesc.reg of aiinfo.res the sixteen terrain types; the three grids sized 64 x 64 cells per sector and every sector file stamped into them) and `AttachTerrain` (open marks, cost tables, water depths, searches, region map, events, sector map, the refresh). ai_map.h: `ClimbTable` is `mfc::CArray<int>`, the terrain types a `lib::AlignedArray<AITerrainType>` with their fields named, the cell / occluder / water grids have `SearchGrid` accessors, +0 is `loaded` | LoadZoneQuests and LoadZone ours only with the state left checked (the result against the file's presence, exactly one quest file for the zone; grids sized for the terrain, no prints, sky, exits, lower-case allod). CAIMap::Load side by side: the map is copied before the original runs (its bytes and deep copies of the climb tables, terrain types and the three grids), ours' `LoadTerrain` then loads the same archive into the copy; height scale, climb tables, terrain types, grid sizes and rows, every cell's ground level and flags (flags only when the refresh was deferred) and the occluder words' bits 0x19F compared, once allowing a unit at a rounding edge and once exactly. `AttachTerrain` runs in the original only (verify_world_briefing.cpp, verify_world_client_maps.cpp, verify_ai_map_load.cpp; `newgame` and every zone change, vanilla launch) |
| A-W15 static initialisers | They run before the hooks are installed, so the ports are registered at their addresses but a running game calls only the exit halves. `ai_path_smooth.cpp`: the smoother's 81 transition tables 007C3058, BuildSmoothTables 005C42B0 (for every previous / next direction pair the states of the previous step moved back by it against those of the next), BuildSmoothEdges 005C3690 with the straight / diagonal state lists 005C3840 / 005C3A00 (seven / nine moves), SmoothTable::Build 005C3E10 (copies, steps, chained steps), MakeSmoothStep 005C4050 (turn, cost factor 0x400 / 0x5A8 / 0x479 / 0x46E / 0x648 / 0x200 / 0, way back), SmoothTable::Destruct 005C4250. `sector_map.cpp`: the sector spiral 005BF8F0 (441 offsets of a 21 x 21 square sorted by the gap between the sectors with the game's qsort, a length per distinct distance, the last radius 1e30) and RadiusTree::Build 005BF5F0. `ai_shape_cache.cpp` (`ai_shape_cache.h`: `UnitShapeCache`, an STLport hash_map of handle to shape): Construct 005B32E0 (193 buckets), Destruct 005B31F0 (every shape released as its owner, the handle's reference dropped: what 005BE280 does, through RefPtr::Destroy and Release). Library: 005C5930 / 005C5AB0 (CArray::SetAtGrow of the 20-byte and 8-byte elements) | `EI_STARTUP_CHECK` (include/ei/hook.h): a check that runs once at the end of `InstallHooks`, when what the initialisers built is still untouched. verify_ai_static_tables.cpp builds each with our code into its own storage and compares: the 81 tables (sizes, allocated sizes and every element), the spiral (offsets in order, lengths, the tree's depth, mask and keys), the cache (vtable, 193 empty buckets; its functor bytes are stack garbage and skipped). Every start; the exit halves ours only, unchecked |
| A-W15 retypes | CWorldClient +0x1B4 `numbers_` is a `CFlyingHitPoints*` (its Construct 005E1710 / Destruct 005E17E0 called by name: the two forwarders dropped), +0x6F4 is the `CRainSnow` (abstract like every engine object, so the world keeps its 0xC8 bytes and `RainSnow()` is the object; its `mode` replaces `unknown7A8_`); `ClientNumbers` and `ClientRibbons` are gone | builds only |
| Party records, templates | 004A5BD0 Reset, 004A5E90 Add, 004A5FA0 Remove, 004A5E10 Find, 004A7630 ClassTemplatesBuild, 004A7080 ClassTemplateFill, 004A74F0, 004A88E0, 004A8910, 004A88A0, 004A88C0, 004A8890, 004A8880, 004A8790, 004A8800 | above |

## Original bugs and oddities kept

- `CountLivingMercs` returns living - 1 (0 when the player is missing, so -1 with no living units). `RemoveTimedObject` looks
  the id up again after Release; `Tick` reads a timed object's `next` after its slot 6 and Release ran; the missile walk saves
  `next` before Update. `NearestVisibleUnit`, `LocalPlayer*` do not null-check the session / view; `DepthLess` asks each
  object's IsDead up to three times.
- `InvokeAlarm` stops at the first map entry with a null object and does not check the controller for null; `InvokeAlarm(0,
  ..., force)` clears all five alarms including padding. `ClearMapObjects` / `SaveMapObjects` stop at the first null map
  entry; `LoadMapObjects` / `LoadMobFile` return what `FatalError` returns on failure (LoadMobFile treats non-zero as
  success); `SetZoneExits` re-reads the source size each iteration; SEH frames of the loaders are not reproduced.
- `LoadSave`: shared-object elements are copied from a local whose first three dwords are never written; the player list
  iterator comes from the heap (`operator_new(8)`), as in `SaveWorld`. `LoadSavedMapObjects` marks every part changed when
  any change was saved (tests `1 << i`, not `changed & bit`) and uses the object before its null test. `SyncPlayer` passes
  0066EC10 a dword whose upper bytes are the dirty flags. `CreateUnits` places a unit near the requested position, not near
  its line position.
- CastSpell / Loot / CreateMissile / record syncs / RecreateClientSession / CopyItem write +4 (the count) of a new allocation
  before testing it for null; `AwardExperience`'s solo branch needs the player list's +0x2C non-zero; `AddSharedObject`'s
  second argument is unused; `LootDeadUnits` does not stop at an empty map entry and calls Loot / RemoveObject on the global
  server, not `this`; `MultiplayerZoneInto` maps game types above 3 (unsigned) to bz1mpg.
- Figure meshes: 005A4D60 does not test the loaded flag; `DrawOffset`'s shared buffer grows without copying and allocates an
  empty block for 0 vertices; the old-format loader rounds the box minimum differently for class 0x41 and 0x42; FLD / FSTP
  copies quiet a signalling NaN (ours may copy bits). `AddFace` copies 0x4C bytes of uninitialised stack over the new record
  first. Client: picking runs 100 + 100 height queries when |dz| never drops to 0.01; `ObjectsInRange`'s dz is the point's z
  minus itself (0 or NaN).
- ReferenceItemsAndSpells tests the item factory twice and never the spell manager before walking the spells.
- UpdateLighting writes stack garbage as the sunlight's fourth float (+0x6D8) and the sky colour's fourth argument (ours
  leave +0x6D8 alone and pass 0); a black ambient entry (0 or NaN in all three) takes 0.8 x the sunlight; NaN hours take
  the night formulas.
- With every flag off the Rebirth-patched rewrites keep the vanilla behaviour (`rebirth.md`).

## Floating point

Files with float code use `float_control(precise, on)` (world_units, world_weather, world_update, world_spells, world_party,
parts of world_save / world_quests, world_client_*): stores to float where the original stores (`StoreFloat`), FPU-stack
values as doubles, NaN branches as the original's FCOMP tests (`!(a >= b)` forms), random words through
`x87::FromUnsigned`, truncation through the game's `_ftol` (006E853C). `Redeploy`'s facing uses FPATAN through a naked
helper. `DepthLess`'s `a + b + c + offset` must stay `(a + b) + (c + offset)`. Plain C++ (rule 7) for the rest: a rotated
normal exactly at z = 0.8 or a distance exactly on a radius may rarely differ. world_save.cpp's UpdatePlayerTimes lost its
pragma (DR-C4): release code is fild elapsed, fmul secondsPerTick_, fadd offset, faddp gtime, fstp float, the same order.

## Open items

- A-W15 finished the range: nothing of 5 bytes or more is unported in 0x00540000-0x005E0000 (46 functions under 5 bytes
  cannot be hooked). The three Rebirth-patched loaders 005809D0, 00567AB0 and 005B43C0 are vanilla behaviour, hooked in
  the vanilla launch only; rewritten callers keep reaching them through the forwarders at their addresses
  (`FlowLoadQuestsOriginal`, `ClientLoadZone` in player_client.cpp, `AIMapLoad` in world_map.cpp).
- A-W15's checks have not been run either (no game runs in that session), nor A-W14's. The first vanilla run should
  watch, in this order: the three start-up checks ("static initialiser: ..." in the log, right after the hooks), then
  LoadZoneQuests, CWorldClient::LoadZone and "CAIMap::Load" / "CAIMap::Load exact" at `newgame`, then A-W14's zone start
  ones (Setup, NewZone, BuildZoneTable, MapsFolder, MapPath) and StepNoise. The start-up checks call `Report` at the
  precision the process starts with (53 bits), so they show in the report's precision list: that is the precision the
  initialisers themselves ran at, and the spiral's square roots need it.
- CAIMap::Load: `AttachTerrain` is not exercised by the wrapper (it cannot run twice on the real map; each function it
  calls has its own check). A map whose float steps sit on a rounding edge would fail "CAIMap::Load exact" only.
- The exit halves of the static initialisers (SmoothTable::Destruct, UnitShapeCache::Destruct) and the two loaders that
  run ours only have no comparison with the original. UnitShapeCache::Destruct walks the live cache at exit: a fault
  there would show as a crash when the game quits.
- Not listed as functions, so not hooked: the initialiser stubs 005C41C0 / 005C41F0 / 005C42A0 / 005BF8E0 / 005B31C0 /
  005B31E0, the tables' element constructor 005C4210 (`SmoothTable::Construct` is its body) and the spiral's sort
  comparison 005BFC10 (ours is passed to the game's qsort).
- Next candidates: none in this range. Around it: 006C68B0 (the landscape's map load LoadZone calls), the event table's
  users in unit_ai, and typing CacheGet 005BD7F0 and CAIMap::UnitShape over `UnitShapeCache` (they still read the
  cache through offsets and keep their own handle structs and the 005BE280 forwarder).
- Forwarders A-W15 added (calls kept at their addresses so the callees' wrappers see them, or callees not rewritten):
  `LandscapeLoadMapOriginal` 006C68B0 (originals_world.h; not rewritten); in world_client_maps.cpp for LoadZone
  0057F150, 004E5750, 0055A190, 00578730, 00498630, 00692F00, 006C9290, 00567500, 006B4E30, 00667B40, 00568800,
  0066B300; in ai_map_load.cpp the event table's Setup 005C9310. The merge of main at the ticket's start left one port
  of the maps folder 0055E990 (world_map_files.cpp) and one shared forwarder for CMappedResource::OpenResource
  00671730 (originals_engine.h).
- In the overlap with the units area and left to it (units.md / combat.md): 00545FD0, 005461E0, 00546200, 0054E5D0,
  00558840.
- Lifetime code: the CWorldServer constructor 00589ED0, destructor 0058AAD0, member destructors 0058A140 / 0058A450 and
  CWorld's 0055EC80 are compiler-generated (library.csv, A-W11), as are the database globals' constructors, destructors and
  in-place resets 0057CBE0-0057E510 (18). Left: the coder classes 00580DD0-00580EE0 (vtables 0073F8C8 / 0073F8A4, used by
  005809D0; script.md "Buffered file streams"). `CWorldClient`'s constructor 005612F0 / destructor 00561B00 are in
  library.csv (compiler-generated member construction; the destructor calls our `Teardown`).
- Static initialisers (before the hooks): 0057F480, 005809A0, 00581070.

## Cleanup left and unverified points

- Declarations found in cleanup (C7, C8, C10): CMapObjectCompanion slot 17 IsDead, CMapObjectVirtuals slot 40 KeyId,
  world_effect.h CTimedObjectVirtuals (18-20), Terrain::heightRange, WeaponRecord bonuses / damageTypes; script "danger" is
  CPlayerServer::refreshPending_, GetAIClass reads CUnit::sizeClass, GameTime's fraction is tickFraction_; CWorldServer
  sentObjects_ (+0x12D88) and zoneHeader_ (+0x12E00, a CMemoryStream built by 0041DD70 in 00589ED0, called through a
  CStream* so the slot dispatch stays), MainScript() (the CScript at +0x12874; globals_ holds "Heroes"); WorldPlayer link
  (+0x1C) / Connection() (+0x20); Projectile slot 0 DeletingDestructor and its 16-bit reference count; ObjectPtrListVtable()
  0073F530. script_exec's callees are call-only `...Calls` facades.
- Left: world_game.cpp's `At<int>(World(), 0x5C)` (CWorld::shared5C_: on the server the pending connection source
  world_update.cpp steps (slot 6 the lobby's TakeNew, the last VCall in AcceptNewPlayers) and hands to 0058DAF0;
  ClientSetPlayer 00566CE0, declared with an int, passes it to 004782E0 as a link): needs a class and the forwarder retyped
  (app.cpp shares it); its two `__asm` helpers (_ftol, 24.0), `TextOpsAt`, the literal-address accessors (GameMode,
  Campaign, ...) that belong in game_flow_data.h; world_quests' GameFtol `__asm`; world_client_draw.cpp's six x87 `__asm`
  helpers; world_client_view.cpp's own draw-queue view (007AF508, a local DeferredUnits()) that could use world.h
  `FrameObjects` / `FrameDeferredUnits`.
- 00564AF0 (`CObject3D::PartBox`, once "CWorldClient::ListItem") runs on a platform object (AddObject passes the object in
  ECX; it reads CObject3D::parts +0xD0 / +0xD4 and pendingParts +0xE0), not on the client. The client's +0x1F0 list is the
  platform set's face list (PlatformSet +0x1D4: an engine object, then an mfc::CList<Platform> at +0x1C; 00573000 carves
  0x54-byte nodes; AddFace is AddHead).
- Unverified: owned1A8_ / owned1B0_ are engine objects (only slots 0 / 2 are called); the +0xA0 array's elements are engine
  objects (00570000 calls their slot 4); whether CWorldServer's aiLayer_ (+0x2468..+0x2804, 005B1380's argument) and
  occluderReplanTime_ (+0x2804) belong to the sector map. world.h's SaveFileRead parameters `minimum` / `maximum` are swapped
  in meaning (ReadHeader takes newest, oldest; LoadMission passes 00790260, 00790268).
- Behaviour risks kept: the torch's slot 16 now takes `float` (flame->volume) where the old code pushed the dword, so a
  signalling NaN could be quietened; Setup writes the handle's refs / flags as three stores (one dword in the original). Now
  called by name instead of through their hooked address (their wrappers no longer see these calls): PartBox,
  PlatformSet::AddBox, 004031B0, 00404410 (CBaseManager::FreeSpares), 00404920, 00404960, 0040D3B0, 00417DF0, 00413250,
  00413270 / 00418BE0, 004A53F0; world_savefile.cpp's coder (0042FCA0 / 00430450 / 00430960 / 0041D930) and
  CStream::ReadHeader / WriteHeader; AddSharedObject's ServerSession::FindPlayer (0041A2E0). None is Rebirth-patched.

X28 (2026-10-09): A-W10's two mismatches. LoadSky (00562F70): the port was right; its check wanted
GetClassId() == 0x44, but 0x44 is the figure factory's class number, CSkyObject's own id is 0x20.
The check now prints class, name, flags and material. UnitTextureRecords (005683D0): the port was
wrong; the body list is [0] body, [1] empty, [2..7] items[1..6] (the loop at 00568441 starts at
record offset 0xC + 0xC), not [1..6] with [7] empty. Both removed from tests/verify/disabled.txt.
