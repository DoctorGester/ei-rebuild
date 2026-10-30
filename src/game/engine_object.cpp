// EngineObject: the default slots of its vtable and its non-virtual members.
#include "engine_object.h"

#include "base_manager.h"

void EngineObject::CopyFrom(const EngineObject& other) {
    flags = other.flags;
    type = other.type;
    prototypeId = other.prototypeId;
}

// The default slots: the destructor lets go of the owner; 1 is CopyFrom, 5 / 9 / 10 empty, 6 returns 0.
EngineObject::~EngineObject() { owner = nullptr; }
void EngineObject::ClearState() {}
bool EngineObject::RunTick() { return false; }
void EngineObject::Load(ByteStream*) {}
void EngineObject::Save(ByteStream*) {}

// When the last reference goes, the owning manager releases the object (its
// BaseManager::ReleaseObject). The result is `this`, or what the manager's call
// leaves there; no caller reads it.
void EngineObject::Release() {
    if (--refCount == 0) owner->ReleaseObject(this);
}

const char* EngineObject::GetName() { return "noname"; }
const char* EngineObject::GetDescription() { return "nodesc"; }

ManagerContext* EngineObject::GetManagerContext() const { return owner->context; }

BaseManager* EngineObject::SetOwner(BaseManager* newOwner) {
    BaseManager* previous = owner;
    owner = newOwner;
    return previous;
}
