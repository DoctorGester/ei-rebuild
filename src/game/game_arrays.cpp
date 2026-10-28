// The CArray<T> instantiations that destroy game elements (game_arrays.h; the plain ones are in
// lib/mfc_array.cpp).
#include "lib/static_object.h"
#include "game/game_arrays.h"
#include "game/record_arrays.h"
#include "game/item.h"
#include "game/world.h"

// ---- The members of the script / world arrays (script.h, world.h) ----------------------------------

ScriptObjectRef* ScriptObjectRefs::CopyAt(ScriptObjectRef* out, int index) const {
    CEngineObject* object = data[index].object;
    out->object = object;
    if (object) ++object->refCount_;
    return out;
}

void ScriptObjectRefs::CopyFrom(const ScriptObjectRefs& source) {
    mfc::impl::ArrayAs<gamearrays::ObjectRefs>(*this).SetSize(source.size, -1);
    for (int i = 0; i < source.size; ++i) {
        ScriptObjectRef copy;
        source.CopyAt(&copy, i);
        ScriptObjectRef* slot = &data[i];
        gamearrays::ReleaseAndClear(slot);
        slot->object = copy.object;
        if (copy.object) ++copy.object->refCount_;
        gamearrays::ReleaseAndClear(&copy);
    }
}

ScriptGroup* ScriptGroups::CopyAt(ScriptGroup* out, int index) const {
    gamearrays::InitGroup(out);
    out->CopyFrom(data[index]);
    return out;
}

void ScriptGroups::CopyFrom(const ScriptGroups& source) {
    mfc::impl::ArrayAs<gamearrays::Groups>(*this).SetSize(source.size, -1);
    for (int i = 0; i < source.size; ++i) {
        lib::InPlace<ScriptGroup> storage;
        auto* copy = storage.Get();
        source.CopyAt(copy, i);
        data[i].CopyFrom(*copy);
        copy->DestructBody();
    }
}
