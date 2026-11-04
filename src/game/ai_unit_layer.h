#pragma once

// The unit occupancy layers' helpers (the layers are AIMap::unitLayerSmall and unitLayerLarge, declared in
// ai_map.h): the shape cache's creator and the block hash.

#include "ai_map.h"
#include "ai_shape_cache.h"
#include "core_refcount.h"
#include "unit.h"

#include <cstddef>
#include <cstdint>

// The shape cache's creator virtual: a new shape for the handle's radius (`this` unused).
UnitShape* CreateUnitShape(const ShapeCacheHandle* handle);

inline int UnitLayerBucket(int blockX, int blockY) { return (blockX * 17 + blockY) & 0x3FF; }
