// CEngineObject: the default slots of its vtable (game.exe's 0073B8E0) and its non-virtual members. See
// engine_object.h. Slots 5, 6, 9 and 10 (00402CC0, 00402CD0, 00402D00, 00402D10) are shorter than
// the hook's jump and stay original.
#include "engine_object.h"

#include "base_manager.h"

#include "ei/hook.h"
#include "lib/crt.h"

// 00402D40 (slot 1).
void CEngineObject::CopyFromImpl(const CEngineObject& other) {
    flags_ = other.flags_;
    type_ = other.type_;
    prototypeId_ = other.prototypeId_;
}

// The default slots: the destructor lets go of the owner; 1 is CopyFromImpl, 5 / 9 / 10 empty, 6 returns 0 (00402CC0,
// 00402CD0, 00402D00, 00402D10).
CEngineObject::~CEngineObject() { owner_ = nullptr; }
EI_REWRITTEN_SPECIAL(0x00455930, CEngineObject::~CEngineObject);
EI_REWRITTEN_SPECIAL(0x0055ED90, CEngineObject::~CEngineObject);   // a jump to 00455930 (CParty's base)
EI_REWRITTEN_SPECIAL(0x00402D20, CEngineObject::~CEngineObject);   // deleting
EI_REWRITTEN_SPECIAL(0x006611B0, CEngineObject::~CEngineObject);   // deleting (S13: CParty's base, 0074306C slot 0)
void CEngineObject::CopyFrom(const CEngineObject& other) { CopyFromImpl(other); }
void CEngineObject::ClearState() {}
int CEngineObject::RunTick() { return 0; }
void CEngineObject::Load(CStream*) {}
void CEngineObject::Save(CStream*) {}

// 00455980 (slot 4). When the last reference goes, the owning manager releases the object (its
// slot 24, CBaseManager::ReleaseObject).
void CEngineObject::Release() {
    if (--refCount_ == 0) owner_->ReleaseObject(this);
}

// 00402CE0 / 00402CF0 (slots 7 / 8): the exe's "noname" and "nodesc" (callers get these pointers).
const char* CEngineObject::GetName() { return "noname"; }
const char* CEngineObject::GetDescription() { return "nodesc"; }

namespace {
// The original leaves `this` in EAX, or what the manager's slot 24 leaves there.
int __fastcall ReleaseHook(CEngineObject* self) {
    if (--self->refCount_ == 0) return self->owner_->ReleaseObject(self);
    return reinterpret_cast<int>(self);
}
const char* __fastcall GetNameHook(CEngineObject* self) { return self->CEngineObject::GetName(); }
const char* __fastcall GetDescriptionHook(CEngineObject* self) { return self->CEngineObject::GetDescription(); }
} // namespace
EI_REWRITTEN(0x00402D40, CEngineObject::CopyFromImpl);
EI_REWRITTEN(0x00455980, ReleaseHook);
EI_REWRITTEN(0x00402CE0, GetNameHook);
EI_REWRITTEN(0x00402CF0, GetDescriptionHook);

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
