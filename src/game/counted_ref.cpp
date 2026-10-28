// A counted reference to an engine object (AIRefTo<T>, unit_ai.h; the map objects, the AI, the traps,
// the effects and the unit controller keep these): reset, assign, and load / save by object id.
// The bodies are one template, instantiated below for the classes the fields hold.
#include "unit_ai.h"

#include "base_manager.h"
#include "world.h"

#include <list>

namespace {
constexpr int kNoObject = -1;
}

// The count and the release are the engine object's (seen through the base, whatever T declares).
template <typename T>
void AIRefTo<T>::Reset() {
    if (CEngineObject* held = object) held->Release();
    object = nullptr;
}

template <typename T>
void AIRefTo<T>::Assign(T* newObject) {
    if (newObject == object) return;
    if (CEngineObject* held = object) held->Release();
    object = nullptr;
    if (CEngineObject* taken = newObject) ++taken->refCount_;
    object = newObject;
}

// The id read; -1 drops the object, any other is looked up in the world's map object manager.
template <typename T>
void AIRefTo<T>::Load(CStream* stream, CWorld* world) {
    int id;
    stream->ReadValue(id);
    if (id == kNoObject) {
        Reset();
        return;
    }
    auto* manager = world->MapObjectManager();
    Assign(manager->FindObjectAs<T>(static_cast<unsigned>(id)));
}

template <typename T>
void AIRefTo<T>::Save(CStream* stream) {
    int id = object ? object->id_ : kNoObject;
    stream->WriteValue(id);
}

// The references: the engine object's two members, the typed ones' (the AI's map objects and units, the effects'
// figures) four.
template void AIRefTo<CEngineObject>::Reset();
template void AIRefTo<CEngineObject>::Assign(CEngineObject*);
template void AIRefTo<CMapObject>::Reset();
template void AIRefTo<CMapObject>::Assign(CMapObject*);
template void AIRefTo<CMapObject>::Load(CStream*, CWorld*);
template void AIRefTo<CMapObject>::Save(CStream*);
template void AIRefTo<CUnit>::Reset();
template void AIRefTo<CUnit>::Assign(CUnit*);
template void AIRefTo<CUnit>::Load(CStream*, CWorld*);
template void AIRefTo<CUnit>::Save(CStream*);

// Reads a count, then that many counted references added at the tail of the list, each loaded in
// `context` (the trap's units, the brain's lists).
void CountedListLoad(std::list<CUnit*>* list, CStream* stream, CWorld* context) {
    int count;
    stream->ReadValue(count);
    while (count-- > 0) {
        list->push_back(nullptr);
        AIRefTo<CUnit>::Of(&list->back())->Load(stream, context);
    }
}
