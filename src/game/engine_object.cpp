// CEngineObject: the default slots of its vtable (0073B8E0) and its non-virtual members. See
// engine_object.h. Slots 5, 6, 9 and 10 (00402CC0, 00402CD0, 00402D00, 00402D10) are shorter than
// the hook's jump and stay original.
#include "engine_object.h"

#include "base_manager.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

// 00402D40 (slot 1).
void CEngineObject::CopyFromImpl(const CEngineObject& other) {
    flags_ = other.flags_;
    type_ = other.type_;
    prototypeId_ = other.prototypeId_;
}

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
EI_REPLACE_METHOD(0x00402D40, CEngineObject::CopyFromImpl);
EI_REPLACE(0x00455980, ReleaseHook);
EI_REPLACE(0x00402CE0, GetNameHook);
EI_REPLACE(0x00402CF0, GetDescriptionHook);

CEngineObject* CEngineObject::DeletingDestruct(unsigned flags) {
    DestructBase();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00402D20, CEngineObject::DeletingDestruct);
// S13: CParty's base (vtable 0074306C slot 0); its body 0055ED90 is a jump to 00455930.
EI_REPLACE_METHOD(0x006611B0, CEngineObject::DeletingDestruct);

void* CEngineObject::ManagerContext() const { return owner_->context_; }
EI_REPLACE_METHOD(0x00455940, CEngineObject::ManagerContext);

CBaseManager* CEngineObject::SetOwner(CBaseManager* owner) {
    CBaseManager* previous = owner_;
    owner_ = owner;
    return previous;
}
EI_REPLACE_METHOD(0x00455970, CEngineObject::SetOwner);

void CEngineObject::DestructBase() {
    ei::SetVtable(this, kVtableAddress);
    owner_ = nullptr;
}
EI_REPLACE_METHOD(0x00455930, CEngineObject::DestructBase);
// The compiler's jump thunks to it (functions.csv "thunk" rows; S16a: the standalone image has no
// code at them): the same replacement, so their calls skip its check.
EI_REPLACE_METHOD(0x0055ED90, CEngineObject::DestructBase);

void CEngineObject::ClearFields() {
    refCount_ = 0;
    id_ = -1;
    prototypeId_ = 0;
    type_ = 0;
    flags_ = 0;
    owner_ = nullptr;
}
