// A counted engine object reference (Ref<T>, ref.h; the map objects, the AI, the traps, the effects and the unit
// controller keep these) saved and loaded by object id.
#include "unit_ai.h"

#include "base_manager.h"
#include "world.h"


template <typename T>
void LoadRef(Ref<T>& ref, ByteStream* stream, World* world) {
    const int id = stream->ReadId();
    if (id == kNoObjectId) {
        ref.Reset();
        return;
    }
    auto* manager = world->GetMapObjectManager();
    ref.Assign(manager->FindObjectAs<T>(static_cast<unsigned>(id)));
}

// The references loaded: the AI's map objects and units, the effects' figures.
template void LoadRef(Ref<MapObject>&, ByteStream*, World*);
template void LoadRef(Ref<Unit>&, ByteStream*, World*);

// Reads a count, then that many counted references that replace the list, each loaded in `context` (the
// trap's units, the brain's lists).
void CountedListLoad(HeldUnits* list, ByteStream* stream, World* context) {
    list->clear();
    for (int count = stream->ReadCount(); count > 0; --count) LoadRef(list->emplace_back(), stream, context);
}
