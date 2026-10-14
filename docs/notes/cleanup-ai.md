# Cleanup phase 1: the AI family (cleanup plan, ticket C2a)

What changed in the AI files and what is left. This covers `ai_*.cpp/.h`, `unit_ai*.cpp/.h`, `sector_map.cpp`
and the headers that declare their classes. The conventions are the ones in `cleanup-engine-base.md` and
`cleanup-units.md`.

Every rewritten address still has its hook and verify wrapper. The release objects were compared with a
disassembly diff for each group. The only differences are:

- virtual calls replacing the VCall helpers;
- member loads replacing At<> and byte offsets;
- register and stack moves;
- the shared `lib::AlignedArray::SetSize` replacing FootprintCellSet's own copy. It computes the same values,
  but its three final stores come in a different order.

Nothing disagreed with the original.

## Counts in these files (before -> after)

| Count | Before | After |
|---|---|---|
| Offset access (At<>/Field<>) | 17 | 0 |
| VCall | 15 | 0 |
| Byte arithmetic | 18 + 4 in sector_map.cpp | 0 |
| *At views | 0 | 0 |
| Per-file vector/map/set | 2 | 2 (see below) |
| Duplicate forwarders | 0 | 0 |

## Declarations

- **CAIMap** (`ai_map.h`) embeds:
  - `CSectorMap sectorMap` at +0x124;
  - `mfc::CArray<AreaOffset> areaLists[6]`;
  - two `CUnitLayer`s (`unitLayerSmall` and `unitLayerLarge`). `CUnitLayer` and `UnitLayerNode` moved here
    from `ai_unit_layer.h`. Their virtual `Set` and `Merge` are slots 0 and 1.
- **AIPath** starts with its `MotionOrder` (moved here from `ai_motion.h`). Its excluded blocks are a
  `BlockPointArray`.
- **sector_map.h** no longer includes `ai_map.h` first. It includes it last, so files that include only
  `sector_map.h` still see CAIMap.
- **UnitController** (`unit_ai.h`) is now declared after AIBrain. It embeds `AIBrain brain` (+0x980) and
  `AIAlarm alarm` (+0xB10), so `Say` is `alarm.Raise`.
- **CUnitServer** (`combat.h`) embeds the same `AIBrain brain` and `AIAlarm voice`, and includes
  `unit_ai.h`. The brain offset constants in `combat_actions.cpp` and `combat_motion.cpp` became members:
  - `callRadius`;
  - `memory`;
  - `unknown_0F4` (the nearby list);
  - `visible`;
  - `AIBrain::kActionFollow`.
- **UnitShapeCache** (`ai_shape_cache.h`) is a novtable class whose slot 0 is `CreateShape`. `UnitShapeHandle`
  is now `ShapeCacheHandle`. The shape map's iterator, pair and node are the `stlp::hash_map` ones.
- **FootprintCellSet** holds its cells in a `lib::AlignedArray<FootprintCell>`.
- **The water patch** is a `CUnitShape`.
- **The cost tables** are `passCosts`, `passCostsAlt` and `stepCosts`.
- **Virtual slots used:**
  - CMapObject:
    - `KeepsMapFlag` (43, now returns bool, AL);
    - `PositionAt` (52);
    - `AimPoint` (53);
    - `IsLargeUnit` (54);
    - `IsDead` (55).
  - CWorld: `ObjectWithId` (40).
  - CStream: `Read` / `Write` (path save).
  - CRefCounted's deleting destructor, through `delete` (the original's inlined release).
- **Part names in CollectFootprint** come from the server companion's string array (`CMapObjectServer`
  +0x80) or the object's own. Both are read as `mfc::CArray<mfc::CString>` over hand-laid fields in
  `unit.h` and `map_object_figures.h`.
- **The player that controls a unit directly** (`unit_ai_modes.cpp`) is found as
  `CPlayer::connection_` -> `CPlayerServer::refreshPending_ == 2`. `CUnit::player` is still `char*`
  (`unit.h`).

## Left

- **Per-file vector/map/set** (the counter matches these by name):
  - `FootprintCellSet` is a hashed cell set (16 buckets chained through the cells) built on the shared
    aligned array.
  - `CRegionMap` is a grid, not a container.
- **Duplicate controller declarations.** UnitController (`unit_ai.h`) and CUnitServer (`combat.h`) declare the
  same object under different field names (command / action, nextCommand / action2, ...). Merging them
  touches every `unit_ai*` and `combat*` file. `CUnitController` (`unit.h`) is the slot view.
- **AIStream** (`unit_ai.h`, the remaining `Slot0` placeholder) is CStream with `version` at +8. The verify
  stream doubles derive from it, so replacing it needs those doubles to implement CStream's pure slots.
- **AIBrain** still has these placeholders:
  - `memory[8]`: the perception memory has no struct of its own, and its methods are AIBrain members called
    on +0xD0.
  - `unknown_0F4`: the nearby list. Renaming it touches `player_sync.cpp` (network).
- **Hand-laid fields that remain:**
  - LocalSearch's `cellRows` CArray;
  - the water grid fields;
  - `bridgeHeights` as bytes.
- **Other leftovers:**
  - the `__fastcall` layer Set / Merge shims in `ai_unit_layer.h`;
  - `SmoothPath`'s fraction, which is still passed as raw bits. It is stored to memory before the octant's x87
    work; a float parameter would change when it is stored and pushed.
  - the `CStringArray` layouts in `CMapObjectServer` and `CObject3D`, which belong to other areas.
