// A counted reference to an engine object (AIRefTo<T>, unit_ai.h; the map objects, the AI, the traps,
// the effects and the unit controller keep these): reset, assign, and load / save by object id.
// The bodies are one template, instantiated below for the classes the fields hold; AIRef's
// (CEngineObject) are the registered ones.
#include "unit_ai.h"

#include "base_manager.h"
#include "world.h"

#include "ei/hook.h"

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
    stream->Read(&id, 4);
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
    stream->Write(&id, 4);
}

template struct AIRefTo<CEngineObject>;
// The typed references (the AI's map objects and units, the effects' figures): the four members
// (DeleteScalar is AIRef's alone).
template void AIRefTo<CMapObject>::Reset();
template void AIRefTo<CMapObject>::Assign(CMapObject*);
template void AIRefTo<CMapObject>::Load(CStream*, CWorld*);
template void AIRefTo<CMapObject>::Save(CStream*);
template void AIRefTo<CUnit>::Reset();
template void AIRefTo<CUnit>::Assign(CUnit*);
template void AIRefTo<CUnit>::Load(CStream*, CWorld*);
template void AIRefTo<CUnit>::Save(CStream*);

EI_REWRITTEN(0x0052B030, AIRef::Reset);
EI_REWRITTEN(0x0052B050, AIRef::Reset);
EI_REWRITTEN(0x00558CE0, AIRef::Reset);
EI_REWRITTEN(0x0052A8F0, AIRef::Assign);
EI_REWRITTEN(0x0050E220, AIRef::Load);
EI_REWRITTEN(0x0050E290, AIRef::Save);

// Reads a count, then that many counted references added at the tail of the list, each loaded in
// `context` (the trap's units, the brain's lists).
void __fastcall CountedListLoad(mfc::CTypedPtrList<CUnit>* list, CStream* stream, CWorld* context) {
    int count;
    stream->Read(&count, 4);
    while (count-- > 0) {
        auto* node = list->AddTail(nullptr);
        AIRefTo<CUnit>::Of(&node->data)->Load(stream, context);
    }
}
EI_REWRITTEN(0x0052AF20, CountedListLoad);
