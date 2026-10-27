// The orientation watch's unit in the image (image_side.h; harness_watch.cpp).
#include "image_side.h"

#include "game/game_singletons.h"
#include "game/unit.h"
#include "game/world.h"

namespace ei::verify::image {

void* ClientUnit(unsigned id, std::uint32_t** flags, std::uint32_t** rotation) {
    CWorldClient* c = TheClientWorld();
    if (!c || !c->objects_.head) return nullptr;
    for (auto* n = c->objects_.First(); n != c->objects_.End(); n = n->next) {
        if (n->data && static_cast<unsigned>(n->data->id_) == id && n->data->GetClassId() == world::kClassUnit) {
            auto* unit = static_cast<CUnit*>(n->data);
            *flags = reinterpret_cast<std::uint32_t*>(&unit->flags_);
            *rotation = reinterpret_cast<std::uint32_t*>(&unit->localRotation_);
            return unit;
        }
    }
    return nullptr;
}

} // namespace ei::verify::image
