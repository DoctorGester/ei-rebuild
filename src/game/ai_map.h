#pragma once

#include "engine_object.h"
#include "maths.h"
#include "unit.h"

#include <cstddef>
#include <cstdint>

// One AI map cell (9 bytes): a 4-bit passability field per unit size class, then heights.
#pragma pack(push, 1)
struct AIMapCell {
    std::uint32_t passable;     // bits 4*sizeClass .. +3: non-zero = passable for that class
    std::uint16_t groundLevel;  // +4 terrain height in AI height units; blocks lines of sight
    std::uint8_t flags;         // +6 bits 0-5: height of what stands on the ground; 0x40: the
                                //    cell is under a bridge (surface height in CAIMap::bridgeHeights)
    std::uint8_t unknown_7;
    std::uint8_t level;         // +8 compared with the unit's standing level
};
#pragma pack(pop)
static_assert(sizeof(AIMapCell) == 9);
constexpr std::uint8_t kCellBridge = 0x40;

// A cell an object's footprint covers, collected by CAIMap::MarkFootprint before it is applied to
// the grid. Name provisional.
struct FootprintCell {
    int x, y;
    int low;                    // +0x08 lowest bottom-face height (AI height units), 0x7FFFFFFF if none
    int high;                   // +0x0C highest top-face height, 0 if none
    std::int8_t kind;           // +0x10 occluder kind (part flags bits 3-5; 4 = bridge once applied)
    std::int8_t transparency;   // +0x11 index into the transparency table (part flags bits 6-9)
    std::uint8_t edge;          // +0x12 lies on the outline of one of the part's faces
    std::uint8_t unknown_13;    // padding (left uninitialised by the original)
    int next;                   // +0x14 next index in the same hash bucket, -1 ends
};
static_assert(sizeof(FootprintCell) == 0x18);

// A growable array of FootprintCell with a 16-bucket hash on (x + y). Name provisional.
struct FootprintCellSet {
    // 005BD350. Resizes the array to `size` (0 frees it). The storage is 32-byte aligned, with the
    // raw allocation stored just before it. `growBy` != -1 also sets the growth step.
    void SetSize(int size, int growBy);

    // 005B2D10. Adds a top-face sample: raises `high` of the cell (x, y) to `height`, lowering the
    // kind and transparency to the given ones; a new cell gets low = 0x7FFFFFFF.
    void AddTop(int x, int y, int height, std::int8_t kind, std::int8_t transparency);

    // 005B2E10. Adds a bottom-face sample: lowers `low` of the cell (x, y) to `height`; a new cell
    // gets high = 0 and the given kind and transparency.
    void AddBottom(int x, int y, int height, std::int8_t kind, std::int8_t transparency);

    // 005B2F40. Bounding box of the cells: {minX, maxX, minY, maxY}; {100000, 0, 100000, 0} if empty.
    void GetBounds(int* bounds) const;

    FootprintCell* cells;   // +0x00
    int count;              // +0x04
    int capacity;           // +0x08
    int growBy;             // +0x0C 0 = an eighth of the size, clamped to 4..1024
    int buckets[16];        // +0x10 first index per (x + y) & 15, -1 when empty
    bool hashed;            // +0x50 set by the first insertion, never cleared
    std::uint8_t unknown_51[3];
};
static_assert(sizeof(FootprintCellSet) == 0x54);

// A box of an object's model that blocks the AI map (CEngineObject + 0x160, 0x38 bytes each).
struct FootprintPart {
    std::uint32_t flags;    // bits 3-5 kind, 6-9 transparency, 0x400 bridge, 0x800 ignored
    Vector3 offset;         // +0x04
    std::uint32_t unknown_10;
    Vector3 boxMin;         // +0x14
    Vector3 boxMax;         // +0x20
    Vector3 pivot;          // +0x2C
};
static_assert(sizeof(FootprintPart) == 0x38);

// The passability cost tables of one size class, filled by CAIMap::CostContext (005B9510).
struct PassCostContext {
    const int* costs;       // CAIMap + 0x14: cost per passability nibble
    const int* climb;       // middle of the climb table of the size class
    int shift;              // size class * 4: the nibble in AIMapCell::passable
    float unknown_0C;       // 0.1
    float unknown_10;       // 10
    float unknown_14;       // 0.1
};
static_assert(sizeof(PassCostContext) == 0x18);

// A growable square of byte marks over map cells (0x18 bytes), filled by MarkVisibleCells.
struct VisibilityGrid {
    // 005C8D40 (forwarder). Grows the grid to cover (x - n .. x + n, y - n .. y + n); the first
    // call (x = 0x7FFFFFFF) sizes and clears it.
    void Include(int x, int y, int n);

    int originX, originY;       // cell of data[0]; 0x7FFFFFFF before the first Include
    std::uint8_t* data;         // +0x08 width * height
    std::uint8_t** rows;        // +0x0C rows[i] = data + i * width
    int width, height;          // +0x10
};
static_assert(sizeof(VisibilityGrid) == 0x18);

// The region map at CAIMap + 0x22F8: connected areas per size class.
struct CRegionMap {
    // 005B0600. Region of cell (x, y) for a size class, in AX (the rest of EAX is returned as the
    // original leaves it).
    int RegionAt(int sizeClass, int x, int y);
    // The rewrite of 005B0600 (ai_region_reach.cpp): the region most of the reachable 3x3 blocks
    // around the cell belong to (the first seen on a tie; 0 when none); EAX's upper half is 0.
    int DominantRegion(int sizeClass, int x, int y);
    // 005B07C0. Can cell (x, y) reach `region`? Caches the answers in `marks` (1 yes, 2 no).
    bool Reaches(int sizeClass, int x, int y, int region, void* marks);
    // The rewrite of 005B07C0 (ai_region_reach.cpp); Reaches stays the forwarder to its address.
    bool ReachesRegion(int sizeClass, int x, int y, int region, void* marks);
    // 005B0050 (ai_region_reach.cpp). Numbers the connected areas of blocks of every size class.
    void LabelRegions();
    // 005B0F70 (ai_region_reach.cpp). The per-direction block edge words: -1 out of the map, and
    // each edge between two blocks the smaller of its two directions, for every size class.
    void ClampEdgeCosts();
    // 005B1180 (RET 4, AL; ai_region_reach.cpp). Reads the block grids of every size class from
    // the AI graph stream (width and height 1..256 first); returns the stream's state byte (+5).
    bool LoadBlocks(void* stream);
    // 005AF900 (RET 0xC; ai_region_reach.cpp). Per size class the representative cell of block
    // (bx, by): (3, 3) unless a passable cell in rings from the middle is better (005AF7B0).
    void PickRepresentatives(class CAIMap* map, int bx, int by);
    // 005B0D90 (RET 0x1C, AL; ai_region_reach.cpp). The chain of regions from (fromX, fromY) to
    // (toX, toY) into `chain` (a CList), avoiding the blocks in `excluded` (CArray<{x, y}>).
    bool FindRegionChain(int sizeClass, int fromX, int fromY, int toX, int toY, void* chain, void* excluded);
    // 005AF5F0 (RET 4, AL; ai_region_reach.cpp). Expands the two block searches until they meet and
    // joins their paths into `chain`; false when one runs out.
    bool JoinBlockSearches(struct PathCellList* chain);
    // 005AFAE0 (RET 0xC; ai_region_reach.cpp). Per size class the block (bx, by)'s edge words:
    // the local search cost to each neighbour's representative cell / 128. The first argument is
    // not read.
    void MeasureBlockEdges(void* unused, int bx, int by);
    // 005B0C00 (RET 4; ai_region_reach.cpp). Sizes the block grids for the map (8x8-cell blocks),
    // sets up the local search and rebuilds the derived tables.
    void Attach(class CAIMap* map);
    // 005AF7B0 (RET 0x14, AL; ai_region_reach.cpp). Whether cell (xx, yy) of block (bx, by) can
    // reach both side edges or both end edges of the block for a size class.
    bool CellSuitsBlockAt(int sizeClass, int bx, int by, int xx, int yy);
    // 005AFF10 (RET 0x10; ai_region_reach.cpp). Floods `label` over the blocks connected through open
    // edges, breadth first from `frontier` (CArray<{x, y}>), `next` holding the following ring.
    void FloodRegionFrom(int sizeClass, void* frontier, void* next, int label);
    // 005B0370 (RET 0x14; ai_region_reach.cpp). The 3x3 blocks around cell (x, y) whose
    // representative cell the local search reaches from it, as {block x, block y, cost} appended
    // to `out` (a CList, emptied first). `flag` goes to +0x114 for the search.
    void NearbyBlocks(int sizeClass, bool flag, int x, int y, struct BlockCostList* out);
};

// A CList of {block x, block y, cost} (0x1C bytes, nodes 0x14).
struct BlockCostList {
    struct Node {
        Node* next;
        Node* prev;
        int x, y, cost;
    };
    void* vtable;
    Node* head;
    Node* tail;
    int count;
    Node* free;
    void* blocks;
    int blockSize;
};
static_assert(sizeof(BlockCostList) == 0x1C);

// Per size class tables of the cost of a height step between neighbouring cells, indexed from the
// middle (negative steps down, positive up). Layout provisional.
struct ClimbTable {
    int* costs;
    int count;
};

// A cell of a path (CList element, 8 bytes). Layout provisional.
struct PathCell {
    std::int16_t x, y;
    std::uint16_t flags;        // 0x200 on a path that is a single cell
    std::uint16_t unknown_6;
};
static_assert(sizeof(PathCell) == 8);

// MFC CList<PathCell, PathCell&> (0x1C bytes); the game inlines most of its operations.
struct PathCellList {
    struct Node {
        Node* next;
        Node* prev;
        PathCell data;
    };
    void* vtable;
    Node* head;         // +0x04
    Node* tail;         // +0x08
    int count;          // +0x0C
    Node* free;         // +0x10
    void* blocks;       // +0x14 CPlex chain
    int blockSize;      // +0x18

    // 005B1B70. Takes a node from the free list (allocating a block when empty) and counts it.
    Node* NewNode(Node* prev, Node* next);
    // 00556D40. Frees all nodes and blocks.
    void RemoveAll();
};
static_assert(sizeof(PathCellList) == 0x1C);

// A unit's path (name and layout provisional). Its methods at 005C7xxx steer along `steps`.
struct AIPath {
    // The order in the first 0x20 bytes (MotionOrder, ai_motion.h); src/game/ai_motion.cpp.
    // 005C7D50. Stand still at (0, 0); the steps are emptied. Returns 0 (EAX, as the original).
    int Reset();
    // 005C7B00. Stand at world `position` facing `direction` from world time `time`; the steps are
    // emptied. False if the unit already stood there that way.
    bool Start(const Vector3* position, const Vector3* direction, int time);
    // 005C7C70. Follow the steps to (x, y) (half-cells).
    void SetTarget(float x, float y);
    // 005C7C00 / 005C7C40. Turn to `direction` (kind 1 at `rate`, kind 4 until `until`).
    void SetTurn(const Vector3* direction, float rate);
    void SetTurnUntil(const Vector3* direction, int arg6, int arg7);
    // 005C7C20. Track object `id` between `from` and `to`.
    void SetTrack(int id, int from, int to);
    // 005C7C90. Forget a tracked object.
    void ForgetObject(int id);
    // 005C7CB0. Shifts the order's times by `delta`.
    void ShiftTime(int delta);
    // 005C7CE0. Sets the speed word of every step. Returns 0 (EAX, as the original).
    int SetStepSpeeds(std::uint16_t speed);
    // 005C7D00. Where the order leads (world); returns `out`.
    Vector3* Destination(Vector3* out);
    // 005B23A0. Empties the path, the steps and the order.
    void Clear();

    std::uint8_t unknown_00[0x20];
    PathCellList steps;         // +0x20 the next cells to walk (at most 32)
    PathCellList cells;         // +0x3C the rest of the path
    float targetX, targetY;     // +0x58 in half-cells
    Vector3 goal;               // +0x60 world position asked for
    bool reached;               // +0x6C no cells left
    bool failed;                // +0x6D the last search failed; search again
    bool flag6E, flag6F;        // +0x6E, +0x6F
    void* pointsVtable;         // +0x70 CArray of points (vtable 0073F07C; 00557230 / 00557250)
    void* points;               // +0x74
    int pointCount;             // +0x78
    int pointMax;               // +0x7C
    int pointGrowBy;            // +0x80
    int smoothing;              // +0x84 from 005C46C0 (how much longer the path is than the way)
};
static_assert(offsetof(AIPath, steps) == 0x20);
static_assert(offsetof(AIPath, cells) == 0x3C);
static_assert(offsetof(AIPath, goal) == 0x60);
static_assert(offsetof(AIPath, smoothing) == 0x84);

// The navigation grid of the current map, loaded by CAIMap::Load (005B43C0, patched by Rebirth).
// Name from the game's error strings; layout provisional.
class CAIMap {
public:
    // 005B80F0. Can `unit` stand on cell (x, y)? `reason` gets 0 (ok or not a unit), 1 (cell level
    // too high) or 2 (water too deep).
    bool CanStandAt(CUnit* unit, int x, int y, int* reason);

    // 005B81B0. Like CanStandAt, but a cell no worse than the one the unit is on is also accepted.
    bool CanStandAtFromHere(CUnit* unit, int x, int y, int* reason);

    // 005B79D0. Can `unit` stand on and enter cell (x, y)? Always true for non-units.
    bool IsPassable(CUnit* unit, int x, int y);

    // 005B7A40. IsPassable at the cell of world `position`, with the unit's own footprint lifted
    // (the unit is taken out of the sector map and put back).
    bool IsPassableWithoutSelf(CUnit* unit, const Vector3* position);

    // 005BA6E0. Is the cell the unit stands on passable for its size class?
    bool IsUnitCellPassable(CUnit* unit);

    // 005BA790. Is the cell at world `position` passable for size class `sizeClass` (0..7)?
    bool IsPassableForClass(std::uint8_t sizeClass, const Vector3* position);

    // 005BA830. Can the unit leave its cell: is the cell or one of its 8 neighbours passable and
    // reachable with a positive climb cost?
    bool CanLeaveCell(CUnit* unit);

    // 005BA9D0. Moves `target` to the nearest cell (spiralling out from it) where `unit` can stand
    // and that lies in the unit's region; false if there is none. `target` is unchanged when its
    // own cell qualifies.
    bool FindFreeCellNear(CUnit* unit, const Vector3* from, Vector3* target);

    // 005BAC10. Moves `target` (world in, half-cell units out) to a cell `unit` can stand on: the
    // target cell itself, else the first good cell walking back towards the unit, at least
    // `minDistance` from the target. With `sameRegion`, only cells that reach the unit's region.
    bool SnapTarget(CUnit* unit, Vector3* target, bool sameRegion, float minDistance);

    // 005BB350. Plans a path for `unit` to `target` into `path`; `range` limits the search.
    bool FindPath(CUnit* unit, const Vector3* target, AIPath* path, float range, float minDistance);
    // 005B9C90 (PlanPath, RET 0x20; ai_plan.cpp). The long-range plan from cell (fromX, fromY) to
    // (toX, toY): region chain, local-search legs between the regions' representative cells, then
    // the cells into path->cells. On a failed leg: aim at the best reached cell when it is far
    // enough from the start, else (with `retry`) exclude the region and plan again.
    bool PlanLongPath(CUnit* unit, int fromX, int fromY, int toX, int toY, AIPath* path, int retry, int maxCells);
    // 005BB900. Searches again if the last search failed and nothing is left, else moves on along
    // the path. True if the path had already been walked to the end.
    bool ContinuePath(CUnit* unit, AIPath* path);
    // 005BB190. Moves up to 32 cells from the path into the steps and points the unit at the next one.
    void AdvancePath(CUnit* unit, AIPath* path);

    // 005BC3E0. Visibility from `from` to `to` (world positions): 0 when terrain blocks the line,
    // else the product of the transparencies of the objects it passes (at least 0.0001).
    float LineOfSight(const Vector3* from, const Vector3* to);

    // 005B77F0. Terrain height of cell (x, y) in world units.
    float GroundHeight(int x, int y);
    // 005BB960. Terrain height at world `position` (0 outside the map).
    float GroundHeightAt(const Vector3* position);
    // 005BBA10. Height of the walkable surface at `position`: the bridge deck where there is one.
    float SurfaceHeightAt(const Vector3* position);
    // 005BBB20. Terrain height plus what stands on it, at `position`.
    float TopHeightAt(const Vector3* position);
    // 005BC2A0. Largest height step to the 4 neighbours of the cell at `position` (0 at the edge).
    float SlopeAt(const Vector3* position);
    // 005BBBD0. Is the cell at `position` under a bridge?
    bool IsBridgeAt(const Vector3* position);
    // 005BBC70. Flag 0x80 of the cell at `position`.
    bool CellFlag80At(const Vector3* position);
    // 005BBD10. Has cell (x, y) an occluder of kind 3?
    bool HasKind3Occluder(int x, int y);
    // 005BBDB0. HasKind3Occluder at the cell of `position`.
    bool HasKind3OccluderAt(const Vector3* position);
    // 005BBE40. Material index (0..31) of the cell at `position`, -1 outside the map.
    int MaterialAt(const Vector3* position);
    // 005BC1F0. Material record (0x118 bytes) of the cell at `position`; the first one outside.
    void* MaterialRecordAt(const Vector3* position);
    // 005B64E0. Has cell (x, y) a bridge occluder? `height` (optional) gets the highest deck.
    bool BridgeDeckAt(int x, int y, int* height);

    // 005B6A80. Stamps (remove = false) or clears (remove = true) the object's footprint in the
    // grid; false if it does not fit. Units go to the unit occupancy layers instead.
    bool MarkFootprint(CEngineObject* object, bool remove);
    // The footprint of a non-unit object, collected into `footprint` and `bridges` (the parts
    // flagged 0x400). Returns true if a bridge part was seen. Part of MarkFootprint.
    bool CollectFootprint(CEngineObject* object, FootprintCellSet* footprint, FootprintCellSet* bridges);
    // 005B65B0. Applies a footprint to the occluder grid (stamp, undone if a cell fails) or clears it.
    bool ApplyFootprint(FootprintCellSet* set, bool remove);
    // 005B7820. Stamps a footprint and refreshes the passability around it.
    bool StampFootprint(int owner, FootprintCellSet* set);
    // 005B78F0. Clears a footprint and refreshes the passability around it.
    bool ClearFootprint(FootprintCellSet* set);

    // 005B8B30. Starts collecting refreshes into the dirty rectangle (flushing a pending one).
    void BeginRefresh();
    // 005B8B80. Refreshes the collected dirty rectangle.
    void EndRefresh();

    // src/game/ai_movement.cpp:
    // 005B8350. Occupancy shape of a unit with standing height `height` (null at 0.2 or below):
    // the shape cache entry for the height rounded up to tenths (created on first use).
    void* UnitShape(float standingHeight);
    // 005B7BB0. The first unit near world `position` (within the sum of standing heights) whose
    // occupancy shape blocks `unit` there; null if none.
    CUnit* BlockingUnitAt(CUnit* unit, const Vector3* position);
    // 005BA400. Fills each step's speed word (cost table / climb cost, in 1/512) and flag word.
    void SetStepSpeeds(CUnit* unit, PathCellList* steps);
    // 005B9510. The passability cost tables for size class `sizeClass`; returns the climb table's
    // middle (EAX, as the original).
    const int* CostContext(int sizeClass, struct PassCostContext* out);
    // 005BA5A0. Is the cell under `unit` passable for it at a cost below 12000? (A cell with a
    // kind-3 occluder is evaluated at the size class's level through 005B8BA0.) Only AL is set
    // when the unit is outside the map.
    bool StandsOnCheapCell(CUnit* unit);
    // 005BC970. Marks in `grid` the cells within `radius` of `eye` that it sees: inside the cone
    // around `look` (cos >= `cosLimit`; none when cosLimit <= -1) and, unless `targetHeight` is
    // below -100, with a line of sight to the cell's ground raised by `targetHeight` that reaches
    // at least `radius` * visibility.
    void MarkVisibleCells(const Vector3* eye, float radius, const Vector3* look, float cosLimit, float targetHeight,
                          struct VisibilityGrid* grid);

    // Not rewritten (forwarders):
    // 005B6740. Applies or clears the bridge decks of a footprint.
    bool ApplyBridges(FootprintCellSet* set, bool remove);
    // The rewrite of 005B6740 (ai_bridges.cpp); ApplyBridges stays the forwarder to its address.
    bool ApplyBridgeDecks(FootprintCellSet* set, bool remove);
    // 005BC750 (RET 8, ST0; ai_line_step.cpp). The largest ground step between consecutive cells on
    // the line, in world height; 1.0 for a zero-length line, 100.0 when an end is off the map.
    double LargestStepAlong(const Vector3* from, const Vector3* to);
    // 005B9690 (RET 8, AL; ai_join.cpp). Joins the forward and backward local searches where they
    // meet: the path into `cells`, the summed cost into `cost` (optional); false when they cannot.
    bool JoinLocalSearches(struct PathCellList* cells, int* cost);
    // 005B9A20 (RET 0xC; ai_join.cpp). The path to cell (x, y) walked back along the direction marks
    // (+0x26BC) into `path` (emptied first).
    void BuildCellsTo(int x, int y, struct PathCellList* path);
    // 005B7EF0 (RET 8; ai_water_patch.cpp). Adds (or lifts) the water depths around `position` as
    // a pseudo unit of the small-unit layer.
    void StampWaterPatch(const Vector3* position, bool add);
    // 005B61B0 / 005B6380. Adds / removes one footprint cell's occluder.
    bool AddOccluder(FootprintCell* cell);
    bool ClearOccluder(FootprintCell* cell);
    // The rewrites of 005B61B0 / 005B6380 (ai_occluders.cpp); the two above stay the forwarders.
    bool AddOccluderRecord(FootprintCell* cell);
    bool ClearOccluderRecord(FootprintCell* cell);
    // 005B8FA0. Recomputes passability in the rectangle (inclusive, grown by 3 cells).
    void Refresh(int minX, int maxX, int minY, int maxY);
    // The rewrite of 005B8FA0 (ai_refresh.cpp); Refresh stays the forwarder to its address.
    void RefreshArea(int minX, int maxX, int minY, int maxY);
    // 005B8BA0 (RET 0x14; ai_refresh.cpp). The passability mark of cell (x, y) for a standing
    // level and its height in AI units: from the terrain type, water depth and the occluders over
    // the cell (cost and damping), looked up in the mark table 00791164. `scratch` gets the state.
    std::uint32_t CellMark(void* scratch, int level, int levelHeight, int x, int y);
    // 005B8710 (RET 0x10; ai_refresh.cpp). Refresh's second pass over [x0, x1] x [y0, y1] (kept 2
    // cells from the border): a cell whose step to a direct neighbour exceeds what size class 0
    // can climb becomes impassable, one beyond the larger classes keeps only class 0; either way
    // nibbles 5-6 of one neighbour and nibble 7 of 13 cells around it are cleared.
    void LimitSteepCells(int x0, int x1, int y0, int y1);

    CRegionMap* Regions() { return reinterpret_cast<CRegionMap*>(regions); }

    // Refresh the rectangle now, or add it to the dirty rectangle while batching.
    void QueueRefresh(int minX, int maxX, int minY, int maxY);

    std::uint8_t unknown_000;
    bool batching;                      // +0x001 refreshes collect into the dirty rectangle
    bool refreshUnits;                  // +0x002 a refresh also re-registers the units around it
    std::uint8_t unknown_003;
    int dirtyMinX, dirtyMaxX;           // +0x004
    int dirtyMinY, dirtyMaxY;           // +0x00C
    std::uint8_t unknown_014[0xD8 - 0x14];
    ClimbTable climbLarge;              // +0x0D8 size classes other than 0
    std::uint8_t unknown_0E0[0xEC - 0xE0];
    ClimbTable climbSmall;              // +0x0EC size class 0
    std::uint8_t unknown_0F4[0xFC - 0xF4];
    std::uint8_t* materials;            // +0x0FC 0x118-byte records
    std::uint8_t unknown_100[0x10C - 0x100];
    std::uint8_t bridgeHeights[0x14];   // +0x10C hash_map<int, uint16> keyed (short)x << 16 | (short)y
    float heightScale;                  // +0x120 world z to AI height units
    std::uint32_t unknown_124;          // +0x124 the sector map (CSectorMap) starts here
    int sectorsPerRow;                  // +0x128
    std::uint8_t unknown_12C[0x138 - 0x12C];
    std::uint8_t* sectors;              // +0x138 0x54-byte sector records (see LineOfSight)
    std::uint8_t unknown_13C[0x14C - 0x13C];
    FootprintCellSet footprint;         // +0x14C scratch for MarkFootprint
    FootprintCellSet bridges;           // +0x1A0
    std::uint8_t unknown_1F4[0x1F8 - 0x1F4];
    std::int8_t** waterDepth;           // +0x1F8 rows of per-cell water depth
    std::uint8_t unknown_1FC[0x27C - 0x1FC];
    std::uint8_t unitLayerSmall[0x12A4 - 0x27C];    // +0x27C unit occupancy (005B36B0 / 005B3890)
    std::uint8_t unitLayerLarge[0x22CC - 0x12A4];   // +0x12A4
    std::uint8_t unknown_22CC[0x22F8 - 0x22CC];
    std::uint8_t regions[0x2694 - 0x22F8];          // +0x22F8 region map (005B0600)
    int lastFootprintOwner;             // +0x2694 set by StampFootprint
    std::uint32_t unknown_2698;
    AIMapCell** cells;                  // +0x269C rows of cells
    int width;                          // +0x26A0
    int height;                         // +0x26A4
    std::uint32_t unknown_26A8;
    std::uint32_t** occluders;          // +0x26AC rows of per-cell occluder words (see LineOfSight)
};
static_assert(offsetof(CAIMap, climbLarge) == 0xD8);
static_assert(offsetof(CAIMap, climbSmall) == 0xEC);
static_assert(offsetof(CAIMap, bridgeHeights) == 0x10C);
static_assert(offsetof(CAIMap, heightScale) == 0x120);
static_assert(offsetof(CAIMap, footprint) == 0x14C);
static_assert(offsetof(CAIMap, bridges) == 0x1A0);
static_assert(offsetof(CAIMap, waterDepth) == 0x1F8);
static_assert(offsetof(CAIMap, unitLayerLarge) == 0x12A4);
static_assert(offsetof(CAIMap, regions) == 0x22F8);
static_assert(offsetof(CAIMap, lastFootprintOwner) == 0x2694);
static_assert(offsetof(CAIMap, cells) == 0x269C);
static_assert(offsetof(CAIMap, occluders) == 0x26AC);

// A cached lookup in the global spiral of cell offsets (pairs of int16 dx, dy ordered by distance).
struct SpiralCache {
    // 005BFC40. The offsets, with `length` set to how many lie within `radius` (cached for the last
    // radius asked).
    const std::int16_t* Get(float radius, int* length);

    int length;
    float radius;
};

// World coordinate to AI map cell (half-unit cells), as every AI map function computes it:
// rounded to float, then FISTP (round to nearest).
int WorldToCell(float world);

// 005B7B20. Standing level of size class `sizeClass` (0..7): (2 - height) * 16, at least 0. Outside
// 0..7 the original returns only AL = 0 (the rest of EAX is the argument), reproduced here.
int __stdcall LevelForSizeClass(int sizeClass);
// 005B7B70. Standing level of `unit`: (2 - standingHeight) * 16, at least 0.
int __stdcall StandingLevel(const CUnit* unit);
// 005B3160. Bridge deck height of cell (x, y) from the hash map at CAIMap + 0x10C, 0 if none.
int __fastcall BridgeHeight(const void* map, void*, short x, short y);
