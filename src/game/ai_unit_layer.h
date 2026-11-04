#pragma once

// The unit occupancy layers at AIMap + 0x27C (small units) and + 0x12A4 (large units): every unit
// on the map is stamped as a 20x20-cell block of levels (its UnitShape, 16x16, at the unit's cell
// offset within a 4-cell block), stored as 4x4-cell nodes in a 1024-bucket hash keyed by block.
// Names provisional; layout static_asserted below.

#include "ai_map.h"
#include "ai_shape_cache.h"
#include "core_refcount.h"
#include "unit.h"

#include <cstddef>
#include <cstdint>

// The shape cache's creator virtual: a new shape for the handle's radius (`this` unused).
UnitShape* CreateUnitShape(const ShapeCacheHandle* handle);

inline int UnitLayerBucket(int blockX, int blockY) { return (blockX * 17 + blockY) & 0x3FF; }
