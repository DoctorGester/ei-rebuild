# AI map, path search, unit motion

Code (layouts and per-function comments are in `ai_map.h`, `ai_motion.h`, `ai/unit_shape.h`, `sector_map.h`; each
file's header says what it does): `ai_map*.cpp` (cell queries, heights, line of sight, construction, load),
`ai_footprint*.cpp`, `ai/ai_footprint.cpp`, `ai/ai_footprint.cpp`, `ai_refresh.cpp`, `ai_region_reach.cpp`,
`ai_block_search.cpp`, `ai_local_search.cpp`, `ai_path*.cpp`, `ai_movement.cpp`, `ai_motion.cpp`, `ai_unit_layer.cpp`,
`ai_events.cpp`, `sector_map.cpp`. Scenario: the `movement` section of `tests/scenarios/batch_units_death.txt` (the
map functions also run in every scenario).

## How a unit moves

The unit movement tick (combat.md) for a moving unit: `UnitMotion::Sample` on the unit's trajectory with the
controller's order (the AIPath) and the world time gives the next position, direction and speed; `SectorMap::Remove`;
`CanStandAtFromHere` at the new cell; `BlockingUnitAt` when it cannot stand there; `SectorMap::Add`; then
`SectorMap::Move`, which lifts and re-stamps the unit through `MarkFootprint` -> `UnitShape` and the unit layers (whose
Set / Merge write the cell levels). When the path needs new steps `ContinuePath` / `AdvancePath` refill them and
`SetStepSpeeds` prices them; the unit's RefreshMotion then rebuilds the trajectory with `UnitMotion::Build`. At the end
of the tick `StandsOnCheapCell`. Long paths: `PlanLongPath` (region chain, then local searches per leg) ->
`BuildCellsTo`.

## The map archive

`AIMap::Load` (`ai_map_load.cpp`): a map archive holds one .mp file (MapHeader, the materials, a type word per tile
texture, the animated tiles) and a .sec file per sector of 32 x 32 terrain squares (SectorHeader, 33 x 33 vertices, the
same again for the water when the sector has any, then 16 x 16 tile words, the water's tile words and the water's
material indices). The AI map has 2 x 2 cells per terrain square and 4 x 4 per tile: 64 x 64 cells per sector.
LoadTerrain reads the files, AttachTerrain does the rest.

## Regions

The region map keeps, per size class, a u16 region per 8x8 block and the block's representative cell (rows of bytes: low
nibble x, high nibble y in the block). `ReachesRegion` asks whether cell (x, y) can reach a region: when none of the 3x3
blocks around the cell's block is in the region, the cell's block is marked unreachable (2) in `marks`; otherwise the
local search floods the 3x3 blocks from the cell, the region is reached when a representative cell of one of them that
is in the region was reached, and every reached cell of the window gets the answer (1 yes, 2 no). `LabelRegions` numbers
the connected areas of blocks per size class: each class's region grid is cleared, and every block still 0 starts a new
region (1, 2, ...) flooded from a queue of blocks.

## The map's data

- The grids are `SearchGrid`s (search_grid.h: a `std::vector` of `height` rows of `rowLength` elements, sized by
  `Resize(width, height, fill, perCell)`, which leaves a grid of that size alone; `grid[y][x]` reads a row; new cells are
  `fill`, 0 by default): `waterDepth`, `cells` (the map's size), `occluders`, `marks`. The large-unit layer's Set / Merge
  also write signed bytes into `waterDepth`'s rows (the small layer writes the cells' `level` byte).
- Cells and blocks are `Point`s (`grid[cell]`, `grid.Contains(cell)`). Areas come two ways: the refreshes
  (`QueueRefresh`, `RefreshArea`, `LimitSteepCells`, `ClearUnmarkedArea`, the dirty rectangle) and
  `LocalSearch::BeginInRect` take a `first` and a `last` cell, both inside; `RegionMap::UpdateBlocks` takes a `Rect` of
  blocks (right and bottom edges outside, as everywhere `Rect` is used).
- The packed words have names in ai_map.h: AIMapCell::flags is the water's depth (kCellDepthMask), kCellBridge and
  kCellWater; AIMapCell::Passability reads a size class's nibble; namespace `occluder` reads and writes the occluder word.
  The occluder kinds (sector_map.h `OccluderKind`) come from the part names (pipeline.h BoundsFlag: "CROWN" kind 2, "BASE"
  a bridge, "EMPTY" no footprint), the spells' walls and clouds (kind 3, HasSpellArea) and ApplyBridgeDecks (kind 4); no
  part gives kind 1, whose cost-table entries are unused.
- `UnitLayer` (small and large): a list of every node ever allocated, 1024 hash buckets by (block x * 17 + block y), 4x4
  blocks. Unit shapes are cached keyed by standing height rounded up to tenths.
- Path cells' second dword: the original's `BuildCellsTo` filled `flags` from a stack slot it never wrote, where the
  planner leaves either 0 or, after any leg, `SearchCommit`'s row offset 4 * (last row + 1), in practice 0x84.
  `PlanLongPath` records it (`CommitLeg`) and `BuildCellsTo` uses it. `SetStepSpeeds` rewrites both halves of every step
  but the last, whose flags stay as built; the steering reads every step's flags as its speed word (writing 0 gave the
  last step of every path speed 0). The region chain's cells are read only for x and y.
- Trajectory: waypoints at the order start, at path corners (direction change > 45 degrees), and the target; one point
  per path cell, speed = step speed * scale / 512, slowed where turning takes longer than the way (`LimitSpeeds`); the
  unit first turns in place unless the first direction is within acos(0.9) of its facing; segments are fitted with two
  Hermite cubics.
- The AI events (`AIEventTable`, ai/ai_events.h, after the map): a time wheel (`Advance`), `ForgetUnit` drops a dead
  unit's events; saved with the map.
- The smoothing tables and the spiral tables are built on first use (`SmoothTables`, `SectorSpiralTables`).

## Original bugs kept

Visible behaviour of the original kept until the user decides (rewrite-guide rule 9; C84 triaged the list, with a
recommendation per entry). Crashes, undefined behaviour and invisible bookkeeping are fixed, not listed.

- Path smoother: the first-cell climb test is `<= 0`, the later ones `< 0`.
- LimitSteepCells clears the neighbour that broke the scan, or entry 4 of the offset table when none did.

