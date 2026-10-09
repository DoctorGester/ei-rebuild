# AI map, path search, unit motion (0x005A0000-0x005D0000)

Code (layouts and per-function comments are in `ai_map.h`, `ai_motion.h`, `ai_unit_layer.h`,
`sector_map.h`; each file's header says what it does): `ai_map.cpp` (cell queries, heights, line of
sight), `ai_footprint*.cpp`, `ai_bridges.cpp`, `ai_occluders.cpp`, `ai_refresh.cpp`,
`ai_region_reach.cpp`, `ai_path*.cpp`, `ai_plan.cpp`, `ai_join.cpp`, `ai_motion.cpp`, `ai_movement.cpp`,
`ai_unit_layer.cpp`, `sector_map.cpp`, and the small ones named in the table. Verify:
`tests/verify/verify_ai_*.cpp`, `verify_sector_map.cpp`. Scenario: `tests/scenarios/movement.txt`
(the map functions also run in every scenario). Names are provisional.

## How a unit moves (the per-tick path)

The unit movement tick 00547890 (controller `this`, outside the range) for a moving unit:
`UnitMotion::Sample` (005C7430) on the trajectory at CUnit + 0x27C with the controller's order
(controller + 0x3C, the AIPath) and the world time gives the next position, direction and speed;
`CSectorMap::Remove`; `CanStandAtFromHere` at the new cell; `BlockingUnitAt` (005B7BB0) when it
cannot stand there; `CSectorMap::Add`; then `CSectorMap::Move` (Rebirth-patched), which lifts and
re-stamps the unit through `MarkFootprint` -> `UnitShape` (005B8350) and the unit layers (005B36B0 /
005B3890, whose virtual callbacks write the cell levels). When the path needs new steps
`ContinuePath` / `AdvancePath` refill them and `SetStepSpeeds` (005BA400) prices them; 0052E9D0 then
rebuilds the trajectory with `UnitMotion::Build` (005C67D0). 00548520 at the end of the tick asks
`StandsOnCheapCell` (005BA5A0). Long paths: `PlanLongPath` (005B9C90, region chain, then local
searches per leg) -> `BuildCellsTo` (005B9A20).

## Facts

- `CAIMap + 0x1F8` is named `waterDepth` but the large-unit layer's Set / Merge also write signed
  bytes there (the small layer writes the cells' `level` byte).
- CUnitLayer (CAIMap + 0x27C small, + 0x12A4 large; 0x1028 bytes): MFC CList of every node ever
  allocated, 1024 hash buckets by (block x * 17 + block y), 4x4 blocks; vtable 0073FD74 (small) /
  0073FD6C (large), slots 0 Set / 1 Merge. Shape cache: owner 007C2F70, hash_map 007C2F78, shapes
  keyed by standing height rounded up to tenths. Globals: direction table 00791988, class heights
  007912BC, climb-neighbour table 007916BC, spiral tables 007C2FC4..007C2FFC, spiral cache 007C2F48,
  list vtable 0073D454.
- Path cells' second dword (`BuildCellsTo`, 005B9A20): the original fills `flags` from a stack
  slot it never writes, 20 bytes below the planner's stack pointer; there the planner (005B9C90)
  leaves either 0 or, after any leg, `SearchCommit`'s row offset 4 * (last row + 1), in practice 0x84.
  `PlanLongPath` records it (`CommitLeg`) and `BuildCellsTo` uses it. Readers: `SetStepSpeeds`
  rewrites both halves of every step but the last, whose flags stay as built; the steering
  (ai_motion.cpp) reads every step's flags as its speed word. (Writing 0 gave the last step of every
  path speed 0.) The region chain cells (`BlockSearchPathTo`) are read only for x and y.
- Sample / trajectory: waypoints at the order start, at path corners (direction change > 45
  degrees; table 00791988), and the target; one point per path cell, speed = step speed * scale / 512,
  slowed where turning takes longer than the way (`LimitSpeeds`); the unit first turns in place unless
  the first direction is within acos(0.9) of its facing; segments are fitted with two Hermite cubics.
- Calls to originals so the verify wrappers see them: CSectorMap::Add / Remove / FindUnitsInRadius,
  MarkFootprint (from sector_map.cpp, ai_path.cpp), `LineOfSight` (005BC3E0, its ST0 is taken
  unrounded). Virtual callbacks (layer Set / Merge) are free `__fastcall` functions called through the
  vtable (`ei::VCall`).
- None of this area is in `docs/rebirth-patched-functions.txt` except `CSectorMap::Move` (005C11D0),
  installed only in the vanilla run, and `CAIMap::Load` (005B43C0, not rewritten).

## Rewritten

Pure = same input, result compared; differential = original first, state kept (and put back), then ours,
compared; ours only = consistency check.

| Address | Name | File | Verify |
|---|---|---|---|
| 005B7B20, 005B7B70, 005B3160, 005B77F0, 005BB960, 005BBA10, 005BBB20, 005BC2A0, 005BBBD0, 005BBC70, 005BBD10, 005BBDB0, 005BBE40, 005BC1F0, 005B64E0, 005B81B0, 005BA6E0, 005BA790, 005BA830 | LevelForSizeClass, StandingLevel, BridgeHeight, GroundHeight, GroundHeightAt, SurfaceHeightAt, TopHeightAt, SlopeAt, IsBridgeAt, CellFlag80At, HasKind3Occluder(At), MaterialAt, MaterialRecordAt, BridgeDeckAt, CanStandAtFromHere, IsUnitCellPassable, IsPassableForClass, CanLeaveCell | ai_map.cpp | pure (float / int bits; out params and reason compared) |
| 005B79D0, 005B80F0, 005BC3E0 | IsPassable, CanStandAt, LineOfSight | ai_map.cpp | pure (CanStandAt is in `verified.txt`) |
| 005B7A40 | IsPassableWithoutSelf | ai_map.cpp | ours only: equals IsPassable at the cell afterwards |
| 005BFC40, 005BA9D0 | SpiralCache::Get, FindFreeCellNear | ai_map.cpp | differential on a copy (return, cache, target bits) |
| 005BD350, 005B2D10, 005B2E10 | FootprintCellSet::SetSize, AddTop, AddBottom | ai_footprint.cpp | deep copy: original on a clone (own game-heap storage), ours on the real set; sets over 4096 cells ours only |
| 005B2F40 | FootprintCellSet::GetBounds | ai_footprint.cpp | output |
| 005B6A80 | CAIMap::MarkFootprint | ai_footprint.cpp | non-units: our CollectFootprint into scratch first, then the original runs and its sets are captured at ApplyFootprint / ApplyBridges, compared (padding byte skipped); units: ours only |
| 005B65B0, 005B7820, 005B78F0, 005B8B30, 005B8B80 | ApplyFootprint, StampFootprint, ClearFootprint, BeginRefresh, EndRefresh | ai_footprint.cpp | ours only (cells with low < high hold an occluder; owner stored; refreshUnits restored; batching flag / rectangle) |
| 005B2760, 005B2A00 | FootprintDiscAt (cloud), FootprintAlongDirection (wall) | ai_footprint_shapes.cpp | differential on the cell list (byte 0x13 and `next` skipped) |
| 005B6740 | ApplyBridgeDecks | ai_bridges.cpp | ours only inside the MarkFootprint spy (kind 4, ground >= deck, occluder bits 7) |
| 005B61B0, 005B6380, 005BFE70 | AddOccluderRecord, ClearOccluderRecord, NewOccluderRecord | ai_occluders.cpp | ours only: word state |
| 005B3C30, 005B5E10 | ConstructAIMap, ResetAIMap | ai_map_construct.cpp | ours only: refresh flag off, owner 0, inlined vtables and layer back pointers |
| 005B4150, 005B6070 | FillAreaLists, InitCostTables | ai_area_lists.cpp, ai_cost_tables.cpp | differential |
| 005B8FA0, 005B8BA0, 005B8710 | RefreshArea, CellMark, LimitSteepCells | ai_refresh.cpp | differential (RefreshArea runs twice: slow on big refreshes) |
| 005AF1C0, 005AF3D0, 005AF5F0, 005AF7B0, 005AF900, 005AFAE0, 005AFF10, 005B0050, 005B0370, 005B0600, 005B07C0, 005B0C00, 005B0D90, 005B0F70, 005B1180 | CRegionMap: BlockSearchPathTo, ctor, JoinBlockSearches, CellSuitsBlockAt, PickRepresentatives, MeasureBlockEdges, FloodRegionFrom, LabelRegions, NearbyBlocks, DominantRegion, ReachesRegion, Attach, FindRegionChain, ClampEdgeCosts, LoadBlocks | ai_region_reach.cpp | differential except the allocating / stateful ones (ctor, JoinBlockSearches, Attach, FindRegionChain, LoadBlocks: ours only) |
| 005B9C90 | CAIMap::PlanLongPath | ai_plan.cpp | ours only: a plan starts at the start cell, list linked |
| 005B9690, 005B9A20 | JoinLocalSearches, BuildCellsTo | ai_join.cpp | ours only / differential on x, y |
| 005BC750 | LargestStepAlong | ai_line_step.cpp | pure on the ST0 result (bits as double) |
| 005B7EF0 | StampWaterPatch | ai_water_patch.cpp | ours only: shared patch exists, holds a reference |
| 005AEA80 | StepAlarm | ai_alarm.cpp | ours only: pending / active / timer state |
| 005BD7F0 | CacheGet (object cache lookup, used for unit shapes) | ai_shape_cache.cpp | ours only |
| 005BAC10 | SnapTarget | ai_path.cpp | differential (result, target bits; with sameRegion both write the reachability marks) |
| 005BB350, 005BB900, 005BB190, 005B1B70 | FindPath, ContinuePath, AdvancePath, PathCellList::NewNode | ai_path.cpp | ours only: both lists consistent, goal copied, flag 6F cleared on success; reached == no cells left |
| 005B1DE0, 005B1F10, 005B2050, 005B2190 | stand orders | ai_path_stand.cpp | ours only: no cells, flags {1, 0, 0, 1}, target = unit's cell |
| 005B2430, 005B2570 | LoadPath, SavePath | ai_path_save.cpp | save: differential through a recording stream; load: ours only |
| 005C7430, 005C6FC0 | UnitMotion::Sample, SamplePath | ai_motion.cpp | original on a copy with scratch outputs; result, outputs and 0xA4 bytes compared |
| 005C67D0, 005C6750, 005C66C0, 005C6370, 005C6290 | Build, AssignCells, Subdivide, SubdivideSegment, LimitSpeeds | ai_motion.cpp | deep copy (own game-heap arrays), arrays and scalars compared |
| 005C6270, 005C73B0, 005C7410 | Clear, MotionCubic::Fit, Slope | ai_motion.cpp | copy / output / pure |
| 005C7B00, 005C7D50, 005B23A0, 005C7C00, 005C7C40, 005C7C20, 005C7C70, 005C7CB0, 005C7D00 | AIPath Start, Reset, Clear, SetTurn, SetTurnUntil, SetTrack, SetTarget, ShiftTime, Destination | ai_motion.cpp | original on a copy with empty lists, header / EAX compared |
| 005C7C90, 005C7CE0 | AIPath::ForgetObject, SetStepSpeeds | ai_motion.cpp | ours only |
| 005B7BB0, 005B8350, 005B9510, 005BA5A0 | BlockingUnitAt, UnitShape, CostContext, StandsOnCheapCell | ai_movement.cpp | pure / output (UnitShape: same shape object) |
| 005BA400 | CAIMap::SetStepSpeeds | ai_movement.cpp | original runs, its step words recorded and put back, ours, compared |
| 005BC970 | MarkVisibleCells | ai_movement.cpp | original marks a copy of the grid, ours the real one, compared |
| 005B36B0, 005B3890 | CUnitLayer::Add, Remove | ai_unit_layer.cpp | ours only: non-empty blocks / no nodes of the unit left |
| 005B39D0, 005B3A70, 005B3B20, 005B3B90 | small / large layer Set, Merge | ai_unit_layer.cpp | original first (its 16 map bytes recorded and restored), then ours |
| 005C35F0, 005C2F50, 005C2FB0, 005C3050, 005C3090, 005C1D50, 005C2D40, 005C30E0, 005C31A0, 005C3200 | LocalSearch Reset, PickCurrent, Requeue, Best, CostAt, StepCost, Expand, ExpandAll, ExpandUntil, PathTo | ai_local_search.cpp | in place: search header and node block recorded, original, recorded, put back, ours, compared (Expand / Requeue sampled after 512 calls); Best / CostAt / StepCost pure; PathTo x, y on a scratch list |
| 005C22A0, 005C21A0, 005C2420, 005C24F0, 005C2B10, 005C2C90, 005C2CF0, 005C2A30, 005C33F0, 005C3530, 005C1EA0, 005C25D0, 005B9580 | LocalSearch PlaceWindow, SetUnit, Begin, BeginForClass, BeginInRect(For, ForClass), CommitDirections, Save / RestoreLevels, Attach, Recenter; CAIMap::CostContextFor | ai_local_search.cpp | in place as above (CommitDirections / levels with the map bytes under the window); Attach ours only (fresh grid); SaveLevels' first (allocating) call ours only; CostContextFor output |
| 005BD200, 005BD2A0, 005C5DA0, 005C5E60, 005C6000, 005C5CC0 | PathCellList AddHead, AddTail, AppendList, Reversed, RemoveRepeats; PathCell::Step | ai_path_cells.cpp | ours only against a model of the list taken before (cells as 8-byte records, links checked); Reversed and Step: the original on scratch outputs |
| 005C46C0, 005C45D0, 005C44D0, 005C4C20 | PathSmoother Smooth, Advance, Relax; SmoothPool::SetSize | ai_path_smooth.cpp | Smooth: the original on a copy of the path, result and cells' x, y compared; Advance: pool and states after the original (pool put back) and ours; Relax: the original on a scratch state; SetSize ours only |
| 005AED60, 005AEE30, 005AEEC0, 005AEFF0, 005AF0C0, 005B0AF0, 005AFEC0 | BlockSearch Attach, Requeue, Relax, PickBest, Expand; CRegionMap SeedSearch, AttachSearches | ai_block_search.cpp | PickBest pure; the rest ours only (buckets sorted and linked, relaxed cost against a model, records reset) |
| 005AFC70, 005AFEE0 | CRegionMap::UpdateBlocks, UpdateBlocksFor (thunk) | ai_block_search.cpp | ours only (steps called through their addresses, verified on their own) |
| 005B89F0 | CAIMap::ClearUnmarkedArea | ai_refresh.cpp | differential on the cell words |
| 005C8D40 | VisibilityGrid::Grow (Include stays the forwarder) | ai_movement.cpp | ours only: covers the square, keeps exactly the old marks |
| 005C0930, 005C0BD0, 005C0DF0, 005C1010, 005C11D0 | CSectorMap: UnitsAt, FindUnitsInRadius, Add, Remove, Move | sector_map.cpp | UnitsAt pure; others in `verified.txt` |
| 005C7DB0, 005C7FC0 | AIPath::WriteState / ReadState (order and steps on a delta field) | ai_path_state.cpp | differential on field / reader copies (the read into a scratch path) |

## Original bugs and oddities kept on purpose

- Local search (`LocalSearch`, ai_map.h; CAIMap + 0x2594 / + 0x2614): `Requeue` leaves a node that is its bucket's head in
  place even when its cost changes. `PathTo` fills the cells' second dword from a stack slot it never writes; its only
  caller (JoinLocalSearches) leaves the forward search's address there (Best pushes ESI), so ours writes that address.
  `ExpandUntil` forms the target's row pointer without a bounds check (only compared). `Recenter` that fails on a
  move of 31 cells or more has already moved the window and row table (the nodes stay); its grid copy (005C4BA0) is inlined.
- Path smoother (CAIMap + 0x22CC, trellis of nine states per cell over the transition tables 007C3058, node pool 007C3038):
  the cells it rebuilds get second dwords from two stack slots the original never writes (the tail one, the stepped ones);
  ours give them the old tail's. Which value vanilla leaves depends on FindPath's earlier calls; the steering reads the
  last step's flags as its speed word (see BuildCellsTo). The first-cell climb test is `<= 0`, the later ones `< 0`.

- FindPath computes the fractional part of the snapped target twice from `targetX` (the second was presumably meant to be `targetY`), stores odd values in the second dword of single-cell path entries (the unit's y in
  half-cells minus 0.5 in one branch, min(|dx|, |dy|) in the other) before overwriting the low half with 0x200, and re-checks whether the unit is in the target cell in its local-search branch. SnapTarget divides 0/0
  (NaN steps) when the unit already stands in the target cell with sameRegion; the NaN propagates as in the original (FCOMPP / FCOM treat NaN as "less").
- MarkFootprint resets the scratch sets' buckets only when `hashed` is set, and nothing clears it; the new-cell record of AddTop / AddBottom has an uninitialised padding byte (ours 0; verify skips it), the wall / cloud
  shapes leave byte 0x13 and `next` as stack garbage (ours 0 and -1). CanStandAtFromHere reads the unit's own cell without a bounds check; LevelForSizeClass sets only AL for a class outside 0..7.
- LimitSteepCells clears the neighbour that broke the scan, or entry 4 of the table when none did, four times over. LargestStepAlong returns 1.0 for a NaN length like for zero (FCOMP sets C3). DominantRegion counts in a
  temporary CList in the original, ours in a local array of at most nine entries (heap traffic differs, not the result); ReachesRegion's 3x3 block list is a static CArray (007C2EE0) freed with plain delete every call;
  LabelRegions calls the out-of-line SetSize 005B16D0 instead of the inlined one.
- Sample kind 3 and SamplePath normalise angles with `while` loops that never end on NaN; UnitMotion::Build reads the first trajectory point even for a path with no steps; SamplePath compares the time with |turn / rate|
  twice (the inner test cannot fail); SetStepSpeeds and StandsOnCheapCell ask the unit for its movement speeds (0052F920) and never use them (ours skips the call). CUnitLayer::Add copies an uninitialised stack element
  into each new node and Remove passes its callback a record with an uninitialised first dword (ours zeros); small / large layer Set / Merge check only the block's first cell against the map size and write all 16.

## Skipped and why

- Run only by the static initialiser 005C42A0 (before the hooks): the smoothing tables 005C42B0 and its helpers 005C3690, 005C3840, 005C3A00,
  005C3E10, 005C4050, 005C4F30, 005C5930, 005C5AB0, 005C5A80.
- Shape cache helpers 005BCCB0, 005BE320, 005BE450, 005BE730 (STLport hash_map instantiations) are in config/library.csv (A-W7).
- CRegionMap has its members (ai_map.h: the two BlockSearch, the local search, `progress`, the per class grids).
- 005B43C0 CAIMap::Load (Rebirth-patched). 005BF8F0 / 005BF5F0 (spiral table constructor) run from the static initializer 005BF8E0, before the hooks are installed. 005C6E30 (waypoint constructor) is inlined. 005A0000-005AF000
  (AI helpers of other systems: voice, groups) and 005C8xxx / 005C9xxx above 005C8D40 (time wheel, containers) are not movement code.

## Risks

- Float order follows the instruction order; intermediates are `double` where the original keeps them on the FPU stack (identical at 24 or 53-bit precision, could differ in the last bit at 64-bit). `CollectFootprint` (every
  face of every part) is the most sensitive; its verify compares the full collected sets. MarkFootprint's verify depends on the ApplyFootprint / ApplyBridges hooks seeing the original's call (capture keyed on the map and
  set addresses); the AddTop / AddBottom deep copies slow object stamping in the verify build.
- `Sample` kind 3 calls the world's slot 0xA0/4 and the tracked object's slot 0xD0/4 or 0xD4/4 twice in the verify build (assumed free of side effects). Unit layers, `ForgetObject`, `SetStepSpeeds`, FindPath /
  PlanLongPath are verified by consistency checks only; `UnitShape`'s create path follows the original's helper calls one by one and the verify compares only the returned object.
