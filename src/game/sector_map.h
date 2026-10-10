#pragma once

#include "ai_map.h"
#include "lib/mfc.h"

// The world split into 32x32-cell sectors, each listing the objects and units inside it. Used to
// find what is near a point and to keep the AI map's footprints in sync. Names provisional.
// The game constructs sectors (0059DF90: vtable 0073FADC, whose only slot 0059E080 is the
// destructor); an explicit ~Sector() dispatches to it with flags 0.
struct __declspec(novtable) Sector {
    virtual ~Sector() {}                         // 0: 0059E080
    mfc::CTypedPtrList<CEngineObject> objects;   // +0x04 non-unit objects
    mfc::CTypedPtrList<CEngineObject> units;     // +0x20
    std::uint8_t unknown_3C[0x54 - 0x3C];        // +0x40 a CArray (vtable 0073FAE0): the occluder records
};
static_assert(sizeof(Sector) == 0x54);

// CArray<Sector>'s element functions: 0059DF90 (__stdcall, RET 8) constructs, the destructor slot
// (flags 0) destroys.
struct SectorElements {
    static void Construct(Sector* sectors, int count);
    static void Destruct(Sector* sectors, int count) {
        for (; count != 0; --count, ++sectors) sectors->~Sector();
    }
};

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

    std::uint32_t unknown_00;
    int width;              // +0x04 in sectors
    int height;             // +0x08
    int added;              // +0x0C successful Add calls (Remove does not decrement it)
    mfc::CArray<Sector, SectorElements> sectors;   // +0x10 width * height, row by row
    CAIMap* aiMap;          // +0x24
};
static_assert(offsetof(CSectorMap, sectors) == 0x10);
static_assert(offsetof(CSectorMap, aiMap) == 0x24);
