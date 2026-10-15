// The CArray<T> instantiations that destroy game elements (game_arrays.h), registered at their
// library addresses (ticket S6; the plain ones are in lib/mfc_array.cpp).
#include "game/game_arrays.h"

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
ScriptObjectRef* __fastcall ObjectRefGetAt(const ObjectRefs::Array* self, void*, ScriptObjectRef* result, int index) {
    CEngineObject* object = self->data[index].object;
    result->object = object;
    if (object) ++object->refCount_;
    return result;
}
EI_REPLACE(0x004CC7A0, ObjectRefGetAt);
EI_REPLACE(0x004C21E0, ObjectRefGetAt);
void __fastcall ObjectRefCopy(ObjectRefs::Array* self, void*, const ObjectRefs::Array* source) {
    self->SetSize(source->size, -1);
    for (int i = 0; i < source->size; ++i) {
        ScriptObjectRef copy;
        ObjectRefGetAt(source, nullptr, &copy, i);
        ScriptObjectRef* slot = &self->data[i];
        ReleaseAndClear(slot);
        slot->object = copy.object;
        if (copy.object) ++copy.object->refCount_;
        ReleaseAndClear(&copy);
    }
}
EI_REPLACE(0x004C1290, ObjectRefCopy);

// Script groups by value.
namespace {
ObjectRefs::Array* Refs(ScriptGroup* group) { return reinterpret_cast<ObjectRefs::Array*>(group); }
const ObjectRefs::Array* Refs(const ScriptGroup* group) { return reinterpret_cast<const ObjectRefs::Array*>(group); }
} // namespace
ScriptGroup* __fastcall GroupGetAt(const Groups::Array* self, void*, ScriptGroup* result, int index) {
    InitGroup(result);
    ObjectRefCopy(Refs(result), nullptr, Refs(&self->data[index]));
    return result;
}
EI_REPLACE(0x004CC890, GroupGetAt);
EI_REPLACE(0x004C3B00, GroupGetAt);
void __fastcall GroupArrayCopy(Groups::Array* self, void*, const Groups::Array* source) {
    self->SetSize(source->size, -1);
    for (int i = 0; i < source->size; ++i) {
        alignas(ScriptGroup) std::uint8_t storage[sizeof(ScriptGroup)];
        auto* copy = reinterpret_cast<ScriptGroup*>(storage);
        GroupGetAt(source, nullptr, copy, i);
        ObjectRefCopy(Refs(&self->data[i]), nullptr, Refs(copy));
        copy->DestructBody();
    }
}
EI_REPLACE(0x004C1BE0, GroupArrayCopy);
EI_REPLACE(0x004C3B20, Groups::Destruct<GroupArrayClass>);

EI_REPLACE(0x0059C8C0, ObjectPtrs::SetSize);
EI_REPLACE(0x00570000, ObjectPtrs::SetSize);
EI_REPLACE(0x0059C890, ObjectPtrs::DestructElements);
EI_REPLACE(0x0059A100, ObjectPtrs::Destruct<ObjectPtrArrayBaseClass>);

} // namespace gamearrays
