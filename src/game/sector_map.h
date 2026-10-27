#pragma once

// The AI map embeds the sector map (+0x124), so ai_map.h includes this header first and the AI
// map's declaration comes last, for the files that include only this one.
#include "engine_object.h"
#include "maths.h"

#include "lib/mfc.h"
#include "lib/static_object.h"

#include <cstddef>
#include <cstdint>

class CAIMap;
class CMapObject;
class CUnit;

// The world split into 32x32-cell sectors, each listing the objects and units inside it. Used to
// find what is near a point and to keep the AI map's footprints in sync. Names provisional.
// A sector's one slot (game.exe's vtable 0073FADC) is its destructor.
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
    // 0059DF90's body for each sector (sector_map.cpp): the two lists with blocks of 10, the empty record
    // array, then Clear.
    Sector();
    // 005BFCD0 (deleting: 0059E080; world_destructors.cpp): Clear, then the members last to first (the record
    // array's storage, both lists emptied).
    virtual ~Sector();

    // 005BFE00 (the constructor's body). No free record, the records' storage freed, both lists
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

    mfc::CTypedPtrList<CMapObject> objects;      // +0x04 non-unit objects
    mfc::CTypedPtrList<CUnit> units;             // +0x20
    int freeOccluder;                            // +0x3C head of the free records (-1 none)
    mfc::CArray<SectorOccluder> occluders;       // +0x40 (mfc_slots.h's SectorOccluderArrayClass)
};
static_assert(sizeof(Sector) == 0x54);
static_assert(offsetof(Sector, freeOccluder) == 0x3C);
static_assert(offsetof(Sector, occluders) == 0x40);

// CArray<Sector>'s element functions: 0059DF90 (__stdcall, RET 8; sector_map.cpp) constructs
// (zeroed, then each sector's constructor), the destructor destroys.
void __stdcall ConstructSectors(Sector* sectors, int count);
struct SectorElements {
    static void Construct(Sector* sectors, int count);
    static void Destruct(Sector* sectors, int count) {
        for (; count != 0; --count, ++sectors) sectors->~Sector();
    }
};

// The radii of the sector spiral (007C2FD8, filled by the static initialiser 005BF8F0): a complete
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

    mfc::CArray<float> keys;    // +0x00
    int depth;                  // +0x14
    int mask;                   // +0x18
};
static_assert(sizeof(RadiusTree) == 0x1C);

// A sector offset of the spiral.
struct SpiralOffset {
    std::int16_t x, y;
};
static_assert(sizeof(SpiralOffset) == 4);
// The sector spiral's tables, built by the static initialiser 005BF8F0: the 441 offsets of a 21 x 21
// square of sectors ordered by distance, the number of offsets within each distinct
// distance and the tree that finds a distance's place among them.
// Defined, built and destroyed in sector_spiral.cpp.
extern lib::StaticObject<mfc::CArray<int>> g_spiralLengths;
extern lib::StaticObject<RadiusTree> g_spiralRadii;
extern lib::StaticObject<mfc::CArray<SpiralOffset>> g_spiralOffsets;
inline mfc::CArray<int>* SpiralLengthArray() { return &*g_spiralLengths; }
inline RadiusTree* SpiralRadiusTree() { return &*g_spiralRadii; }
inline mfc::CArray<SpiralOffset>* SpiralOffsetArray() { return &*g_spiralOffsets; }
// What 005BF8F0 does, on any three constructed tables (sector_map.cpp).
void BuildSectorSpiralAt(mfc::CArray<int>* lengths, RadiusTree* tree, mfc::CArray<SpiralOffset>* offsets);

// The AI map's sector map (game.exe's vtable 0073F9DC: one slot, the destructor).
class CSectorMap {
public:
    // The AI map's constructor's stores (inlined there): no sectors, no AI map.
    CSectorMap();
    // 0058A700 (deleting: 0058A780; reached by the AI map's unwinding and the deleting destructor): every sector
    // destroyed (its destructor) and the array's storage freed.
    virtual ~CSectorMap();

    // Registers `object` in the sector under its position and stamps its footprint.
    // Undone again if the footprint does not fit.
    bool Add(CMapObject* object);

    // Fills `result` (cleared first) with the units within `radius` of `position`,
    // nearest sectors first; each match is inserted at the head of the list.
    void FindUnitsInRadius(const Vector3* position, mfc::CTypedPtrList<CUnit>* result, float radius);

    // Moves `object` to `position`, re-stamping its footprint (restored on failure) and
    // moving it between sector lists when it crosses into another sector. Rebirth's FixMoveObject
    // first refuses a position with x or y below 0 (or NaN).
    bool Move(CMapObject* object, const Vector3* position);

    // The unit list of sector (sx, sy), null outside the map.
    mfc::CTypedPtrList<CUnit>* UnitsAt(int sx, int sy);

    // Unregisters `object` from the sector it was added to and clears its footprint.
    bool Remove(CMapObject* object);

    // 005C06F0 (RET 0xC; called by CAIMap::Load). Sizes the map for a cell grid: width and
    // height in sectors (cells / 32, rounded towards zero), `added` 0, the sector array set to
    // width * height (SetSize inlined), then the AI map.
    void Init(int cellsX, int cellsY, CAIMap* map);

    // 00599330 (thiscall on `sectors`; the destructor's body): the array's vtable back, every sector
    // destroyed, the storage freed.
    static void __fastcall DestructSectors(mfc::CArray<Sector, SectorElements>* sectors);

    int width;              // +0x04 in sectors
    int height;             // +0x08
    int added;              // +0x0C successful Add calls (Remove does not decrement it)
    mfc::CArray<Sector, SectorElements> sectors;   // +0x10 width * height, row by row
    CAIMap* aiMap;          // +0x24
};
static_assert(offsetof(CSectorMap, sectors) == 0x10);
static_assert(offsetof(CSectorMap, aiMap) == 0x24);

#include "ai_map.h"
#include "game_data.h"
