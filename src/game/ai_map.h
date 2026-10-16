#pragma once

#include "engine_object.h"
#include "game_data.h"
#include "maths.h"
#include "sector_map.h"
#include "unit.h"

#include "lib/containers.h"
#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>

// One AI map cell (9 bytes): a 4-bit passability field per unit size class, then heights.
#pragma pack(push, 1)
struct AIMapCell {
    std::uint32_t passable;     // bits 4*sizeClass .. +3: non-zero = passable for that class
    std::uint16_t groundLevel;  // +4 terrain height in AI height units; blocks lines of sight
    std::uint8_t flags;         // +6 bits 0-5: height of what stands on the ground; 0x40: the
                                //    cell is under a bridge (surface height in CAIMap::bridgeHeights)
    std::uint8_t unused_7;
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
    std::uint8_t padding;               // +0x13 zeroed by AddCell; the original leaves it uninitialised
    int next;                   // +0x14 next index in the same hash bucket, -1 ends
};
static_assert(sizeof(FootprintCell) == 0x18);

// A growable array of FootprintCell with a 16-bucket hash on (x + y). Name provisional.
struct FootprintCellSet {
    void AlongDirection(CAIMap* map, const Vector3* position, float width, const Vector3* direction);
    void DiscAt(CAIMap* map, const Vector3* position, float radius);
    // 0067E4E0 (A-Z1; returns the set). The constructor as the spells' global set 007C8478 gets it
    // from its static initialiser 0067E4C0 (before the hooks; compared at start-up): an empty
    // array, `hashed` set. The buckets are not written (the global's are zero until the first
    // footprint empties them).
    FootprintCellSet* Construct();

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

    // +0x00 the cells, chained per bucket through FootprintCell::next. The engine's aligned array
    // (SetSize above is its out-of-line copy).
    lib::AlignedArray<FootprintCell> cells;
    int buckets[16];        // +0x10 first index per (x + y) & 15, -1 when empty
    bool hashed;            // +0x50 set by Construct and by every insertion, never cleared
    std::uint8_t unused_51[3];
};
static_assert(offsetof(FootprintCellSet, buckets) == 0x10);
static_assert(offsetof(FootprintCellSet, hashed) == 0x50);
static_assert(sizeof(FootprintCellSet) == 0x54);

// A box of an object's model that blocks the AI map (CEngineObject + 0x160, 0x38 bytes each).
struct FootprintPart {
    std::uint32_t flags;    // bits 3-5 kind, 6-9 transparency, 0x400 bridge, 0x800 ignored
    Vector3 offset;         // +0x04
    std::uint32_t radius;               // +0x10 float bits: the occluder record's radius (world_client_maps.cpp)
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
    float backwardSpeed;    // +0x0C 0.1, or the unit's (CostContextFor)
    float turnScale;                    // +0x10 10: scales the turn cost of a path step
    float forwardSpeed;     // +0x14 0.1, or the unit's
};
static_assert(sizeof(PassCostContext) == 0x18);

// A growable square of byte marks over map cells (0x18 bytes), filled by MarkVisibleCells.
struct VisibilityGrid {
    // 005C8D40 (forwarder). Grows the grid to cover (x - n .. x + n, y - n .. y + n); the first
    // call (x = 0x7FFFFFFF) sizes and clears it.
    void Include(int x, int y, int n);
    // The rewrite of 005C8D40 (ai_movement.cpp); Include stays the forwarder to its address.
    void Grow(int x, int y, int n);

    int originX, originY;       // cell of data[0]; 0x7FFFFFFF before the first Include
    std::uint8_t* data;         // +0x08 width * height
    std::uint8_t** rows;        // +0x0C rows[i] = data + i * width
    int width, height;          // +0x10
};
static_assert(sizeof(VisibilityGrid) == 0x18);

// A cell of a path (CList element, 8 bytes). Layout provisional.
struct DeltaField;   // net_delta.h

struct PathCell {
    std::int16_t x, y;
    std::uint16_t flags;        // 0x200 on a path that is a single cell
    std::uint16_t terrainBits;          // +0x06 bit 0 the cell's flags bit 7, bit 1 a costly cell (path state bits 4-5)

    // 005C5CC0 (RET 8). The neighbour in direction 1..8 (1 = y - 1, then clockwise towards -x: 2 =
    // (-1, -1), 3 = (-1, 0), 4 = (-1, +1), 5 = (0, +1), 6 = (+1, +1), 7 = (+1, 0), 8 = (+1, -1)) into
    // `out` (x and y only); any other direction copies the whole cell. Returns `out`.
    PathCell* Step(PathCell* out, int direction) const;
};
static_assert(sizeof(PathCell) == 8);

// The vtable of CList<PathCell> (PathCellList).
inline void* PathCellListVtable() { return reinterpret_cast<void*>(0x0073EDB0); }

// ConstructElements of CList<PathCell>: the instantiation's own (00539980, ai_path_cells.cpp).
struct PathCellElements {
    static void Construct(PathCell* cells, int count);
    static void Destruct(PathCell*, int) {}
};
// MFC CList<PathCell, PathCell&> (0x1C bytes); the game inlines most of its operations. Registered
// on the template: NewNode 005B1B70 (ai_path.cpp), AddHead / AddTail 005BD200 / 005BD2A0
// (ai_path_cells.cpp). RemoveAll is 00556D40's code.
using PathCellList = mfc::CList<PathCell, PathCellElements>;
static_assert(sizeof(PathCellList::Node) == 0x10);

// The speed word and terrain bits a path builder gives its cells (SV7d2): the plain step speed, no
// terrain bits. The originals leave stack leftovers there (SearchPathTo 005C3200 the caller's
// register, BuildCellsTo 005B9A20 a search's row offset, PathSmoother::Smooth 005C46C0 the open cost
// or a list vtable); the harness patches them to this too (tests/verify/harness_path_cells.cpp).
// SetStepSpeeds rewrites both words of every step but the last, whose speed word becomes
// UnitMotion's last step speed (read only by a single-cell trajectory).
constexpr std::uint16_t kPlainStepSpeed = 0x200;
inline void SetPlainCellWords(PathCell* cell) {
    cell->flags = kPlainStepSpeed;
    cell->terrainBits = 0;
}
inline void SetPlainCellWords(PathCellList* list) {
    for (PathCellList::Node* node = list->head; node; node = node->next) SetPlainCellWords(&node->data);
}

// 005C5DA0 (thiscall, RET 4; ai_path_cells.cpp). Appends copies of `other`'s cells to `list`.
void __fastcall PathCellsAppend(PathCellList* list, void*, const PathCellList* other);
// 005C5E60 (thiscall, RET 4). Constructs `out` (blocks of 0x80) holding `list`'s cells in reverse
// order; returns `out`.
PathCellList* __fastcall PathCellsReversed(const PathCellList* list, void*, PathCellList* out);
// 005C6000 (thiscall). Removes each cell equal (x, y) to the one before it (the first is compared
// with (-1, -1)); an emptied list frees its blocks.
void __fastcall PathCellsRemoveRepeats(PathCellList* list);

// A block a search reaches: {block x, block y, cost}.
struct BlockCost {
    int x, y, cost;
};
// CList<BlockCost>'s ConstructElements is the instantiation's own (005B1C70, zero-fill).
struct BlockCostElements {
    static void Construct(BlockCost* elements, int count);   // ai_region_reach.cpp
    static void Destruct(BlockCost*, int) {}
};
// The CList of reached blocks (0x1C bytes, nodes 0x14; vtable 0073FCFC).
using BlockCostList = mfc::CList<BlockCost, BlockCostElements>;
inline void* BlockCostListVtable() { return reinterpret_cast<void*>(0x0073FCFC); }
static_assert(sizeof(BlockCostList::Node) == 0x14);

struct CRegionMap;


// A block of the region map, or a cell: {x, y}. The excluded blocks of a plan (AIPath) and the
// region map's work lists are CArray<BlockPoint> (constructor 00557230, SetSize 005B16D0).
struct BlockPoint {
    int x, y;
};
using BlockPointArray = mfc::CArray<BlockPoint>;

// Per size class tables of the cost of a height step between neighbouring cells, indexed from the
// middle (negative steps down, positive up): an MFC CArray<int> of 1023 costs in 1/1024, -1 for a
// step that cannot be taken (CAIMap::Load fills them).
using ClimbTable = mfc::CArray<int>;


// The first 0x20 bytes of AIPath: what the unit is told to do. Kinds:
//   0 stand at (x, y) facing `angle` (set by Start / Reset),
//   1 turn to direction `arg5` (an angle) at rate `arg6` (0: the unit's own rate),
//   2 follow the steps to the target (arg5, arg6) (SetTarget),
//   3 track object `arg5` between times arg6 and arg7,
//   4 turn to direction `arg5`.
// (x, y) and targets are in half-cell units (world * 2).
struct MotionOrder {
    int kind;           // +0x00
    float x, y;         // +0x04 where the order starts
    float angle;        // +0x0C facing at the start
    int startTime;      // +0x10 world time the order started
    std::uint32_t arg5; // +0x14 float or object id, by kind
    std::uint32_t arg6; // +0x18
    std::uint32_t arg7; // +0x1C
};
static_assert(sizeof(MotionOrder) == 0x20);

// A unit's path (name and layout provisional). Its methods at 005C7xxx steer along `steps`.
struct AIPath {
    void LoadPath(CStream* stream);
    void SavePath(CStream* stream);
    void StandAndTurn(int time, std::uint8_t* unit, std::uint32_t direction, std::uint32_t rate);
    void StandAndTrack(int time, std::uint8_t* unit, std::uint32_t id, std::uint32_t from, std::uint32_t to);
    void StandAndTurnUntil(int time, std::uint8_t* unit, std::uint32_t direction, std::uint32_t a, std::uint32_t b);
    void StandStill(std::uint8_t* unit);
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
    // 005C7DB0 / 005C7FC0 (RET 4; ai_path_state.cpp). The order (kind byte, the steps' count as two
    // bytes, start x / y, the angle within +-007C4378, the time and three arguments) on the field's
    // delta channels, then each step as a 16-bit header (x and y codes: one less, the same, one more
    // than the previous step, or written out after it; unknown_6 & 3; the flags up to 0x3FF) on both
    // outputs. ReadState empties the steps and reads them back.
    void WriteState(DeltaField* field);
    void ReadState(DeltaField* field);
    // 005B23A0. Empties the path, the steps and the order.
    void Clear();
    // 005B22C0 (RET 4; ai_path_cells.cpp). The path as a motion order in `out` (its step list
    // constructed): the order, the steps; and for an order that follows steps (kind 2) the rest
    // of the path appended, repeats removed, every step's speed 0x200 and the target set.
    void ToMotionOrder(AIPath* out) const;
    // 005C8230 / 005C8370 (RET 4; ai_path_save.cpp). The order and its steps in the save stream:
    // kind, x / y, angle, start time and the three arguments (4, 8, 4, 4, 0xC bytes), the steps'
    // count, then each step (8 bytes). Loading appends that many steps and reads over every step
    // from the head.
    void LoadOrder(CStream* stream);
    void SaveOrder(CStream* stream);
    // 006706A0 (player_server_handlers.cpp). An out-of-line copy of the inlined constructor: both
    // cell lists empty (blocks of 0x80), no excluded blocks (vtable kExcludedBlocksVtable), then
    // Clear. Returns this.
    // The destructor body is 00546100 (Destruct).
    AIPath* Construct();
    // 0054EAC0 (ai_path_cells.cpp, A-W14). The constructor of a motion order (the first 0x3C bytes
    // of a path): the step list empty, blocks of 0x80; the order itself is left as it is. Returns
    // this.
    AIPath* ConstructSteps();
    // 00546100 (combat_lifetime.cpp, A-U7). The destructor body: the excluded blocks freed (vtable
    // 0073F07C), then both cell lists emptied with their vtable (0073EDB0).
    void Destruct();
    // 00670730 (RET 4). The search's target cell (targetX, targetY) in world units: each times
    // 0074326C * 0073B86C, z 0. Returns `out`.
    Vector3* TargetPoint(Vector3* out) const;

    static constexpr std::uintptr_t kExcludedBlocksVtable = std::uintptr_t(0x0073F07C);

    MotionOrder motionOrder;    // +0x00 (ai_motion.cpp)
    PathCellList steps;         // +0x20 the next cells to walk (at most 32)
    PathCellList cells;         // +0x3C the rest of the path
    float targetX, targetY;     // +0x58 in half-cells
    Vector3 goal;               // +0x60 world position asked for
    bool reached;               // +0x6C no cells left
    bool failed;                // +0x6D the last search failed; search again
    bool flag6E, flag6F;        // +0x6E, +0x6F
    // +0x70 the blocks the long-range plan avoids (PlanLongPath adds the region it failed in;
    // vtable 0073F07C, constructor 00557230, destructor 00557250).
    BlockPointArray excludedBlocks;
    int smoothing;              // +0x84 from 005C46C0 (how much longer the path is than the way)
};
static_assert(offsetof(AIPath, steps) == 0x20);
static_assert(offsetof(AIPath, excludedBlocks) == 0x70);
static_assert(offsetof(AIPath, cells) == 0x3C);
static_assert(offsetof(AIPath, goal) == 0x60);
static_assert(offsetof(AIPath, smoothing) == 0x84);

class CAIMap;
class CSectorMap;

// A terrain type of the AI map (0x118 bytes; CAIMap::materials, indexed by the low 5 bits of a
// cell's occluder word): a section of tileDesc.reg in the text resources' aiinfo.res, whose name
// is the type's number (CAIMap::Load reads them). Name provisional.
struct AITerrainType {
    char name[0x100];           // +0x000 "Name"
    int speed;                  // +0x100 "Speed" (-1 without one)
    int cost;                   // +0x104 "CostMul": what a cell of it costs (CellMark's starting cost)
    float look;                 // +0x108 "StepLook" in hundredths
    float smell;                // +0x10C "StepSmell" in hundredths
    float noise;                // +0x110 "StepSound" in hundredths: how loud steps on it are (AIViewer::StepNoise)
    std::uint32_t openMarks;    // +0x114 per size class nibble: the mark of a cell with nothing on it
};
static_assert(offsetof(AITerrainType, cost) == 0x104 && offsetof(AITerrainType, noise) == 0x110);
static_assert(sizeof(AITerrainType) == 0x118);

// A cell of the local search's window (0x14 bytes). Name provisional.
struct SearchNode {
    int cost;                   // +0x00 0x7FFFFFFF until reached
    SearchNode* next;           // +0x04 in its bucket, or in the settled list
    SearchNode* prev;           // +0x08 in its bucket
    std::int16_t x, y;          // +0x0C window coordinates (0..33)
    bool usable;                // +0x10 false on the window's border and outside a rectangle
    bool settled;               // +0x11 expanded, or the start
    std::int8_t direction;      // +0x12 1..8: the way back to the cell it was reached from; 0 none
    std::uint8_t unused_13;
};
static_assert(sizeof(SearchNode) == 0x14);

// {data, rows, width, height}: a grid allocated in one block with a row pointer table (the game's
// template; its instantiations are in ai_grids.h).
template <typename T>
struct SearchGrid {
    T* data;
    T** rows;
    int width, height;

    // ai_grids.cpp. 005CD3A0 (RET 8; the visibility marks, VisibilityGrid + 8). Reallocated to
    // newWidth x newHeight (the cells are not kept or cleared) unless the grid already has that size.
    void Resize(int newWidth, int newHeight);
    // 005CD410 (RET 4) / 005C4BA0 (RET 4; A-W14: the local search's nodes, whose Recenter 005C25D0
    // keeps the old window in such a copy). Makes the grid a copy of `source`: its size, a block and
    // row table of its own, the cells copied.
    void CopyFrom(const SearchGrid* source);
    // 005BCBD0 / 005BCC50 / 005BCEE0 / 005BCF00 (RET 8). The constructor for a size; returns this.
    SearchGrid* Construct(int newWidth, int newHeight);
};
template <> void SearchGrid<std::uint8_t>::Resize(int newWidth, int newHeight);
template <> void SearchGrid<std::uint8_t>::CopyFrom(const SearchGrid* source);
template <> void SearchGrid<SearchNode>::CopyFrom(const SearchGrid* source);
template <> SearchGrid<std::int8_t>* SearchGrid<std::int8_t>::Construct(int newWidth, int newHeight);
template <> SearchGrid<SearchNode>* SearchGrid<SearchNode>::Construct(int newWidth, int newHeight);
template <> SearchGrid<AIMapCell>* SearchGrid<AIMapCell>::Construct(int newWidth, int newHeight);
template <> SearchGrid<std::uint32_t>* SearchGrid<std::uint32_t>::Construct(int newWidth, int newHeight);
static_assert(sizeof(SearchGrid<SearchNode>) == 0x10);

// The local search (0x80 bytes; CAIMap + 0x2594 forward, + 0x2614 backward, and the region map's):
// Dijkstra over a 34 x 34 window of map cells whose border is never entered. Open nodes wait in
// four sorted lists chosen by the low two bits of the node's x; `current` is the cheapest head.
// Methods in ai_local_search.cpp. Name provisional.
struct LocalSearch {
    // 005C35F0. Puts every settled and queued node back to unreached; the search is inactive.
    void Reset();
    // 005C2F50. `current` = the cheapest bucket head (null when every bucket is empty).
    void PickCurrent();
    // 005C2FB0. Moves `node` to its place in its bucket for `cost` (a node already at the head
    // stays there).
    void Requeue(SearchNode* node, int cost);
    // 005C3050. The current node's map cell and cost (cost 0x7FFFFFFF when there is none).
    void Best(int* x, int* y, int* cost);
    // 005C3090. The cost of map cell (x, y) if it is settled, else 0x7FFFFFFF.
    int CostAt(int x, int y);
    // 005C1D50. The cost of stepping between `current` and `node` (neighbours), `factor` 0x400
    // straight or 0x5A8 diagonal; -1 when the step is impossible. A backward search prices the
    // step from `node` into `current`.
    int StepCost(const SearchNode* node, int factor);
    // 005C2D40. Settles `current`, relaxes its eight neighbours, picks the next current.
    void Expand();
    // 005C30E0. Expands until nothing is left (when active).
    void ExpandAll();
    // 005C31A0. Expands until map cell (x, y) is current; false when the search runs out (or is
    // inactive).
    bool ExpandUntil(int x, int y);
    // 005C3200. The path from the start to map cell (x, y), walked back along the directions, into
    // `out` (emptied first; nothing when the cell is outside the window).
    void PathTo(int x, int y, PathCellList* out);
    // 005C22A0. Places the window over map cells (x0, y0) and (x1, y1): less than 32 apart, both on
    // the map; centred, both at least one cell inside. False (window unchanged) otherwise.
    bool PlaceWindow(int x0, int y0, int x1, int y1);
    // 005C21A0. Takes the unit's cost tables, size class and standing level.
    void SetUnit(CUnit* unit);
    // 005C2420. Starts a search for `unit` at (x0, y0) with the window over it and (x1, y1).
    bool Begin(CUnit* unit, int x0, int y0, int x1, int y1);
    // 005C24F0. Begin for a size class instead of a unit.
    bool BeginForClass(int sizeClass, int x0, int y0, int x1, int y1);
    // 005C2B10. Starts a search at (sx, sy) confined to the rectangle (x0, y0)-(x1, y1) (x0 <= x1,
    // y0 <= y1, at most 28 apart): the row below and the column right of it are made unusable.
    bool BeginInRect(int x0, int y0, int x1, int y1, int sx, int sy);
    // 005C2C90 / 005C2CF0. BeginInRect for a unit / a size class.
    bool BeginInRectFor(CUnit* unit, int x0, int y0, int x1, int y1, int sx, int sy);
    bool BeginInRectForClass(int sizeClass, int x0, int y0, int x1, int y1, int sx, int sy);
    // 005C2A30. Copies the reached cells' directions into the map's marks (CAIMap::marks) where
    // they are still 0.
    void CommitDirections();
    // 005C33F0. Saves the window's cell levels into `levels` (zeroing them in the map with `clear`).
    void SaveLevels(bool clear);
    // 005C3530. Puts the saved cell levels back.
    void RestoreLevels();
    // 005C1EA0 (RET 8). Attaches the search to `map`: a fresh 34 x 34 node grid (the border
    // unusable) and the row table.
    void Attach(CAIMap* map, bool backward);
    // 005C25D0 (RET 0x14). Moves the window over (x0, y0)-(x1, y1) keeping what was searched: the
    // nodes are shifted, then (with `settleInside`) the ones cheaper than the cheapest kept border
    // node are settled and the rest queued, or (without) the settled stay and the reached are
    // queued. False when the window cannot be placed (unchanged) or moved 31 cells or more (the
    // window and row table are already moved, the nodes not).
    bool Recenter(int x0, int y0, int x1, int y1, bool settleInside);
    // 005B3FF0 (RET 4; the constructor; ai_grids.cpp). 1 x 1 node and level grids, no map rows, no
    // map; then Attach(owner, false) when `owner` is not null. Returns this.
    LocalSearch* Construct(CAIMap* owner);
    // 0058AA60 (ai_grids.cpp). The destructor: the row table's vtable back and its storage freed,
    // then the level and node grids freed.
    void Destruct();

    bool active;                        // +0x00
    bool levelsSaved;                   // +0x01 the window's cell levels are in `levels`
    std::uint8_t unused_02[2];
    SearchGrid<SearchNode> nodes;       // +0x04 34 x 34
    SearchGrid<std::uint8_t> levels;    // +0x14 saved cell levels (005C33F0 / 005C3530)
    CAIMap* map;                        // +0x24
    void* cellRowsVtable;               // +0x28 CArray<AIMapCell*>: the window's map rows
    AIMapCell** cellRows;               // +0x2C cellRows[i] = &map cell (originX, originY + i)
    int cellRowCount;                   // +0x30
    int cellRowMax;                     // +0x34
    int cellRowGrowBy;                  // +0x38
    SearchNode* settledList;            // +0x3C linked through `next`
    SearchNode* buckets[4];             // +0x40 open nodes by x & 3, sorted by cost
    SearchNode* current;                // +0x50 the cheapest open node
    int originX, originY;               // +0x54 map cell of window cell (0, 0)
    int currentCost;                    // +0x5C cost of the node being expanded
    PassCostContext costs;              // +0x60
    int sizeClass;                      // +0x78
    bool backward;                      // +0x7C searching from the target (prices steps reversed)
    std::uint8_t level;                 // +0x7D standing level
    std::uint8_t unused_7E[2];
};
static_assert(offsetof(LocalSearch, map) == 0x24);
static_assert(offsetof(LocalSearch, settledList) == 0x3C);
static_assert(offsetof(LocalSearch, costs) == 0x60);
static_assert(offsetof(LocalSearch, backward) == 0x7C);
static_assert(sizeof(LocalSearch) == 0x80);

// The direction steps 1..8 of the block searches and paths (00791108 / 0079112C; index 0 unused):
// 1 = (0, -1), then clockwise towards -x as in PathCell::Step.
inline const int* DirectionDx() { return ei::GameData<const int[9]>(0x00791108); }
inline const int* DirectionDy() { return ei::GameData<const int[9]>(0x0079112C); }

// A block of a block search (24 bytes): open records wait in sorted doubly linked buckets; a
// settled one moves to the search's `touched` chain (through `next`).
struct BlockRecord {
    int cost;                   // 0x7FFFFFFF unreached, -1 excluded
    BlockRecord* next;
    BlockRecord* prev;          // +0x08
    std::int16_t x, y;          // +0x0C block coordinates
    int backDirection;          // +0x10 1..8: the way back to the block it was reached from; 0 none
    bool settled;               // +0x14
    std::uint8_t unused_15[3];
};
static_assert(sizeof(BlockRecord) == 24);

// Dijkstra over the blocks of one size class (CRegionMap + 0 from the start, + 0x4C from the
// target; 0x4C bytes): open records in eight buckets by x & 7, each sorted by cost. Methods in
// ai_block_search.cpp.
struct BlockSearch {
    // 005AED60 (RET 8). Attaches to the region map: one record per block, all unreached.
    void Attach(CRegionMap* regionMap, bool fromTarget);
    // 005AEE30 (RET 4). Moves `record` to its place in its bucket (a bucket head stays).
    void Requeue(BlockRecord* record);
    // 005AEEC0 (RET 8). Relaxes the neighbour of `best` in `direction` (1..8) through the open edge,
    // its back direction `back`.
    void Relax(int direction, int back);
    // 005AEFF0. `best` = the cheapest bucket head (null when every bucket is empty).
    void PickBest();
    // 005AF0C0. Settles `best`, relaxes its eight neighbours, picks the next best.
    void Expand();

    SearchGrid<BlockRecord> records;    // +0x00
    CRegionMap* map;                    // +0x10
    SearchGrid<std::int16_t>* edges;    // +0x14 the size class's edge words (8 per block)
    BlockRecord* touched;               // +0x18 settled and excluded records
    BlockRecord* open[8];               // +0x1C
    BlockRecord* best;                  // +0x3C
    int bestCost;                       // +0x40 cost of the record being expanded
    std::uint8_t unused_44[4];
    bool fromTarget;                    // +0x48
    bool active;                        // +0x49
    std::uint8_t unused_4A[2];
};
static_assert(offsetof(BlockSearch, map) == 0x10);
static_assert(offsetof(BlockSearch, open) == 0x1C);
static_assert(offsetof(BlockSearch, bestCost) == 0x40);
static_assert(sizeof(BlockSearch) == 0x4C);

// The region map at CAIMap + 0x22F8 (0x29C bytes): connected areas of 8x8-cell blocks per size class.
struct CRegionMap {
    // 005B0600. Region of cell (x, y) for a size class, in AX (the rest of EAX is returned as the
    // original leaves it).
    int RegionAt(int sizeClass, int x, int y);
    // The rewrite of 005B0600 (ai_region_reach.cpp): the region most of the reachable 3x3 blocks
    // around the cell belong to (the first seen on a tie; 0 when none); EAX's upper half is 0.
    int DominantRegion(int sizeClass, int x, int y);
    // 005B07C0 (ai_region_reach.cpp). Can cell (x, y) reach `region`? Caches the answers in `marks` (the AI
    // map's, CAIMap::marks: 1 yes, 2 no).
    bool ReachesRegion(int sizeClass, int x, int y, int region, SearchGrid<std::int8_t>* marks);
    // 005B0050 (ai_region_reach.cpp). Numbers the connected areas of blocks of every size class.
    void LabelRegions();
    // 005B0F70 (ai_region_reach.cpp). The per-direction block edge words: -1 out of the map, and
    // each edge between two blocks the smaller of its two directions, for every size class.
    void ClampEdgeCosts();
    // 005B1180 (RET 4, AL; ai_region_reach.cpp). Reads the block grids of every size class from
    // the AI graph stream (width and height 1..256 first); returns the stream's state byte (+5).
    bool LoadBlocks(CStream* stream);
    // 005B1380 (RET 4; the save, 00597BD0). The block grids as LoadBlocks reads them: width and
    // height, then per size class each row's edge words, representative cells and regions.
    void SaveBlocks(CStream* stream);
    // 005AF900 (RET 0xC; ai_region_reach.cpp). Per size class the representative cell of block
    // (bx, by): (3, 3) unless a passable cell in rings from the middle is better (005AF7B0).
    void PickRepresentatives(class CAIMap* map, int bx, int by);
    // 005B0D90 (RET 0x1C, AL; ai_region_reach.cpp). The chain of regions from (fromX, fromY) to
    // (toX, toY) into `chain` (a CList), avoiding the blocks in `excluded` (CArray<{x, y}>).
    bool FindRegionChain(int sizeClass, int fromX, int fromY, int toX, int toY, void* chain, void* excluded);
    // 005AF5F0 (RET 4, AL; ai_region_reach.cpp). Expands the two block searches until they meet and
    // joins their paths into `chain`; false when one runs out.
    bool JoinBlockSearches(PathCellList* chain);
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
    void NearbyBlocks(int sizeClass, bool flag, int x, int y, BlockCostList* out);

    // 005AFC70 (RET 0x14; ai_block_search.cpp). Re-measures blocks [x0, x1) x [y0, y1) (clamped to
    // the grid): representative cells, then edges, row by row (reporting progress), then the
    // edge clamp, the two block searches and the regions. The first argument is the AI map.
    void UpdateBlocks(CAIMap* map, int x0, int x1, int y0, int y1);
    // 005AFEE0 (RET 0x14). UpdateBlocks (through its address).
    void UpdateBlocksFor(CAIMap* map, int x0, int x1, int y0, int y1);
    // 005AFEC0 (ai_block_search.cpp). Re-attaches both block searches.
    void AttachSearches();
    // 005B0AF0 (RET 0x14, AL; ai_block_search.cpp). Seeds `search` with the blocks around cell
    // (x, y) its representative cells are reached from (NearbyBlocks), cost / 128; true when one
    // was lowered.
    bool SeedSearch(int sizeClass, bool flag, BlockSearch* search, int x, int y);

    // A progress report (stdcall, 0..1) during UpdateBlocks; cleared by it.
    using ProgressFn = void(__stdcall*)(float);

    BlockSearch forward;                        // +0x000
    BlockSearch backward;                       // +0x04C
    LocalSearch local;                          // +0x098
    ProgressFn progress;                        // +0x118
    SearchGrid<std::int16_t> edges[8];          // +0x11C per size class: 8 edge words per block
    SearchGrid<std::uint8_t> cells[8];          // +0x19C representative cell (x | y << 4)
    SearchGrid<std::uint16_t> regions[8];       // +0x21C region per block
};
static_assert(offsetof(CRegionMap, local) == 0x98);
static_assert(offsetof(CRegionMap, progress) == 0x118);
static_assert(offsetof(CRegionMap, edges) == 0x11C);
static_assert(offsetof(CRegionMap, regions) == 0x21C);
static_assert(sizeof(CRegionMap) == 0x29C);

// The region map's constructor (005AF3D0, ai_region_reach.cpp) hands these to the CRT's `eh
// vector constructor iterator` for its three grid arrays: the grids' constructors 005AF580 /
// 005AF590 / 005AF5A0 and destructors 00598440 / 00598460 / 00598480 (edges, cells, regions); and
// stores the vtable of the local search's row array (CArray<AIMapCell*>, 0073FA04).
constexpr std::uintptr_t kEdgeGridConstructor = std::uintptr_t(0x005AF580);
constexpr std::uintptr_t kEdgeGridDestructor = std::uintptr_t(0x00598440);
constexpr std::uintptr_t kCellGridConstructor = std::uintptr_t(0x005AF590);
constexpr std::uintptr_t kCellGridDestructor = std::uintptr_t(0x00598460);
constexpr std::uintptr_t kRegionGridConstructor = std::uintptr_t(0x005AF5A0);
constexpr std::uintptr_t kRegionGridDestructor = std::uintptr_t(0x00598480);
constexpr std::uintptr_t kCellRowArrayVtable = std::uintptr_t(0x0073FA04);

// The path smoother's trellis (005C46C0): per path cell nine states, one per direction the unit
// leaves the cell in (index direction - 1; index 8 unused), each the cheapest cost so far and its
// back-pointer chain. Names provisional.
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
// A transition table step (0x24 bytes): from state `from` to state `to`, a step of `factor`
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
// A state of the smoother's trellis as the tables' builder sees it (0x14 bytes): a move between two
// cells around a path cell, from (x0, y0) to (x1, y1) relative to it. `index` is its place among the
// cell's states.
struct SmoothEdge {
    int x0, y0;
    int x1, y1;
    int index;
};
static_assert(sizeof(SmoothEdge) == 0x14);
using SmoothEdgeArray = mfc::CArray<SmoothEdge>;     // vtable 0073FE28

// One of the 81 transition tables (previous direction * 9 + next direction; 0 at the path's end),
// built by the static initialiser 005C42B0 at 007C3058.
struct SmoothTable {
    // The three arrays empty: what the initialiser's vector constructor (005C41C0, callback
    // 005C4210) leaves in each table before 005C42B0 fills them. Returns this (MOV EAX,ECX).
    SmoothTable* Construct();
    // 005C4250 (the callback of the vector destructor the initialiser registers with atexit). The
    // three arrays' storage freed, the last member first.
    void Destruct();
    // 005C3E10 (RET 8). The table between a cell with the states `from` and the next one with the
    // states `to`: a copy for every state both have, a step from every `from` state to every `to`
    // state that starts where it ends, and the same between the `to` states themselves.
    void Build(const SmoothEdgeArray* from, const SmoothEdgeArray* to);

    mfc::CArray<SmoothCopy> copies;       // +0x00
    mfc::CArray<SmoothStep> steps;        // +0x14 from the previous cell's states
    mfc::CArray<SmoothStep> chained;      // +0x28 between the new states
};
static_assert(sizeof(SmoothTable) == 0x3C);
// The node pool (007C3038): an array of nodes and a bump pointer.
struct SmoothPool {
    // 005C4C20 (RET 8). CArray<SmoothNode>::SetSize (new nodes zeroed; growBy -1 keeps the step).
    void SetSize(int size, int growBy);

    mfc::CArray<SmoothNode> nodes;
    int zeroed;                     // set to 0 by each pass, not read
    SmoothNode* next;                   // the next free node
};
static_assert(sizeof(SmoothPool) == 0x1C);
inline SmoothPool& SmoothNodePool() { return ei::GameData<SmoothPool>(0x007C3038); }
inline const SmoothTable* SmoothTables() { return ei::GameData<const SmoothTable[81]>(0x007C3058); }
// The direction (1..8, 0 for none) of the step (dx, dy) in -1..1: DirectionSteps()[dy * 3 + dx], the
// middle of the nine words at 00791978.
inline const int* DirectionSteps() { return ei::GameData<const int[9]>(0x00791978) + 4; }
// 005C3690 (thiscall on the array, RET 8; ai_path_smooth.cpp). The states of a cell the path crosses
// in `direction` (1..8): seven moves for a straight step, nine for a diagonal one. Direction 0 (the
// path's ends) gives the eight moves from the cell to its neighbours, or from the neighbours to it
// when `incoming`.
void __fastcall BuildSmoothEdges(SmoothEdgeArray* edges, void*, int direction, bool incoming);
// What 005C42B0 does, on any 81 constructed tables: for every pair of directions the states of the
// previous step (moved back by that step) against those of the next.
void BuildSmoothTablesAt(SmoothTable* tables);

// The path smoother at CAIMap + 0x22CC (0x2C bytes; ai_path_smooth.cpp). Name provisional.
struct PathSmoother {
    // 005C46C0 (RET 0x14). Re-plans the path's cells (at least three; else 0) through a trellis of
    // the eight directions per cell: step costs, a turn cost against `startDirection` (1..8) and
    // the previous step, and diagonal end states charged (1 - fraction) * 2048. While a pass finds
    // a cheaper path the cells are rebuilt from the tail along it and the pass repeats. Returns the
    // last pass's cost.
    int Smooth(CUnit* unit, CAIMap* map, PathCellList* path, int startDirection, float fraction);
    // 005C45D0 (RET 0xC). The states of the next cell (`to`, fresh nodes from the pool) from those
    // of the current one through `table`.
    void Advance(const SmoothState* from, SmoothState* to, const SmoothTable* table);
    // 005C44D0 (RET 0xC). Relaxes `to` through `step` from `from`.
    void Relax(const SmoothStep* step, const SmoothState* from, SmoothState* to);

    std::uint8_t level;             // +0x00 standing level
    std::uint8_t unused_01[3];
    int sizeClass;                  // +0x04
    CAIMap* map;                    // +0x08
    int x, y;                       // +0x0C the current cell
    PassCostContext costs;          // +0x14
};
static_assert(sizeof(PathSmoother) == 0x2C);

// A 4x4-cell block of one unit's levels (0x1C bytes).
struct UnitLayerNode {
    UnitLayerNode* next;        // next in the bucket, or in the free list
    int key;                    // (block y << 16) + block x, blocks of 4 cells
    std::int8_t levels[16];     // rows of 4
    CUnit* owner;               // +0x18
};
static_assert(sizeof(UnitLayerNode) == 0x1C);

// The unit occupancy layers at CAIMap + 0x27C (small units) and + 0x12A4 (large units): every unit
// on the map is stamped as a 20x20-cell block of levels (its UnitShape, 16x16, at the unit's cell
// offset within a 4-cell block), stored as 4x4-cell nodes in a 1024-bucket hash keyed by block.
// Methods in ai_unit_layer.cpp and ai_grids.cpp. Names provisional.
//
// Two slots: 0 Set (the block's levels replace the map's) and 1 Merge (the map keeps the larger
// level), _purecall in the base vtable 0073FD7C. The small-unit layer (vtable 0073FD74) writes
// AIMapCell::level (unsigned maximum), the large-unit one (vtable 0073FD6C) the signed bytes of
// CAIMap::waterDepth; their bodies are the SetSmall / MergeSmall / SetLarge / MergeLarge members below, registered by
// address. The game builds the layers (the AI map's constructor stores the vtables), so the class
// is novtable and the slots are only declared.
class __declspec(novtable) CUnitLayer {
public:
    // The bodies of the derived layers' slots 0 (Set: the block's levels replace the map's) and 1 (Merge:
    // the map keeps the larger level), registered by address (ai_unit_layer.cpp). The small-unit layer
    // (vtable 0073FD74) writes AIMapCell::level (unsigned maximum), the large-unit one (vtable 0073FD6C)
    // the signed bytes of CAIMap::waterDepth.
    void SetSmall(const UnitLayerNode* block);     // 005B39D0
    void MergeSmall(const UnitLayerNode* block);   // 005B3A70
    void SetLarge(const UnitLayerNode* block);     // 005B3B20
    void MergeLarge(const UnitLayerNode* block);   // 005B3B90
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073FD7C);
    static constexpr std::uintptr_t kSmallVtableAddress = std::uintptr_t(0x0073FD74);
    static constexpr std::uintptr_t kLargeVtableAddress = std::uintptr_t(0x0073FD6C);
    static constexpr std::uintptr_t kNodeListVtable = std::uintptr_t(0x0073F9F8);

    virtual void Set(const UnitLayerNode* block);     // 0: small 005B39D0, large 005B3B20
    virtual void Merge(const UnitLayerNode* block);   // 1: small 005B3A70, large 005B3B90

    // 005B36B0. Stamps `unit`'s shape (16x16 levels at shape + 8) with its top-left cell at
    // (x, y): one node per non-empty 4x4 block, each announced through Merge.
    void Add(CUnit* unit, const std::uint8_t* shape, int x, int y);
    // 005B3890. Removes `unit`'s nodes from the 5x5 blocks from block (x >> 2, y >> 2); for each
    // block it had, Set gets the block with the other units' levels merged (maximum).
    void Remove(CUnit* unit, int x, int y);
    // 005B4110 (the base constructor; ai_grids.cpp). The empty node list (blocks of 0x400), the
    // base vtable 0073FD7C, no buckets, no free nodes. `map` is the owner's to set. Returns this.
    CUnitLayer* Construct();
    // 0058A8A0 / 0058A900 (the two layer classes' destructors, reached by the AI map's unwinding
    // only; ai_grids.cpp). The node list emptied and its blocks freed.
    void Destruct();

    // +0x04 the storage of every node ever allocated (vtable kNodeListVtable, NewNode 005BE7B0).
    mfc::CList<UnitLayerNode> nodes;
    UnitLayerNode* buckets[1024];   // +0x20 by (key x * 17 + key y) & 0x3FF
    UnitLayerNode* freeNodes;       // +0x1020 unused nodes
    CAIMap* map;                    // +0x1024
};
static_assert(offsetof(CUnitLayer, nodes) == 0x04);
static_assert(offsetof(CUnitLayer, buckets) == 0x20);
static_assert(offsetof(CUnitLayer, freeNodes) == 0x1020);
static_assert(sizeof(CUnitLayer) == 0x1028);

// A level's special area (CAIMap::areaLists, ClearUnmarkedArea): the offset {dx, dy} of the cell
// whose mark decides.
struct AreaOffset {
    int dx, dy;
};

struct AIEventTable;   // unit_ai.h

// The navigation grid of the current map, loaded by CAIMap::Load (005B43C0, patched by Rebirth).
// Name from the game's error strings; layout provisional.
class CAIMap {
public:
    // 005B43C0 (RET 4; ai_map_load.cpp, A-W15). Rebirth patches it: this is the vanilla body, and it
    // is installed in the vanilla launch only (callers reach it through its address). Loads the map
    // archive `path`: LoadTerrain, then AttachTerrain. Returns 1; 0 when the archive cannot be
    // opened; any other failure is a fatal error, whose result is returned.
    int Load(const char* path);
    // Load's first part: the archive's .mp file (header, materials, tile types) gives the height
    // scale and the climb tables; tileDesc.reg in the text resources' aiinfo.res gives the terrain
    // types; the three cell grids are sized for the map (64 x 64 cells per sector) and every sector
    // file stamped into the cells (ground level, water depth, water flag) and the occluder words
    // (terrain type). It touches nothing else of the map, so the verify wrapper runs it on a copy.
    int LoadTerrain(const char* path);
    // Load's second part: the map counts as loaded, units are not re-registered by refreshes; the
    // open marks and cost tables, the water depths zeroed (the grid sized for the map), both local
    // searches, the region map, the AI events and the sector map attached to the new size, and the
    // whole map refreshed (or added to the dirty rectangle while batching).
    void AttachTerrain();

    // The cells', the occluder words' and the water depths' grids as the grid template sees them
    // (block, rows, width, height), and the AI events that follow the map in the world.
    SearchGrid<AIMapCell>& CellGrid() { return *reinterpret_cast<SearchGrid<AIMapCell>*>(&cellBlock); }
    SearchGrid<std::uint32_t>& OccluderGrid() { return *reinterpret_cast<SearchGrid<std::uint32_t>*>(&occluderBlock); }
    SearchGrid<std::int8_t>& WaterGrid() { return *reinterpret_cast<SearchGrid<std::int8_t>*>(&waterBlock); }
    AIEventTable* Events() { return reinterpret_cast<AIEventTable*>(this + 1); }

    // The sector map embedded at +0x124 (sector_map.h).
    CSectorMap* SectorMap() { return &sectorMap; }
    // script_runtime_helpers.cpp (A-S4): the sector map (+0x124) as the script runtime calls it.
    // 004BA090 (RET 4). refreshUnits = the low byte of `on`; returns the old byte (AL).
    std::uint8_t SetRefreshUnits(int on);
    // 004BA0A0 / 004BA0C0 (RET 4). CSectorMap::Add / Remove (AL).
    bool AddToSectors(CEngineObject* object);
    bool RemoveFromSectors(CEngineObject* object);
    // 004BA0E0 (RET 8). CSectorMap::Move, through its address (Rebirth patches it; AL).
    bool MoveInSectors(CEngineObject* object, const Vector3* position);
    // 004BA100 (RET 0xC). CSectorMap::FindUnitsInRadius into `result` (a CTypedPtrList<CEngineObject>).
    void FindUnitsInRadius(const Vector3* position, void* result, float radius);

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
    // 005BC180 (RET 8; ai_map.cpp, A-W14; UnitSpecApply 0046C4D0). Gives `object` the orientation
    // `rotation`: it is taken out of the sector map, turned and put back.
    void TurnObject(CEngineObject* object, const Quaternion* rotation);
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
    // 005B9580 (RET 8). CostContext for `unit`: its size class, its movement speeds (0052F920) in
    // +0x14 / +0x0C, and the alternative cost table when CUnit::altPassCosts is set.
    void CostContextFor(CUnit* unit, struct PassCostContext* out);
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

    // 005B6740 (ai_bridges.cpp). Applies or clears the bridge decks of a footprint.
    bool ApplyBridgeDecks(FootprintCellSet* set, bool remove);
    // 005BC750 (RET 8, ST0; ai_line_step.cpp). The largest ground step between consecutive cells on
    // the line, in world height; 1.0 for a zero-length line, 100.0 when an end is off the map.
    double LargestStepAlong(const Vector3* from, const Vector3* to);
    // 005B9690 (RET 8, AL; ai_join.cpp). Joins the forward and backward local searches where they
    // meet: the path into `cells`, the summed cost into `cost` (optional); false when they cannot.
    bool JoinLocalSearches(PathCellList* cells, int* cost);
    // 005B9A20 (RET 0xC; ai_join.cpp). The path to cell (x, y) walked back along the direction marks
    // (+0x26BC) into `path` (emptied first).
    void BuildCellsTo(int x, int y, PathCellList* path);
    // 005B7EF0 (RET 8; ai_water_patch.cpp). Adds (or lifts) the water depths around `position` as
    // a pseudo unit of the small-unit layer.
    void StampWaterPatch(const Vector3* position, bool add);
    // 005B61B0 / 005B6380 (ai_occluders.cpp). Adds / removes one footprint cell's occluder.
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
    // 005B89F0 (RET 0x1C; ai_refresh.cpp). A level's special area {dx, dy}: in [x0, x1] x [y0, y1]
    // (kept so that (x + dx, y + dy) is on the map) each cell whose mark at (x + dx, y + dy) is 0
    // loses the level's passability nibble.
    void ClearUnmarkedArea(int level, int dx, int dy, int x0, int x1, int y0, int y1);

    CRegionMap* Regions() { return &regionMap; }

    // 005B9BC0 (RET 4; PlanPath; ai_path_cells.cpp). Clears the marks of the cells around the
    // blocks of `chain` (8 x 8 cells each): their bounding box widened by four blocks, clipped to
    // the map. Nothing for an empty chain.
    void ClearMarksAround(const PathCellList* chain);
    // 005B8620 (CAIMap::Load; ai_grids.cpp). Every terrain type's `openMarks`: per size class the
    // passability by its cost (up to 1: 14, 2: 13, up to 9: 10, up to 34: 6, up to 70: 3, above:
    // 0; the table 0079118C); size class 0 always gets 14.
    void BuildOpenMarks();

    // Refresh the rectangle now, or add it to the dirty rectangle while batching.
    void QueueRefresh(int minX, int maxX, int minY, int maxY);

    bool loaded;                        // +0x000 set by Load
    bool batching;                      // +0x001 refreshes collect into the dirty rectangle
    bool refreshUnits;                  // +0x002 a refresh also re-registers the units around it
    std::uint8_t unused_003;
    int dirtyMinX, dirtyMaxX;           // +0x004
    int dirtyMinY, dirtyMaxY;           // +0x00C
    int passCosts[16];                  // +0x014 cost per passability nibble (1/1024)
    int passCostsAlt[16];               // +0x054 the same for units with CUnit::altPassCosts set
    int stepCosts[16];                  // +0x094 step speeds per nibble (SetStepSpeeds)
    ClimbTable climbLarge;              // +0x0D4 size classes other than 0: no step steeper than 40 degrees
    ClimbTable climbSmall;              // +0x0E8 size class 0: up to 60 degrees, every step at the flat cost
    lib::AlignedArray<AITerrainType> materials;     // +0x0FC the terrain types (16 after Load)
    std::uint8_t bridgeHeights[0x14];   // +0x10C hash_map<int, uint16> keyed (short)x << 16 | (short)y
    float heightScale;                  // +0x120 world z to AI height units
    CSectorMap sectorMap;               // +0x124 (vtable 0073F9DC)
    FootprintCellSet footprint;         // +0x14C scratch for MarkFootprint
    FootprintCellSet bridges;           // +0x1A0
    std::int8_t* waterBlock;            // +0x1F4 the water depths' grid (WaterGrid) from here
    std::int8_t** waterDepth;           // +0x1F8 rows of per-cell water depth
    int waterWidth, waterHeight;        // +0x1FC
    // +0x204 the standing levels' special areas (ClearUnmarkedArea): RefreshArea reads the first
    // list for levels 0..4, the second for 5 and 6, the third for 7 (the other three are not read
    // there). CArray<{int, int}> (vtable 0073F9E0), filled by FillAreaLists (005B4150).
    mfc::CArray<AreaOffset> areaLists[6];
    CUnitLayer unitLayerSmall;          // +0x27C unit occupancy (005B36B0 / 005B3890)
    CUnitLayer unitLayerLarge;          // +0x12A4
    PathSmoother smoother;                          // +0x22CC
    CRegionMap regionMap;                           // +0x22F8
    LocalSearch forwardSearch;                      // +0x2594
    LocalSearch backwardSearch;                     // +0x2614
    int lastFootprintOwner;             // +0x2694 set by StampFootprint
    AIMapCell* cellBlock;               // +0x2698 the cells' grid (CellGrid) from here
    AIMapCell** cells;                  // +0x269C rows of cells
    int width;                          // +0x26A0
    int height;                         // +0x26A4
    std::uint32_t* occluderBlock;       // +0x26A8 the occluder words' grid (OccluderGrid) from here
    std::uint32_t** occluders;          // +0x26AC rows of per-cell occluder words (see LineOfSight)
    int occluderWidth, occluderHeight;  // +0x26B0
    SearchGrid<std::int8_t> marks;      // +0x26B8 per-cell marks: path directions (BuildCellsTo), reach answers
};
static_assert(offsetof(CAIMap, batching) == 0x01 && offsetof(CAIMap, refreshUnits) == 0x02);
static_assert(offsetof(CAIMap, dirtyMinX) == 0x04 && offsetof(CAIMap, dirtyMaxX) == 0x08);
static_assert(offsetof(CAIMap, dirtyMinY) == 0x0C && offsetof(CAIMap, dirtyMaxY) == 0x10);
static_assert(offsetof(CAIMap, passCosts) == 0x14);
static_assert(offsetof(CAIMap, sectorMap) == 0x124);
static_assert(offsetof(CAIMap, areaLists) == 0x204);
static_assert(offsetof(CAIMap, waterWidth) == 0x1FC && offsetof(CAIMap, waterHeight) == 0x200);
static_assert(offsetof(CAIMap, stepCosts) == 0x94);
static_assert(offsetof(CAIMap, climbLarge) == 0xD4);
static_assert(offsetof(CAIMap, climbSmall) == 0xE8);
static_assert(offsetof(CAIMap, materials) == 0xFC);
static_assert(offsetof(CAIMap, waterBlock) == 0x1F4);
static_assert(offsetof(CAIMap, cellBlock) == 0x2698 && offsetof(CAIMap, occluderBlock) == 0x26A8);
static_assert(offsetof(CAIMap, bridgeHeights) == 0x10C);
static_assert(offsetof(CAIMap, heightScale) == 0x120);
static_assert(offsetof(CAIMap, footprint) == 0x14C);
static_assert(offsetof(CAIMap, bridges) == 0x1A0);
static_assert(offsetof(CAIMap, waterDepth) == 0x1F8);
static_assert(offsetof(CAIMap, unitLayerSmall) == 0x27C && offsetof(CAIMap, unitLayerLarge) == 0x12A4);
static_assert(offsetof(CAIMap, regionMap) == 0x22F8);
static_assert(offsetof(CAIMap, smoother) == 0x22CC);
static_assert(offsetof(CAIMap, forwardSearch) == 0x2594);
static_assert(offsetof(CAIMap, backwardSearch) == 0x2614);
static_assert(offsetof(CAIMap, lastFootprintOwner) == 0x2694);
static_assert(offsetof(CAIMap, cells) == 0x269C);
static_assert(offsetof(CAIMap, occluders) == 0x26AC);
static_assert(offsetof(CAIMap, marks) == 0x26B8);
static_assert(sizeof(CAIMap) == 0x26C8);   // the AI events follow (Events)
// 005B3C30 (thiscall; ai_map_construct.cpp; the world server's constructor 00589ED0). Returns self.
CAIMap* __fastcall ConstructAIMap(CAIMap* self);
// 0058A450 (thiscall; ai_map_construct.cpp; the world server's destructor 0058AAD0). Every member
// destroyed in reverse order (the reset 005B5E10 without the constructor after).
void __fastcall DestructAIMap(CAIMap* self);

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

// 005B6070 (thiscall on the map; ai_cost_tables.cpp). The cost tables per passability nibble.
void __fastcall InitCostTables(CAIMap* map);
// 005B7B20. Standing level of size class `sizeClass` (0..7): (2 - height) * 16, at least 0. Outside
// 0..7 the original returns only AL = 0 (the rest of EAX is the argument), reproduced here.
int __stdcall LevelForSizeClass(int sizeClass);
// 005B7B70. Standing level of `unit`: (2 - standingHeight) * 16, at least 0.
int __stdcall StandingLevel(const CUnit* unit);
// 005B30B0 (thiscall on the bridge map CAIMap + 0x10C, RET 0x10; ai_bridges.cpp). Puts back what
// the cell's bridge map entry saved (ground level, object height, the occluder word's low five
// bits) and erases the entry; nothing without one. x and y are read as shorts.
void __fastcall RestoreBridgeCell(void* map, void*, int x, int y, AIMapCell* cell, std::uint32_t* occluder);
// 005B3160. Bridge deck height of cell (x, y) from the hash map at CAIMap + 0x10C, 0 if none.
int __fastcall BridgeHeight(const void* map, void*, short x, short y);
