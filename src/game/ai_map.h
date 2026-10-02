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

// The region map at CAIMap + 0x22F8: connected areas per size class. Not rewritten.
struct CRegionMap {
    // 005B0600. Region of cell (x, y) for a size class, in AX (the rest of EAX is returned as the
    // original leaves it).
    int RegionAt(int sizeClass, int x, int y);
    // 005B07C0. Can cell (x, y) reach `region`? Caches the answers in `marks` (1 yes, 2 no).
    bool Reaches(int sizeClass, int x, int y, int region, void* marks);
};

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
    // 005C7D50 / 005C7B00 / 005C7C70: not rewritten.
    void Reset();
    void Start(const Vector3* position, const Vector3* backwards, int count);
    void SetTarget(float x, float y);

    std::uint8_t unknown_00[0x20];
    PathCellList steps;         // +0x20 the next cells to walk (at most 32)
    PathCellList cells;         // +0x3C the rest of the path
    float targetX, targetY;     // +0x58 in half-cells
    Vector3 goal;               // +0x60 world position asked for
    bool reached;               // +0x6C no cells left
    bool failed;                // +0x6D the last search failed; search again
    bool flag6E, flag6F;        // +0x6E, +0x6F
    std::uint8_t unknown_70[0x84 - 0x70];
    int smoothing;              // +0x84 from 005C46C0
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

    // Not rewritten (forwarders):
    // 005B6740. Applies or clears the bridge decks of a footprint.
    bool ApplyBridges(FootprintCellSet* set, bool remove);
    // 005B61B0 / 005B6380. Adds / removes one footprint cell's occluder.
    bool AddOccluder(FootprintCell* cell);
    bool ClearOccluder(FootprintCell* cell);
    // 005B8FA0. Recomputes passability in the rectangle (inclusive, grown by 3 cells).
    void Refresh(int minX, int maxX, int minY, int maxY);
    // 005B8350. Occupancy shape of a unit with standing height `height` (null below 0.2).
    void* UnitShape(float height);

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
