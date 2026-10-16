# Cleanup phase 1: the AI family (cleanup plan, ticket C2a)

What changed in the AI files and what is left. This covers `ai_*.cpp/.h`, `unit_ai*.cpp/.h`, `sector_map.cpp` and the headers that declare
their classes. The conventions are the ones in `cleanup-engine-base.md` and `cleanup-units.md`. The release objects were compared with a
disassembly diff: the only differences were virtual calls replacing the VCall helpers, member loads, register and stack moves, and the shared
`lib::AlignedArray::SetSize` replacing FootprintCellSet's own copy (same values, its three final stores in a different order).

## Declarations

- **CAIMap** (`ai_map.h`) embeds `CSectorMap sectorMap` at +0x124, `mfc::CArray<AreaOffset> areaLists[6]` and two `CUnitLayer`s
  (`unitLayerSmall`, `unitLayerLarge`; `CUnitLayer` and `UnitLayerNode` live here; their virtual `Set` and `Merge` are slots 0 and 1). The cost
  tables are `passCosts`, `passCostsAlt` and `stepCosts`; the water patch is a `CUnitShape`. **AIPath** starts with its `MotionOrder` (in
  `ai_map.h`); its excluded blocks are a `BlockPointArray`. `sector_map.h` includes `ai_map.h` last, so files that include only `sector_map.h`
  still see CAIMap.
- **UnitController** (`unit_ai.h`) is declared after AIBrain and embeds `AIBrain brain` (+0x980) and `AIAlarm alarm` (+0xB10), so `Say` is
  `alarm.Raise`. **CUnitServer** (`combat.h`) embeds the same `AIBrain brain` and `AIAlarm voice` and includes `unit_ai.h`; the brain offset
  constants in `combat_actions.cpp` and `combat_motion.cpp` are members (`callRadius`, `memory`, `unknown_0F4` the nearby list, `visible`,
  `AIBrain::kActionFollow`).
- **UnitShapeCache** (`ai_shape_cache.h`) is a novtable class whose slot 0 is `CreateShape`; `UnitShapeHandle` is `ShapeCacheHandle`; the shape map
  is an `stlp::hash_map`. **FootprintCellSet** holds its cells in a `lib::AlignedArray<FootprintCell>`.
- **Virtual slots used.** CMapObject: `KeepsMapFlag` (43, returns bool, AL), `PositionAt` (52), `AimPoint` (53), `IsLargeUnit` (54), `IsDead` (55).
  CWorld: `ObjectWithId` (40). CStream: `Read` / `Write` (path save). CRefCounted's deleting destructor through `delete` (the original's inlined
  release).
- **Part names in CollectFootprint** come from the server companion's string array (`CMapObjectServer` +0x80) or the object's own, read as
  `mfc::CArray<mfc::CString>` over hand-laid fields in `unit.h` and `map_object_figures.h`.
- **The player that controls a unit directly** (`unit_ai_modes.cpp`) is `CPlayer::connection_` -> `CPlayerServer::refreshPending_ == 2`.
  `CUnit::player` is still `char*` (`unit.h`).

## Left

- Per-file container structs the counter matches by name: `FootprintCellSet` (a hashed cell set, 16 buckets chained through the cells, on the shared
  aligned array) and `CRegionMap` (a grid, not a container).
- Duplicate controller declarations: UnitController (`unit_ai.h`) and CUnitServer (`combat.h`) declare the same object under different field names
  (command / action, nextCommand / action2, ...); merging them touches every `unit_ai*` and `combat*` file. `CUnitController` (`unit.h`) is the slot
  view.
- `AIStream` (`unit_ai.h`, the remaining `Slot0` placeholder) is CStream with `version` at +8; the verify stream doubles derive from it, so replacing
  it needs those doubles to implement CStream's pure slots.
- `AIBrain` placeholders: `memory[8]` (the perception memory has no struct of its own; its methods are AIBrain members called on +0xD0) and
  `unknown_0F4` (the nearby list; renaming it touches `player_sync.cpp`).
- Hand-laid fields that remain: LocalSearch's `cellRows` CArray, the water grid fields, `bridgeHeights` as bytes, the `__fastcall` layer Set / Merge
  shims in `ai_unit_layer.h`, `SmoothPath`'s fraction passed as raw bits (it is stored to memory before the octant's x87 work; a float parameter
  would change when it is stored and pushed), and the `CStringArray` layouts in `CMapObjectServer` and `CObject3D` (other areas).
