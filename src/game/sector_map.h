#pragma once

#include "ai_map.h"
#include "lib/mfc.h"

// The world split into 32x32-cell sectors, each listing the objects and units inside it. Used to
// find what is near a point and to keep the AI map's footprints in sync. Names provisional.
struct Sector {
    std::uint32_t unknown_00;
    mfc::CTypedPtrList<CEngineObject> objects;   // +0x04 non-unit objects
    mfc::CTypedPtrList<CEngineObject> units;     // +0x20
    std::uint8_t unknown_3C[0x54 - 0x3C];
};
static_assert(sizeof(Sector) == 0x54);

class CSectorMap {
public:
    // 005C0DF0. Registers `object` in the sector under its position and stamps its footprint.
    // Undone again if the footprint does not fit.
    bool Add(CEngineObject* object);

    // 005C0BD0. Fills `result` (cleared first) with the units within `radius` of `position`,
    // nearest sectors first; each match is inserted at the head of the list.
    void FindUnitsInRadius(const Vector3* position, mfc::CTypedPtrList<CEngineObject>* result, float radius);

    // 005C11D0. Moves `object` to `position`, re-stamping its footprint (restored on failure) and
    // moving it between sector lists when it crosses into another sector.
    bool Move(CEngineObject* object, const Vector3* position);

    // 005C1010. Unregisters `object` from the sector it was added to and clears its footprint.
    bool Remove(CEngineObject* object);

    std::uint32_t unknown_00;
    int width;              // +0x04 in sectors
    int height;             // +0x08
    int added;              // +0x0C successful Add calls (Remove does not decrement it)
    std::uint32_t unknown_10;
    Sector* sectors;        // +0x14 width * height, row by row
    std::uint8_t unknown_18[0x24 - 0x18];
    CAIMap* aiMap;          // +0x24
};
static_assert(offsetof(CSectorMap, aiMap) == 0x24);
