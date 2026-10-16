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
  and `CAIMap::Load` (005B43C0, `ai_map_load.cpp`, A-W15), both installed only in the vanilla run.

## Rewritten

Every function carries its address in its source comment (grep the address); the per-function table that used to be here is gone.
By file:
- `ai_map.cpp`: cell queries and heights (LevelForSizeClass 005B7B20, StandingLevel, BridgeHeight, GroundHeight(At), SurfaceHeightAt,
  TopHeightAt, SlopeAt, MaterialAt, CanStandAtFromHere 005B64E0, IsUnitCellPassable, IsPassableForClass, CanLeaveCell), IsPassable
  005B79D0, IsPassableWithoutSelf 005B7A40, CanStandAt 005B80F0, LineOfSight 005BC3E0, SpiralCache::Get 005BFC40, FindFreeCellNear
  005BA9D0. `ai_footprint*.cpp`: FootprintCellSet, MarkFootprint 005B6A80, Apply / Stamp / ClearFootprint, Begin / EndRefresh, the
  cloud and wall shapes; `ai_bridges.cpp` ApplyBridgeDecks 005B6740 (kind 4, ground >= deck, occluder bits 7); `ai_occluders.cpp`;
  `ai_map_construct.cpp` (ConstructAIMap 005B3C30, ResetAIMap 005B5E10: refresh flag off, owner 0, inlined vtables and layer back
  pointers); `ai_area_lists.cpp`, `ai_cost_tables.cpp`; `ai_refresh.cpp` (RefreshArea 005B8FA0, CellMark, LimitSteepCells,
  ClearUnmarkedArea 005B89F0); `ai_water_patch.cpp` StampWaterPatch 005B7EF0; `ai_alarm.cpp` StepAlarm 005AEA80; `ai_shape_cache.cpp`
  CacheGet 005BD7F0 (object cache lookup for unit shapes); `ai_line_step.cpp` LargestStepAlong 005BC750.
- `ai_region_reach.cpp` CRegionMap 005AF1C0-005B1180 (BlockSearchPathTo, JoinBlockSearches, FloodRegionFrom, LabelRegions,
  DominantRegion, ReachesRegion, FindRegionChain, ...); `ai_block_search.cpp` (BlockSearch Attach / Requeue / Relax / PickBest /
  Expand 005AED60-005AF0C0, SeedSearch, AttachSearches, UpdateBlocks 005AFC70).
- Paths: `ai_path.cpp` (SnapTarget 005BAC10, FindPath 005BB350, ContinuePath 005BB900, AdvancePath 005BB190, PathCellList::NewNode),
  `ai_path_stand.cpp` (stand orders: no cells, flags {1, 0, 0, 1}, target = the unit's cell), `ai_path_save.cpp` (LoadPath 005B2430,
  SavePath 005B2570), `ai_plan.cpp` PlanLongPath 005B9C90, `ai_join.cpp` JoinLocalSearches 005B9690 / BuildCellsTo 005B9A20,
  `ai_local_search.cpp` (LocalSearch 005C1D50-005C35F0, CostContextFor 005B9580), `ai_path_cells.cpp` (PathCellList 005BD200-005C6000,
  PathCell::Step 005C5CC0), `ai_path_smooth.cpp` (PathSmoother Smooth 005C46C0 / Advance 005C45D0 / Relax 005C44D0, SmoothPool::SetSize
  005C4C20), `ai_path_state.cpp` (AIPath::WriteState / ReadState 005C7DB0 / 005C7FC0, the order and steps on a delta field).
- Motion: `ai_motion.cpp` (UnitMotion::Sample 005C7430, SamplePath 005C6FC0, Build 005C67D0, AssignCells, Subdivide(Segment),
  LimitSpeeds, MotionCubic::Fit, AIPath Start / Reset / Clear / SetTurn / SetTurnUntil / SetTrack / SetTarget / ShiftTime /
  Destination / ForgetObject, UnitMotion::LoadState / SaveState 005C7860 / 005C79B0 = the unit save at CUnit + 0x27C),
  `ai_movement.cpp` (BlockingUnitAt 005B7BB0, UnitShape 005B8350, CostContext 005B9510, StandsOnCheapCell 005BA5A0,
  CAIMap::SetStepSpeeds 005BA400, MarkVisibleCells 005BC970, VisibilityGrid::Grow 005C8D40 (Include stays the forwarder)),
  `ai_unit_layer.cpp` (CUnitLayer Add 005B36B0 / Remove 005B3890, small / large Set / Merge 005B39D0-005B3B90).
- `sector_map.cpp`: CSectorMap Init 005C06F0, UnitsAt 005C0930, FindUnitsInRadius 005C0BD0, Add 005C0DF0, Remove 005C1010, Move 005C11D0.
  `ai_events.cpp`: AIEventTable (CWorldServer + 0x2838, unit_ai.h) Advance 005C9530 (the time wheel's tick), ForgetUnit 005C97B0 (a dead
  unit's events), Load / Save 005CAE30 / 005CAFA0; AIEvent records come from the pool CList 007C43A8 and the free list 007C53F0.

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

- 005C9310 (the event table's add, Rebirth-patched: ResOverride). 005D7970 (the fear order's operator=: only the original 005D30A0 calls
  it; our Fear inlines it). 005C6E30 (waypoint constructor) is inlined. 005A0000-005AF000 (AI helpers of other systems: voice, groups) and
  005C8xxx / 005C9xxx above 005C8D40 (time wheel, containers) are not movement code.
- Run only by static initialisers (before the hooks), ported and compared once at start-up with what the original built
  (`EI_STARTUP_CHECK`, verify_ai_static_tables.cpp; world.md "Rewritten"): the smoothing tables 005C42B0 with 005C3690, 005C3840,
  005C3A00, 005C3E10, 005C4050 (`ai_path_smooth.cpp`), the spiral 005BF8F0 / 005BF5F0 (`sector_map.cpp`), the shape cache 005B32E0 /
  005B31F0 (`ai_shape_cache.cpp`). 005C4F30, 005C5930, 005C5AB0, 005C5A80 are MFC and the shape cache helpers 005BCCB0, 005BE320,
  005BE450, 005BE730 STLport hash_map instantiations (config/library.csv).

## Risks

- Float order follows the instruction order; intermediates are `double` where the original keeps them on the FPU stack (identical at 24 or
  53-bit precision, could differ in the last bit at 64-bit). `CollectFootprint` (every face of every part) is the most sensitive.
- `Sample` kind 3 calls the world's slot 0xA0/4 and the tracked object's slot 0xD0/4 or 0xD4/4 twice in the verify build (assumed free of
  side effects). Unit layers, `ForgetObject`, `SetStepSpeeds`, FindPath / PlanLongPath were checked for consistency only; `UnitShape`'s
  create path follows the original's helper calls one by one.