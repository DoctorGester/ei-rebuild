#pragma once

// The unit occupancy layers at CAIMap + 0x27C (small units) and + 0x12A4 (large units): every unit
// on the map is stamped as a 20x20-cell block of levels (its UnitShape, 16x16, at the unit's cell
// offset within a 4-cell block), stored as 4x4-cell nodes in a 1024-bucket hash keyed by block.
// Names provisional; layout static_asserted below.

#include "ai_map.h"
#include "ai_shape_cache.h"
#include "core_refcount.h"
#include "unit.h"

#include <cstddef>
#include <cstdint>

// A unit's footprint on the layers: 16 x 16 cell levels around the unit, shared by radius through
// the shape cache (007C2F70, ai_shape_cache.cpp; its handles are UnitShapeHandle). Methods in
// ai_unit_shape.cpp. Names provisional.
class CUnitShape final : public CRefObject {
public:
    // The levels of a disc: per cell (2 + radius - half its distance from the
    // middle cell (8, 8)) x 16, rounded; cells where that is not above 0 stay 0. No references.
    explicit CUnitShape(float radius);
    // An empty shape, as OnShutdown leaves one (the water patch, ai_water_patch.cpp). No references.
    CUnitShape();
    // Constructed again empty: no levels, radius -1. Returns 0.
    int OnShutdown() override;
    // A new shape with the same levels and radius (no references yet).
    CUnitShape* Clone() override;

    std::uint8_t levels[16][16];    // +0x008 rows of 16
    float radius;                   // +0x108
};

// The shape cache's handle (ai_shape_cache.h); its height is the shape's radius.
using UnitShapeHandle = ShapeCacheHandle;
// 005B33D0 (the shape cache's slot 0, thiscall with `this` unused: RET 4). A new shape for the
// handle's radius.
CUnitShape* __stdcall CreateUnitShape(const UnitShapeHandle* handle);

// The bucket of a block key.
inline int UnitLayerBucket(int blockX, int blockY) { return (blockX * 17 + blockY) & 0x3FF; }
