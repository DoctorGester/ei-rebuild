// CEngineObject: the default slots of its vtable and its non-virtual members.
#include "engine_object.h"

#include "base_manager.h"

void CEngineObject::CopyFrom(const CEngineObject& other) {
    flags = other.flags;
    type = other.type;
    prototypeId = other.prototypeId;
}

// The default slots: the destructor lets go of the owner; 1 is CopyFrom, 5 / 9 / 10 empty, 6 returns 0.
CEngineObject::~CEngineObject() { owner = nullptr; }
void CEngineObject::ClearState() {}
bool CEngineObject::RunTick() { return false; }
void CEngineObject::Load(CStream*) {}
void CEngineObject::Save(CStream*) {}

// When the last reference goes, the owning manager releases the object (its
// CBaseManager::ReleaseObject). The result is `this`, or what the manager's call
// leaves there; no caller reads it.
void CEngineObject::Release() {
    if (--refCount == 0) owner->ReleaseObject(this);
}

const char* CEngineObject::GetName() { return "noname"; }
const char* CEngineObject::GetDescription() { return "nodesc"; }

CManagerContext* CEngineObject::ManagerContext() const { return owner->context; }

CBaseManager* CEngineObject::SetOwner(CBaseManager* newOwner) {
    CBaseManager* previous = owner;
    owner = newOwner;
    return previous;
}
