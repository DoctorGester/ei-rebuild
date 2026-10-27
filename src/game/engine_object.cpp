// CEngineObject: the default slots of its vtable and its non-virtual members. See
// And 10 are shorter than a hook's jump and stay unregistered.
#include "engine_object.h"

#include "base_manager.h"

#include "ei/hook.h"
#include "lib/crt.h"

void CEngineObject::CopyFrom(const CEngineObject& other) {
    flags_ = other.flags_;
    type_ = other.type_;
    prototypeId_ = other.prototypeId_;
}

// The default slots: the destructor lets go of the owner; 1 is CopyFrom, 5 / 9 / 10 empty, 6 returns 0.
CEngineObject::~CEngineObject() { owner_ = nullptr; }
EI_REWRITTEN_SPECIAL(0x00455930, CEngineObject::~CEngineObject);
EI_REWRITTEN_SPECIAL(0x0055ED90, CEngineObject::~CEngineObject);   // a jump to CParty's base
EI_REWRITTEN_SPECIAL(0x00402D20, CEngineObject::~CEngineObject);   // deleting
EI_REWRITTEN_SPECIAL(0x006611B0, CEngineObject::~CEngineObject);   // deleting (CParty's base, slot 0)
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

EI_REWRITTEN(0x00402D40, CEngineObject::CopyFrom);
EI_REWRITTEN(0x00455980, CEngineObject::Release);
EI_REWRITTEN(0x00402CE0, CEngineObject::GetName);
EI_REWRITTEN(0x00402CF0, CEngineObject::GetDescription);

CManagerContext* CEngineObject::ManagerContext() const { return owner_->context_; }
EI_REWRITTEN(0x00455940, CEngineObject::ManagerContext);

CBaseManager* CEngineObject::SetOwner(CBaseManager* owner) {
    CBaseManager* previous = owner_;
    owner_ = owner;
    return previous;
}
EI_REWRITTEN(0x00455970, CEngineObject::SetOwner);

void* CEngineObject::operator new(std::size_t size) noexcept { return gamecrt::operator_new(size); }
void CEngineObject::operator delete(void* block) { gamecrt::operator_delete(block); }

void CEngineObject::ClearFields() {
    refCount_ = 0;
    id_ = -1;
    prototypeId_ = 0;
    type_ = 0;
    flags_ = 0;
    owner_ = nullptr;
}
EI_REWRITTEN(0x0040CB20, CEngineObject::ClearFields);   // the constructor's body out of line (returns this)
