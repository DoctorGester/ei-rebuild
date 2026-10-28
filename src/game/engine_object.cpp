// CEngineObject: the default slots of its vtable and its non-virtual members.
#include "engine_object.h"

#include "base_manager.h"

#include "lib/crt.h"

void CEngineObject::CopyFrom(const CEngineObject& other) {
    flags_ = other.flags_;
    type_ = other.type_;
    prototypeId_ = other.prototypeId_;
}

// The default slots: the destructor lets go of the owner; 1 is CopyFrom, 5 / 9 / 10 empty, 6 returns 0.
CEngineObject::~CEngineObject() { owner_ = nullptr; }
void CEngineObject::ClearState() {}
int CEngineObject::RunTick() { return 0; }
void CEngineObject::Load(CStream*) {}
void CEngineObject::Save(CStream*) {}

// When the last reference goes, the owning manager releases the object (its
// slot 24, CBaseManager::ReleaseObject). EAX holds `this`, or what the manager's slot 24
// leaves there; no caller reads it.
void CEngineObject::Release() {
    if (--refCount_ == 0) owner_->ReleaseObject(this);
}

const char* CEngineObject::GetName() { return "noname"; }
const char* CEngineObject::GetDescription() { return "nodesc"; }

CManagerContext* CEngineObject::ManagerContext() const { return owner_->context_; }

CBaseManager* CEngineObject::SetOwner(CBaseManager* owner) {
    CBaseManager* previous = owner_;
    owner_ = owner;
    return previous;
}


void CEngineObject::ClearFields() {
    refCount_ = 0;
    id_ = -1;
    prototypeId_ = 0;
    type_ = 0;
    flags_ = 0;
    owner_ = nullptr;
}
