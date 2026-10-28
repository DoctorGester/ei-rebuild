# Cleanup phase 1: world server / client and combat (cleanup plan, ticket C2w, F1)

What changed in the world and combat files (`world_*.cpp` without `world_effect*`, and `combat*.cpp`) and what is left. The conventions are the
ones in `cleanup-engine-base.md` and `cleanup-units.md`.

## Declarations

- **The unit set** (`world.h`): `UnitSet` holds two typed identity-hashed maps: `figured` (unit id to a `FiguredUnitMap` of `FiguredUnitRecord`)
  and `dressed` (unit id to `DressedUnitRecord`). `UnitSetEntryHead` is merged into `DressedUnitRecord`, whose `Write` (00558FA0) is a member.
  `ReadUnitLooks` takes typed vectors. `world_sync.cpp` builds a `UnitSet` and constructs / destroys its two `stlp::hashtable` members directly (005985E0 ... 005986B0).
- **CWorldServer**: `diplomacy_` is a `CDiplomacy` embedded at +0x12C64 (users pass `&diplomacy_` or read `allied_`). `aiEvents_` is the
  `AIEventTable` (world.h includes unit_ai.h; `AIEvents()` returns it). The object sets a player is sent are `std::vector<CEngineObject*>`
  (world_object_sets.cpp): the radix sort takes the address apart through `AddressWord`, the merge compares by `std::less` on the pointers.
- **Virtual slots used instead of VCall.** CWorld: `TimedObjectManager` 14, `PlayerList` 20, `RawSurfaceHeightAt` 28, `SurfaceHeightAt` 30.
  CMapObject: `ReceiveHit` 31, `ApplyScience` 41, `EyePoint` 48, `PositionAt` 52, `AimPoint` 53, `IsLargeUnit` 54, `IsDead` 55, `ForgetObject` 56,
  `UpdateMoveFactors` 57, `MoveSpeed` 59, `Radius` 60. Also CBaseManager `CreateObject`, CStream `Read` / `Write`, mfc::CFileVirtuals `Close`,
  CItem `GetUse`, CParty `ItemManager`, CPlayerSource `TakeNew` / `Query`, CEngineObject `ClearState` / `Release`, CSpellEffect `DeletingDestructor`.
- **Members instead of offsets:** `CEffect::startTick_` (timed objects), `CParty::Money()` (player gold), `CPlayerServer::partyUnits_`,
  `AIBrain::visible` / `unknown_37`, `CUnitServer::stats`, `CTextManager::cursor_`, `CWorld::shared5C_`, `CWorldServer::hoover_`,
  `CPlayer::units_` / `Server()`.
- **AddUnitToServer's record builder** (`world_zone.cpp`) is storage for a `CParty`. Its record is the party's first `PartyUnitRecord`, and the
  unit id goes in `unknown_01C`. `world.h` still counts `ObjectIdMap`, `PlatformSet` and `UnitSet` as per-file container structs: they are game
  structures built on the shared templates, not container layouts.

## Left

- Offsets: `combat_actions.cpp` (the brain offset constants `kBrainMemory` etc.), `combat_sight.cpp` (`Get<>` reads), `combat_orders.cpp` (`server
  + 0x1C / 0x34 / 0x38` (mana) and `world[0x6C]`), `world_server.cpp` (the game server record +0x30, +4), `world_briefing.cpp` (the variable
  event's record +8, +0x0C, +0x18).
- Types: `ApplyScience` (slot 41) is declared with two ints (the use record and the unit). Slot 54 is `IsLargeUnit`, though the planner reads it as
  "moving". The multi-target spells search units within CSpell `range_` (+0x34), up to `targetCount_`. `CMapObject::updateCountdown` (+0x174) is the
  unit's update slot. The controller is reached through `reinterpret_cast` to `UnitController` / `CUnitServer`; these two declarations overlap and
  should become one. `FiguredUnitRecord` does not derive from `DressedUnitRecord` (its head shares the layout, and Write is called on it through a
  cast); `RecordPtrArray` is still a `uint8_t*` array. `ZoneExit` (object_tables.h) is the one exit record: the map database's zone records,
  the world server and the client world all hold `ZoneExits` (CArray<ZoneExit, ZoneExitElements>, SetSize 00573080).

## Meanings of formerly unknown fields (ticket F1)

A gap became a name when src reads or writes it through the class, else `unused_XXX`; writes only in construction count as unused.
- `CWorldServer::sendsStartPosition_` (+0x11C) is never set non-zero by the game code, so SendZone's start-position branch is dead.
- `CUnitServer::aiPresence` (+0x918) holds 005C9770's result, which is `AIViewer::StepNoise` and always returns 0: the presence record is never
  written and `AIEventPresence` is speculative.
- `CPlayer` +0x108 is god mode (`CPlayer::godMode_`); `CPlayerClient` +0x84 is the local session.
- Written, never read: `CWorldClient::lightingMark_`, `CUnitServer::orderStopped` (saved too), `DamageInfo::weaponValue`.
- Methods renamed with their fields: `SetCave`, `SetClimate`, `SetWeather` (the forwarders `WorldSetCave`, `WorldSetClimate`, `GameSetWeather`); the
  save version constant `kVersionRainBoltTick`.

## C21 batch 22 (2026-10-22)

- world_client_maps.cpp calls its rewritten callees directly (nine rows left config/direct_calls_skip.csv); LoadLightsAt (00563930) and
  006B6040 stay forwarders (Rebirth patches them).
- The client's visible list is a `stlp::list<CEngineObject*>`; BuildUnitSkins takes the typed figured and dressed record vectors.
- Unit skins: `SkinRequest::layers` is a `std::vector<std::uint8_t>`.

## C21 batch 23 (2026-10-22)

- SyncPlayer's four static arrays are vectors; the player
  state's Send* functions take each element as the pass's class through `NextAs<CMapObject>` / `NextAs<CEffect>` (player_sync.cpp:
  the same arrays carry map objects, then timed objects).
- `CWorldServer::ReadAIGraph` writes a `CStream*`; the picking sort's `DepthLess::manager` is a `CBaseManager*`; the selection
  markers look their objects up with `manager40_->FindObjectAs<CMapObject>` (the 00403F40 forwarder is gone).
