# AI map and path search (0x005B0000-0x005C4000)

Code: `src/game/ai_map.*` (cell queries, heights, line of sight, free-cell search),
`src/game/ai_footprint.cpp` (footprint cell sets, MarkFootprint and refresh batching),
`src/game/ai_path.cpp` (target snapping, path search, path lists), `src/game/sector_map.*`.
Verify wrappers: `tests/verify/verify_ai_map.cpp`, `verify_ai_footprint.cpp`, `verify_ai_path.cpp`,
`verify_sector_map.cpp`.

## Structures recovered

All names provisional; offsets are `static_assert`ed in `ai_map.h`.

- **CAIMap** (the navigation grid, `this` of everything here):
  - `+0x001` `batching` (refreshes go to the dirty rectangle), `+0x002` `refreshUnits`,
    `+0x004..+0x010` dirty rectangle `minX, maxX, minY, maxY` (empty = 10000, 0, 10000, 0).
  - `+0x0D8` / `+0x0EC` climb cost tables `{int* costs; int count}` for size classes != 0 / == 0,
    indexed from the middle by the height step.
  - `+0x0FC` material records (0x118 bytes each), indexed by occluder word bits 0-4.
  - `+0x10C` STLport `hash_map<int, uint16>` of bridge deck heights, key `(short)x << 16 | (short)y`
    (buckets begin/end at +4/+8; node `{next, key, value}`).
  - `+0x120` height scale, `+0x124` embedded CSectorMap (0x28 bytes).
  - `+0x14C` / `+0x1A0` FootprintCellSet scratch: footprint and bridge parts of the object
    being stamped.
  - `+0x1F8` water depth rows (int8), `+0x27C` / `+0x12A4` unit occupancy layers (0x1028 bytes
    each, hash of 4x4 blocks; small/large units), `+0x22CC` path smoother, `+0x22F8` region map,
    `+0x2594` / `+0x2614` forward/backward local searches, `+0x2694` owner of the last stamped
    footprint, `+0x269C` cell rows, `+0x26A0/A4` width/height, `+0x26AC` occluder word rows,
    `+0x26B8`/`+0x26BC` reachability marks (rows of bytes: 0 unknown, 1 reaches, 2 does not).
- **AIMapCell** (9 bytes): passability nibble per size class, `+4` ground level, `+6` flags
  (bits 0-5: height of what stands on the ground, 0x40: under a bridge, 0x80: unknown), `+8` level.
- **Occluder word** (per cell): 0x100 none; 0x80 chain (bits 9-28 = first record index in the
  sector's record list at sector + 0x44); otherwise a single occluder: bits 5-6 transparency,
  9-11 kind (3 and 4 = bridge are tested), 12-21 high, 22-31 low. Chain records (8 bytes):
  `{int16 high, int16 low, int8 kind, int8 transparency, int16 next}`. (The previous LineOfSight
  code called high/low "bottom/top"; renamed, same logic.)
- **FootprintCellSet** (0x54): growable array of FootprintCell (0x18: x, y, low, high, kind,
  transparency, edge, pad, next) with 16 hash buckets on (x + y) & 15 and a `hashed` flag.
  Storage is 32-byte aligned, raw pointer stored at `cells[-1]`.
- **FootprintPart** (CEngineObject + 0x160, 0x38 bytes): flags (bits 3-5 kind, 6-9 transparency,
  0x400 bridge, 0x800 ignored), offset, box min/max, pivot. Part names come from the CStringArray
  at figure (+0x170) + 0x80, or the object's own at +0x110 when the figure's is empty. The object's
  transform matrix is at +0x54 (rotation from the quaternion at +0x34, translation = position).
- **AIPath** (the path object passed to FindPath): `+0x20` steps and `+0x3C` cells, both MFC
  `CList<PathCell>` (0x1C: vtable, head, tail, count, free, blocks, blockSize; node 0x10);
  `+0x58` target in half-cells, `+0x60` goal (world), `+0x6C` reached, `+0x6D` failed,
  `+0x6E/+0x6F` flags, `+0x84` smoothing result. PathCell: `{int16 x, y; uint16 flags (0x200 =
  single-cell path); uint16}`.
- **SpiralCache** (`{int length; float radius}`, the static at 007C2F48 in FindFreeCellNear): cache
  over the global spiral tables at 007C2FC4..007C2FFC.

Units: CUnit +0x324 size class, +0x32C standing height. Virtuals used: slot 0xDC/4 (non-zero:
skip unit layers, "dead"?), 0xD8/4 (large unit), 0xAC/4 on objects (refresh units around it).

## Functions rewritten

| Address | Name | Verification |
|---|---|---|
| 005B7B20 | LevelForSizeClass | pure, bit compare (int incl. upper bytes) |
| 005B7B70 | StandingLevel | pure |
| 005B3160 | BridgeHeight | pure |
| 005B77F0 | CAIMap::GroundHeight | pure (float bits) |
| 005BB960 | CAIMap::GroundHeightAt | pure |
| 005BBA10 | CAIMap::SurfaceHeightAt | pure |
| 005BBB20 | CAIMap::TopHeightAt | pure |
| 005BC2A0 | CAIMap::SlopeAt | pure |
| 005BBBD0 | CAIMap::IsBridgeAt | pure |
| 005BBC70 | CAIMap::CellFlag80At | pure |
| 005BBD10 | CAIMap::HasKind3Occluder | pure |
| 005BBDB0 | CAIMap::HasKind3OccluderAt | pure |
| 005BBE40 | CAIMap::MaterialAt | pure |
| 005BC1F0 | CAIMap::MaterialRecordAt | pure |
| 005B64E0 | CAIMap::BridgeDeckAt | pure (+ out param compared) |
| 005B81B0 | CAIMap::CanStandAtFromHere | pure (+ reason compared) |
| 005BA6E0 | CAIMap::IsUnitCellPassable | pure |
| 005BA790 | CAIMap::IsPassableForClass | pure |
| 005BA830 | CAIMap::CanLeaveCell | pure |
| 005B7A40 | CAIMap::IsPassableWithoutSelf | stateful (sector map remove/add): ours only, result must equal IsPassable at the cell afterwards (a unit's own footprint is in the unit layers, not the cells) |
| 005BFC40 | SpiralCache::Get | output: original on a copy of the cache; return, length and cache compared |
| 005BA9D0 | CAIMap::FindFreeCellNear | output: original into a copy of the target; result and target bits compared |
| 005BD350 | FootprintCellSet::SetSize | deep copy: original on a cloned set (own game-heap storage), ours on the real one; header, buckets and the kept cells compared |
| 005B2D10 | FootprintCellSet::AddTop | deep copy as above (sets over 4096 cells run ours only) |
| 005B2E10 | FootprintCellSet::AddBottom | deep copy as above |
| 005B2F40 | FootprintCellSet::GetBounds | output: original into scratch bounds |
| 005B6A80 | CAIMap::MarkFootprint | non-units: our CollectFootprint into scratch sets first, then the original runs and its sets are captured where it applies them (ApplyFootprint wrapper, ApplyBridges spy); cells (minus padding byte) and buckets compared. Units: ours only. |
| 005B65B0 | CAIMap::ApplyFootprint | ours only; after a successful stamp every in-map cell with low < high must hold an occluder (word bit 0x100 clear) |
| 005B7820 | CAIMap::StampFootprint | ours only; owner stored and refreshUnits restored |
| 005B78F0 | CAIMap::ClearFootprint | ours only; refreshUnits restored |
| 005B8B30 | CAIMap::BeginRefresh | ours only; batching on, rectangle reset |
| 005B8B80 | CAIMap::EndRefresh | ours only; batching off |
| 005BAC10 | CAIMap::SnapTarget | output: original on a copy of the target, then ours; result and target bits compared. With sameRegion both runs write the reachability marks (each clears its box first, so the second run sees the same state) |
| 005BB350 | CAIMap::FindPath | ours only; both path lists consistent (links, tail, count), goal copied, flag6F cleared on success |
| 005BB900 | CAIMap::ContinuePath | ours only; list consistency |
| 005BB190 | CAIMap::AdvancePath | ours only; list consistency, reached == (no cells left) |
| 005B1B70 | PathCellList::NewNode | ours only; links and count |

Also renamed in Ghidra (forwarders): CRegionMap::RegionAt 005B0600, CRegionMap::Reaches 005B07C0,
CAIMap::ApplyBridges 005B6740, AddOccluder 005B61B0, ClearOccluder 005B6380, Refresh 005B8FA0,
UnitShape 005B8350, CUnitLayer::Add/Remove 005B36B0/005B3890, CAIMap::PlanPath 005B9C90,
BuildLocalPath 005B9690, SteerAlong 005BA400; and the earlier CanStandAt, IsPassable, LineOfSight
and CSectorMap methods.

## Forwarders added (EI_ORIGINAL; the standalone build must replace these)

- In range, not rewritten: 005B0600 RegionAt, 005B07C0 Reaches, 005B6740 ApplyBridges,
  005B61B0 AddOccluder, 005B6380 ClearOccluder, 005B8FA0 Refresh, 005B8350 UnitShape,
  005B36B0 / 005B3890 unit layer add/remove, 005B9690 BuildLocalPath, 005B9C90 PlanPath (A*),
  005BA400 SteerAlong, 005BD200 AddPathCell, 005C2420 / 005C35F0 local search and its reset.
  (005B8350, 005B36B0 / 005B3890 and 005BA400 were rewritten in the second pass below; the
  forwarders now land on those rewrites.)
- Outside the range: 004D9AA0 quaternion to matrix, 00560E10 matrix * vector, 004698A0
  CStringArray element copy, 00517990 object FindPart, 0040F560 facing direction of a quaternion,
  00539980 ConstructElements<PathCell>, 00556D40 CList::RemoveAll, 005C46C0 path smoothing,
  005C7D50 / 005C7B00 / 005C7C70 AIPath Reset / Start / SetTarget, 006E727B atexit (for the
  function-local static of FindFreeCellNear, destructor 005BAC00).
- 005C0DF0 / 005C1010 / 005B6A80: our own CSectorMap::Add/Remove and MarkFootprint are called
  through their original addresses from sector_map.cpp and ai_path.cpp, so the verify build's
  wrappers see those calls (in the release build that jump lands on our code).
- A local FPATAN helper (ai_path.cpp) for FindPath's facing octant.

## Original bugs and oddities kept on purpose

- FindPath computes the fractional part of the snapped target twice from `targetX` (the second
  was presumably meant to be `targetY`), then picks the smaller of two equal values.
- FindPath stores garbage-ish values in the second dword of single-cell path entries: the unit's
  y in half-cells minus 0.5 (float bits) in one branch, min(|dx|, |dy|) in the other, before
  overwriting the low half with 0x200. Reproduced.
- FindPath's local-search branch checks again whether the unit is in the target cell although that
  case was handled before it.
- MarkFootprint resets the scratch sets' buckets only when `hashed` is set, and nothing ever clears
  `hashed`.
- CanStandAtFromHere reads the unit's own cell without a bounds check.
- SnapTarget divides 0/0 (NaN steps) when the unit already stands in the target cell with
  sameRegion; the NaN propagates exactly as in the original (FCOMPP / FCOM treat NaN as "less").
- LevelForSizeClass returns only AL = 0 for a class outside 0..7 (EAX keeps the argument's upper
  bytes); reproduced.
- The new-cell record in FootprintCellSet::AddTop/AddBottom has an uninitialised padding byte in
  the original (we write 0); verify comparisons skip it.

## Skipped and why

- 005B43C0 CAIMap::Load: patched by Rebirth; not attempted.
- 005B8FA0 Refresh (passability recomputation, calls 005B8BA0, 005B89F0, 005B8710, the sector
  map and 005AFEE0), 005B9C90 PlanPath (1904 bytes, A*), 005B6740 ApplyBridges, 005B61B0 /
  005B6380 (occluder record allocation in the sector lists via 005BFE70 / 005C0060), 005B0600 /
  005B0370 region lookup (drives the region search at 005C2CF0..): large and stateful, left as
  forwarders; good next candidates are 61B0/6380 (with a grid snapshot verify) and 0600.
- The occluder-record and region-search code at 005BF..-005C3..: not reached.

## Risks

- Float order: written to match the instruction order; intermediate precision assumes the game's
  FPU control word (single precision under D3D, as the existing LineOfSight already relies on).
  CollectFootprint (sampling every face of every model part) is the most sensitive; its verify
  compares the full collected sets against the original's.
- MarkFootprint's verify depends on the ApplyFootprint and ApplyBridges hooks seeing the
  original's call; the capture is keyed on the AI map and set addresses.
- The verify build's deep-copy checks for AddTop/AddBottom make object stamping slower (a copy
  per sample, capped at 4096 cells).

# Motion, movement checks and unit layers (0x005A0000-0x005D0000, second pass)

Code: `src/game/ai_motion.*` (the AIPath order header and the unit trajectory),
`src/game/ai_movement.cpp` (blocking unit, step speeds, cheap-cell test, visible cells, unit
shapes), `src/game/ai_unit_layer.*` (unit occupancy layers), `CSectorMap::UnitsAt` in
`sector_map.cpp`. Verify wrappers: `tests/verify/verify_ai_motion.cpp`, `verify_ai_movement.cpp`,
`verify_ai_unit_layer.cpp`, `verify_sector_map.cpp`. Scenario: `tests/scenarios/movement.txt`.

## How a unit moves (the per-tick path)

The unit movement tick 00547890 (controller `this`, outside the range) does, for a moving unit:
`UnitMotion::Sample` (005C7430) on the unit's trajectory at CUnit + 0x27C with the controller's
order (controller + 0x3C, the AIPath) and the world time -> next position, direction and speed;
`CSectorMap::Remove`; `CanStandAtFromHere` at the new cell; `BlockingUnitAt` (005B7BB0) when it
cannot stand there; `CSectorMap::Add`; then `CSectorMap::Move` (patched by Rebirth), which lifts and
re-stamps the unit through `MarkFootprint` -> `UnitShape` (005B8350) and the unit layers
(005B36B0 / 005B3890, whose virtual callbacks write the cell levels). When the path needs new
steps, `ContinuePath` / `AdvancePath` refill them and `SetStepSpeeds` (005BA400) prices them;
0052E9D0 then rebuilds the trajectory with `UnitMotion::Build` (005C67D0). 00548520 at the end of
the tick asks `StandsOnCheapCell` (005BA5A0).

## Structures recovered

Names provisional; offsets `static_assert`ed in the headers.

- **MotionOrder** (the first 0x20 bytes of AIPath): kind (0 stand at (x, y) facing `angle`, 1 turn
  to direction arg5 at rate arg6, 2 follow the steps to target (arg5, arg6), 3 track object arg5
  between times arg6..arg7, 4 turn to direction arg5), x, y (half-cells), angle, start time.
  AIPath + 0x74 is a buffer freed by `AIPath::Clear`.
- **UnitMotion** (CUnit + 0x27C, 0xA4 bytes): three MFC CArrays — trajectory points (0x1C: x, y,
  direction, speed, path cell), waypoints (0x0C: x, y, number of path cells up to it) and step
  speeds (float, the steps' speed words); +0x3C speed scale, +0x40 base turn rate, +0x44 turn rate,
  +0x48 start angle, +0x4C turn to make first; +0x50..+0x74 the cached segment (ends, directions,
  length, speed); +0x78 / +0x88 cubic Hermite coefficients for x and y; +0x98 / +0x9C segment
  start/end time; +0xA0 segment index (-1 none).
- **Trajectory build**: waypoints at the order start, at corners of the step path (direction table
  00791988, indexed by dy * 3 + dx from its middle; a corner is a direction change of more than 45
  degrees; cardinal moves add the cell corner, diagonal moves at corners add an edge midpoint), and
  at the target; each segment is split into one point per path cell (the first starts half a cell
  early, the last ends half a cell late), speed = step speed * scale / 512; points slow down where
  turning to the next direction takes longer than the way (`LimitSpeeds`); the unit first turns in
  place unless the first direction is within acos(0.9) of its facing.
- **Sampling**: time before the turn time -> turning in place (`turning` set, only the facing
  changes); then the segments are walked (time accumulated unrounded on the FPU, as the original)
  and the cached segment is fitted with two Hermite cubics in the segment's length parameter.
- **CUnitLayer** (CAIMap + 0x27C small units, + 0x12A4 large units; 0x1028 bytes): vtable, an MFC
  CList holding every node ever allocated, 1024 hash buckets by (block x * 17 + block y), a free
  list, the owning CAIMap at +0x1024. Nodes (0x1C): next, key (block y << 16 | block x), 4x4 signed
  levels, owner unit. Slot 0 (Set) / slot 1 (Merge) write the cells' `level` byte (small layer,
  unsigned max) or the signed bytes at CAIMap + 0x1F8 (large layer, signed max) — so +0x1F8 is not
  only water depth; the name `waterDepth` is kept until its other writers are known.
- **PassCostContext** (005B9510, 0x18): cost table (CAIMap + 0x14, per passability nibble), climb
  table middle, nibble shift, three constants (0.1, 10, 0.1). CAIMap + 0x94 is a second per-nibble
  table used for step speeds.
- **VisibilityGrid** (0x18): origin, data, row pointers, width, height; grown by 005C8D40.
- **Shape cache** (007C2F70 owner, STLport hash_map at 007C2F78): unit occupancy shapes keyed by
  standing height rounded up to tenths; a shape's 16x16 signed levels start at + 8.

## Functions rewritten

| Address | Name | Verification |
|---|---|---|
| 005C7430 | UnitMotion::Sample (every tick, moving units) | original on a copy of the motion object with scratch outputs; result, all outputs and the 0xA4 bytes compared |
| 005C6FC0 | UnitMotion::SamplePath | same |
| 005C67D0 | UnitMotion::Build | deep copy (own game-heap arrays): original on the copy, ours on the real one; arrays (size, capacity, growth, elements) and scalars compared |
| 005C6750 | UnitMotion::AssignCells | deep copy, as Build |
| 005C66C0 | UnitMotion::Subdivide | deep copy |
| 005C6370 | UnitMotion::SubdivideSegment | deep copy, step index compared |
| 005C6290 | UnitMotion::LimitSpeeds | deep copy |
| 005C6270 | UnitMotion::Clear | copy; bytes and EAX (0) compared |
| 005C73B0 | MotionCubic::Fit | output: original into a copy, 16 bytes compared |
| 005C7410 | MotionCubic::Slope | pure (double) |
| 005C7B00 | AIPath::Start | original on a copy of the path with empty lists; result, header and the real steps list (emptied) checked |
| 005C7D50 | AIPath::Reset | same; EAX (0) compared |
| 005B23A0 | AIPath::Clear | copy with empty lists and no buffer; all 0x88 bytes compared |
| 005C7C00 / 005C7C40 / 005C7C20 / 005C7C70 / 005C7CB0 | AIPath::SetTurn / SetTurnUntil / SetTrack / SetTarget / ShiftTime | original on a copy; header compared |
| 005C7C90 | AIPath::ForgetObject | ours only (3-byte prologue before a JNZ): arg5 cleared exactly when kind 3 tracks the id |
| 005C7CE0 | AIPath::SetStepSpeeds | ours only: every step carries the speed, EAX 0 |
| 005C7D00 | AIPath::Destination | output; vector and returned pointer compared |
| 005B7BB0 | CAIMap::BlockingUnitAt (every tick when a unit cannot stand at its next cell) | pure (temporary list; the shape cache fill is idempotent) |
| 005B8350 | CAIMap::UnitShape (every unit footprint stamp) | pure (the original may create the cache entry, ours then finds it): same shape object |
| 005B9510 | CAIMap::CostContext | output; struct and EAX compared |
| 005BA400 | CAIMap::SetStepSpeeds (was the `SteerAlong` forwarder) | the original runs, its step words are recorded and put back, ours runs, words compared |
| 005BA5A0 | CAIMap::StandsOnCheapCell (every movement tick) | pure (AL) |
| 005BC970 | CAIMap::MarkVisibleCells | the original marks a copy of the grid with its own buffers, ours the real one; origin, size and marks compared |
| 005B36B0 | CUnitLayer::Add | ours only (it calls the layer's virtual callbacks): each non-empty 4x4 block of the placed shape is the bucket head with the unit, the key and exactly those levels |
| 005B3890 | CUnitLayer::Remove | ours only: none of the unit's nodes left in the 5x5 blocks |
| 005B39D0 / 005B3A70 | small layer Set / Merge (vtable 0073FD74 slots 0 / 1) | original first, its 16 map bytes recorded and restored, then ours; bytes compared |
| 005B3B20 / 005B3B90 | large layer Set / Merge (vtable 0073FD6C) | same |
| 005C0930 | CSectorMap::UnitsAt | pure |

None of these is in docs/rebirth-patched-functions.txt; the only patched function in the area that
our code reaches is CSectorMap::Move (005C11D0), already rewritten earlier and installed only in the
vanilla run. The callbacks are virtual: registered as free `__fastcall` functions at their addresses
(rule 4), and called through the vtable (`ei::VCall`).

Shared headers changed (additive except where noted): `ai_map.h` — AIPath gained the order methods
(`Reset` now returns int 0 and `Start` returns bool, both as the original's EAX; the forwarders in
`ai_path.cpp` were removed), CAIMap gained `BlockingUnitAt`, `SetStepSpeeds`, `CostContext`,
`StandsOnCheapCell`, `MarkVisibleCells` (the `UnitShape` declaration moved to that block), and the
structs `PassCostContext` and `VisibilityGrid`; `sector_map.h` — `CSectorMap::UnitsAt`.
`ai_footprint.cpp` now includes `ai_unit_layer.h` (its local `struct CUnitLayer;` was removed) and
no longer defines the `UnitShape` forwarder.

## Forwarders added (EI_ORIGINAL; the standalone build must replace these)

- CArray template instantiations of the trajectory: 005C8400 (waypoints Add), 005C87F0 (waypoints
  SetAtGrow), 005C8950 (step speeds SetAtGrow), 005C8560 (points SetSize).
- 004E5660 cubic value (outside the range).
- 005BE7B0 CList<UnitLayerNode>::NewNode.
- Shape cache: 005BCCB0 handle init, 005BD7F0 cache Get (used inline by BlockingUnitAt), 005BE320
  hash find, 005BE450 iterator init, 005BE370 / 005BE280 pair construct / destroy, 005BE730 insert,
  and the cache object's slot 0 (create) through `ei::VCall`.
- 005B8BA0 cell passability at a level (StandsOnCheapCell), 005C8D40 VisibilityGrid::Include.
- Called through original addresses so verify wrappers see them: 005C0BD0 FindUnitsInRadius,
  005BC3E0 LineOfSight (its ST0 is taken unrounded), and the existing 005BA400 / 005B36B0 /
  005B3890 forwarders in `ai_path.cpp` / `ai_footprint.cpp`.
- Globals by address: direction table 00791988, class heights 007912BC, shape cache 007C2F70 /
  007C2F78, list vtable 0073D454.

## Original bugs and oddities kept on purpose

- `Sample` kind 3 and `SamplePath` normalise angles with `while` loops that never end on NaN
  (the original's FCOM treats NaN as "less than -pi"); reproduced.
- `UnitMotion::Build` reads the first trajectory point even when there is none (a path with no
  steps); same in ours.
- `SamplePath` compares the time with |turn / rate| twice before the path, the second time with
  a rate just copied from the first: the inner test cannot fail.
- `SetStepSpeeds` and `StandsOnCheapCell` ask the unit for its movement speeds (0052F920) and never
  use them (SetStepSpeeds also divides by one); the getter has no side effects, so ours skips it.
- `CUnitLayer::Add` copies an uninitialised stack element into each newly allocated node, and
  `Remove` passes its callback a record whose first dword is uninitialised; ours uses zeros (the
  fields are overwritten / never read).
- The small layer's Set/Merge and the large layer's check only the block's first cell against the
  map size and write all 16.

## Skipped and why

- 005B7EF0 (unit water shape add/remove, a function-local static allocated with atexit),
  005B8BA0 (passability of a cell at a level, 1 KB, also used by Refresh), 005C8D40 (grid growth),
  the 005C1xxx-005C46xx local search / path smoothing (005C2420, 005C46C0, 005C25D0 ...), the
  region map (005AFxxx-005B0xxx), 005B43C0 Load (Rebirth-patched) and the 005A0000-005AF000 range
  (AI helpers of other systems: voice, groups) were not reached in this pass. 005C8xxx/005C9xxx
  above 005C8D40 (time wheel, misc containers) are not movement code.
- 005C6E30 (waypoint constructor) is inlined; it has no other callers worth hooking.

## Risks

- The trajectory code keeps x87 intermediates as `double`: identical when the FPU runs at 24 or
  53 bits; at 64-bit precision (Rebirth may raise it) a spill of an intermediate could differ in
  the last bit. The deep-copy verifies compare every float bit of the trajectory, so drift would
  show up as mismatches.
- `Sample` kind 3 calls the world's slot 0xA0/4 and the tracked object's slot 0xD0/4 or 0xD4/4 for
  the original and again for ours in the verify build; assumed free of side effects.
- The unit layers and `ForgetObject` / `SetStepSpeeds` are verified only by consistency checks.
- `UnitShape`'s create path (first use of a height) follows the original's helper calls one by
  one; the verify compares only the returned object (the original runs first and creates it).


# AI map, region map and footprint shapes (0x005A0000-0x005C0000, worker 7)

Rewritten (all hooked; verify wrappers in tests/verify):

- 005B9C90 CAIMap::PlanLongPath (ai_plan.cpp): the long-range planner (region chain, then local
  searches per leg). The old `PlanPath` forwarder in ai_path.cpp stays. Verify: differential on
  the returned path.
- 005B8FA0 CAIMap::RefreshArea (ai_refresh.cpp; the `Refresh` forwarder stays): per level the
  cell marks over the rectangle grown by 3, packed as nibbles into the cells, the level's special
  areas (005B89F0), the steep-step pass, the three border rows/columns cleared, and with
  `refreshUnits` the objects around it taken out of the sector map around the region update
  (005AFEE0). Verify: differential (ours re-runs from the state the original left; every value it
  writes is recomputed from the terrain).
- 005B8BA0 CAIMap::CellMark: a cell's mark for a level and its height, from terrain type, water
  depth and the occluders (cost and damping through an exp, transcribed as naked x87). Verify:
  differential on a copy of the scratch state.
- 005B8710 CAIMap::LimitSteepCells: the second pass of Refresh. The largest climbable step of a
  climb table is the run of non-negative costs from its middle, less one. A cell whose step to a
  direct neighbour (table 007916BC, {dy, dx}) exceeds size class 0's becomes impassable; one that
  exceeds the larger classes' keeps only nibble 0; either way one neighbour's nibbles 5-6 and
  nibble 7 of 13 cells around are cleared. Quirk kept: the neighbour whose nibbles 5-6 are cleared
  is the one that broke the scan, or entry 4 of the table (the first of the 13) when none did; the
  original clears it four times over. Verify: differential (snapshot, original, restore, ours).
- 005B07C0 CRegionMap::ReachesRegion (ai_region_reach.cpp; the `Reaches` forwarder stays): see
  the file comment. The 3x3 block list is a function-local static CArray (007C2EE0) freed with
  plain delete every call. Verify: differential over the marks window.
- 005B0050 CRegionMap::LabelRegions: per size class the region grid cleared and every unlabelled
  block flooded (005AFF10) with the next label from a static queue (007C2F10; 007C2EF8 the
  flood's second list). The original inlines the queue's CArray::SetSize; ours calls the
  out-of-line instance 005B16D0 (same logic). Verify: differential on all 8 grids.
- 005B2A00 / 005B2760 the wall and cloud footprints (ai_footprint_shapes.cpp): a disc around the
  point (scaled by 2 to cells, radius grown by one cell; the radius is the direction's length for
  the wall) limited, for the wall, by (cy - y) * ny + (cx - x) * nx < 1 with n the side normal
  over (its length * (2 * width + 1)). Values the original keeps on the FPU are doubles. Quirk:
  the appended cell's byte 0x13 and `next` are stack garbage in the original (ours 0 and -1;
  nothing reads them, the cells are in no bucket). Verify: differential on the cell list.

Risks: RefreshArea's verify runs the refresh twice (slow on big refreshes). The footprint shapes
and CellMark depend on the FPU precision assumption above.


## Second batch (worker 7)

Rewritten, hooked, with verify wrappers:

- 005B6740 CAIMap::ApplyBridgeDecks (ai_bridges.cpp; `ApplyBridges` stays the forwarder). The
  bridge map's node value is written as a whole dword: low word the saved ground, byte 2 the
  object height, byte 3 the occluder kind (005B30B0 restores from it). Verify: ours only inside
  the MarkFootprint spy (kind 4; applied cells under a bridge, ground >= deck, occluder bits 7).
- 005B61B0 / 005B6380 CAIMap::AddOccluderRecord / ClearOccluderRecord (ai_occluders.cpp): the
  occluder word layout is in the file comment; a removal that leaves one record moves it back
  inline (clearing bit 7 with the transparency) and frees it. Verify: ours only, word state.
- 005B3C30 / 005B5E10 the CAIMap constructor and reset (ai_map_construct.cpp), 005B4150 the area
  lists from their data tables (ai_area_lists.cpp, differential).
- 005B0370 CRegionMap::NearbyBlocks (differential into a scratch list), 005B0F70 ClampEdgeCosts
  (snapshot/restore differential), 005B1180 LoadBlocks (ours only), 005AF900
  PickRepresentatives (differential), 005B0D90 FindRegionChain (ours only: both block searches
  finished), all in ai_region_reach.cpp.
- 005BC750 CAIMap::LargestStepAlong (ai_line_step.cpp): pure, bit-exact differential on the
  ST0 result. A NaN length returns 1.0 like the zero length (FCOMP sets C3 when unordered).
- 005B9690 CAIMap::JoinLocalSearches (ai_join.cpp): ours only.

Skipped: 005BF8F0 (the spiral table's constructor) and 005BF5F0 (called only from it) run from
the static initializer 005BF8E0, before the hooks are installed.


## Third batch (worker 7)

- 005B7EF0 CAIMap::StampWaterPatch (ai_water_patch.cpp): the water depths around a point as a
  pseudo unit (key ((y + 0x800) << 16) + x) of the small-unit layer; a shared reference-counted
  16x16 shape (static 007C2F64). Verify: ours only.
- 005B0600 CRegionMap::DominantRegion: the region most of the reachable 3x3 blocks belong to. The
  original counts in a temporary CList (plex nodes); ours counts in a local array of at most nine
  entries, so its heap traffic differs, not its result. Verify: differential on the whole EAX.
- 005AF5F0 CRegionMap::JoinBlockSearches, 005AF1C0 BlockSearchPathTo, 005AFAE0
  MeasureBlockEdges (snapshot/restore differential), 005B0C00 Attach, 005AF3D0 the region map's
  constructor, all in ai_region_reach.cpp. BlockSearchPathTo and CAIMap::BuildCellsTo (005B9A20,
  ai_join.cpp) give each path cell a second dword the original takes from a stack slot it never
  writes: ours writes 0; their differentials compare x and y only.
- 005AEA80 StepAlarm (ai_alarm.cpp): the alarm state {level, pending, active, timer}; the AI log
  lines ("Unit <id> acknowledges with ...") and remarks are kept. Verify: ours only.

Library: 005AD680 (STLport vector<pair<string, int>>::_M_insert_overflow).
