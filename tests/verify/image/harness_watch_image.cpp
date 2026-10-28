// The orientation watch's unit in the image (image_side.h; harness_watch.cpp).
#include "image_side.h"

#include "game/game_singletons.h"
#include "game/unit.h"
#include "game/world.h"

namespace ei::verify::image {

void* ClientUnit(unsigned id, std::uint32_t** flags, std::uint32_t** rotation) {
    CWorldClient* c = TheClientWorld();
    if (!c) return nullptr;
    for (CMapObject* object : c->objects_) {
        if (object && static_cast<unsigned>(object->id_) == id && object->GetClassId() == world::kClassUnit) {
            auto* unit = static_cast<CUnit*>(object);
            *flags = reinterpret_cast<std::uint32_t*>(&unit->flags_);
            *rotation = reinterpret_cast<std::uint32_t*>(&unit->localRotation_);
            return unit;
        }
    }
    return nullptr;
}

} // namespace ei::verify::image
