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
