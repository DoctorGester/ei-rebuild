#pragma once

#include "ai_map.h"
#include "lib/mfc.h"

// The world split into 32x32-cell sectors, each listing the objects and units inside it. Used to
// find what is near a point and to keep the AI map's footprints in sync. Names provisional.
// The game constructs sectors (0059DF90: vtable 0073FADC, whose only slot 0059E080 is the
// destructor); an explicit ~Sector() dispatches to it with flags 0.
//
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

constexpr std::uintptr_t kSectorVtable = std::uintptr_t(0x0073FADC);
constexpr std::uintptr_t kSectorOccluderArrayVtable = std::uintptr_t(0x0073FAE0);
constexpr std::uintptr_t kSectorObjectListVtable = std::uintptr_t(0x0073F530);   // world.h ObjectPtrListVtable

struct __declspec(novtable) Sector {
    virtual ~Sector() {}                         // 0: 0059E080

    // 005BFE00 (the constructor's body). No free record, the records' storage freed, both lists
    // emptied (RemoveAll).
    void Clear();
    // 005BFE70. A free record's index: the head of the free list, else the array grown by one
    // (SetSize; new records zeroed) and its last index.
    int NewOccluder();
    // 005BFFD0 (RET 0x14). Unlinks the first record equal to (top, bottom, kind, transparency) from
    // the chain starting at `head` (its low 16 bits; negative: no chain) and puts it on the free
    // list. Returns the chain's head afterwards, sign-extended from 16 bits.
    int RemoveOccluder(int head, int bottom, int top, int kind, int transparency);
    // 005C0060 (RET 4). Puts record `index` on the free list.
    void FreeOccluder(int index);

    mfc::CTypedPtrList<CEngineObject> objects;   // +0x04 non-unit objects
    mfc::CTypedPtrList<CEngineObject> units;     // +0x20
    int freeOccluder;                            // +0x3C head of the free records (-1 none)
    mfc::CArray<SectorOccluder> occluders;       // +0x40 (vtable 0073FAE0)
};
static_assert(sizeof(Sector) == 0x54);
static_assert(offsetof(Sector, freeOccluder) == 0x3C);
static_assert(offsetof(Sector, occluders) == 0x40);

// CArray<Sector>'s element functions: 0059DF90 (__stdcall, RET 8; sector_map.cpp) constructs
// (zeroed, the two lists with blocks of 10, the empty record array, then Clear), the destructor
// slot (flags 0) destroys.
void __stdcall ConstructSectors(Sector* sectors, int count);   // 0059DF90
struct SectorElements {
    static void Construct(Sector* sectors, int count);
    static void Destruct(Sector* sectors, int count) {
        for (; count != 0; --count, ++sectors) sectors->~Sector();
    }
};

// The radii of the sector spiral (007C2FD8, filled by the static initialiser 005BF8F0): a complete
// binary search tree in an array, root at index 1.
struct RadiusTree {
    // 005BF7D0 (RET 4). The slot of `radius` (in sectors): `depth` steps down the tree, right
    // where the radius is above the node's key, then the node index masked. The floats are
    // compared as integers, which orders non-negative values.
    int Find(float radius) const;

    mfc::CArray<float> keys;    // +0x00
    int depth;                  // +0x14
    int mask;                   // +0x18
};
static_assert(sizeof(RadiusTree) == 0x1C);

class CSectorMap {
public:
    // 005C0DF0. Registers `object` in the sector under its position and stamps its footprint.
    // Undone again if the footprint does not fit.
    bool Add(CEngineObject* object);

    // 005C0BD0. Fills `result` (cleared first) with the units within `radius` of `position`,
    // nearest sectors first; each match is inserted at the head of the list.
    void FindUnitsInRadius(const Vector3* position, mfc::CTypedPtrList<CEngineObject>* result, float radius);

    // 005C11D0. Moves `object` to `position`, re-stamping its footprint (restored on failure) and
    // moving it between sector lists when it crosses into another sector. Rebirth's FixMoveObject
    // first refuses a position with x or y below 0 (or NaN).
    bool Move(CEngineObject* object, const Vector3* position);

    // 005C0930. The unit list of sector (sx, sy), null outside the map.
    mfc::CTypedPtrList<CEngineObject>* UnitsAt(int sx, int sy);

    // 005C1010. Unregisters `object` from the sector it was added to and clears its footprint.
    bool Remove(CEngineObject* object);

    // 005C06F0 (RET 0xC; called by CAIMap::Load). Sizes the map for a cell grid: width and
    // height in sectors (cells / 32, rounded towards zero), `added` 0, the sector array set to
    // width * height (SetSize inlined), then the AI map.
    void Init(int cellsX, int cellsY, CAIMap* map);

    // 0058A700 (reached by the AI map's unwinding and the deleting destructor). The destructor's
    // body: every sector destroyed (its slot 0, flags 0) and the array's storage freed.
    void DestructBody();
    // 0058A780 (slot 0, RET 4): DestructBody, then operator delete when flags bit 0 is set.
    CSectorMap* DeletingDestruct(unsigned flags);

    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073F9DC);
    static constexpr std::uintptr_t kSectorArrayVtable = std::uintptr_t(0x0073F9EC);

    void* vtable;           // +0x00 (one slot: the deleting destructor)
    int width;              // +0x04 in sectors
    int height;             // +0x08
    int added;              // +0x0C successful Add calls (Remove does not decrement it)
    mfc::CArray<Sector, SectorElements> sectors;   // +0x10 width * height, row by row
    CAIMap* aiMap;          // +0x24
};
static_assert(offsetof(CSectorMap, sectors) == 0x10);
static_assert(offsetof(CSectorMap, aiMap) == 0x24);
