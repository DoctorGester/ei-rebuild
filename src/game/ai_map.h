#pragma once

#include "ai_event_table.h"
#include "engine_object.h"
#include "maths.h"
#include "search_grid.h"
#include "sector_map.h"
#include "unit.h"

#include "lib/containers.h"
#include <cstddef>
#include <cstdint>
#include "lib/static_object.h"

#include <list>
#include <unordered_map>
#include <vector>

// One AI map cell: a 4-bit passability field per unit size class, then heights.
#pragma pack(push, 1)
struct AIMapCell {
    std::uint32_t passable;     // bits 4*sizeClass .. +3: non-zero = passable for that class
    std::uint16_t groundLevel;  // terrain height in AI height units; blocks lines of sight
    std::uint8_t flags;         // bits 0-5: height of what stands on the ground; 0x40: the
                                // cell is under a bridge (surface height in CAIMap::bridgeHeights)
    std::uint8_t unused_7;
    std::uint8_t level;         // compared with the unit's standing level
};
#pragma pack(pop)
static_assert(sizeof(AIMapCell) == 9);
constexpr std::uint8_t kCellBridge = 0x40;

// A cell an object's footprint covers, collected by CAIMap::MarkFootprint before it is applied to the grid.
struct FootprintCell {
    int x, y;
    int low;                    // lowest bottom-face height (AI height units), 0x7FFFFFFF if none
    int high;                   // highest top-face height, 0 if none
    std::int8_t kind;           // occluder kind (part flags bits 3-5; 4 = bridge once applied)
    std::int8_t transparency;   // index into the transparency table (part flags bits 6-9)
    std::uint8_t edge;          // lies on the outline of one of the part's faces
    std::uint8_t padding;               // zeroed by AddCell (uninitialised in the original)
    int next;                   // next index in the same hash bucket, -1 ends
};

// A growable array of FootprintCell with a 16-bucket hash on (x + y).
struct FootprintCellSet {
    void AlongDirection(CAIMap* map, const Vector3* position, float width, const Vector3* direction);
    void DiscAt(CAIMap* map, const Vector3* position, float radius);
    // An empty array, `hashed` set; the buckets are not written (the spells' global set's are zero until the
    // first footprint empties them).
    FootprintCellSet* Construct();

    // Resizes the array to `size`.
    void SetSize(int size);

    // Adds a top-face sample: raises `high` of the cell (x, y) to `height`, lowering the
    // kind and transparency to the given ones; a new cell gets low = 0x7FFFFFFF.
    void AddTop(int x, int y, int height, std::int8_t kind, std::int8_t transparency);

    // Adds a bottom-face sample: lowers `low` of the cell (x, y) to `height`; a new cell
    // gets high = 0 and the given kind and transparency.
    void AddBottom(int x, int y, int height, std::int8_t kind, std::int8_t transparency);

    // Bounding box of the cells: {minX, maxX, minY, maxY}; {100000, 0, 100000, 0} if empty.
    void GetBounds(int* bounds) const;

    std::vector<FootprintCell> cells;
    int buckets[16];        // first index per (x + y) & 15, -1 when empty
    bool hashed;            // set by Construct and by every insertion, never cleared
};

// The passability cost tables of one size class, filled by CAIMap::CostContext.
struct PassCostContext {
    const int* costs;       // cost per passability nibble
    const int* climb;       // middle of the climb table of the size class
    int shift;              // size class * 4: the nibble in AIMapCell::passable
    float backwardSpeed;    // 0.1, or the unit's (CostContextFor)
    float turnScale;                    // 10: scales the turn cost of a path step
    float forwardSpeed;     // 0.1, or the unit's
};

struct FogGrid;

struct DeltaField;

struct PathCell {
    std::int16_t x, y;
    std::uint16_t flags;        // 0x200 on a path that is a single cell
    std::uint16_t terrainBits;          // bit 0 the cell's flags bit 7, bit 1 a costly cell (path state bits 4-5)

    // The neighbour in direction 1..8 (1 = y - 1, then clockwise towards -x: 2 =
    // (-1, -1), 3 = (-1, 0), 4 = (-1, +1), 5 = (0, +1), 6 = (+1, +1), 7 = (+1, 0), 8 = (+1, -1)) into
    // `out` (x and y only); any other direction copies the whole cell. Returns `out`.
    PathCell* Step(PathCell* out, int direction) const;
};
static_assert(sizeof(PathCell) == 8);

struct PathCells : std::list<PathCell> {
    void Append(const PathCells* other);
    // `out` constructed (blocks of 0x80) with the cells in reverse order.
    PathCells* Reversed(PathCells* out) const;
};

// The speed word and terrain bits a path builder gives its cells: the plain step speed, no terrain bits. The
// original leaves stack leftovers there; the harness patches them to this too. SetStepSpeeds rewrites both
// words of every step but the last.
constexpr std::uint16_t kPlainStepSpeed = 0x200;
inline void SetPlainCellWords(PathCell* cell) {
    cell->flags = kPlainStepSpeed;
    cell->terrainBits = 0;
}
inline void SetPlainCellWords(PathCells* list) {
    for (PathCell& cell : *list) SetPlainCellWords(&cell);
}

// Removes each cell equal (x, y) to the one before it (the first is compared with (-1, -1)); an emptied list
// frees its blocks.
void PathCellsRemoveRepeats(PathCells* list);

// A block a search reaches.
struct BlockCost {
    int x, y, cost;
};

struct CRegionMap;

// A block of the region map, or a cell: {x, y}. The excluded blocks of a plan (AIPath) and the region map's
// work lists are CArray<BlockPoint>.
struct BlockPoint {
    int x, y;
};

// The first 0x20 bytes of PathOrder: what the unit is told to do. Kinds:
// 0 stand at (x, y) facing `angle` (set by Start / Reset),
// 1 turn to direction `arg5` (an angle) at rate `arg6` (0: the unit's own rate),
// 2 follow the steps to the target (arg5, arg6) (SetTarget),
// 3 track object `arg5` between times arg6 and arg7,
// 4 turn to direction `arg5`.
// (x, y) and targets are in half-cell units (world * 2).
struct MotionOrder {
    int kind;
    float x, y;         // where the order starts
    float angle;        // facing at the start
    int startTime;      // world time the order started
    std::uint32_t arg5; // float or object id, by kind
    std::uint32_t arg6;
    std::uint32_t arg7;
};
static_assert(sizeof(MotionOrder) == 0x20);

// A motion order with the steps it walks next: the first part of a unit's path (AIPath) and all the client's
// controller keeps of one (CUnitClientSpecific). Its methods steer along `steps`.
struct PathOrder {
    // Stand still at (0, 0); the steps are emptied. Returns 0.
    int Reset();
    // Stand at world `position` facing `direction` from world time `time`; the steps are
    // emptied. False if the unit already stood there that way.
    bool Start(const Vector3* position, const Vector3* direction, int time);
    // Follow the steps to (x, y) (half-cells).
    void SetTarget(float x, float y);
    // Turn to `direction` (kind 1 at `rateBits`, a float's bits kept as given; kind 4 until `until`).
    void SetTurn(const Vector3* direction, std::uint32_t rateBits);
    void SetTurnUntil(const Vector3* direction, int arg6, int arg7);
    void SetTrack(int id, int from, int to);
    void ForgetObject(int id);
    void ShiftTime(int delta);
    // Sets the speed word of every step. Returns 0.
    int SetStepSpeeds(std::uint16_t speed);
    // Where the order leads, in world units.
    Vector3* Destination(Vector3* out);
    // The order (kind byte, the steps' count as two bytes, start x / y, the angle within the
    // order angle range, the time and three arguments) on the field's delta channels, then each step as a 16-bit
    // header (x and y codes: one less, the same, one more than the previous step, or written out after it;
    // terrainBits & 3; the flags up to 0x3FF) on both outputs. ReadState empties the steps and reads them back.
    void WriteState(DeltaField* field);
    void ReadState(DeltaField* field);
    // The order and its steps in the save stream: kind, x / y, angle, start time and the three
    // arguments (4, 8, 4, 4, 0xC bytes), the steps' count, then each step (8 bytes). Loading appends that many
    // steps and reads over every step from the head.
    void LoadOrder(CStream* stream);
    void SaveOrder(CStream* stream);

    MotionOrder motionOrder;
    PathCells steps;         // the next cells to walk (at most 32)
};

// A unit's path: its order and next steps (PathOrder), the rest of the cells and the search's state.
struct AIPath : PathOrder {
    void LoadPath(CStream* stream);
    void SavePath(CStream* stream);
    void StandAndTurn(int time, const CMapObject* unit, const Vector3* direction, std::uint32_t rateBits);
    void StandAndTrack(int time, const CMapObject* unit, int id, int from, int to);
    void StandAndTurnUntil(int time, const CMapObject* unit, const Vector3* direction, int a, int b);
    void StandStill(const CMapObject* unit);
    void Clear();
    // The path as a motion order in `out` (its step list constructed): the order, the steps; and for an order that
    // follows steps (kind 2) the rest of the path appended, repeats removed, every step's speed 0x200 and the
    // target set.
    void ToMotionOrder(PathOrder* out) const;
    AIPath* Construct();
    void Destruct();
    // The search's target cell (targetX, targetY) in world units, z 0.
    Vector3* TargetPoint(Vector3* out) const;

    PathCells cells;         // the rest of the path
    float targetX, targetY;     // in half-cells
    Vector3 goal;               // world position asked for
    bool reached;               // no cells left
    std::uint8_t failed;        // 1: the last search failed; search again
    // FindPath lets the long-range plan exclude a region it failed in and plan again (PlanLongPath's `retry`)
    // when either is set: mayExcludeRegions for units without a player (set by their planners), firstSearch
    // from Clear until the first search ends.
    bool mayExcludeRegions, firstSearch;
    // the blocks the long-range plan avoids (PlanLongPath adds the region it failed in).
    std::vector<BlockPoint> excludedBlocks;
    int smoothing;              // set by the smoother (how much longer the path is than the way)
};

class CAIMap;
class CSectorMap;
class CUnitShape;

// A terrain type of the AI map (CAIMap::materials, indexed by the low 5 bits of a cell's occluder word): a
// section of tileDesc.reg in aiinfo.res, named by the type's number.
struct AITerrainType {
    char name[0x100];           // "Name"
    int speed;                  // "Speed" (-1 without one)
    int cost;                   // "CostMul": what a cell of it costs (CellMark's starting cost)
    float look;                 // "StepLook" in hundredths
    float smell;                // "StepSmell" in hundredths
    float noise;                // "StepSound" in hundredths: how loud steps on it are (AIViewer::StepNoise)
    std::uint32_t openMarks;    // per size class nibble: the mark of a cell with nothing on it
};

struct SearchNode {
    int cost;                   // 0x7FFFFFFF until reached
    SearchNode* next;           // in its bucket, or in the settled list
    SearchNode* prev;           // in its bucket
    std::int16_t x, y;          // window coordinates (0..33)
    bool usable;                // false on the window's border and outside a rectangle
    bool settled;               // expanded, or the start
    std::int8_t direction;      // 1..8: the way back to the cell it was reached from; 0 none
    std::uint8_t unused_13;
};
static_assert(sizeof(SearchNode) == 0x14);

template <> SearchGrid<SearchNode>* SearchGrid<SearchNode>::Construct(int newWidth, int newHeight);
template <> SearchGrid<AIMapCell>* SearchGrid<AIMapCell>::Construct(int newWidth, int newHeight);

// The local search (the AI map's forward and backward ones, and the region map's): Dijkstra over a 34 x 34
// window of map cells whose border is never entered. Open nodes wait in four sorted lists chosen by the low two
// bits of the node's x; `current` is the cheapest head.
struct LocalSearch {
    // Puts every settled and queued node back to unreached; the search is inactive.
    void Reset();
    // `current` = the cheapest bucket head (null when every bucket is empty).
    void PickCurrent();
    // Moves `node` to its place in its bucket for `cost` (a node already at the head stays there).
    void Requeue(SearchNode* node, int cost);
    // The current node's map cell and cost (cost 0x7FFFFFFF when there is none).
    void Best(int* x, int* y, int* cost);
    // The cost of map cell (x, y) if it is settled, else 0x7FFFFFFF.
    int CostAt(int x, int y);
    // The cost of stepping between `current` and `node` (neighbours), `factor` 0x400 straight or 0x5A8
    // diagonal; -1 when the step is impossible. A backward search prices the step from `node` into `current`.
    int StepCost(const SearchNode* node, int factor);
    // Settles `current`, relaxes its eight neighbours, picks the next current.
    void Expand();
    void ExpandAll();
    // Expands until map cell (x, y) is current; false when the search runs out (or is inactive).
    bool ExpandUntil(int x, int y);
    // The path from the start to map cell (x, y), walked back along the directions, into
    // `out` (emptied first; nothing when the cell is outside the window).
    void PathTo(int x, int y, PathCells* out);
    // Places the window over map cells (x0, y0) and (x1, y1): less than 32 apart, both on
    // the map; centred, both at least one cell inside. False (window unchanged) otherwise.
    bool PlaceWindow(int x0, int y0, int x1, int y1);
    void SetUnit(CUnit* unit);
    // Starts a search for `unit` at (x0, y0) with the window over it and (x1, y1).
    bool Begin(CUnit* unit, int x0, int y0, int x1, int y1);
    // Starts a search at (sx, sy) confined to the rectangle (x0, y0)-(x1, y1) (x0 <= x1,
    // y0 <= y1, at most 28 apart): the row below and the column right of it are made unusable.
    bool BeginInRect(int x0, int y0, int x1, int y1, int sx, int sy);
    bool BeginInRectForClass(int sizeClass, int x0, int y0, int x1, int y1, int sx, int sy);
    // Copies the reached cells' directions into the map's marks (CAIMap::marks) where they are still 0.
    void CommitDirections();
    // Saves the window's cell levels into `levels` (zeroing them in the map with `clear`).
    void SaveLevels(bool clear);
    void RestoreLevels();
    // Attaches the search to `map`: a fresh 34 x 34 node grid (the border unusable) and the row table.
    void Attach(CAIMap* map, bool backward);
    // Moves the window over (x0, y0)-(x1, y1) keeping what was searched: the nodes are shifted, then (with
    // `settleInside`) the ones cheaper than the cheapest kept border node are settled and the rest queued, or
    // (without) the settled stay and the reached are queued. False when the window cannot be placed
    // (unchanged) or moved 31 cells or more (the window and row table are already moved, the nodes not).
    bool Recenter(int x0, int y0, int x1, int y1, bool settleInside);
    // Attach(owner, false) when `owner` is not null.
    LocalSearch* Construct(CAIMap* owner);
    void Destruct();

    bool active;
    bool levelsSaved;                   // the window's cell levels are in `levels`
    SearchGrid<SearchNode> nodes;       // 34 x 34
    SearchGrid<std::uint8_t> levels;    // saved cell levels
    CAIMap* map;
    std::vector<AIMapCell*> cellRows;   // cellRows[i] = &map cell (originX, originY + i): the window's map rows
    SearchNode* settledList;            // linked through `next`
    SearchNode* buckets[4];             // open nodes by x & 3, sorted by cost
    SearchNode* current;                // the cheapest open node
    int originX, originY;               // map cell of window cell (0, 0)
    int currentCost;                    // cost of the node being expanded
    PassCostContext costs;
    int sizeClass;
    bool backward;                      // searching from the target (prices steps reversed)
    std::uint8_t level;                 // standing level
};

// The standing height of each of the 8 levels (size classes): the occluders' refresh and the
// movement's speed costs scale it to the cells' height units.
inline constexpr float kLevelHeights[8] = /*007912BC*/ {2.0f, 0.4f, 1.0f, 1.6f, 2.2f, 1.7f, 3.0f, 6.0f};

// The direction steps 1..8 of the block searches and paths (index 0 unused): 1 = (0, -1), then clockwise towards
// -x as in PathCell::Step.
inline constexpr int kDirectionDx[9] = /*00791108*/ {0, 0, -1, -1, -1, 0, 1, 1, 1};
inline constexpr int kDirectionDy[9] = /*0079112C*/ {0, -1, -1, 0, 1, 1, 1, 0, -1};

// A block of a block search: open records wait in sorted doubly linked buckets; a settled one moves to the
// search's `touched` chain (through `next`).
struct BlockRecord {
    int cost;                   // 0x7FFFFFFF unreached, -1 excluded
    BlockRecord* next;
    BlockRecord* prev;
    std::int16_t x, y;          // block coordinates
    int backDirection;          // 1..8: the way back to the block it was reached from; 0 none
    bool settled;
};

// Dijkstra over the blocks of one size class (the region map's forward and backward search):
// open records in eight buckets by x & 7, each sorted by cost.
struct BlockSearch {
    // Attaches to the region map: one record per block, all unreached.
    void Attach(CRegionMap* regionMap, bool fromTarget);
    // The path from the search's start to block (x, y) into `path` (emptied first).
    void PathTo(int x, int y, PathCells* path);
    // Moves `record` to its place in its bucket (a bucket head stays).
    void Requeue(BlockRecord* record);
    // Relaxes the neighbour of `best` in `direction` (1..8) through the open edge, its back direction `back`.
    void Relax(int direction, int back);
    // `best` = the cheapest bucket head (null when every bucket is empty).
    void PickBest();
    void Expand();

    SearchGrid<BlockRecord> records;
    CRegionMap* map;
    SearchGrid<std::int16_t>* edges;    // the size class's edge words (8 per block)
    BlockRecord* touched;               // settled and excluded records
    BlockRecord* open[8];
    BlockRecord* best;
    int bestCost;                       // cost of the record being expanded
    bool fromTarget;
    bool active;
};

struct CRegionMap {
    // The region of cell (x, y) for a size class, the one most of the reachable 3x3 blocks around the cell belong
    // to (the first seen on a tie; 0 when none).
    int DominantRegion(int sizeClass, int x, int y);
    // Can cell (x, y) reach `region`? Caches the answers in `marks` (the AI map's, CAIMap::marks: 1 yes, 2 no).
    bool ReachesRegion(int sizeClass, int x, int y, int region, SearchGrid<std::int8_t>* marks);
    // Numbers the connected areas of blocks of every size class.
    void LabelRegions();
    // The per-direction block edge words: -1 out of the map, and each edge between two blocks the smaller of its
    // two directions, for every size class.
    void ClampEdgeCosts();
    // Reads the block grids of every size class from the AI graph stream; returns the stream's state byte.
    bool LoadBlocks(CStream* stream);
    // The block grids as LoadBlocks reads them: width and height, then per size class each row's edge words,
    // representative cells and regions.
    void SaveBlocks(CStream* stream);
    // Per size class the representative cell of block (bx, by): (3, 3) unless a passable cell in rings from the
    // middle is better.
    void PickRepresentatives(class CAIMap* map, int bx, int by);
    // The chain of regions from (fromX, fromY) to (toX, toY) into `chain`, avoiding the blocks in `excluded`.
    bool FindRegionChain(int sizeClass, int fromX, int fromY, int toX, int toY, PathCells* chain,
                         std::vector<BlockPoint>* excluded);
    // Expands the two block searches until they meet and joins their paths into `chain`; false when one runs out.
    bool JoinBlockSearches(PathCells* chain);
    // Per size class the block (bx, by)'s edge words: the local search cost to each neighbour's representative
    // cell / 128. The first argument is not read.
    void MeasureBlockEdges(void* unused, int bx, int by);
    // Sizes the block grids for the map (8x8-cell blocks), sets up the local search and rebuilds the tables.
    void Attach(class CAIMap* map);
    // Whether cell (xx, yy) of block (bx, by) reaches both side edges or both end edges of the block.
    bool CellSuitsBlockAt(int sizeClass, int bx, int by, int xx, int yy);
    // Floods `label` over the blocks connected through open edges, breadth first from `frontier`.
    void FloodRegionFrom(int sizeClass, std::vector<BlockPoint>* frontier, std::vector<BlockPoint>* next, int label);
    // The 3x3 blocks around cell (x, y) whose representative cell the local search reaches from it, as {block x,
    // block y, cost} into `out` (emptied first).
    void NearbyBlocks(int sizeClass, bool flag, int x, int y, std::list<BlockCost>* out);

    // Re-measures blocks [x0, x1) x [y0, y1) (clamped to the grid): representative cells, then edges, row by
    // row (reporting progress), then the edge clamp, the two block searches and the regions.
    void UpdateBlocks(CAIMap* map, int x0, int x1, int y0, int y1);
    void UpdateBlocksFor(CAIMap* map, int x0, int x1, int y0, int y1);
    void AttachSearches();
    // Seeds `search` with the blocks around cell (x, y) its representative cells are reached from
    // (NearbyBlocks), cost / 128; true when one was lowered.
    bool SeedSearch(int sizeClass, bool flag, BlockSearch* search, int x, int y);

    // A progress report (0..1) during UpdateBlocks; cleared by it.
    using ProgressFn = void(*)(float);

    BlockSearch forward;
    BlockSearch backward;
    LocalSearch local;
    ProgressFn progress;
    SearchGrid<std::int16_t> edges[8];          // per size class: 8 edge words per block
    SearchGrid<std::uint8_t> cells[8];          // representative cell (x | y << 4)
    SearchGrid<std::uint16_t> regions[8];       // region per block
};
CRegionMap* ConstructRegionMap(CRegionMap* self);

// The path smoother's trellis: per path cell nine states, one per direction the unit leaves the cell in (index
// direction - 1; index 8 unused), each the cheapest cost so far and its back-pointer chain.
struct SmoothNode {
    int direction;              // the way back to the previous cell (1..8)
    SmoothNode* prev;
};
struct SmoothState {
    int cost;                   // 0x7FFFFFFF unreachable
    SmoothNode* node;
};
// A transition table entry: state `from` of the previous cell carries over to state `to`.
struct SmoothCopy {
    int from, to;
};
// A transition table step: from state `from` to state `to`, a step of `factor`
// between the cells at (dx, dy) and (dx + stepX, dy + stepY) from the current cell, with `turn`
// eighths of a turn; the reached state's node gets `direction`.
struct SmoothStep {
    int from, to;
    int dx, dy;
    int stepX, stepY;
    int turn;
    int factor;
    int direction;
};
static_assert(sizeof(SmoothStep) == 0x24);
// A state of the smoother's trellis as the tables' builder sees it: a move between two cells
// around a path cell, from (x0, y0) to (x1, y1) relative to it. `index` is its place among the cell's states.
struct SmoothEdge {
    int x0, y0;
    int x1, y1;
    int index;
};
struct SmoothEdges : std::vector<SmoothEdge> {
    // The seven states of a cell crossed by the straight step (mx, my), with (sx, sy) the step to its side.
    void AddStraightEdges(int mx, int my, int sx, int sy);
    // The nine states of a cell crossed by the diagonal step (mx, my), with (sx, sy) one of its straight halves.
    void AddDiagonalEdges(int mx, int my, int sx, int sy);
    // Emptied, then the states of a cell the path crosses in `direction` (1..8):
    // seven moves for a straight step, nine for a diagonal one. Direction 0 (the path's ends) gives the
    // eight moves from the cell to its neighbours, or from the neighbours to it when `incoming`.
    void BuildEdges(int direction, bool incoming);
};

// One of the 81 transition tables (previous direction * 9 + next direction; 0 at the path's end), built by a
// static initialiser.
struct SmoothTable {
    SmoothTable* Construct();
    void Destruct();
    // The table between a cell with the states `from` and the next one with the
    // states `to`: a copy for every state both have, a step from every `from` state to every `to`
    // state that starts where it ends, and the same between the `to` states themselves.
    void Build(const SmoothEdges* from, const SmoothEdges* to);

    std::vector<SmoothCopy> copies;
    std::vector<SmoothStep> steps;        // from the previous cell's states
    std::vector<SmoothStep> chained;      // between the new states
};
struct SmoothPool {
    // New nodes zeroed.
    void SetSize(int size);

    std::vector<SmoothNode> nodes;
    int zeroed;                     // set to 0 by each pass, not read
    SmoothNode* next;                   // the next free node
};
extern lib::StaticObject<SmoothPool> g_smoothPool;
extern lib::StaticObject<SmoothTable[81]> g_smoothTables;
inline SmoothPool& SmoothNodePool() { return *g_smoothPool; }
inline const SmoothTable* SmoothTables() { return g_smoothTables.Get(); }
// The direction (1..8 counter-clockwise from (0, -1), 0 for none) of the step (dx, dy) in -1..1:
// DirectionSteps()[dy * 3 + dx], from the middle of the table.
inline constexpr int kDirectionSteps[9] = /*00791978*/ {2, 1, 8, 3, 0, 7, 4, 5, 6};
inline const int* DirectionSteps() { return kDirectionSteps + 4; }
// What the table initialiser does, on any 81 constructed tables: for every pair of directions the states of
// the previous step (moved back by that step) against those of the next.
void BuildSmoothTablesAt(SmoothTable* tables);

struct PathSmoother {
    // Re-plans the path's cells (at least three; else 0) through a trellis of the eight directions per cell:
    // step costs, a turn cost against `startDirection` (1..8) and the previous step, and diagonal end states
    // charged (1 - fraction) * 2048. While a pass finds a cheaper path the cells are rebuilt from the tail
    // along it and the pass repeats. Returns the last pass's cost.
    int Smooth(CUnit* unit, CAIMap* map, PathCells* path, int startDirection, float fraction);
    // The states of the next cell (fresh nodes from the pool) from those of the current one through `table`.
    void Advance(const SmoothState* from, SmoothState* to, const SmoothTable* table);
    void Relax(const SmoothStep* step, const SmoothState* from, SmoothState* to);

    std::uint8_t level;             // standing level
    std::uint8_t unused_01[3];
    int sizeClass;
    CAIMap* map;
    int x, y;                       // the current cell
    PassCostContext costs;
};
static_assert(sizeof(PathSmoother) == 0x2C);

// A 4x4-cell block of one unit's levels.
struct UnitLayerNode {
    UnitLayerNode* next;        // next in the bucket, or in the free list
    int key;                    // (block y << 16) + block x, blocks of 4 cells
    std::int8_t levels[16];     // rows of 4
    CUnit* owner;
};

// The unit occupancy layers of CAIMap (small units and large units): every unit on the map is stamped as a
// 20x20-cell block of levels (its UnitShape, 16x16, at the unit's cell offset within a 4-cell block), stored
// as 4x4-cell nodes in a 1024-bucket hash keyed by block. Set: the block's levels replace the map's; Merge:
// the map keeps the larger level. The small-unit layer writes AIMapCell::level (unsigned maximum), the
// large-unit one the signed bytes of CAIMap::waterDepth.
class CUnitLayer {
public:
    // `map` is the owner's to set.
    CUnitLayer();
    ~CUnitLayer();

    virtual void Set(const UnitLayerNode* block) = 0;
    virtual void Merge(const UnitLayerNode* block) = 0;

    // Stamps `unit`'s shape (16x16 levels at shape + 8) with its top-left cell at
    // (x, y): one node per non-empty 4x4 block, each announced through Merge.
    void Add(CUnit* unit, const CUnitShape* shape, int x, int y);
    // Removes `unit`'s nodes from the 5x5 blocks from block (x >> 2, y >> 2); for each
    // block it had, Set gets the block with the other units' levels merged (maximum).
    void Remove(CUnit* unit, int x, int y);
    std::list<UnitLayerNode> nodes;
    UnitLayerNode* buckets[1024];   // by (key x * 17 + key y) & 0x3FF
    UnitLayerNode* freeNodes;       // unused nodes
    CAIMap* map;
};

class CSmallUnitLayer : public CUnitLayer {
public:
    void Set(const UnitLayerNode* block) override;
    void Merge(const UnitLayerNode* block) override;
};

class CLargeUnitLayer : public CUnitLayer {
public:
    void Set(const UnitLayerNode* block) override;
    void Merge(const UnitLayerNode* block) override;
};

// A level's special area (CAIMap::areaLists, ClearUnmarkedArea): the offset {dx, dy} of the cell
// whose mark decides.
struct AreaOffset {
    int dx, dy;
};

// The cells under a bridge (CAIMap::bridgeHeights): a hash map keyed (short)x << 16 | (short)y, the
// value the cell's saved ground level (low word), object height (bits 16-21) and occluder kind (bits 24-28).
struct BridgeHeights : std::unordered_map<std::uint32_t, std::uint32_t> {
    // Puts back what the cell's bridge map entry saved (ground level, object height, the occluder word's low five
    // bits) and erases the entry; nothing without one. x and y are read as shorts.
    void RestoreCell(int x, int y, AIMapCell* cell, std::uint32_t* occluder);
    // Bridge deck height of cell (x, y), 0 if none.
    int HeightAt(short x, short y) const;
};

// CAIMap's dirty rectangle: the area refreshed when a batch of refreshes ends.
struct AIDirtyRect {
    // Empty: the minimums 10000, the maximums 0.
    void Reset();

    int minX, maxX;
    int minY, maxY;
};

// The navigation grid of the current map (Rebirth patches Load).
class CAIMap {
public:
    CAIMap();
    ~CAIMap();
    void Reset();

    // Loads the map archive `path`: LoadTerrain, then AttachTerrain. Returns 1; 0 when the archive cannot be opened;
    // any other failure is a fatal error, whose result is returned.
    int Load(const char* path);
    // Load's first part: the archive's .mp file gives the height scale and the climb tables, tileDesc.reg in
    // aiinfo.res the terrain types; the cell grids are sized for the map (64 x 64 cells per sector) and every
    // sector file stamped into the cells and the occluder words. It touches nothing else of the map, so the
    // verify wrapper runs it on a copy.
    int LoadTerrain(const char* path);
    // Load's second part: the open marks and cost tables, the water depths zeroed, both local searches, the
    // region map, the AI events and the sector map attached to the new size, and the whole map refreshed (or
    // added to the dirty rectangle while batching).
    void AttachTerrain();

    AIEventTable* Events() { return &events; }

    CSectorMap* SectorMap() { return &sectorMap; }
    // refreshUnits = the low byte of `on`; returns the old byte.
    std::uint8_t SetRefreshUnits(int on);
    bool AddToSectors(CMapObject* object);
    bool RemoveFromSectors(CMapObject* object);
    // CSectorMap::Move (Rebirth patches it).
    bool MoveInSectors(CMapObject* object, const Vector3* position);
    void FindUnitsInRadius(const Vector3* position, std::list<CUnit*>* result, float radius);

    // Can `unit` stand on cell (x, y)? `reason` gets 0 (ok or not a unit), 1 (cell level
    // too high) or 2 (water too deep).
    bool CanStandAt(CUnit* unit, int x, int y, int* reason);

    // Like CanStandAt, but a cell no worse than the one the unit is on is also accepted.
    bool CanStandAtFromHere(CUnit* unit, int x, int y, int* reason);

    // IsPassable at the cell of world `position`, with the unit's own footprint lifted
    // (the unit is taken out of the sector map and put back).
    bool IsPassableWithoutSelf(CUnit* unit, const Vector3* position);

    bool IsUnitCellPassable(CUnit* unit);

    // Is the cell at world `position` passable for size class `sizeClass` (0..7)?
    bool IsPassableForClass(std::uint8_t sizeClass, const Vector3* position);

    // Can the unit leave its cell: is the cell or one of its 8 neighbours passable and
    // reachable with a positive climb cost?
    bool CanLeaveCell(CUnit* unit);

    // Moves `target` to the nearest cell (spiralling out from it) where `unit` can stand and that lies in the
    // unit's region; false if there is none. `target` is unchanged when its own cell qualifies.
    bool FindFreeCellNear(CUnit* unit, const Vector3* from, Vector3* target);

    // Moves `target` (world in, half-cell units out) to a cell `unit` can stand on: the
    // target cell itself, else the first good cell walking back towards the unit, at least
    // `minDistance` from the target. With `sameRegion`, only cells that reach the unit's region.
    bool SnapTarget(CUnit* unit, Vector3* target, bool sameRegion, float minDistance);

    bool FindPath(CUnit* unit, const Vector3* target, AIPath* path, float range, float minDistance);
    // The long-range plan from cell (fromX, fromY) to (toX, toY): region chain,
    // local-search legs between the regions' representative cells, then the cells into path->cells. On a failed
    // leg: aim at the best reached cell when it is far enough from the start, else (with `retry`) exclude the
    // region and plan again.
    bool PlanLongPath(CUnit* unit, int fromX, int fromY, int toX, int toY, AIPath* path, int retry, int maxCells);
    // Searches again if the last search failed and nothing is left, else moves on; true at the path's end.
    bool ContinuePath(CUnit* unit, AIPath* path);
    // Moves up to 32 cells from the path into the steps and points the unit at the next one.
    void AdvancePath(CUnit* unit, AIPath* path);

    // Visibility from `from` to `to` (world positions): 0 when terrain blocks the line,
    // else the product of the transparencies of the objects it passes (at least 0.0001).
    float LineOfSight(const Vector3* from, const Vector3* to);

    float GroundHeight(int x, int y);
    // Terrain height plus what stands on it, at `position`.
    float TopHeightAt(const Vector3* position);
    // Largest height step to the 4 neighbours of the cell at `position` (0 at the edge).
    float SlopeAt(const Vector3* position);
    bool CellFlag80At(const Vector3* position);
    bool HasKind3Occluder(int x, int y);
    bool HasKind3OccluderAt(const Vector3* position);
    // Material index (0..31) of the cell at `position`, -1 outside the map.
    int MaterialAt(const Vector3* position);
    void TurnObject(CMapObject* object, const Quaternion* rotation);
    // The cell's terrain type (AITerrainType); the first one outside the map.
    AITerrainType* MaterialRecordAt(const Vector3* position);
    // Has cell (x, y) a bridge occluder? `height` (optional) gets the highest deck.
    bool BridgeDeckAt(int x, int y, int* height);

    // Stamps (remove = false) or clears (remove = true) the object's footprint in the
    // grid; false if it does not fit. Units go to the unit occupancy layers instead.
    bool MarkFootprint(CMapObject* object, bool remove);
    // The footprint of a non-unit object into `footprint` and `bridges` (parts flagged 0x400); true with a bridge.
    bool CollectFootprint(CMapObject* object, FootprintCellSet* footprint, FootprintCellSet* bridges);
    // Applies a footprint to the occluder grid (stamp, undone if a cell fails) or clears it.
    bool ApplyFootprint(FootprintCellSet* set, bool remove);
    // Stamps a footprint and refreshes the passability around it; `tick` (the world's) becomes footprintTick.
    bool StampFootprint(int tick, FootprintCellSet* set);
    bool ClearFootprint(FootprintCellSet* set);

    // Starts collecting refreshes into the dirty rectangle (flushing a pending one).
    void BeginRefresh();
    void EndRefresh();

    // Occupancy shape for standing height `height` rounded up to tenths (cached; null at 0.2 or below).
    void* UnitShape(float standingHeight);
    // The first unit near world `position` whose occupancy shape blocks `unit` there; null if none.
    CUnit* BlockingUnitAt(CUnit* unit, const Vector3* position);
    // Fills each step's speed word (cost table / climb cost, in 1/512) and flag word.
    void SetStepSpeeds(CUnit* unit, PathCells* steps);
    // The passability cost tables for size class `sizeClass`; returns the climb table's middle.
    const int* CostContext(int sizeClass, struct PassCostContext* out);
    // CostContext for `unit`: its size class, speeds and the alternative table with CUnit::altPassCosts.
    void CostContextFor(CUnit* unit, struct PassCostContext* out);
    // Is the cell under `unit` passable for it at a cost below 12000? (A cell with a kind-3 occluder is
    // evaluated at the size class's level.) Only the low byte of the result is set when the unit is outside the
    // map.
    bool StandsOnCheapCell(CUnit* unit);
    // Marks in `grid` the cells within `radius` of `eye` that it sees: inside the cone around `look` (cos >=
    // `cosLimit`; none when cosLimit <= -1) and, unless `targetHeight` is below -100, with a line of sight to
    // the cell's ground raised by `targetHeight` that reaches at least `radius` * visibility.
    void MarkVisibleCells(const Vector3* eye, float radius, const Vector3* look, float cosLimit, float targetHeight,
                          FogGrid* grid);

    bool ApplyBridgeDecks(FootprintCellSet* set, bool remove);
    // The largest ground step between consecutive cells on the line, in world height; 1.0 for a zero-length line,
    // 100.0 when an end is off the map.
    double LargestStepAlong(const Vector3* from, const Vector3* to);
    // Joins the forward and backward local searches where they meet; false when they cannot.
    bool JoinLocalSearches(PathCells* cells, int* cost);
    // The path to cell (x, y) walked back along the direction marks into `path` (emptied first).
    void BuildCellsTo(int x, int y, PathCells* path);
    // Adds (or lifts) the water depths around `position` as a pseudo unit of the small-unit layer.
    void StampWaterPatch(const Vector3* position, bool add);
    bool AddOccluderRecord(FootprintCell* cell);
    bool ClearOccluderRecord(FootprintCell* cell);
    void RefreshArea(int minX, int maxX, int minY, int maxY);
    // The passability mark of cell (x, y) for a standing level from the terrain type, the water depth and the
    // occluders over the cell, looked up in the mark table.
    std::uint32_t CellMark(void* scratch, int level, int levelHeight, int x, int y);
    // Refresh's second pass over [x0, x1] x [y0, y1] (kept 2 cells from the border): a cell whose step to a direct
    // neighbour exceeds what size class 0 can climb becomes impassable, one beyond the larger classes keeps only
    // class 0; either way nibbles 5-6 of one neighbour and nibble 7 of 13 cells around it are cleared.
    void LimitSteepCells(int x0, int x1, int y0, int y1);
    // A level's special area {dx, dy}: in [x0, x1] x [y0, y1] (kept so that (x + dx, y + dy) is on the map) each
    // cell whose mark at (x + dx, y + dy) is 0 loses the level's passability nibble.
    void ClearUnmarkedArea(int level, int dx, int dy, int x0, int x1, int y0, int y1);

    CRegionMap* Regions() { return &regionMap; }

    // Clears the marks of the cells around the blocks of `chain` (8 x 8 cells each): their bounding box widened
    // by four blocks, clipped to the map. Nothing for an empty chain.
    void ClearMarksAround(const PathCells* chain);
    // Every terrain type's `openMarks`, set when the map loads: per size class the passability by
    // its cost (up to 1: 14, 2: 13, up to 9: 10, up to 34: 6, up to 70: 3, above: 0); size class 0 always gets 14.
    void BuildOpenMarks();

    // Refresh the rectangle now, or add it to the dirty rectangle while batching.
    void QueueRefresh(int minX, int maxX, int minY, int maxY);

    bool loaded;                        // set by Load
    bool batching;                      // refreshes collect into the dirty rectangle
    std::uint8_t refreshUnits;          // non-zero: a refresh also re-registers the units around it
    AIDirtyRect dirty;                  // the area to refresh when batching ends
    int passCosts[16];                  // cost per passability nibble (1/1024)
    int passCostsAlt[16];               // the same for units with CUnit::altPassCosts set
    int stepCosts[16];                  // step speeds per nibble (SetStepSpeeds)
    // Per size class: the cost of a height step between neighbouring cells, indexed from the middle (negative
    // steps down, positive up): 1023 costs in 1/1024, -1 for a step that cannot be taken (CAIMap::Load fills them).
    std::vector<int> climbLarge;              // size classes other than 0: no step steeper than 40 degrees
    std::vector<int> climbSmall;              // size class 0: up to 60 degrees, every step at the flat cost
    std::vector<AITerrainType> materials;     // the terrain types (16 after Load)
    BridgeHeights bridgeHeights;            // keyed (short)x << 16 | (short)y
    float heightScale;                  // world z to AI height units
    CSectorMap sectorMap;
    FootprintCellSet footprint;         // scratch for MarkFootprint
    FootprintCellSet bridges;
    SearchGrid<std::int8_t> waterDepth;  // per-cell water depth
    // the standing levels' special areas (ClearUnmarkedArea): RefreshArea reads the first list for levels
    // 0..4, the second for 5 and 6, the third for 7 (the other three are not read there).
    std::vector<AreaOffset> areaLists[6];
    CSmallUnitLayer unitLayerSmall;     // unit occupancy
    CLargeUnitLayer unitLayerLarge;
    PathSmoother smoother;
    CRegionMap regionMap;
    LocalSearch forwardSearch;
    LocalSearch backwardSearch;
    // the world tick of the last StampFootprint: an idle unit standing in a kind-3 occluder replans while its
    // motion order started before it.
    int footprintTick;
    SearchGrid<AIMapCell> cells;        // the map's cells (cells.width x cells.height)
    SearchGrid<std::uint32_t> occluders;  // per-cell occluder words (see LineOfSight)
    SearchGrid<std::int8_t> marks;      // per-cell marks: path directions (BuildCellsTo), reach answers
    AIEventTable events;                // the world's AI events (CWorldServer::AIEvents)
};

// A cached lookup in the global spiral of cell offsets (pairs of int16 dx, dy ordered by distance).
struct SpiralCache {
    // The offsets, with `length` set to how many lie within `radius` (cached for the last radius asked).
    const std::int16_t* Get(float radius, int* length);

    int length;
    float radius;
};

// World coordinate to AI map cell (half-unit cells), as every AI map function computes it: rounded to float,
// then to the nearest integer.
int WorldToCell(float world);

void InitCostTables(CAIMap* map);
// Standing level of size class `sizeClass` (0..7): (2 - height) * 16, at least 0. Outside 0..7 the original
// returns only the low byte 0 (the rest of the result is the argument), reproduced here.
int LevelForSizeClass(int sizeClass);
// Standing level of `unit`: (2 - standingHeight) * 16, at least 0.
int StandingLevel(const CUnit* unit);
