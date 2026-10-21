// The CArray<T> instantiations that destroy game elements (game_arrays.h), registered at their
// library addresses (ticket S6; the plain ones are in lib/mfc_array.cpp).
#include "game/game_arrays.h"
#include "game/world.h"

#include "ei/hook.h"

namespace gamearrays {

// The destructors of global arrays only exit functions call (S3c): the vtable back, the elements
// destroyed, the storage freed. Plain elements: the sector spiral's radius tree keys (CArray<float>,
// 007C2FD8; the exit function 005BF860) and offsets (007C2FF8; 005BF830), the path smoother's node
// pool (007C3038; 005C4480), the particles' draw order (007A9C50; 00498930). The script quest's
// objectives (007AA608; 004B17E0): each objective's text.
EI_REPLACE(0x005BF870, mfc::impl::ArrayHeader<mfc::impl::FloatArrayClass>::Destruct);
EI_REPLACE(0x005C18E0, mfc::impl::ArrayHeader<SpiralOffsetArrayClass>::Destruct);
EI_REPLACE(0x005C4490, mfc::impl::ArrayHeader<SmoothNodeArrayClass>::Destruct);
EI_REPLACE(0x00498E80, mfc::impl::ArrayHeader<ParticleDepthArrayClass>::Destruct);
EI_REPLACE(0x004C9DB0, Objectives::Destruct<ObjectiveArrayClass>);

// Object references: the script groups' (ScriptObjectRef) and the bare pointers' (a save's shared
// objects, the client maps).
EI_REPLACE_METHOD(0x004C2290, ScriptGroup::DestructBody);   // byte-identical to 004B5EF0

} // namespace gamearrays

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
        alignas(ScriptGroup) std::uint8_t storage[sizeof(ScriptGroup)];
        auto* copy = reinterpret_cast<ScriptGroup*>(storage);
        source.CopyAt(copy, i);
        data[i].CopyFrom(*copy);
        copy->DestructBody();
    }
}

CEngineObject** EngineObjectPtrs::CopyAt(CEngineObject** out, int index) const {
    CEngineObject* object = data[index];
    *out = object;
    if (object) ++object->refCount_;
    return out;
}

void EngineObjectPtrs::CopyFrom(const EngineObjectPtrs& source) {
    mfc::impl::ArrayAs<gamearrays::ObjectPtrs>(*this).SetSize(source.size, -1);
    for (int i = 0; i < source.size; ++i) {
        CEngineObject* copy;
        source.CopyAt(&copy, i);
        CEngineObject* old = data[i];
        data[i] = copy;
        if (copy) ++copy->refCount_;
        if (old) old->Release();
        if (copy) copy->Release();
    }
}
