#pragma once

// The AI map embeds the sector map, so ai_map.h includes this header first and the AI
// map's declaration comes last, for the files that include only this one.
#include "engine_object.h"
#include "maths.h"

#include "lib/mfc.h"
#include "lib/static_object.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <vector>

class CAIMap;
class CMapObject;
class CUnit;

// The world split into 32x32-cell sectors, each listing the objects and units inside it. Used to
// find what is near a point and to keep the AI map's footprints in sync. Names provisional.
// A sector's one virtual is its destructor.
// An AI map cell with more than one occluder chains 8-byte records of its sector
// (ai_occluders.cpp); the sector recycles them through a free list.
struct SectorOccluder {
    std::int16_t top;
    std::int16_t bottom;
    std::int8_t kind;
    std::int8_t transparency;
    std::int16_t next;          // the cell's next record, or the next free one; negative ends
};
static_assert(sizeof(SectorOccluder) == 8);

struct Sector {
    Sector();
    virtual ~Sector();

    // Clear: no free record, the records' storage freed, both lists
    // emptied (RemoveAll).
    void Clear();
    // A free record's index: the head of the free list, else the array grown by one
    // (SetSize; new records zeroed) and its last index.
    int NewOccluder();
    // Unlinks the first record equal to (top, bottom, kind, transparency) from
    // the chain starting at `head` (its low 16 bits; negative: no chain) and puts it on the free
    // list. Returns the chain's head afterwards, sign-extended from 16 bits.
    int RemoveOccluder(int head, int bottom, int top, int kind, int transparency);
    // Puts record `index` on the free list.
    void FreeOccluder(int index);

    std::list<CMapObject*> objects;      // non-unit objects
    std::list<CUnit*> units;
    int freeOccluder;                            // head of the free records (-1 none)
    std::vector<SectorOccluder> occluders;       // (mfc_slots.h's SectorOccluderArrayClass)
};

// CArray<Sector>'s element functions: the constructor (sector_map.cpp) constructs
// (zeroed, then each sector's constructor), the destructor destroys.
void ConstructSectors(Sector* sectors, int count);
struct SectorElements {
    static void Construct(Sector* sectors, int count);
    static void Destruct(Sector* sectors, int count) {
        for (; count != 0; --count, ++sectors) sectors->~Sector();
    }
};

// The radii of the sector spiral (filled by its static initialiser): a complete
// binary search tree in an array, root at index 1.
struct RadiusTree {
    // The slot of `radius` (in sectors): `depth` steps down the tree, right
    // where the radius is above the node's key, then the node index masked. The floats are
    // compared as integers, which orders non-negative values.
    int Find(float radius) const;
    // The tree over `count` ascending values: the smallest depth (at least 1) with
    // 2^depth >= count, the keys of every level filled from the values in order (past the last
    // value, the last one). The bottom level holds the values themselves.
    void Build(const float* values, int count);

    std::vector<float> keys;
    int depth;
    int mask;
};

// A sector offset of the spiral.
struct SpiralOffset {
    std::int16_t x, y;
};
static_assert(sizeof(SpiralOffset) == 4);
// The sector spiral's tables, built by the static initialiser: the 441 offsets of a 21 x 21
// square of sectors ordered by distance, the number of offsets within each distinct
// distance and the tree that finds a distance's place among them.
// Defined, built and destroyed in sector_spiral.cpp.
extern lib::StaticObject<std::vector<int>> g_spiralLengths;
extern lib::StaticObject<RadiusTree> g_spiralRadii;
extern lib::StaticObject<std::vector<SpiralOffset>> g_spiralOffsets;
inline std::vector<int>* SpiralLengthArray() { return &*g_spiralLengths; }
inline RadiusTree* SpiralRadiusTree() { return &*g_spiralRadii; }
inline std::vector<SpiralOffset>* SpiralOffsetArray() { return &*g_spiralOffsets; }
// What the spiral's static initialiser does, on any three constructed tables (sector_map.cpp).
void BuildSectorSpiralAt(std::vector<int>* lengths, RadiusTree* tree, std::vector<SpiralOffset>* offsets);

// The AI map's sector map (one virtual: the destructor).
class CSectorMap {
public:
    CSectorMap();
    virtual ~CSectorMap();

    // Registers `object` in the sector under its position and stamps its footprint.
    // Undone again if the footprint does not fit.
    bool Add(CMapObject* object);

    // Fills `result` (cleared first) with the units within `radius` of `position`,
    // nearest sectors first; each match is inserted at the head of the list.
    void FindUnitsInRadius(const Vector3* position, std::list<CUnit*>* result, float radius);

    // Moves `object` to `position`, re-stamping its footprint (restored on failure) and
    // moving it between sector lists when it crosses into another sector. Rebirth's FixMoveObject
    // first refuses a position with x or y below 0 (or NaN).
    bool Move(CMapObject* object, const Vector3* position);

    // The unit list of sector (sx, sy), null outside the map.
    std::list<CUnit*>* UnitsAt(int sx, int sy);

    // Unregisters `object` from the sector it was added to and clears its footprint.
    bool Remove(CMapObject* object);

    // Called by CAIMap::Load. Sizes the map for a cell grid: width and
    // height in sectors (cells / 32, rounded towards zero), `added` 0, the sector array set to
    // width * height, then the AI map.
    void Init(int cellsX, int cellsY, CAIMap* map);

    // The destructor's body (on `sectors`): the array's vtable back, every sector
    // destroyed, the storage freed.
    static void DestructSectors(mfc::CArray<Sector, SectorElements>* sectors);

    int width;              // in sectors
    int height;
    int added;              // successful Add calls (Remove does not decrement it)
    mfc::CArray<Sector, SectorElements> sectors;   // width * height, row by row
    CAIMap* aiMap;
};
static_assert(offsetof(CSectorMap, sectors) == 0x10);
static_assert(offsetof(CSectorMap, aiMap) == 0x24);

#include "ai_map.h"
#include "game_data.h"
