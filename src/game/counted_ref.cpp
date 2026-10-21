// A counted reference to an engine object (AIRef, unit_ai.h; the map objects, the AI, the traps and
// the unit controller keep these): reset 0052B030 (0052B050 is an identical instantiation), assign
// 0052A8F0, load 0050E220 and save 0050E290 by object id (A-U6).
#include "unit_ai.h"

#include "base_manager.h"
#include "world.h"

#include "ei/hook.h"

namespace {
constexpr int kNoObject = -1;
}

void AIRef::Reset() {
    if (object) object->Release();
    object = nullptr;
}
EI_REPLACE_METHOD(0x0052B030, AIRef::Reset);
EI_REWRITTEN(0x0052B050, AIRef::Reset);
EI_REWRITTEN(0x00558CE0, AIRef::Reset);

void AIRef::Assign(CEngineObject* newObject) {
    if (newObject == object) return;
    if (object) object->Release();
    object = nullptr;
    if (newObject) ++newObject->refCount_;
    object = newObject;
}
EI_REWRITTEN(0x0052A8F0, AIRef::Assign);

// The id read; -1 drops the object, any other is looked up in the world's map object manager.
void AIRef::Load(CStream* stream, void* world) {
    int id;
    stream->Read(&id, 4);
    if (id == kNoObject) {
        Reset();
        return;
    }
    auto* manager = static_cast<CWorld*>(world)->MapObjectManager();
    Assign(manager->FindObject(static_cast<unsigned>(id)));
}
EI_REWRITTEN(0x0050E220, AIRef::Load);

void AIRef::Save(CStream* stream) {
    int id = object ? object->id_ : kNoObject;
    stream->Write(&id, 4);
}
EI_REWRITTEN(0x0050E290, AIRef::Save);

// 0052AF20 (fastcall: ECX the list, EDX the stream; RET 4; A-U12): a count read, then that many
// counted references added at the tail of the list, each loaded in `context` (the trap's units,
// the brain's lists).
void __fastcall CountedListLoad(mfc::CTypedPtrList<CUnit>* list, CStream* stream, void* context) {
    int count;
    stream->Read(&count, 4);
    while (count-- > 0) {
        auto* node = list->AddTail(nullptr);
        reinterpret_cast<AIRef*>(&node->data)->Load(stream, context);
    }
}
EI_REWRITTEN(0x0052AF20, CountedListLoad);
