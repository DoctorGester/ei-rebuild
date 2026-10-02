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
