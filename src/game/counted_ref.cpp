// A counted engine object reference (Ref<T>, ref.h; the map objects, the AI, the traps, the effects and the unit
// controller keep these) saved and loaded by object id.
#include "unit_ai.h"

#include "base_manager.h"
#include "world.h"


namespace {
constexpr int kNoObject = -1;
}

template <typename T>
void LoadRef(Ref<T>& ref, ByteStream* stream, CWorld* world) {
    int id;
    stream->ReadValue(id);
    if (id == kNoObject) {
        ref.Reset();
        return;
    }
    auto* manager = world->GetMapObjectManager();
    ref.Assign(manager->FindObjectAs<T>(static_cast<unsigned>(id)));
}

void SaveRef(const EngineObject* object, ByteStream* stream) {
    int id = object ? object->id : kNoObject;
    stream->WriteValue(id);
}

// The references loaded: the AI's map objects and units, the effects' figures.
template void LoadRef(Ref<CMapObject>&, ByteStream*, CWorld*);
template void LoadRef(Ref<CUnit>&, ByteStream*, CWorld*);

// Reads a count, then that many counted references added at the tail of the list, each loaded in
// `context` (the trap's units, the brain's lists).
void CountedListLoad(HeldUnits* list, ByteStream* stream, CWorld* context) {
    int count;
    stream->ReadValue(count);
    while (count-- > 0) LoadRef(list->emplace_back(), stream, context);
}
