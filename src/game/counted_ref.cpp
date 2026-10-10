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
EI_REPLACE_METHOD(0x0052B050, AIRef::Reset);
EI_REPLACE_METHOD(0x00558CE0, AIRef::Reset);   // byte-identical copy (A-U11)

void AIRef::Assign(CEngineObject* newObject) {
    if (newObject == object) return;
    if (object) object->Release();
    object = nullptr;
    if (newObject) ++newObject->refCount_;
    object = newObject;
}
EI_REPLACE_METHOD(0x0052A8F0, AIRef::Assign);

// The id read; -1 drops the object, any other is looked up in the world's map object manager.
void AIRef::Load(AIStream* stream, void* world) {
    int id;
    stream->Read(&id, 4);
    if (id == kNoObject) {
        Reset();
        return;
    }
    auto* manager = static_cast<CBaseManager*>(static_cast<CWorld*>(world)->MapObjectManager());
    Assign(manager->FindObject(static_cast<unsigned>(id)));
}
EI_REPLACE_METHOD(0x0050E220, AIRef::Load);

void AIRef::Save(AIStream* stream) {
    int id = object ? object->id_ : kNoObject;
    stream->Write(&id, 4);
}
EI_REPLACE_METHOD(0x0050E290, AIRef::Save);
