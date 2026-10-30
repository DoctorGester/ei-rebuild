# Cleanup phase 1: the AI family (cleanup plan, ticket C2a)

What changed in the AI files and what is left. This covers `ai_*.cpp/.h`, `unit_ai*.cpp/.h`, `sector_map.cpp` and the headers that declare
their classes. The conventions are the ones in `cleanup-engine-base.md` and `cleanup-units.md`. The release objects were compared with a
disassembly diff: the only differences were virtual calls replacing the VCall helpers, member loads, register and stack moves, and the shared
the aligned array's SetSize replacing FootprintCellSet's own copy (a `std::vector` since) (same values, its three final stores in a different order).

## Declarations

- **CAIMap** (`ai_map.h`) embeds `SectorMap sectorMap` at +0x124, `std::vector<AreaOffset> areaLists[6]` and two `UnitLayer`s
  (`unitLayerSmall`, `unitLayerLarge`; `UnitLayer` and `UnitLayerNode` live here; their virtual `Set` and `Merge` are slots 0 and 1). The cost
  tables are `passCosts`, `passCostsAlt` and `stepCosts`; the water patch is a `UnitShape`. **AIPath** starts with its `MotionOrder` (in
  `ai_map.h`); its excluded blocks are a `BlockPointArray`. `sector_map.h` includes `ai_map.h` last, so files that include only `sector_map.h`
  still see AIMap.
- **UnitController** (`unit_ai.h`) is declared after AIBrain and embeds `AIBrain brain` (+0x980) and `AIAlarm alarm` (+0xB10), so `Say` is
  `alarm.Raise`. **CUnitServer** (`combat.h`) embeds the same `AIBrain brain` and `AIAlarm voice` and includes `unit_ai.h`; the brain offset
  constants in `combat_actions.cpp` and `combat_motion.cpp` are members (`callRadius`, `memory`, `unknown_0F4` the nearby list, `visible`,
  `AIBrain::kActionFollow`).
- **Orders.** `UnitAction` (`combat.h`, 0x24 bytes) is an `AIOrder` (`unit_ai.h`: kind, source, the counted `target`, point, pointIsUseSpot) plus
  `param` and `modifier`; the AI builds only the AIOrder on the stack, and `CanReachAndPlan` / `From` and the planning under them
  (`CanPlanAction`, `PlanAction`, `CanActFrom`, `FindSightTarget`, `ActionDistance`, `CanReachTarget`, `CanCast`, `TimeUntilAction`) take a
  `const AIOrder*`, so nothing reads past it. The brain's action point and looks are one `AIMoveOrder move` (+0x008; 00545B20 clears it).
- **UnitShapeCache** (`ai_shape_cache.h`) is a novtable class whose slot 0 is `CreateShape`; `UnitShapeHandle` is `ShapeCacheHandle`; the shape map
  is a `std::unordered_map<float, SharedRef<RefObject>>`. **FootprintCellSet** holds its cells in a `std::vector<FootprintCell>`.
- **Virtual slots used.** MapObject: `KeepsMapFlag` (43, returns bool, AL), `PositionAt` (52), `AimPoint` (53), `IsLargeUnit` (54), `IsDead` (55).
  World: `ObjectWithId` (40). ByteStream: `Read` / `Write` (path save). RefCounted's deleting destructor through `delete` (the original's inlined
  release).
- **Part names in CollectFootprint** come from the server companion's string array (`MapObjectServer` +0x80) or the object's own, read as
  `std::vector<std::string>` members in `unit.h` and `map_object_figures.h`.
- **The party in danger** (`PartyInDanger`, `unit_ai_modes.cpp`) is `Player::connection` -> `PlayerServer::danger == InDanger`.
  `Unit::player` is still `char*` (`unit.h`).

## Left

- Per-file container structs the counter matches by name: `FootprintCellSet` (a hashed cell set, 16 buckets chained through the cells, on the shared
  aligned array) and `RegionMap` (a grid, not a container).
- Duplicate controller declarations: UnitController (`unit_ai.h`) and UnitServer (`combat.h`) declare the same object under different field names
  (command / action, nextCommand / action2, ...); merging them touches every `unit_ai*` and `combat*` file. `UnitController` (`unit.h`) is the slot
  view.
- `AIStream` (`unit_ai.h`, the remaining `Slot0` placeholder) is ByteStream with `version` at +8; the verify stream doubles derive from it, so replacing
  it needs those doubles to implement ByteStream's pure slots.
- `AIBrain` placeholders: `memory[8]` (the perception memory has no struct of its own; its methods are AIBrain members called on +0xD0) and
  `unknown_0F4` (the nearby list; renaming it touches `player_sync.cpp`).
- Hand-laid fields that remain: the water grid fields, `bridgeHeights` as bytes, the `__fastcall` layer Set / Merge
  shims in `ai_unit_layer.h`, `SmoothPath`'s fraction passed as raw bits (it is stored to memory before the octant's x87 work; a float parameter
  would change when it is stored and pushed), and the `CStringArray` layouts in `MapObjectServer` and `Object3D` (other areas).
- Counted object references are `Ref<T>` fields (ref.h): `PlayerMode::leader` is a `Ref<Unit>` (Assign / Reset on the field,
  `LoadRef` / `SaveRef` from engine_object.h). `Ref<T>::Of` remains for list nodes that are plain pointers.
