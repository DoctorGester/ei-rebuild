// The CArray<T> instantiations that destroy game elements (game_arrays.h), registered at their
// library addresses (ticket S6; the plain ones are in lib/mfc_array.cpp).
#include "game/game_arrays.h"
#include "game/world.h"

#include "ei/hook.h"

namespace gamearrays {

// Records holding an aligned block.
EI_REPLACE(0x0044F7E0, BlocksAt8::SetSize);
EI_REPLACE(0x00452DF0, BlocksAt8::DestructElements);
EI_REPLACE(0x00453700, BlocksAt4::DestructElements);

// Zone exits and the map database's zone records.
EI_REPLACE(0x00573080, Exits::SetSize);
EI_REPLACE(0x00460670, Exits::DestructElements);
EI_REPLACE(0x004607B0, ZoneRecords::DestructElements);

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

// Script groups and quest objectives.
EI_REPLACE(0x004C3980, Groups::SetSize);
EI_REPLACE(0x004CC980, Groups::DestructElements);
EI_REPLACE(0x004CDF40, Objectives::SetSize);

// The client companion's effect particles.
EI_REPLACE(0x005121B0, Particles::SetSize);

// Object references: the script groups' (ScriptObjectRef) and the bare pointers' (a save's shared
// objects, the client maps).
EI_REPLACE(0x004C2050, ObjectRefs::SetSize);
EI_REPLACE(0x004C2200, ObjectRefs::RemoveAt);
EI_REPLACE(0x004CC7D0, ObjectRefs::DestructElements);
EI_REPLACE(0x0052CEC0, ObjectRefs::DestructElements);
EI_REPLACE_METHOD(0x004C2290, ScriptGroup::DestructBody);   // byte-identical to 004B5EF0
EI_REPLACE(0x004C3B20, Groups::Destruct<GroupArrayClass>);
EI_REPLACE(0x0059C8C0, ObjectPtrs::SetSize);
EI_REPLACE(0x00570000, ObjectPtrs::SetSize);
EI_REPLACE(0x0059C890, ObjectPtrs::DestructElements);
EI_REPLACE(0x0059A100, ObjectPtrs::Destruct<ObjectPtrArrayBaseClass>);

// The script's point lights.
EI_REPLACE(0x004C5CB0, PointLights::SetSize);

// CScript's arrays' destructors (S13b; CScript::Destruct, world_destructors.cpp).
EI_REPLACE(0x004CB150, PointLights::Destruct<PointLightArrayClass>);
EI_REPLACE(0x004CB650, Areas::Destruct<AreaArrayClass>);
EI_REPLACE(0x004CB310, Definitions::Destruct<DefinitionArrayClass>);

} // namespace gamearrays

// ---- The members of the script / world arrays (script.h, world.h) ----------------------------------

ScriptObjectRef* ScriptObjectRefs::CopyAt(ScriptObjectRef* out, int index) const {
    CEngineObject* object = data[index].object;
    out->object = object;
    if (object) ++object->refCount_;
    return out;
}
EI_REPLACE_METHOD(0x004CC7A0, ScriptObjectRefs::CopyAt);
EI_REPLACE_METHOD(0x004C21E0, ScriptObjectRefs::CopyAt);

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
EI_REPLACE_METHOD(0x004C1290, ScriptObjectRefs::CopyFrom);

ScriptGroup* ScriptGroups::CopyAt(ScriptGroup* out, int index) const {
    gamearrays::InitGroup(out);
    out->CopyFrom(data[index]);
    return out;
}
EI_REPLACE_METHOD(0x004CC890, ScriptGroups::CopyAt);
EI_REPLACE_METHOD(0x004C3B00, ScriptGroups::CopyAt);

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
EI_REPLACE_METHOD(0x004C1BE0, ScriptGroups::CopyFrom);

CEngineObject** EngineObjectPtrs::CopyAt(CEngineObject** out, int index) const {
    CEngineObject* object = data[index];
    *out = object;
    if (object) ++object->refCount_;
    return out;
}
EI_REPLACE_METHOD(0x0059E300, EngineObjectPtrs::CopyAt);
EI_REPLACE_METHOD(0x0059CA40, EngineObjectPtrs::CopyAt);

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
EI_REPLACE_METHOD(0x0059B8F0, EngineObjectPtrs::CopyFrom);
