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

### Built graphs

The region map a zone starts with (MF13d). One rule, whatever the map's format: a zone's graph is no older than its
ground. `WorldServer::NewZone` (world_save.cpp) reads the zone's graph (`ReadAIGraph`: the .mob's 0x31415926 chunk,
else `<map>.grp`; one whose block grid is not the map's size counts as none) and `LoadMap` loads it
(`RegionMap::LoadBlocks`), unless
- the map comes from the mod's `Maps\` and the .mob from the game's (`MapFromMod`, Rebirth's Maps rule as
  `SelectMaps` decides it for each file; docs/notes/mods.md, "Maps as folders"): a mod's ground under the game's graph
  (the showcase island's folder, mod_map_heights' painted heights, rebirth_mod_maps' copy of zone2.mpr);
- or no graph is found (the original ended the game with "Can't load AIGraph"), or none of the map's size: Lost in
  Astral's main menu map is 24 x 24 blocks but its zonemainmenunew.mob carries the game's 16 x 16 menu graph (one of its
  zones, 24 x 32, has a misfit too); `LoadBlocks` used to size the region grids by the graph, and region reads for
  units past the 16th block went beyond the grid.

Then the graph is built: `AIMap::BuildRegionGraph`, after `EndRefresh` (the objects' footprints stamped, the cells
refreshed) and before `refreshUnits` goes on: the region map reset and attached blank, every unit lifted from the
sector map, `RegionMap::UpdateBlocks` over every block (the editor's computation, which a refresh runs over its area),
the units put back. Logged: "ai graph: built for W x H blocks, N units lifted, in T ms". An install without a mod
(binary or converted: ei-convert's folders sit beside their .mob), a mod that ships its zone's map and .mob (Lost in
Astral) and a mod that ships only the .mob (its objects over the game's ground) read their graph. Nothing tells a
folder painted in place from ei-convert's, so ground changed in a converted install's own folder keeps the zone's
graph (the same change as a mod builds).

- Saves: the graph is written as a difference (`WriteDiff`) from `ReadGraphBase`: the zone's graph when one fits the
  map, else zeros of the graph's size (`RegionMap::SavedSize`), whether the zone built its graph or not. A save's load
  (`LoadSave`) loads the AI map first (`LoadAIMap`), applies the difference over the same base and loads the result;
  it never builds.
- Why not build for every map made from a folder (measured 2026-11-11 with every map of the converted vanilla install built and
  compared with its shipped graph, 32 maps): the shipped graphs are not ours. With the units lifted the representative
  cells match (20 of 1.2 million block-classes differ) and every map has as many class-0 regions as its file, but 11%
  of the edge words differ (1,060,450 of 9,584,640), a quarter of those by 1, 1.2% (117,682) open in one and closed in
  the other; the regions of 7.6% of the block-classes are numbered otherwise. (With the units standing the regions
  split: zone 17 has 337 class-0 regions for the file's 160.) The editor measured with something we do not have (its
  own cost tables, or the objects as they stood then). Built everywhere, the converted install's smoke tier had 40 of
  58 goldens differ from the binary install's (unit moves from the first record: the main menu's zone builds too), so
  the converted check would lose its paths; and every zone load paid the cost below.
- Threads (MF13d1): `UpdateBlocks` runs each pass (the representative cells, then the edges) a row at a time on
  `lib::OnThreads`, each row with a `LocalSearch` of its own attached to the map; the second pass starts when the first
  has ended (the edges read the neighbours' representative cells). A block's measurement reads only the AI map and the
  representative cells and writes only its own block's words, and a search never reaches the cells off the map (the
  three outermost rows and columns are never passable: `RefreshArea`), so the stale window rows a search keeps from an
  earlier one are never read and a fresh search measures as the one search did. One state did carry over: the
  original picked the representative cells with the region map's own search in whatever direction the region queries
  had left it (`ReachesRegion` turns it backward, `NearbyBlocks` by its flag) and the edges set it forward, so the
  first pass's searches take `local.backward` and the second leaves it false (a refresh in play: without it Lost in
  Astral's gz1h saved 18 other aigraph bytes in mod_lost_in_astral_suslanger). Checked byte for byte (SaveBlocks)
  against one search measuring every block in order on all 40 vanilla map and .mob pairs of map.txt and map-LMP.txt
  (the harness step `graphthreads <zone> ...`; scenario `ai_graph_threads` keeps four of them).
- Cost (verify build, 2026-11-11, `--quiet`, two loads each): zone 19 (128 x 128 blocks, the largest) 0.17-0.33 s on
  the threads against 3.2-3.9 s with one search, zone 11 (128 x 96) 0.13-0.24 s against 2.4-2.7 s, zone 1 (64 x 48)
  0.04-0.07 s against 0.5 s; a base (32 x 32) about 0.02 s. Nearly all of it the edges' local searches (8 size classes
  x 8 neighbours a block). (Before MF13d1, four games at once: zone 19 2.55 s, zone 11 2.0 s, zone 1 0.4-1.2 s.)
  Paid at a new zone that builds (a save's load reads the save's graph).

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

