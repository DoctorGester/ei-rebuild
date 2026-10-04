# World server and world logic (0x00560000-0x005A9000)

Code: `src/game/world.h`, `world_server.cpp` (CWorldServer), `world_client.cpp` (CWorldClient,
platforms, matrix helper), `world_rules.cpp` (sorting predicates, game interface glue).
Verify wrappers: `tests/verify/verify_world.cpp`. No shared files were changed.

## Data structures recovered

- **CWorldServer** (vtable 0073F928, 42 slots; the global at 007AFF60) and **CWorldClient**
  (vtable 0073F41C, 43 slots; the global at 007AF5B8) share a base whose methods live at
  0055E000-0055FFFF (outside this range): slot 20 (0055EB50) returns the player list at +0x38,
  slot 40 is FindObject (00593AA0 on the server, 0055FAE0 on the client), 0055FB00 is FindObject
  restricted to units (class id 0x50), 0055FC10 is IsNight (tick +0x64 x seconds per tick +0x20 +
  base +0x1C, modulo the day length: before 6 or from 22 hours). Slot 27 is a ground height (server 00594220
  from its coarse grid, client 00563F70 from the terrain), slot 28 the surface height (client 00564070
  = TerrainOwner::SurfaceHeight in terrain.cpp; TerrainOwner is CWorldClient).
- **Player list**: a reference-counted object whose MFC list head is at +0x38 (node data at +8).
  The original walks it with 00404920 (begin; takes a count on the list) / 00404960 (next) and
  drops the count inline; `PlayerIterator` mirrors that and calls the two helpers out of line.
- **WorldPlayer**: +0x24 player number (0 = local), +0xA0/+0xA4 mercenary records (0x784 bytes,
  unit id at +0x1C), +0xC4 named float variables (00460AF0 Get, 00460EE0 Set; "gtime" etc.),
  +0xD8 name (CString), +0x110 last tick seen, +0x130 dirty flags (bits 2/4/8 set by the server's
  setters: weather-like settings, time, a 0..1 value set by 0058EE20).
- **ObjectIdMap**: STLport `hash_map<unsigned, T*>` (0x14) followed by the highest key inserted
  (+0x14). The server has one at +0x104 (units to update; inserting sets unit+0x174 to
  `((unit >> 5) & 15) + 0x80`), +0x12D70 (every object by id, FindObject) and +0x12DE8 (timed
  objects: spells/effects stamped with the tick at +0x94). 005999E0 / 00599BD0 are its Set (two
  instantiations with identical code); `stlp::hash_map::operator[]` reproduces the inlined insert.
- **Shared-object list** (server +0x12E64): MFC list of `{object id, player index}` (-1 = everyone).
  In multiplayer the player index is +0x30 of the game server's player record (0041A2E0 on the
  global at 0079B5DC); otherwise -1.
- **Zone exits**: 0x3C-byte records, server +0xF0/+0xF4, client +0x90/+0x94; rectangle at +0x14,
  rotation at +0x24 (004E59F0 Contains), target zone CString at +0x28 ("none" = no exit), entrance
  number at +0x2C.
- **Coarse height grid** (server): rows at +0x280C, width/height +0x2810/+0x2814, 9-byte cells
  with a u16 height at +4 divided by the float at +0x290; cell = fistp(2v - 0.5) through a float.
- **Platforms** (client +0x1D4, `PlatformSet`): a singly linked list at +0x20 of 0x4C-byte records:
  bounding circle (x, y, radius), origin, u/v axes, normal, extents. SurfaceHeight projects the point
  onto each platform plane along z and keeps the highest z (margin 0.5), Contains tests (x, y)
  (margin 0.1). The client's slots 29/30 (005618E0/00561930) combine them with the ground/surface
  height.
- **CWorldClient** fields: +0x180 Terrain, +0x184 zone name (gipat, ingos, suslanger, ...), +0x20C
  session whose +0x20 is the local view (005699F0; view +0x1C = local WorldPlayer, +0x20 mode,
  +0x50D8/+0x50DC unit id array).
- Globals: 0079B5D8 multiplayer client session, 0079B5DC game server (both: +4 bit 31 = inactive),
  007AF61C/007AFCA8 game clock (whole ticks / fraction), 007AFFC8 stamp written by 00675CA0.

## Rewritten

| Address | Name | Verification |
|---|---|---|
| 00593AA0 | CWorldServer::FindObject (slot 40) | pure |
| 00594180 | CWorldServer::GetPlayer | pure |
| 00594220 | CWorldServer::GridHeight (slot 27) | pure, float bits |
| 00591740 | CWorldServer::CountLivingMercs ("MercsNumber") | pure |
| 005949D0 | CWorldServer::IsObjectShared (AL) | pure |
| 00594A30 | CWorldServer::CollectSharedObjects | output: original into a clone of the group |
| 0058B6D0 | CWorldServer::MarkPlayersDirty | snapshot: ours, record, restore, original, compare |
| 0058CC70 | CWorldServer::SetUnknownE0 | snapshot |
| 0058CC80 | CWorldServer::SetUnknownDC | snapshot (includes players' dirty flags) |
| 0058EE00 | CWorldServer::SetUnknownE4E8 | snapshot |
| 005942C0 | CWorldServer::SetUnknown6C | snapshot |
| 0058E4C0 | CWorldServer::GetZoneExit | output: original into a scratch CString (buffer pointer compared) |
| 0058E520 | CWorldServer::AddTimedObject | stateful: map entry, count, max id, tick, refcount, flags |
| 0058E5D0 | CWorldServer::RemoveTimedObject (80 callers) | stateful: entry gone, count |
| 0058D5D0 | CWorldServer::AddToUpdateMap | stateful: map entry, count, max id, +0x174 |
| 005999E0, 00599BD0 | ObjectIdMap::Set | stateful: entry, count, max id |
| 00563F70 | CWorldClient::ClampedGroundHeight (slot 27) | pure, float bits |
| 00560F00 | PlatformSet::SurfaceHeight | pure, float bits |
| 00561050 | PlatformSet::Contains (AL) | pure |
| 00560E10 | MatrixRows::Transform (returns out) | output: ours into scratch first, then compare |
| 005640A0 | CWorldClient::NearestVisibleUnit | pure |
| 00564AF0 | CWorldClient::ListItem | pure |
| 005699F0 | CWorldClient::LocalView (69 callers) | pure |
| 00566EC0 | CWorldClient::IsLocalViewMode2 (AL) | pure |
| 00566EF0 | CWorldClient::IsLocalPlayerIdle (AL) | pure |
| 00567440 | CWorldClient::FindExitAt | pure + output index into scratch |
| 005674C0 | CWorldClient::ExitCentre (returns out) | output only |
| 0056D9F0 | CWorldClient::EnvironmentFlags (unit state, island, day/night, two settings) | pure |
| 00573A60 | DepthLess::Less (sort predicate) | pure, AL compared |
| 00574300 | UnitOrderLess::Less (sort predicate) | pure, AL compared |
| 00589580 | EntryIdLess::Less (sort predicate, 0/1 in EAX) | pure |
| 0057F440 | IsMultiplayerClient (77 callers) | pure |
| 00578B40 | GameTime | pure, float bits |
| 00578BA0 | LocalPlayerData (77 callers) | pure |
| 00578A90 | LocalPlayerName (__stdcall, returns its CString* argument) | output: original into a scratch CString |
| 00578B50 | LocalPlayerVariable (__stdcall) | pure, float bits |
| 00578BE0 | PlayerZeroVariable (__stdcall) | pure, float bits |

All names are provisional and set in Ghidra (also CWorld::IsNight 0055FC10 and CWorld::FindUnit
0055FB00, which belong to the base class range and stay original).

### Floating point

Checked the release object code with dumpbin for every float function. /fp:fast regrouped
DepthLess's `a + b + c + offset` into `(a + b) + (c + offset)`, so that function is compiled under
`#pragma float_control(precise, on)`; the others keep the original's association (only operand
order inside single additions differs, which is exact). Values the original stores to float
temporaries go through StoreFloat; everything else stays on the x87 stack. Every comparison is
written so NaN takes the same branch as the original's FCOM + TEST AH,1/0x41 (`!(a >= b)` forms);
dumpbin confirmed the generated tests.

## Forwarders added (standalone build must replace)

- 00404920 / 00404960 PlayerIterator::First / Next (core, player list iteration).
- 0055FB00 CWorld::FindUnit, 0055FC10 CWorld::IsNight (world base class).
- 0041A2E0 game server FindPlayer.
- 00675CA0 new-object mark (flags |= 2, stamp from 007AFFC8).
- 004E59F0 ZoneExit::Contains.
- 00460AF0 named float table Get.
- 006E848B _mbscmp (local GameMbscmp, as in the script files).
- Calls into existing rewrites directly: Terrain::HeightAt, CBaseManager::FindObject,
  ScriptGroup::Insert / SetSize (forwarder 004C2050), mfc::CString.

## Original bugs and oddities kept

- CountLivingMercs returns living - 1 (the leader is not counted) and 0 when the player is missing,
  so a player with no living units gives -1.
- RemoveTimedObject looks the id up a second time after Release (the release may have changed the
  map) and removes whatever the second lookup finds.
- NearestVisibleUnit, LocalPlayerData, LocalPlayerName and LocalPlayerVariable do not check the
  session/view pointers for null (the original crashes there too).
- DepthLess asks each object's IsDead up to three times; the calls are repeated in the same order.
- The setters at 0058CC70 / 005942C0 leave their argument in EAX; no caller reads it, so they are
  void here.

## Verify wrappers

- Pure functions: original through the trampoline and ours with the same arguments, bitwise
  comparison of the result (AL only for the predicates, whose other EAX bytes are left over).
- CollectSharedObjects: the original fills a clone of the group (own references), ours the real one.
- Server setters: `CompareSetter` runs ours, snapshots +0x6C/+0xDC..+0xE8 and every player's dirty
  flags, restores the snapshot taken before, runs the original, compares.
- Map inserts/removals run ours only (they allocate nodes and change reference counts) and check the
  map through `stlp::hash_map::Find`/`Size` plus the object fields they set.
- GetZoneExit / LocalPlayerName: the original writes a scratch CString; CString copies share the
  buffer, so the data pointers must be equal.

## Skipped

- Functions containing Rebirth's patches (00567AB0, 0057EB10, 0057F560, 005800C0, 005809D0,
  005812F0, 005813A0, 00595480, 00595BA0) and the big loaders/updaters (005927C0 object creation,
  0058B750, 0058CCD0, 0058EF20, 00596E70, 00597BD0, 005942D0...).
- 0058D280 (writes every player's "gtime" variable; needs the variable table's Set rewritten or a
  snapshot of the table) and 0058EEA0/0058EEE0/0058EE20 (time and weather setters calling it).
- STLport template instantiations: hash_map find/erase/resize/iterators (0059B6xx-0059E7xx,
  00572xxx, 00598E40...), vector/list helpers, node deallocation stubs (00560180-style), the sort
  algorithm instantiations around the predicates (005776D0, 005778A0, 00577E90, 005782F0,
  00589160-00589AD0) and MFC CList/CArray destructors and constructors (0056Fxxx-00571xxx).
- One-instruction global getters/setters in the glue layer (00578D00, 00578D50, 00578E20, ...)
  and plain forwarding wrappers (005786A0, 00578980, 00578A40, ...): nothing to rewrite.
- Platform/sky loading (00562F70), weather and camera glue (00578820, 00578760: calls CSectorMap::Move).

# World tick, unit waking and alarms (second round)

Code: `src/game/world_tick.cpp`; verify wrappers: `tests/verify/verify_world_tick.cpp`; scenario:
`tests/scenarios/world.txt`. Shared files changed: `src/lib/stlp.h` gained two inline members of
`hash_map` (`First`, `After`: STLport's begin() and iterator ++). `world.h` was extended (fields
split out of unknown arrays, no offsets moved).

## Map of the update path

CWorldServer slot 6 (0058DFF0, the world update, once per world tick) does, in order: drop
disconnected players; for every party unit of every player `WakeUnitsAround(unit + 0x1C,
max(perception * factor, minimum) * 2 + 20)`; accept new players (0058DAF0); unless +0x6C is set,
each player's 00665DE0 and then `Tick(0)`; each player's 0066E130; rebuilds the "Heroes" script
group; each player's 0058B750 (player state; returns 0 to disconnect). 0057A060 also calls
WakeUnitsAround and Tick.

`Tick` (0058DCD0):

1. `++tick_` (+0x64, the game clock at 007AFFC4); every 64 ticks (tick & 63 == 3) 0058CCD0
   (weather: rain, lightning strikes, the day-time value at +0x84).
2. Timed objects (+0x12DE8): walks the map in STLport iterator order calling each object's slot 6;
   an object returning 0 has its id pushed on a local `list<unsigned>` and is released at once.
   After the walk the ids are looked up again and erased.
3. 005C9530 time wheel (+0x2838, 1024 x 16 slots) advanced to the tick.
4. 006D44E0 countdown array (+0x70); when any countdown was running every player gets dirty bit
   0x20.
5. Missiles (+0x12E28, an MFC CList of counted pointers to 0xA8-byte objects created by
   0058E670): 0067E6D0 moves one; on 0 the node is unlinked and freed (0059C810).
6. Script command queues: +0x12874 only while +0x160 is set or `force`; +0x12A6C always (004BF000).
7. 0058D670 runs the units in the update map (Rebirth patches it).

Other structures recovered:

- **Alarms** (+0x8C, five 16-byte `WorldAlarm`s: active byte, tick, x, y): script InvokeAlarm
  (always with force), AIBrain::Think and the script's IsAlarm/AlarmTime/AlarmPosX/AlarmPosY
  (script_eval.cpp reads them at 007AFFEC).
- **+0x294 is a CSectorMap** (sector_map.h): 005C0930 returns sector (x, y)'s unit list
  (Sector + 0x20). WakeUnitsAround uses its own static SpiralCache at 007AFF08.
- +0x138 game result (0058DC90 sets it once), +0x160 enables the first command queue, +0x60 an
  object replaced by 0055F410 at the end of the game.

## Rewritten

| Address | Name | Verification |
|---|---|---|
| 0058DCD0 | CWorldServer::Tick (RET 4, char force) | stateful, ours only: tick + 1, missile list links/tail/count consistent, no null timed object |
| 0058D3D0 | CWorldServer::WakeUnitsAround (RET 8) | original first; its update-map entries recorded; map put back (new keys erased, old values and max id restored); ours; entries and max id compared |
| 00593FC0 | CWorldServer::InvokeAlarm (RET 0xC, char force) | stateful (AI brains), ours only: all five alarm slots equal the expected bytes; when raised, every player-less unit whose brain byte +0x37 is set is in the update map |
| 0058DC90 | CWorldServer::SetGameResult (RET 4) | ours only: +0x138 set only when it was 0 |

Names are provisional and set in Ghidra, together with the callees below.

The inline map insertions of WakeUnitsAround and InvokeAlarm (+0x174 slot, highest id, resize,
find-or-insert at the bucket head, value) are exactly AddToUpdateMap (0058D5D0), which they call.
Tick's local `list<unsigned>` reproduces the original's node pool traffic: head allocated before
the walk, one 12-byte node per finished object, the hash nodes freed during the erase pass, the
list nodes freed in order, the head at the very end (after 0058D670).

Floating point: only WakeUnitsAround (radius * 1/32 rounded to float for SpiralCache::Get; sector
= fistp(2x - 0.5 through a float) / 32, as GridHeight). Checked with dumpbin.

## Forwarders added (standalone build must replace)

- 0058CCD0 CWorldServer::UpdateWeather, 0058D670 CWorldServer::UpdateUnits (Rebirth-patched: must
  stay a call to the original address), 0055F410 (replaces +0x60).
- 005C9530 TimeWheel::Advance, 006D44E0 Countdowns::Tick (AL), 004BF000 CommandQueue::Run.
- 0067E6D0 Projectile::Update, 0059C810 ProjectileList::FreeNode.
- 005D5940 AIBrain::HearAlarm (brain = unit controller + 0x980).
- 006E727B atexit (another local copy; ai_map.cpp and effects_weather.cpp have one each).
- 005C0930 (sector unit list) is inlined (bounds check and address only).
- Calls rewritten code directly: SpiralCache::Get, AddToUpdateMap, MarkPlayersDirty.

## Original bugs and oddities kept

- Tick reads the timed-object node's `next` after the object's slot 6 and Release ran; an object
  that erases its own map entry while updating makes the original read a freed node (same here).
- The missile walk saves `next` before Update; Update removing the next missile breaks the walk in
  both.
- InvokeAlarm stops walking the objects at the first map entry with a null object, and does not
  check the unit's controller for null.
- InvokeAlarm(0, ..., force) clears the five alarms, including the padding bytes, before raising
  alarm 0.

## Skipped

- 0058DFF0 (slot 6 itself): SEH frames, script-group temporaries ("Heroes") and network player
  handling; next candidate once ScriptGroup copies exist.
- 0058CCD0 (weather, random-driven, float heavy), 0058B750 (player state, 3.5 KB), 0058DAF0
  (accepting network players), 0058E670 (missile creation; reads its caller's frame).
- 0058D340 / 00593AF0 (send a unit's sound / removal to every player whose selection holds the
  unit): only network writes, nothing a verify could check.
- 00594F30, 00595020, 005947E0 (shared/charged object lists): CList/CArray node code with
  counted pointers.
- 005939E0, 00595A70, 00595A90: no callers found. Save (0057ABA0), zone loading (0057C460,
  0057BD10) and the state machine (0057C700) are on the Rebirth list; load (0057B3B0) and the zone
  loader 0057BDF0 are large one-off functions, left for later.

# World client: per-frame view code (third round)

Code: `src/game/world_client_view.cpp`; verify wrappers: `tests/verify/verify_world_client.cpp`;
scenario: `tests/scenarios/world_client.txt`. Shared files changed: `world.h` only (three method
declarations in CWorldClient, and the sort entry points and ModelUseLess appended at the end).

## Map of the client frame

The draw path 00569E40 -> 0056AFE0 (patched by Rebirth's LandscapeDrawRadius / ObjectsDrawRadius)
is rewritten as `CWorldClient::Render` / `CWorldClient::DrawPass` in world_client_draw.cpp, with
those patches behind their flags (CWorldClient slot 6, 005641F0, is `CWorldClient::Segment` in
world_client.cpp, with Rebirth's DisableExitOnCrashReport behind its flag). Render: the view
matrix (004F0EF0 of the camera 0079B2A8) replaces the view stack's top and is pushed for the
scene, the sky (flag 0x1000, +0x1AC), the terrain's first pass (0x100: 006CA260 with the camera
position and the draw distance 100.0, 006C6230), DrawPass, the terrain's second pass (006C6330),
the effects, particles, lightning and ribbons (0x400), 00569D60, the pops, the flying hit points
(0x2000). 0056AFE0 (the draw pass) does, per frame:

1. Clears eleven global `vector<CEngineObject*>` (0056E870 = erase(begin, end)): 007AF508 objects
   whose model should stay loaded; visible lists 007AF568 (class 0x52), 578 (units, 0x50),
   588 (0x5D), 598 (every other class), 5A8 (0x54); shadow lists 007AF518 (0x52), 528 (units),
   538 (0x5D), 548 (other), 558 (0x54).
2. Clamps the camera footprint to the terrain and walks the sector grid (+0x124 rows, +0x138..+0x14C
   bounds and cursor) calling **CollectVisible** (0056A990) for each sector's object list with
   `animateUnits` 0: units are only queued on the deferred list 007AF5B4 (an STLport list of counted
   pointers). Then once for 007AF5B4 itself (units animated through slot 25 and filed), and once for
   +0xB4 (with `withScripted`). The deferred list is then emptied (00575510 releases, 0056F700 frees).
3. Sorts 007AF508 by model use (00573D50 + 00573F10, most recent first) and clears flag 0x400 on
   the first max(10, n/8) of them; sorts the visible and shadow lists with UnitOrderLess
   (005740D0 + 00574390; nine call pairs) and draws them through the 005726D0 / 005726F0 functors.

The view matrix CollectVisible gets is the top of the render device's view stack (device global
007AF068: +0x5C matrices, +0x60 depth); 0056AFE0 does not check the depth (see the crash below).

CollectVisible, for each object with a model (+0x94): units of type 0x32 (heroes) with a text-key
id are skipped unless `withScripted`; on the first pass units go to the deferred list; on the
others they get slot 25 (game time) and, in a multiplayer spectator view (0079BE6C set and session
+0x100 == 1), only units with flag 0x400000 go on. Then: the object-to-world matrix at +0x54 from
the orientation quaternion (+0x34, 004D9AA0) with the position as last column, pushed on the
device's object stack (+0x24/+0x28, multiplied by 004D9880); the bounding volume (+0x124) tested
with 004F16A0 (2 = outside); the stack popped. In view: flag 0x20, the view-space position at +0x28
(three rows of the view matrix, no translation, each `(a + b) + c` rounded to float), the visible
and shadow list of its class, and the touched-model list when flag 0x200 is set and `now` minus
the model's last use (+0xEC) exceeds 0079BD24 (the return value counts those). Out of view: flag
0x20 cleared; when the model's shadow volume is in view (0051E300) the figure state at +0x98
applies a pending change (0050D450) and the object goes on its class's shadow list.

Cursor picking: **PickGround** (0056BF30) and **PickSurface** (0056C350) invert the current view
(the view stack's top pushed on the device's picking stack +0x14/+0x18) for screen pixel (x, y),
giving the ray x = a z + b, y = c z + d, then iterate z = height(x, y) from half the terrain's
height range (+0x114) up to 100 times until |dz| <= 0.01, then fall back to up to 100 bisection
steps; x and y are clamped at 0. With an empty view stack they return the vector at 007AFDB0.
PickSurface uses the client's slot 29 (platforms included), PickGround Terrain::HeightAt. Callers:
the interface (00638EF0 / 00639EF0, eight calls) and 005EB940.

## Rewritten

| Address | Name | Verification |
|---|---|---|
| 0056A990 | CWorldClient::CollectVisible (RET 0x18) | ours only (it animates units, takes references and applies one-shot figure changes): the return value equals the touched-model entries appended; every visible entry has flag 0x20, belongs to its class's lists and is appended to that class's shadow list in the same order; deferred entries are units and appear only on the first pass. Also logs draws with an empty view stack |
| 0056BF30 | CWorldClient::PickGround (RET 0xC, returns out) | output: original into a scratch vector, ours into the caller's; 12 bytes compared |
| 0056C350 | CWorldClient::PickSurface (RET 0xC, returns out) | same |
| 005740D0 | IntroSortByModel (STLport __introsort_loop, RET 0xC) | original on a copy of the range, ours on the range; every element compared. Ranges with a null element, no model or a NaN depth are logged and left to the original alone |
| 00574390 | FinalSortByModel (__final_insertion_sort, RET 4) | same |
| 00573D50 | IntroSortByModelUse (RET 0xC) | copy and range, compared |
| 00573F10 | FinalSortByModelUse (RET 4) | same |
| 00573EE0 | ModelUseLess::Less (0/1 in EAX) | pure |

The four sort entry points share one template (`PtrSort`) that reproduces STLport's algorithms:
median of three, the unguarded Hoare partition, recursion on the right part, heap sort at the depth
limit (make_heap 00577FC0 / pop_heap 00577730, and 00577E90 / 005782F0 for the other ordering),
insertion sort of the first 16 and unguarded insertion of the rest. The original inlines the
comparator in places and calls it in others; all copies have the same semantics (UnitOrderLess
treats a null rank pointer as rank 0). The original's recursive call lands on our verify wrapper,
so in the verify build every recursion level is checked on its own.

## Floating point

Compiled under `#pragma float_control(precise, on)` and read back with dumpbin: no reassociation;
values the original stores to float go through StoreFloat. CollectVisible's view-space rows keep
the original's operation order. In the picking, the values the original keeps on the FPU stack
(the screen coordinates and the determinant) are doubles; MSVC spills the determinant and some
matrix entries to 64-bit stack slots. That is exact while the FPU runs at 24 or 53 bits; at 64-bit
precision the determinant would be rounded to 53 bits and a, b, c, d could differ in the last bit
(the verify compares bitwise, so it would show as PickGround / PickSurface mismatches). The first
height result is used unrounded, as the original adds ST0 directly (forwarder to 006C8680 declared
double, VCall<double>); later ones are rounded to float where the original stores them.

## The rare Rebirth crash in the unit sort (docs/open-questions.md)

UnitOrderLess's last test is `FLD a.depth; FCOMP b.depth; TEST AH,1` ("less or unordered"), so
when an object's view depth (+0x30) is NaN, Less(x, x) is true, and Less(pivot, y) is true for
every y of the same rank and +0xF0. STLport's partition is unguarded: `while (Less(pivot, *--last))`
then walks below the start of the vector and hands the comparator whatever lies before it as its
second argument. That matches the crash: 00574318 is `MOV ECX,[ESI+0x94]` on the second argument.
The final unguarded insertion sort fails the same way. Null elements or elements without a model
are not dangerous by themselves (they compare false both ways, which keeps the partition's
sentinels), but they break transitivity and can also make the final insertion run off the front.

NaN depths come from a NaN view matrix or object position. One way to get them during a new-game
load: 0056AFE0 takes the view matrix as `viewStack + (depth - 1) * 64` without checking the depth,
so a frame drawn before the camera pushes its matrix reads the 64 bytes before the stack buffer.
This is not confirmed by a run. The verify build now logs both cases to ei_hook.log ("not
orderable" from the sorts, with the element's depth and position bits; "view stack depth" from
CollectVisible). The rewritten sort behaves exactly like the original here, crash included.

The full run confirmed the overrun: the verify logged a range with first > last ("element 41 of
-11"), which only the original's own recursive call can produce (0056AFE0 passes vector bounds).
With a NaN pivot depth the left scan `while (Less(*first, pivot))` runs past `last`, the returned
cut lies beyond it, the recursive call on (cut, last) returns at once (signed size test), and the
loop goes on with `last = cut`, partitioning memory past the vector (garbage pointers can be swapped
into it). The first verify missed this: its NaN test `x != x` was folded to false by /fp:fast, so
the range was sorted on a scratch copy whose overrun read other memory, giving the one
"MISMATCH IntroSortByModel: n 49" (in 937,548 calls). Rechecked the template step by step against
005740D0, 00574390, 00577FC0 and 00577730 without finding a difference. The verify now tests NaN
on the bits, sends reversed and NaN ranges to the original alone (logged), and on a mismatch logs
the first difference with its keys (rank, +0xF0, depth bits) and, for the first three, the whole
range (input, original's order, ours).

## Forwarders added (standalone build must replace)

- 004F16A0 RenderDevice::Classify (frustum test), 004D9880 Matrix4::Multiply, 004D9AA0 quaternion
  to matrix, 0051E300 ObjectModel::ShadowInView, 0050D450 FigureState::ApplyPending.
- 00571B00 vector<CEngineObject*>::_M_insert_overflow, 0056E8A0 list insert (node from 0056F680,
  which takes a reference). push_back (0056E840) is written out, as the original mostly inlines it.
- 006C8680 Terrain::HeightAt through a double-returning forwarder (not patched by Rebirth; it
  lands on the rewrite in terrain.cpp).
- Called directly: IsTextKey (object_tables), UnitOrderLess::Less, StoreFloat. Virtual calls:
  GetClassId, Release, unit slot 25, client slot 29.

## Original bugs and oddities kept

- The sort crash above (comparator not a strict weak ordering for NaN depths; unchecked view stack
  depth in 0056AFE0, which stays original).
- The view-space position uses only the 3x3 part of the view matrix (no translation).
- CollectVisible's first pass takes the deferred list's reference through a temporary (+1, insert
  +1, release -1); the SEH frame protecting the temporary is not reproduced.
- When |dz| never drops to 0.01 the picking runs 100 fixed-point steps and then 100 bisection steps
  (200 height queries for one cursor position).

## Skipped

- 00567AB0 (the client world load): not rewritten. (0056AFE0, 00569E40 and 005641F0 were rewritten later,
  with the Rebirth patches behind flags.)
- The sorts' heap helpers (00577FC0, 00577730, 00577E90, 005782F0) and the copy_backward /
  unguarded_linear_insert instantiations (005776A0, 005776D0, 005778A0, 00577F90): their logic is
  in the template, but they are not hooked separately.
- 005677F0 (whether a group's units all stand in the same zone exit; glue 00578A20), 00566F70
  (hero portraits "hr%2d"), 00568800 / 005683D0 (skins "%sskin%2d.mmp"), 00566360 / 00567500 /
  00567680 (per-frame manager updates, large), 00565090 / 00564E70 (clearing the sector grids),
  005647E0 / 00564BE0 (adding and removing objects in the client's maps): next candidates.
  (Later: 00564E70, 005647E0, 00564BE0 and 00566F70 are rewritten, see "Client maps" below.)

# Zone loading, map objects, AddUnitToServer, saving the map objects (fifth round)

Code: `src/game/world_zone.cpp`; verify wrappers: `tests/verify/verify_world_zone.cpp`; scenario:
`tests/scenarios/world_load.txt`. Shared files changed: `world.h` (fields split out of unknown
arrays, no offsets moved: +0xEC exit CArray header, +0x124 `mobFiles_`, +0x15C, +0x12DA0
`mapObjects_`, +0x12E44 `counted_`; `ZoneExit::DeletingDestructor`; new `ZoneExitArray` and
`StringArray`; six CWorldServer method declarations), `docs/standalone-cleanup.md` (one row).

## Map of zone loading, saving and loading

- **State machine** 0057C700 (Rebirth-patched; the request at 007AFE20): every request first runs
  005957E0 (zone teardown: each player's view reset, the +0x12E64 list, `mobFiles_` emptied, script
  state, **ClearMapObjects**, the missiles, the +0x12DE8 timed objects, 0055F890). Kind 3 (new
  game) empties saves\current (0057AAB0) and starts zone1 through 0057C460 (Rebirth-patched);
  kind 2 (load) calls 0057ABA0 (Rebirth-patched).
- **NewZone** 00595480 (Rebirth-patched): checks the map / .mob paths, reads the AI graph
  (00596140, "CWorldServer::LoadMap. Can't load AIGraph"), clears the alarms, then **LoadMap**
  00595C70: loads the terrain when the map changed (005B43C0, Rebirth-patched); when the .mob file
  is the current one it tears the zone down (ClearMapObjects, the timed objects, the missiles),
  otherwise it empties the world of the zone (ResetZone 0058B250; called a state save here
  before the seventh round); marks every player dirty; then **LoadMapObjects** 00596030 for the
  .mob file and 005951A0 for a new zone. 00595B50 (glue) reloads only when the map or the .mob
  name (+0x12D6C / +0x12D68) changed.
- **LoadMapObjects** 00596030: a root chunk (vtable 0073FAD4, the 0073BE9C layout: Chunk + MFC
  CFile at +0x18) opened on the full path (00595BA0: the name as is with a drive letter, else under
  the game folder) with flags 0x8020; 00481660 reads the objects (chunk_stream.cpp handles the
  chunks); the file is closed through CFile slot 19. **LoadMobFile** 00595AB0 (script AddMob, and
  the multiplayer zone loader 0057BDF0) loads extra .mob files once per zone; the byte 007C554C is
  set while it runs.
- **SetZoneExits** 005915E0: the zone record's exits (map database entry +0x48, a CArray of 0x3C
  records) are copied to the server's CArray at +0xEC, skipping exits with an empty target; an exit
  with a negative entrance must lead to "none" (FatalError otherwise). Called by the zone loaders
  0057A060 / 0057BDF0 and by the load 0057ABA0.
- **Saving** (0057B3B0; autosave into "savea" when a zone is entered and the option at 0079BE7C is
  on, 006360F0 -> 006570E0 -> 00581610): the files of saves\current are copied into saves\<slot>\,
  scenario.sav gets the global state and mission.sav the world (00597BD0: zone names, settings,
  alarms, players, script state, the id maps, **SaveMapObjects** 00596730, the missiles, the time
  wheel, diplomacy...). **Loading** (0057ABA0): scenario.sav, the files copied back into
  saves\current, then the zone (0057C460) or mission.sav (00579E60 -> 00596E70 LoadSave, which
  reads the map objects back with 00596900) and SetZoneExits. Every path is the game folder plus
  "saves\", so the harness redirection to `harness_saves` covers all of it; `load savea` after
  `newgame` loads the autosave the new game just wrote (0057ABA0 only refuses in the multiplayer
  modes 2 and 3 of 007B0098; a missing scenario.sav sends the game to the main menu).
- **Map objects** (+0x12DA0, an ObjectIdMap): the objects that belong to the zone and are saved
  with it. RemoveObjectFromServer erases an object from +0x12D70, +0x12DD0, +0x12D88, +0x12DA0 and
  +0x12DB8. +0x12E44 is an MFC list of counted references (released through slot 4).
- **AddUnitToServer** 005910A0 (script command 0x88): the script passes the name's text key
  (00461360) as the id. A 0x70-byte unit record builder (00661050 / 006628D0 / 006611D0) is filled
  from the prototype; its 0x784-byte unit record (+0x30) gets the id at +0x1C and goes, alone in a
  CArray of pointers (vtable 0073FAA4), to 0058EF20, which removes an existing object with that id,
  creates the unit (005927C0, Rebirth-patched) near the position with the rotation at 007C2DE0, and
  sets it up (items, spells, AI).

## Rewritten

| Address | Name | Verification |
|---|---|---|
| 005915E0 | CWorldServer::SetZoneExits (RET 4) | ours only: the exits equal the source records with a target, in order (bytes, target text) |
| 005910A0 | CWorldServer::AddUnitToServer (RET 0x10) | ours only: afterwards FindObject(id) is a unit |
| 00596030 | CWorldServer::LoadMapObjects (RET 8) | ours only (reads a file, creates objects): returns 1 |
| 00595AB0 | CWorldServer::LoadMobFile (RET 4) | ours only: returns 1 and the name is on `mobFiles_` |
| 00596C60 | CWorldServer::ClearMapObjects | ours only: no map object left (or the walk stopped at an empty entry), missile and counted lists empty, countdowns cleared |
| 00596730 | CWorldServer::SaveMapObjects (RET 4) | ours only (calls every object's Save): on a memory stream the position moved by at least 4 and the last word written is -1 |

The local list of ClearMapObjects (vtable 0073F530, block size 10) is `mfc::CTypedPtrList`, whose
inline NewNode / AddTail / RemoveAll do what 00573290 and 00570470 do. The map walks use
`stlp::hash_map::First` / `After`, which are 0059C020 / 0059DB90 (00598E40 only builds the end
iterator {0, map}). No floating point.

## Forwarders added (standalone build must replace)

- 0058EF20 CreateUnits (RET 0x20; the rotation is 16 bytes by value), 005935C0
  RemoveObjectFromServer and 00595BA0 the map path (both Rebirth-patched: original address).
- 0042D160 root chunk Open (the path CString by value: its data pointer is passed and the callee
  destroys it), 0042D270 root chunk destructor, 00481660 .mob object reader (AL).
- 00661050 / 006628D0 / 006611D0 unit record builder; 005719B0 CArray<void*>::SetSize; 00573080
  CArray<ZoneExit>::SetSize; 004608B0 ZoneExit deleting destructor; 006FEBCE
  CStringArray::SetAtGrow.
- 0040A3A0 CRefObject::Release (missiles), 0059DDB0 counted pointer destructor, 006D43E0 countdown
  array clear, 00530930 CUnit::GetPrototype (they land on rewrites or plain originals).
- Virtual calls: CFile slot 19 (Close), stream slot 2 (Write), object slots 10 (Save) / 12.

## Original bugs and oddities kept

- ClearMapObjects and SaveMapObjects stop at the first map entry with a null object.
- ClearMapObjects reads each object's id when it removes it; an earlier removal that freed a later
  object would make both read freed memory.
- LoadMapObjects / LoadMobFile return what FatalError returns on failure (LoadMobFile treats a
  non-zero FatalError result as success and lists the file).
- SetZoneExits re-reads the source's size every iteration.
- The SEH frames of AddUnitToServer, LoadMapObjects and ClearMapObjects (destroying the temporaries
  on an exception) are not reproduced.

## Skipped

- Rebirth-patched: 00595480 NewZone, 00595080, 005927C0 (object creation), 005935C0 (removal),
  0057ABA0 (load), 0057C460, 0057C700, 00595BA0.
- 00595C70 LoadMap, 005957E0 zone teardown, 00596E70 LoadSave (3.4 KB), 00596900 map objects
  load, 00597BD0 world save (2 KB), 0057B3B0 save game, 0058EF20 unit creation (3 KB, float-heavy
  placement), 0058B250 (ResetZone), 00596140 AI graph load, 005951A0: large and full of
  network / script state. Next candidates: 00596900 (the counterpart of SaveMapObjects) and
  005957E0.
- 00595B50 (glue: reload when the map or .mob name changed): two compares around patched calls.
- The scenario does not reach LoadMobFile: zone1's scripts use no AddMob, and loading another
  zone's .mob into zone1 is not safe to try blind.

## Scenario

`world_load.txt` (~40 s): new game (zone load, autosave), eight AddUnitToServer wolves, one name
added twice (same id), three RemoveObjectFromServer, `load savea` (waits until a spawned wolf is
gone and the hero is back), one more AddUnitToServer in the loaded zone.

# Figure meshes (0x0059F000-0x005AC200, sixth round)

Code: `src/game/figure_mesh.h/.cpp`; verify wrappers: `tests/verify/verify_figure_mesh.cpp`. No
shared files changed. The 0x005A0000-0x005B0000 range is not AI code: it is the game-side figure
part mesh (classes 0x41-0x44 of the factory 005AA8E0) and the figure loader (005AC010).

## Structures recovered

- **CFigureMesh** (class 0x42, 0x220 bytes, vtable 0073C1C4; 0x43 (0073FC48) and 0x44 derive and
  share the loaders; class 0x41 (0x1E0 bytes, vtable 0073FBF0) has the same layout up to +0x1E0
  and its own loaders). A CTransformObject (+0x1C position, +0x34 quaternion), +0x08 flags (0x20
  loaded), +0x9C MeshBuffers (pointer/count pairs the renderer draws from), +0x128 vertex offsets,
  +0x138/+0x148 colour arrays (one per normal), +0x158/+0x15C header words, +0x160 centre,
  +0x16C/+0x178 box, +0x184 radius, +0x188 ClipBounds, +0x1C0 vertex packets, +0x1D0 normal
  packets, +0x1E0 corners (position / ? / texcoord indices), +0x1F0 unknown dwords, +0x200 corner
  indices (3 per triangle), +0x210 texture coordinates.
- **AlignedArray<T>** {data, size, capacity, growBy}: 32-byte aligned storage (operator_new block
  stored at data[-1]); every loader inlines its SetSize (005A6E70, 0044FA90, 004F57B0 out of line).
- **VertexPacket** (0x40): 4 vertices in SoA form x[4], y[4], z[4], w[4]=1.0. Vertex i is
  component i & 3 of packet i >> 2.
- Vtable slot 19 (005A2490 / 005A4CF0) picks slot 20 (FIG data, "FIG" + version byte) or slot 21
  (older format) by the first dword of the data.
- FIG data: 8-dword header (magic, counts), two words, the bounds (FIG8: centre, box min, box max
  as 8 Vector3 versions each, then 8 radii; others: one of each), then 3 blocks of 8 float4
  versions per vertex packet (FIG8) or one, then the other arrays verbatim. FIG8 morphs trilinearly
  by the figure's three coefficients (s, t, u): lerp(lerp(lerp(v0,v1,s), lerp(v2,v3,s), t),
  lerp(lerp(v4,v5,s), lerp(v6,v7,s), t), u). The old format holds 6 corners (as the bones,
  figure.h) and every array is preceded by its count; a block of 0x34-byte records is skipped.

## Rewritten

| Address | Name | Verification |
|---|---|---|
| 005A18E0 | CFigureMesh::LoadBounds (RET 0x14) | original on a copy of the object bytes and the cursor; +0x160..+0x1B4 and cursor compared |
| 005A2500 | CFigureMesh::LoadFig (slot 20, RET 0x10) | original on a copy whose arrays start empty, ours on the object; every array (size, step, contents, capacity when ours started empty; the colour arrays by shape only, except colorsB_ of the old format, which is filled with 0xFF000000: the loaders size them without writing, so their contents are leftover heap bytes that differed in game), bounds, header words, flags, cursor, buffer descriptor compared; the copy's arrays freed |
| 005A5C70 | CFigureMesh::LoadFigShared (class 0x41) | same |
| 005A3800 | CFigureMesh::LoadOld (slot 21) | same |
| 005A4D60 | CFigureMesh::LoadOldShared (class 0x41) | same |
| 005AAE60 | MorphFigureBounds (__stdcall, RET 0x18; ECX unused) | output: original into a copy of the ClipBounds |
| 005AB9A0 | MorphFigureBounds6 (__stdcall, RET 0x14) | same |
| 0059FDF0 | CFigureMesh::ExtendWorldBox (RET 0x18) | output: original grows copies of the six values |
| 005A4B30 / 005A4BF0 | CFigureMesh::MirrorX / MirrorY (slots 22 / 23) | ours, then the original must undo it exactly (snapshot), then ours again |
| 005A6E70 | AlignedArray<uint32>::Resize (RET 8) | original on a copy with its own buffer |
| 005A6D40 | CFigureMesh::RefreshBuffers | ours only: descriptor equals the arrays, colour arrays 4 per normal |
| 005A0B10 | CFigureMesh::SetOffsets (RET 4) | ours only: offsets equal the source |
| 005A0CD0 | CFigureMesh::DrawOffset (RET 0x10) | ours only: result, shared buffer size and first moved coordinate |

## Floating point (how it was checked)

Every loader rounds differently per component because of VC6's register allocation (the x
component of a FIG8 vertex block stays on the FPU, y/z/w are stored after each blend; in the old
format each bounds component has its own pattern; FST-then-FSUB keeps some sums unrounded). The
patterns were read off with a symbolic x87 trace of the original instructions, and every rewrite
was then run against the original in an x86/x87 emulator (Python, scratch only) on random inputs at
24-bit and 53-bit precision, comparing every output bit: the whole loaders (allocation, growth from
existing arrays, cursor, descriptor), the bounds functions, ExtendWorldBox (including NaNs), the
mirrors, Resize/RefreshBuffers/SetOffsets/DrawOffset. All matched on the release and verify
objects. 64-bit precision is not covered (doubles spilled by MSVC would round).

## Forwarders added (standalone build must replace)

- 00500960 display object (pipeline at +8 -> +0xC, slot 7 fits bounds to the vertices), 00401570
  object matrix (CTransformObject), the pipeline's slot 29 (draw) via ei::VCall.
- 005A18E0 and 005A6E70 are called through their own addresses from our loaders so their verify
  wrappers see the calls. ClipBounds::SetBox (model.cpp) is called directly.
- Globals: 0073FAFC (1e8, far limit), 007C2E18 (DrawOffset's shared vertex buffer).

## Original bugs and oddities kept

- 005A4D60 does not test the loaded flag (the other three loaders do).
- DrawOffset's shared buffer grows without copying the old contents and allocates an empty block
  for 0 vertices instead of freeing.
- The old-format loader with class 0x42 and 0x41 round the box minimum differently (compiled from
  the same source, different register allocation).
- Plain float copies through FLD/FSTP (radius into the ClipBounds, the ExtendWorldBox selects)
  quiet a signalling NaN in the original; ours may copy the bits (only differs for SNaN input).

## Skipped

- 005A7A70 (4.2 KB, the mesh draw: SEH, STLport strings, lights, 004E9010/004E9A50), 005A7640,
  005A7290, 005A0E80 (software corner/texcoord path), 0059F8F0 / 0059F710 (Load/Save to a CFile;
  no caller found), 0059F510 (SetTransform, quaternion maths), 0059F3F0, 005ACE80, 005AC010 (figure
  loader), the figure manager 005AA5F0.. and 005A8B00/005A8FB0/005A9310 (screen rectangles).
- World server: 0058B750, 0058EF20, 00596E70, 00597BD0, 0057B3B0, 00591800 (mercenary variables),
  005942D0, 0058E850: network / script / MFC list heavy; next candidates after the above.
- 005C2xxx-005C46xx local search and path smoothing (005C46C0 reads the precomputed transition
  tables at 007C3058; next candidate in the AI range).

## Scenario

None added: figure meshes are loaded whenever a unit or object is created on the client, so
`world`, `world_load` and `sandbox_*` already reach the FIG loaders, LoadBounds and the figure
bounds; ExtendWorldBox runs when an object's box is recomputed (00518100). The old-format loaders
and MorphFigureBounds6 run only for figures without a FIG header (possibly none in the data).

# Save and load, player sync, game flow, party, briefings (seventh round)

Code: `src/game/world_save.cpp` (mission.sav, the shared-object list, PrepareSave),
`world_sync.cpp` (SyncPlayer, SendZone), `world_units.cpp` (CreateUnits, UseQuickItem),
`world_game.cpp` (the game flow object 007AFE20: SaveGame, the zone starters, the multiplayer
start, the flow's requests and session glue), `world_quests.cpp` (merc briefings, briefing
rewards, experience, party records), `world_party.cpp` (Redeploy, the quest items carried between
zones, item copies), `world_briefing.cpp` (the briefing speaker and variables, quest texts),
`world_guard.cpp` (the player data guard), `world_weather.cpp` (+ ShowBolt), `world_loot.cpp`,
`world_update.cpp` (slot 6), `world_spells.cpp`, `world_map.cpp` (LoadMap, ResetZone,
TeardownZone, SendFreshObjects, ResetState). Verify wrappers:
`tests/verify/verify_world_{save,sync,units,game,quests,party,briefing,guard,weather,loot,update,spells,map}.cpp`.

Shared files changed (additive): `world.h` only. Fields split out of unknown arrays (no offsets
moved): CWorld +0x24..+0x34, the managers +0x3C..+0x58 (`manager40_` map objects, `manager48_`
timed objects, `manager4C_` = slot 22, `itemManager_` +0x54 = slot 18); CWorldServer +0x68
`updateCount_`, `countdowns_` now 0x14 bytes with +0x84 / +0x88 after it, +0x100 `loaded_`,
+0x120 `zoneName_`, +0x13C, +0x164 `startPosition_` (three floats), +0x170 `aiMap_` (CAIMap,
extent unknown), `commandsB_` ends at +0x12C64 followed by `diplomacy_`, +0x12D64
`unknown12D64_` (game kind: 1 single player, 3 no range test, 4 network), +0x12D68 `mobName_`,
+0x12D6C `mapName_`, +0x12DB8 `otherMapObjects_`, +0x12DD0 `namedObjects_`. New: `WorldStream`
(the save stream: slot 1 Read, 2 Write, +8 version), `GameFlow` (its methods below), `struct
Float4;` forward declaration, the method declarations below and the free functions
LoadSharedObject, ShowBolt, GuardPlayerData / GuardPlayers, BriefingSpeakerInto /
BriefingVariableInto, UnloadQuestTexts, NameInList, RequestZone. Also `config/library.csv` (rows
listed under Skipped) and the fifth round's text about 0058B250, which is ResetZone (it empties
the world of the zone), not a zone state save.

## Map

- **Save / load of a zone** (mission.sav, and saves\current\<zone>.sav for visited zones):
  SaveWorld 00597BD0 writes, in order: .mob, map and zone names; +0x24 (12 bytes), +0x30, +0x84,
  the alarms, the tick, +0xE4/+0xE8, the time and time speed (0055FBD0), +0x13C, the game kind;
  the AI-graph state as a diff against the zone's file (00596140 base, 005B1380 current,
  004A4DA0); the map objects' identities (id, type, +0x18; created again by manager40's slot 23
  on load); the managers' objects (slot 10) and SaveMapObjects; the managers' changed objects
  (00404860); the timed objects' ids; the missiles (byte 1 + 0067E960 each, byte 0); the time
  wheel, first command queue, diplomacy, +0xDC/+0xE0, countdowns; the counted items; every
  player (+0xFC, party records 00661C10, +0x100, its object list, +0x108, its id list); +0x6C;
  the item manager's object records; the .mob files; the shared objects. LoadSave 00596E70 reads
  it back with format versions (0x4C time speed as float, 0x4E countdowns, 0x55 players' id
  lists and records, 0x59 map object names, 0x60 .mob files, 0x6E shared objects, 0x74 +0x13C),
  then moves units that cannot leave their cell: text-key units (id > 1e8) to a free cell near
  them, others removed. LoadSavedMapObjects 00596900 is SaveMapObjects' counterpart (registers
  each object in +0x12D70, +0x12DA0, +0x12DB8 (non-units), +0x12DD0 (named), copies a unit's six
  model dwords at +0x4B0 from its prototype's model record, marks parts changed, wakes it).
  PrepareSave 005956E0 (before a save): every living unit refills its controller's slots.
- **The shared-object list** (+0x12E60, CList of 0x20-byte {id, player, value, CArray<CRefPtr>}):
  AddSharedObject 005947E0 puts a new head (from 0066F4C0, a multiplayer client's corpse loot:
  the player of the session record for the id, the gold, the objects); LoadSharedObject 00589CA0
  reads an entry (object ids resolved by the item manager 007AFFB4).
- **Game flow** (007AFE20; +0 state, +4 zone or save name, +8 a party, +0x78, +0x7C, +0x80):
  SaveGame 0057B3B0 (empties saves\<slot>, copies the visited zones' files kept for the save,
  writes scenario.sav (party, input, scripts, globals; header 0xDEAD) and, for zone kinds 0 and 2,
  mission.sav (SaveWorld; header 0x102), prints "Saved <slot>"); 00581610 is a thunk to it.
  EnterZone 0057A060 (__stdcall; from 0057C460): plays the pending dcity movie, then restores a
  visited zone (zs.<zone>) from saves\current and lets the world catch up (one Tick per elapsed
  gtime unit since zt.<zone>, at most 1000, waking units within 70 of the entrance), else loads
  it fresh (NewZone, gz6g frozen/thawed map, time 4); then the zone's scripts and exits.
  StartNetworkZone 0057BDF0 (__stdcall; state machine and 0057ED60) is its network counterpart;
  LeaveZone 0057A780 the way out. The requests the flow's update (005800C0, Rebirth) acts on:
  state 2 load (005816D0), 3 (005816C0), 4 go to a zone (00581700, also BeginMultiplayerGame),
  5 / 6 with a party copy (00581730 / 00581760). Multiplayer: BeginMultiplayerGame 0057F280
  (start zone bz1mpg..bz4mpg by the client's game type, 0057E6B0; the outside party gets unit id
  0x3B9AF5CE when alone and "z.MPGame1"), UpdateMultiplayerVariable 0057F190 (each network step:
  "z.MPGame1" set or removed by the sessions' peers), ReturnLocalParty 0057F0F0,
  UpdateAndAcknowledge 0057F150, RecreateClientSession 0057F390 and CloseSessions 00581660 (the
  sessions' refcount is the low word at +4), RequestZone 00581630.
- **Briefings** (client side): the speaker of a briefing is the second word of the first "#show"
  line of section "briefing <name>" in the briefing texts (007AF788) that is not "Hero"
  (0057FC90); its variable is "b.<speaker>.<name>" or "b.elder.<name>" (0057FDE0); MarkBriefing
  0057FF40 sets it (loading the zone's quests first for "<zone>_N" names, 005809D0 Rebirth),
  ForgetBriefing 00580040 removes it; OnVariableChanged 00580840 passes "q." variables to the
  quest log (007C7D50) and the client world; UnloadQuestTexts 00580F10. Server side:
  CompleteBriefing 00592090 -> ApplyMercBriefing 00591DE0 (b.mercN.n8 cleared, n10 / n2 the
  mercenary leaves (and n10 its unit is removed), n1 / n3 hired: amerc_N = 2 and added to the
  party), UpdateMercBriefings 00591800; RequestBriefing 0057FB60 on a client.
- **Party between zones**: CollectParty 00590A40 / StoreParty 0058FD90 on leaving, Redeploy
  00590700 on arrival: RestorePlayerItems 00590460 gives back fresh copies (CopyItem 00594CE0 =
  world slot 24) of the items the player brings (all on a client; in single player the quest
  items of this zone, TakeZoneQuestItems 005901E0: their description names the zone or an object
  asks for their id), then the units are created at the target exit's centre (+0xDC of the
  player picks the exit of zone +0xD8; a lost game goes back to this zone's exit 0) facing away
  from the map centre (FPATAN) unless the exit has a heading, the player state gets the start
  (0066ED30) and kind 3 updates the merc briefings; runScripts runs the first command queue 5x.
- **Per-player sync** (Update -> SyncPlayer 0058B750 for every player): zone (SendZone 0058C890),
  weather, time, wind, countdowns when dirty; then the map objects the player should see (camera
  range from 0073F91C, own units, named objects, the units its units can hear: max(perception *
  factor, minimum) * 0073F910 + 0073BF34 via the sector map) and the timed objects whose reach
  covers the camera, compared with what it was sent (0047B1F0) into the function statics
  007AFF18..007AFF48 (guards at 007AFF58, atexit destructors 0058C470..0058C4D0).
- **CreateUnits** 0058EF20: builds the 0x324-byte creation message (0046C010; destroyed inline:
  five CStringArrays, a CString, five 0x4C records, 0046AEB0), heroes get their figure parts
  ("hr" -> "hr.NN" hair style from the prototype record), placement from the placement array or
  a line along the rotation's heading; 005927C0 (Rebirth) creates the unit; then AI (player
  mode), free cell, controller, items, spells, the "WorldScript( AddObject( Heroes, ... ) )" line.
  UseQuickItem 0058ECE0 creates the "st.tr.130" effect (base message 0046A5C0, vtable 0073FA58)
  where a potion is drunk.
- **The player data guard** (005790E0, from WinMain on (de)activation): with `plain` 0 the
  players' gold (+0xC0), party records (0x704 bytes from +0x20 of each 0x784-byte record) and, on
  the server, their units' controller statistics (0x704 bytes from +0x1E0) are XORed with a new
  random key (007AFECC), with 1 XORed back; also the party outside a game (007C82B0); state at
  0079026C. 005791C0 does one player list.
- **Zone reset**: ResetZone 0058B250 empties the world of the zone; ResetState 0058B600 adds the
  map name, diplomacy, command queues, AI map and the game state fields; TeardownZone 005957E0;
  LoadMap 00595C70; SendFreshObjects 005951A0.
- **Update** (slot 6) 0058DFF0, **UpdateWeather** 0058CCD0 (Tick, every 64 ticks; lightning
  strikes through ShowBolt 00578E30: a script line on the queue 007C29CC, one time in four only a
  thunder sound), **CastSpell** 0058E850 / **CreateMissile** 0058E670, **Loot** 005942D0 and
  LootDeadUnits 00593D60 (the local player loots every dead unit, then they are removed),
  **AwardExperience** 005911C0, **SyncPartyRecords** 00590C70, CreateItemNamed 00594AB0 /
  CreateSpellNamed 00594BC0 (the scripts' item / spell by name), AddCounted 00594F30 (an item
  whose state changed goes on counted_ once).

## Rewritten

| Address | Name | Verification |
|---|---|---|
| 00596E70 | CWorldServer::LoadSave (RET 8) | ours only: 1, `loaded_` set, no read past the end of a memory stream, every map object registered by id |
| 00597BD0 | CWorldServer::SaveWorld (RET 4) | differential: the original writes into a scratch CMemoryStream first, ours into the real one; size and bytes compared (its side effects repeat harmlessly: gtime +0, nothing left to release) |
| 00596900 | CWorldServer::LoadSavedMapObjects (RET 4) | ours only: stream not overrun, map objects registered |
| 0058D280 | CWorldServer::UpdatePlayerTimes (RET 4) | ours only: every player's last tick is the world's |
| 005956E0 | CWorldServer::PrepareSave | ours only: map objects still registered |
| 00589CA0 | LoadSharedObject (thiscall on the entry, RET 4) | differential on a memory stream and a fresh entry: the original reads into a scratch entry first (its references released), the stream is rewound; fields, objects and bytes read compared |
| 005947E0 | CWorldServer::AddSharedObject (RET 0x10) | ours only: one more entry, at the head, with the id, value and one object per list element |
| 0058B750 | CWorldServer::SyncPlayer (RET 4) | ours only: the collected array consistent, 1 for a leaving player |
| 0058C890 | CWorldServer::SendZone (RET 4) | ours only: 1 when the player is not playing |
| 0058EF20 | CWorldServer::CreateUnits (RET 0x20) | ours only: every record's id names a unit afterwards |
| 0058ECE0 | CWorldServer::UseQuickItem (RET 8) | ours only: the world has no fewer objects |
| 0057B3B0 | GameFlow::SaveGame (RET 4) | ours only: a reported save has its scenario.sav, single player only |
| 00581610 | GameFlow::SaveGameThunk (RET 4) | ours only: 0 or 1 |
| 0057A060 | EnterZone (__stdcall, RET 4) | ours only: 0 or 1 |
| 0057BDF0 | StartNetworkZone (__stdcall, RET 4) | ours only: on success every exit leads to MPGame1 |
| 0057A780 | LeaveZone (__stdcall, RET 4) | ours only: a reported save has its saves\current\<zone>.sav |
| 0057AA80 | CWorldServer::ZoneNameInto (RET 4) | differential: the original copies into a scratch CString; both share the zone name's buffer |
| 0057F190 | GameFlow::UpdateMultiplayerVariable | ours, then the original must leave "z.MPGame1" as ours set it |
| 0057E6B0 | GameFlow::MultiplayerZoneInto (RET 4) | differential: the same name |
| 0057F280 | GameFlow::BeginMultiplayerGame | ours only: state 4, the flow's and the party's zone equal, 007C7DD8 set |
| 0057F0F0 | GameFlow::ReturnLocalParty | logged (effects in original code) |
| 0057F150 | GameFlow::UpdateAndAcknowledge | ours only: an active client's request is not left at kind 3 |
| 0057FB60 | GameFlow::RequestBriefing (RET 4) | logged |
| 0057F390 | GameFlow::RecreateClientSession | ours only: a client session with a reference |
| 00581660 | GameFlow::CloseSessions | ours only: both sessions gone, state 1 |
| 00581630 | RequestZone (__stdcall, RET 8) | logged |
| 005816C0, 005816D0, 00581700, 00581730, 00581760 | GameFlow::RequestState3 / RequestLoad / RequestZoneChange / RequestPartyZone5 / RequestPartyZone6 | ours only: state, name and +0x78 as asked |
| 0057F820 | NameInList (fastcall, AL) | differential: the same answer |
| 0057FC90 | BriefingSpeakerInto (__stdcall, RET 8) | differential: the original into a scratch string first; strings compared |
| 0057FDE0 | BriefingVariableInto (__stdcall, RET 8) | differential, same |
| 0057FF40 | GameFlow::MarkBriefing (RET 4) | ours only: the variable reads 1 |
| 00580040 | GameFlow::ForgetBriefing (RET 4) | ours only: the variable reads 0 |
| 00580840 | GameFlow::OnVariableChanged (RET 4) | logged |
| 00580F10 | UnloadQuestTexts | ours only: the list is empty |
| 005790E0 | GuardPlayerData (thiscall on 007AFEA4, RET 4) | ours only: the guard state is as asked |
| 005791C0 | GuardPlayers (fastcall + key, RET 4) | involution: ours, then the original with the same key must restore the checksum of every guarded dword, then ours again |
| 00591800 | CWorldServer::UpdateMercBriefings | ours only: per slot at most one of n1/n3/n9 set |
| 00592090 | CWorldServer::CompleteBriefing (RET 8) | ours only: the player's gold does not go down |
| 00591DE0 | CWorldServer::ApplyMercBriefing (RET 8) | ours only: the records change by at most one, and only for "b.merc" names |
| 005911C0 | CWorldServer::AwardExperience (RET 8) | counted (effects are in original code) |
| 00590C70 | CWorldServer::SyncPartyRecords (RET 4) | ours only: record list counts sane |
| 0058FD90 | CWorldServer::StoreParty (RET 0xC) | ours only: no stored record's unit is still an owned unit |
| 00590A40 | CWorldServer::CollectParty (RET 8) | ours only: records not more than before |
| 00590700 | CWorldServer::Redeploy (RET 0xC) | ours only: a deployed player's last tick is the world's, others untouched |
| 00590460 | CWorldServer::RestorePlayerItems (RET 4) | ours only: in a network game the carried items are untouched |
| 005901E0 | CWorldServer::TakeZoneQuestItems (RET 8) | differential: the original on a copy of the array into a scratch list first; arrays and moved items compared |
| 00594CE0 | CWorldServer::CopyItem (slot 24, RET 4) | ours only: type, record and count copied |
| 00594AB0 / 00594BC0 | CWorldServer::CreateItemNamed / CreateSpellNamed (RET 4) | logged |
| 00594F30 | CWorldServer::AddCounted (RET 4) | ours, then the original must find the item and change nothing |
| 0058CCD0 | CWorldServer::UpdateWeather | ours only: day factor matches rain, no null timed object |
| 00578E30 | ShowBolt (thiscall on 007AFEA4, RET 0x18) | ours only: exactly two random numbers drawn |
| 005942D0 | CWorldServer::Loot (RET 8, AL) | ours only: gold not lower, a looted shared record removed |
| 00593D60 | CWorldServer::LootDeadUnits | ours only: no more dead units than before |
| 0058DFF0 | CWorldServer::Update (slot 6) | ours only: returns 1 and counts one update |
| 0058E850 | CWorldServer::CastSpell (RET 0x18) | ours only: missile list links consistent |
| 0058E670 | CWorldServer::CreateMissile (RET 0x20) | same |
| 00595C70 | CWorldServer::LoadMap (RET 0x10) | ours only: 0, or 1 with the map and .mob names set and +0x11D / +0x11E set |
| 0058B250 | CWorldServer::ResetZone | ours only: id maps, missiles, counted list, exits and .mob files empty |
| 0058B600 | CWorldServer::ResetState | ours only: kind, alarms, start position, map name cleared, day factor 1 |
| 005957E0 | CWorldServer::TeardownZone | ours only: no player left, missiles and timed objects empty, kind and result 0 |
| 005951A0 | CWorldServer::SendFreshObjects | ours only: every map object still a world object, none left in +0x12D88 |

Calls between these functions go through their original addresses (forwarders) so each verify
wrapper sees its calls; the Rebirth-patched callees (00595080, 005927C0, 005935C0, 00595480,
0057C460, 006AC970, 005800C0, 005809D0) are always called at their original address.

## Floating point

Files with float code are compiled under `#pragma float_control(precise, on)` (world_units,
world_weather, world_update, world_spells, world_party, parts of world_save and world_quests) and
were read back with dumpbin: stores to float where the original stores (StoreFloat;
`static_cast<float>` alone was dropped by /fp:fast in two places, now StoreFloat), values the
original keeps on the FPU as doubles, NaN branches as the original's FCOMP tests (C3-only tests
written as `!(a < b) && !(a > b)`, C0 tests as `!(a >= b)`). Random words are converted with
x87::FromUnsigned; truncations use the game's _ftol (006E853C). Redeploy's facing uses FPATAN
through a naked helper; its differences (start - centre) are spilled to qwords, exact for float
operands. ShowBolt sums x and y into floats but keeps z on the FPU, as the original. MSVC spills
some extended intermediates to qwords (exact at 24/53-bit precision, not at 64-bit; same caveat
as before).

## Forwarders added (standalone build must replace)

All are file-local EI_ORIGINAL forwarders. Notable ones: save streams (00461190 read string,
004A50D0 / 004A4DA0 stream diffs, 00579830 / 00579B80 save file read/write, 00596140 AI graph,
005B1380), managers (004047E0 / 00404860 changed objects, 004048F0 release unused, 00403F40),
missiles (0067E510, 0067E7D0, 0067E960, 0067ECB0, 0067E6D0, 0059C7A0), player state messages
(0066D100 .. 0066F450, 0066EBD0, 0066ED30, 0066EE10, 0066EE80, 0066F280, 0066F2C0, 00665470,
00665780, 0066B1B0), party records (00661050 / 006611D0 / 00661300 / 00661DB0 / 006618C0 /
00661C10 / 006620F0 / 006628D0 / 00662AC0, 006608D0 .. 00660E70, 004A8890 / 004A88C0), the
creation messages (0046C010, 0046A5C0, 0046AEB0, 006E859F), the item factory (006AD000,
006AD1E0, 006A0110), CAIMap (005B8B30/80, 005BA830, 005BA9D0, 005BBE40, 005BBC70, 005BC750,
005BA790), the sector map (005C0BD0, 005C0DF0, 005C1010), the sessions (0041A2E0, 0041BC40,
0041D260), the briefing texts (004DEFC0, 004DF030, 004DFCB0, 004DEDD0), the quest log
(005F7FC0, 00565380), MFC / STLport instantiations of this range (0059C660, 0059CA60, 0059CC60,
0059D2E0, 0059D5F0, 0059D5C0, 005985C0 .. 0059B510, 00598790 .. 005986B0, 0059B8F0, 0059C890,
0059C8C0, 0059CB50, 0059C3D0, 0059D450, 005719B0, 00599FA0, 0052CEF0, 005588A0, 004621A0,
00429F10, 00427830, 0040C720, ...), CString members (006FDAE9 Left), the CRT _CIacos (006E7320)
and _ftol (006E853C), gethostname (006E6100).

## Original bugs and oddities kept

- LoadSave: the shared-object elements are copied from a local whose first three dwords are
  never written (the element's Load overwrites them; ours zeroes them); AddSharedObject does the
  same for its new head (ours does not copy them; they are set right after). LoadSave reads the
  player list's iterator from the heap (operator_new(8)), as SaveWorld does.
- LoadSavedMapObjects marks every part changed when any change was saved (it tests the bit
  `1 << i` itself, not `changed & bit`), and uses the object before its null test.
- SyncPlayer passes 0066EC10 a dword whose upper bytes are the dirty flags (only the low byte is
  the setting); ours passes it zero-extended.
- CreateUnits places a unit on a free cell near the requested position, not near its line
  position.
- CastSpell / Loot / CreateMissile / record syncs / RecreateClientSession write +4 of a new
  allocation before testing it for null; CopyItem writes the copy's count before its null test;
  Update reuses one player iterator for every loop; SaveGame, EnterZone, MarkBriefing and
  BriefingVariableInto reuse argument slots as temporaries (not observable).
- AwardExperience's solo branch needs the player list's +0x2C to be non-zero.
- AddSharedObject's second argument is unused, and the session player looked up is the entry's
  id (the caller passes a unit id twice).
- CopyItem, CreateItemNamed and CreateSpellNamed build and free two empty CPtrLists (vtable
  0073B8D4) around the call; the three FreeDataChain(null) calls are not kept.
- LootDeadUnits does not stop its walk at an empty map entry (most walks do) and calls Loot and
  RemoveObject on the global server, not on `this`.
- UnloadQuestTexts clears its vector through an inlined erase(begin, end) whose copy loop runs
  over no elements.
- GameFlow's MultiplayerZoneInto maps game types above 3 (unsigned) to bz1mpg.

## Skipped

- Rebirth-patched: 0057ABA0 (load game), 0057C460, 0057C700, 005927C0, 005935C0, 00595480,
  00595BA0, 0058D670, 0057EB10, 0057F560, 005800C0, 005809D0, 005812F0, 005813A0, 00595080.
- Lifetime code: the CWorldServer constructor 00589ED0, destructor body 0058AAD0, Create
  0058AE10, the command queue and AI map destructors 0058A140 / 0058A450, vector deleting
  destructors without callers (00570730, 005709F0, 0058A960), the database tables' destructors
  called by the database reload 0057DEE0 (0057D030, 0057D1D0, 0057D500, 0057D670, 0057D810,
  0057D9B0, 0057DBB0, 0057E1A0, 0057E2F0, 0057E3D0, 0057E510, 00582030, 00582730, 0058C650 ...),
  the coder classes at 00580DD0..00580EE0 (vtables 0073F8C8 / 0073F8A4, used by the
  Rebirth-patched 005809D0), the copy constructor 00580900.
- Static initializers (run before the hooks are installed at WinMain): 0057F480, 005809A0,
  00581070.
- Not done: 00579E60, 0059F510 / 0059F0F0 / 0059A4A0 (see the later additions below for the
  rest of this list).
- Library instantiations added to `config/library.csv` this round: the STLport sort internals of
  the draw lists (00573740, 005756F0, 005759B0, 00575BC0, 00576D40, 005769C0, 00577920,
  00577360, 00577C10, 005781A0 with DepthLess; 00577FC0, 00577730 with UnitOrderLess, only
  reached from the rewritten 005740D0), 00575E50, 00587480, 00581860 (STLport vector
  members), 00581030 (char_traits move), 005993A0 (CArray Serialize), the MFC collections
  0059B8F0, 0059C890, 0059CA60, 0059CAD0, 00599FA0, 0059CB50, 0059CCE0, 0059C510, 0059C810,
  0059D5F0, and (earlier) vector<T*>::reserve and hashtable instantiations. Later: the list
  instantiations of 0057F850 (00583A00, 00583A10, 005844F0, 00584870, 00587890, 005878B0,
  005876C0, 00583C60).

## Later additions (after the first in-game runs)

Fixes: the save files were opened through the stream's own vtable (slot 8 of 0073C8D8, past its
end) instead of the embedded CFile's Open (SaveGame, EnterZone, LeaveZone, StartNetworkZone);
SaveWorld's differential now restores the random generator (00661C10 XORs the gold with a fresh
random word), which also kept the game's random sequence from shifting in verify runs; the
SaveGame / LeaveZone checks look for files through game.exe's (harness-redirected)
GetFileAttributesA import.

| Address | Name | Verification |
|---|---|---|
| 0057AAB0 | ClearSaveFolder (ECX = path) | ours only: the folder exists (CreateDirectoryA / DeleteFileA through the game's imports) |
| 00579B80 | SaveFileWrite (ECX file, EDX magic, RET 8) | logged; checked by LoadSave reading the files |
| 00579830 | SaveFileRead (ECX out, EDX magic, RET 0xC, AL) | ours only: a file that reads has data and the file's version |
| 00596140 | CWorldServer::ReadAIGraph (RET 0xC) | differential: the original reads into a scratch memory stream; result, size and bytes compared |
| 00596350 | CWorldServer::LoadZoneScripts (RET 4) | logged |
| 005810A0 | GameFlow::RegisterQuests (RET 4) | ours only: the folder listed at most once |
| 005814C0 | SessionMapNameInto (__stdcall, RET 4) | differential: the same name |
| 0057EE10 | GameFlow::SwitchMultiplayerZone | ours only: +0x80 flips |
| 0057E740 | GameFlow::StartServer (RET 0x18) | ours only: both sessions, the client's game type, +0x25FC |
| 0058DAF0 | CWorldServer::AcceptNewPlayers (RET 4) | ours only: no players lost |
| 0057F850 | PickZones (__stdcall, RET 0xC) | ours only (random): the output only grows |
| 0059F710 / 0059F8F0 | CFigureMesh::SaveToFile / LoadFromFile (RET 4) | logged / sizes not negative |

Notes: SaveFileWrite compresses with the LZ coder, decompresses the result and compares it with
the input; on a mismatch it writes a 0 flag and the raw data. Versions above 0x6B carry a
checksum (h ^= dword; h *= (h & 0xFC) | 3), above 0x71 the flag byte. PickZones keeps its
groups in game-heap pointer arrays instead of the original's STLport lists of copies (same
order, same random draws). StartServer passes 00417E10 a dword whose upper bytes are garbage in
the original; only the low byte is used.


Finished from the list above (third round):

| Address | Function | Verify |
| --- | --- | --- |
| 00579E60 | LoadMission (__stdcall) | logged |
| 0059F0F0 | CFigureMesh::CopyFrom | ours only: descriptors and bounds equal the source |
| 0059F510 | CFigureMesh::SetPose (quaternion pose) | differential |
| 005ACE80 | LoadMenuRects (menus.reg, 11 rects) | ours only: no all-zero rectangle |

0059A4A0 is MFC CList<SharedObject>::Serialize, named in `config/library.csv`. The AI and figure
draw functions of 0x5A0000-0x5C0000 done in the same round are in ai.md and models.md.

# Client maps (0056E000-00574000 helpers, the client's object maps)

Code: `src/game/world_client_maps.cpp`; verify wrappers: `tests/verify/verify_world_client_maps.cpp`.
Shared files changed: `world.h` (additive: `SectorListNode`, `SectorGrid`, the functor and lookup
declarations, `PendingMap`, `RecordVector`, a `CBaseManager` forward declaration).

## Structures

- **Sector grids** (CWorldClient +0x120 objects, +0x150 a second grid): +0x00 the list storage,
  +0x04 rows (row y: its lists, one head pointer each), +0x08/+0x0C sizes checked by the resize
  (00572450), +0x10 width and +0x14 height in cells. Cells are 16x16 world units: the cell of a
  coordinate is FISTP (round to nearest) shifted right by 4 as unsigned, so negative coordinates
  land outside. Each cell is an STLport list of counted object pointers (+4 reference count,
  slot 4 releases). CWorldClient slot 41 (005618A0) moves an object in +0x120, slot 42 (005618C0)
  in +0x150.
- The window lists walked by the collectors: STLport list nodes whose 0x18-byte entry (node + 8)
  holds the object id at +4 and a RECT at +8.

## Rewritten

| Address | Function | Verify |
| --- | --- | --- |
| 00570860 / 00570BB0 | SectorGrid::Move / Move2 (two instantiations) | differential, sampled (the first 256 calls, then every 32nd): ours on a scratch grid holding copies of the two touched cells (nodes from the game's pool, a reference per copy), the original on the real grid; both cells' contents in order, the object's reference change and position compared |
| 00571D50 | MarkWithinRadius (for_each functor) | differential: marks undone after the original, functor compared |
| 00571800 / 005718B0 | CollectWindowsAt / CollectWindowsAt2 (slot 22 / 24 hit test) | differential: the original's pushes taken back, ours compared |
| 0056FC60 | PendingMapLookup (hash_map<unsigned, T*> lookup) | differential |
| 0056E400 / 0056E440 | RecordAt50 / RecordAtB4 (bounds-checked record access) | differential |
| 005647E0 | CWorldClient::AddObject (RET 4) | ours only: returns 1, the id is in +0xB8, a new object is in its grid cell |
| 00564BE0 | CWorldClient::RemoveObject (RET 4) | ours only: one +0xB8 listing fewer, the id map lacks the id |
| 00564E70 | CWorldClient::ClearMaps | ours only: grids, +0xB4, +0x7BC, the MFC lists and the id map empty; no positive id left in +0xB8 |
| 005726B0 | ReleaseIfIdentified (ClearMaps' predicate) | ours only: true exactly for a positive id |
| 00565790 / 005659D0 | CWorldClient::PickAt / PickAt2 (RET 4) | differential: the original first; the object, the sorted entries and the cursor compared |
| 00566F70 | CWorldClient::CreatePartyUnits (RET 8) | ours only: no +0xB8 listing lost, at most one new object per record |

CreatePartyUnits (called by the glue 005789C0 with the player's party records, an MFC pointer
array): per record a CStringArray of the figure's part names "hd", "bd", "hp", "hr", "lh1".."rl3",
where "hr" becomes "hr.%.2d" with the number at +0x10 of the prototype found by name in the map
007AF364 (left out at -1); the object manager's slot 23 creates the hero (type 0x32, the record's
id +0x1C); +0x18 = the prototype's index (-1 when unknown), the second prototype by name (+0x18),
flag 0x4000000, the figure from the model (+8) and the part names (00516DA0; a failure ends the
whole call, like a failed creation), the skin swap from "none" to +0x10 (none when it is "none"),
the stats (+0x20, slot 35), the position (+0x724, also copied to +0x1AC), the items (weapons
+0x730, armour +0x750 (type-0 armour only with the flag), quick items +0x740, each array's count
in the next word), the player (+0, slot 61), then AddObject.

The client's maps (CWorldClient): +0xB4 an STLport list of counted pointers to heroes with a
text-key id; +0xB8 a list of every object (pointers with a reference taken by hand); +0xBC an MFC
CList of the objects answering slot 29, with +0xD8 a hash_map from id to that list's node (+0xEC
the map's highest key); +0x1F0 an MFC CList of ids; +0x7BC a list of 0x0C-byte nodes; +0xF0 the
screen entries (id, RECT) the pickers walk, +0xF4 the vector they fill and sort, +0x100 its
cursor. AddObject's first loop compares ids: an object whose id is already listed is only
referenced and released (+1, slot 4). In AddObject the grid listing is removed before it is
appended, so a re-added object is listed once. The pickers sort with STLport's sort over the
0x18-byte entries and DepthLess {the view stack's top, the object manager}; the original inlines
the first level of the introsort loop, the rewrite calls the out-of-line loop (00573740, the same
steps) and the two insertion sorts.

Move removes every listing of the object from the old cell (list::remove with a counted copy:
+1, the erases release, -1), stores the three position words, then push_back's a counted copy
into the new cell (+1, the insert adds its own, -1). Out-of-grid cells are skipped on either side.
The first, ours-only check walked both cells and computed float floors on every move. The grid
at 007AF6D8 (moved by 0067BEF0 / 0067CEF0) holds hundreds of entries per cell near effects, and
the moves were disabled after map_objects' wolf failed to walk in time with them on. Re-reading
the disassembly against the rewrite found no behavioural difference: FISTP in the same rounding
mode, the same bounds, cells, reference counts and unlink/append order. The likely cause is the
check's cost, so the comparison is now sampled and the other calls run ours with no extra work.
MarkWithinRadius calls the game's `_hypot` and marks when the distance is not greater than
radius (+0x94) x scale; a NaN marks (FCOMPP + TEST AH,0x41). The distance and the product are
plain C++ (rule 7): a distance on the very edge of the radius may rarely mark differently.

## Classified as library (config/library.csv)

The other 96 functions of 0056E000-00574000 are STLport / MFC instantiations (lists, hash_map,
vectors, CArray / CMap SetSize, Serialize, NewAssoc), compiler-generated constructors, copy
constructors and deleting destructors, counted-pointer helpers, and the sector grid template's
storage (allocation, resize, reset, release, clear-all: new[]/delete[] of lists, no logic).
005593D0 (the unit appearance cache) and its twelve hash_map helpers are classified the same way,
as agreed.
