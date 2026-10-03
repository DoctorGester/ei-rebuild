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

CWorldClient slot 6 (005641F0, per tick) and the draw path 00569E40 -> 0056AFE0 are patched by
Rebirth, so they stay original. 0056AFE0 (the draw pass) does, per frame:

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

- 0056AFE0, 00569E40, 005641F0, 00567AB0: patched by Rebirth.
- The sorts' heap helpers (00577FC0, 00577730, 00577E90, 005782F0) and the copy_backward /
  unguarded_linear_insert instantiations (005776A0, 005776D0, 005778A0, 00577F90): their logic is
  in the template, but they are not hooked separately.
- 005677F0 (whether a group's units all stand in the same zone exit; glue 00578A20), 00566F70
  (hero portraits "hr%2d"), 00568800 / 005683D0 (skins "%sskin%2d.mmp"), 00566360 / 00567500 /
  00567680 (per-frame manager updates, large), 00565090 / 00564E70 (clearing the sector grids),
  005647E0 / 00564BE0 (adding and removing objects in the client's maps): next candidates.

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
  otherwise it saves the current zone's state (0058B250); marks every player dirty; then
  **LoadMapObjects** 00596030 for the .mob file and 005951A0 for a new zone. 00595B50 (glue)
  reloads only when the map or the .mob name (+0x12D6C / +0x12D68) changed.
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
  placement), 0058B250 (zone state save), 00596140 AI graph load, 005951A0: large and full of
  network / script state. Next candidates: 00596900 (the counterpart of SaveMapObjects) and
  005957E0.
- 00595B50 (glue: reload when the map or .mob name changed): two compares around patched calls.
- The scenario does not reach LoadMobFile: zone1's scripts use no AddMob, and loading another
  zone's .mob into zone1 is not safe to try blind.

## Scenario

`world_load.txt` (~40 s): new game (zone load, autosave), eight AddUnitToServer wolves, one name
added twice (same id), three RemoveObjectFromServer, `load savea` (waits until a spawned wolf is
gone and the hero is back), one more AddUnitToServer in the loaded zone.
