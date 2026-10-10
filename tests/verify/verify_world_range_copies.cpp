// A-W12: wrappers for byte-identical copies in the world range whose first registration has no
// wrapper of its own: ours only.
// - 005AD870 (ClearArray's copy, figure_slots.cpp): the four header dwords zeroed; returns this.
#include "verify.h"

#include <cstdint>

namespace figure {
void* __fastcall ClearArray(std::uint32_t* self);
}

namespace {
void* s_unused;
}

static void* __fastcall ClearArrayCopyVerify(std::uint32_t* self) {
    void* result = figure::ClearArray(self);
    ei::verify::Check("ClearArray@005AD870", true, result == self && self[0] == 0 && self[1] == 0 && self[2] == 0 && self[3] == 0,
                      "array %p", static_cast<void*>(self));
    return result;
}
EI_VERIFY(0x005AD870, ClearArrayCopyVerify, 0, s_unused);
