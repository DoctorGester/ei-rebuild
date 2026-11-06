// A counted engine object reference (Ref<T>, ref.h; the map objects, the AI, the traps, the effects and the unit
// controller keep these) saved and loaded by object id, and RefObject's two members.
#include "game/unit_ai.h"

#include "game/core/base_manager.h"
#include "game/core/core_refcount.h"
#include "game/world.h"

#include "lib/crt.h"


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

// The trap's units and the brain's lists.
HeldUnits LoadUnitList(ByteStream* stream, World* context) {
    HeldUnits list;
    for (int count = stream->ReadCount(); count > 0; --count) LoadRef(list.emplace_back(), stream, context);
    return list;
}

void SaveUnitList(const HeldUnits& list, ByteStream* stream) {
    stream->WriteCount(list.size());
    for (const HeldRef<Unit>& unit : list) stream->WriteId(unit);
}

// --- RefObject -----------------------------------------------------------------------------------

RefObject* RefObject::Clone() {
    game::FatalExit("%s", "Cloning an object that is never shared twice");
}

void RefObject::Shutdown() {
    ++refs;
    OnShutdown();
    --refs;
}
