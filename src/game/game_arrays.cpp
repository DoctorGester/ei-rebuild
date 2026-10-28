// The CArray<T> instantiations that destroy game elements (game_arrays.h; the plain ones are in
// lib/mfc_array.cpp).
#include "lib/static_object.h"
#include "game/game_arrays.h"
#include "game/record_arrays.h"
#include "game/item.h"
#include "game/world.h"

// The map database's element policies (object_tables.h).
void AllodRecordElements::Construct(AllodRecord* allods, int count) {
    std::memset(static_cast<void*>(allods), 0, static_cast<std::size_t>(count) * sizeof(AllodRecord));
    for (; count > 0; --count, ++allods) {
        std::construct_at(&allods->name);
        for (auto& name : allods->otherNames) std::construct_at(&name);
        std::construct_at(&allods->maps);
    }
}

void AllodRecordElements::Destruct(AllodRecord* allods, int count) {
    for (; count > 0; --count, ++allods) {
        std::destroy_at(&allods->maps);
        for (int i = 4; i >= 0; --i) std::destroy_at(&allods->otherNames[i]);
        std::destroy_at(&allods->name);
    }
}

void MapRecordElements::Construct(MapRecord* maps, int count) {
    std::memset(static_cast<void*>(maps), 0, static_cast<std::size_t>(count) * sizeof(MapRecord));
    for (; count > 0; --count, ++maps) {
        std::construct_at(&maps->name);
        std::construct_at(&maps->allod);
        std::construct_at(&maps->mob);
        std::construct_at(&maps->map);
        std::construct_at(&maps->figure);
        std::construct_at(&maps->picture2);
        std::construct_at(&maps->picture);
        std::construct_at(&maps->camera);
        maps->exits.Construct(mfc::impl::MapRecordItemsClass::Vtable());
    }
}

void MapRecordElements::Destruct(MapRecord* maps, int count) {
    for (; count > 0; --count, ++maps) zone::ZoneRecordDestruct(maps);
}

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

template <class T>
T** CountedPtrs<T>::CopyAt(T** out, int index) const {
    T* object = this->data[index];
    *out = object;
    if (object) ++object->refCount_;
    return out;
}

template <class T>
void CountedPtrs<T>::CopyFrom(const CountedPtrs& source) {
    mfc::impl::ArrayAs<gamearrays::ObjectPtrs>(*this).SetSize(source.size, -1);
    for (int i = 0; i < source.size; ++i) {
        T* copy;
        source.CopyAt(&copy, i);
        T* old = this->data[i];
        this->data[i] = copy;
        if (copy) ++copy->refCount_;
        if (old) old->Release();
        if (copy) copy->Release();
    }
}
template struct CountedPtrs<CEngineObject>;
template struct CountedPtrs<CItem>;   // SharedObject::objects
