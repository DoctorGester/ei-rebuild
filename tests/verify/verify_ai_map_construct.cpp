// Checks for the CAIMap constructor and reset (src/game/ai_map_construct.cpp). Ours only (they
// build the map's members in place): afterwards the refresh flag is off, the last footprint
// owner 0, and the inlined vtables and the unit layers' back pointers are set.
#include "verify.h"

#include "../../src/game/ai_map.h"

#include <cstdint>

CAIMap* __fastcall ConstructAIMap(CAIMap* self);
void __fastcall ResetAIMap(CAIMap* self);

namespace {
void* s_unused;

template <typename T>
T At(const void* object, int offset) {
    return *reinterpret_cast<const T*>(static_cast<const std::uint8_t*>(object) + offset);
}

bool Fresh(const CAIMap* map) {
    return !map->refreshUnits && map->lastFootprintOwner == 0 &&
           At<std::uint32_t>(map, 0xD4) == 0x0073C3D0 && At<std::uint32_t>(map, 0x204) == 0x0073F9E0 &&
           At<std::uint32_t>(map, 0x27C) == 0x0073FD74 && At<std::uint32_t>(map, 0x12A4) == 0x0073FD6C &&
           At<const CAIMap*>(map, 0x27C + 0x1024) == map && At<const CAIMap*>(map, 0x12A4 + 0x1024) == map;
}
} // namespace

// 005B3C30.
static CAIMap* __fastcall ConstructVerify(CAIMap* self) {
    CAIMap* result = ConstructAIMap(self);
    ei::verify::Check("ConstructAIMap", true, result == self && Fresh(self), "dirty [%d,%d]x[%d,%d]", self->dirtyMinX,
                      self->dirtyMaxX, self->dirtyMinY, self->dirtyMaxY);
    return result;
}
EI_VERIFY(0x005B3C30, ConstructVerify, 0, s_unused);

// 005B5E10.
static void __fastcall ResetVerify(CAIMap* self) {
    ResetAIMap(self);
    ei::verify::Check("ResetAIMap", true, Fresh(self), "");
}
EI_VERIFY(0x005B5E10, ResetVerify, 0, s_unused);
