# Cleanup phase 1: world server / client and combat (cleanup plan, ticket C2w)

What changed in the world and combat files (`world_*.cpp` without `world_effect*`, and `combat*.cpp`) and what is
left. The conventions are the ones in `cleanup-engine-base.md` and `cleanup-units.md`. Every rewritten address
still has its hook and verify wrapper. The object code was compared with main using a disassembly diff for each
group. The only differences are virtual calls replacing the VCall helpers, member loads replacing At<>, register
and stack moves, and helper inlining.

## Declarations

- **The unit set** (`world.h`): `UnitSet` holds two typed identity-hashed maps:
  - `figured`: unit id to a `FiguredUnitMap` of `FiguredUnitRecord`;
  - `dressed`: unit id to `DressedUnitRecord`.
  `UnitSetEntryHead` is merged into `DressedUnitRecord`, whose `Write` (00558FA0) is now a member.
  `ReadUnitLooks` takes typed vectors. `world_sync.cpp` uses `UnitSet` through forwarders that construct and
  destruct each map.
- **CWorldServer**:
  - `diplomacy_` is a `CDiplomacy` embedded at +0x12C64. Its users pass `&diplomacy_` or read `allied_`.
  - `timeWheel_` stays a byte array behind `AIEvents()`. Including `unit_ai.h` in `world.h` clashes with the
    local `kClassUnit` of six files.
- **Virtual slots used instead of VCall:**
  - CWorld: `TimedObjectManager` 14, `PlayerList` 20, `RawSurfaceHeightAt` 28, `SurfaceHeightAt` 30.
  - CMapObject: `ReceiveHit` 31, `ApplyScience` 41, `EyePoint` 48, `PositionAt` 52, `AimPoint` 53,
    `IsLargeUnit` 54, `IsDead` 55, `ForgetObject` 56, `UpdateMoveFactors` 57, `MoveSpeed` 59, `Radius` 60.
  - CBaseManager: `CreateObject`.
  - CStream: `Read` / `Write`.
  - mfc::CFileVirtuals: `Close`.
  - CItem: `GetUse`.
  - CParty: `ItemManager`.
  - CPlayerSource: `TakeNew` / `Query`.
  - CEngineObject: `ClearState` / `Release`.
  - Projectile: `DeletingDestructor`.
- **Members instead of offsets:**
  - `CEffect::startTick_` (timed objects).
  - `CParty::Money()` (player gold).
  - `CPlayerServer::partyUnits_`.
  - `AIBrain::visible` / `unknown_37`.
  - `CUnitServer::stats`.
  - `CTextManager::cursor_`.
  - `CWorld::shared5C_`.
  - `CWorldServer::hoover_`.
  - `WorldPlayer::units` / `server`.
- **AddUnitToServer's record builder** (`world_zone.cpp`) is storage for a `CParty`. Its record is the party's
  first `PartyUnitRecord`, and the unit id goes in `unknown_01C`.

## Counts (these files, before / after)

| | Before | After |
|---|---|---|
| Offset access | 40 | 0 |
| VCall | 53 | 0 |
| Byte offset arithmetic | 15 | 0 |
| *At views | 0 | 0 |
| Per-file containers | 0 | 0 |
| Duplicate forwarders | 0 | 0 |

`world.h` still counts 3 under "per-file vector/map/set struct": `ObjectIdMap`, `PlatformSet` and `UnitSet`. These
are game structures built on the shared templates, not container layouts.

## Left

- **Offsets that were not counted:**
  - `combat_actions.cpp`: the brain offset constants (`kBrainMemory` etc.).
  - `combat_sight.cpp`: the `Get<>` reads.
  - `combat_orders.cpp`: `server + 0x1C / 0x34 / 0x38` (mana) and `world[0x6C]`.
  - `world_server.cpp`: the game server record (+0x30, +4).
  - `world_briefing.cpp`: the variable event's record (+8, +0x0C, +0x18).
- **Provisional or awkward types:**
  - `ApplyScience` (slot 41) is declared with two ints. Its call passes the use record and the unit.
  - Slot 54 is `IsLargeUnit`, though the planner reads it as "moving".
  - The CSpell radius and target count are read as `unknown_34` / `unknown_38`. `spell.h` belongs to the spell
    owner and was not renamed here.
  - `CMapObject::unknown_174` is the unit's update slot.
  - The controller is reached through `reinterpret_cast` to `UnitController` / `CUnitServer`. These two
    declarations of the controller overlap and should become one.
- **Structure not merged:**
  - `FiguredUnitRecord` does not derive from `DressedUnitRecord`. Its head shares the layout, and Write is called
    on it through a cast.
  - `RecordPtrArray` is still a `uint8_t*` array.
