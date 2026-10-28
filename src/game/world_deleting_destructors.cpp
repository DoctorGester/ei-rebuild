// The scalar deleting destructors of the world range whose bodies are rewritten elsewhere.
// See world_deleting_destructors.h.
#include "world_deleting_destructors.h"

#include "core_refcount.h"
#include "figure_manager.h"
#include "map_spec.h"
#include "script.h"
#include "sector_map.h"
#include "unit_ai.h"

#include "lib/crt.h"
#include "lib/mfc_slots.h"
#include "game_lists.h"
#include "record_arrays.h"
#include "game/game_slots.h"

namespace {

// The shape of every one of them: the body, the memory freed when bit 0 is set, the object back.
template <typename T, typename Body>
T* DestroyAndFree(T* self, unsigned flags, Body body) {
    body(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}

} // namespace

template <>
AIRef* AIRef::DeleteScalar(unsigned flags) {
    return DestroyAndFree(this, flags, [](AIRef* ref) { ref->Reset(); });
}
