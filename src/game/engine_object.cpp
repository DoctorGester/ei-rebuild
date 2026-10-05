// CEngineObject: the default slots of its vtable (0073B8E0) and its non-virtual members. See
// engine_object.h. Slots 5, 6, 9 and 10 (00402CC0, 00402CD0, 00402D00, 00402D10) are shorter than
// the hook's jump and stay original.
#include "engine_object.h"

#include "base_manager.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

// 00402D40 (slot 1).
void CEngineObject::CopyFrom(const CEngineObject& other) {
    flags_ = other.flags_;
    type_ = other.type_;
    unknown_18 = other.unknown_18;
}

// 00455980 (slot 4). When the last reference goes, the owning manager releases the object (its
// slot 24, CBaseManager::ReleaseObject).
void CEngineObject::Release() {
    if (--refCount_ == 0) owner_->ReleaseObject(this);
}

// 00402CE0 / 00402CF0 (slots 7 / 8): the exe's "noname" and "nodesc" (callers get these pointers).
const char* CEngineObject::GetName() { return reinterpret_cast<const char*>(0x00783BF0); }
const char* CEngineObject::GetDescription() { return reinterpret_cast<const char*>(0x00783BF8); }

namespace {
void __fastcall CopyFromHook(CEngineObject* self, void*, const CEngineObject* other) {
    self->CEngineObject::CopyFrom(*other);
}
// The original leaves `this` in EAX, or what the manager's slot 24 leaves there.
int __fastcall ReleaseHook(CEngineObject* self) {
    if (--self->refCount_ == 0) return self->owner_->ReleaseObject(self);
    return reinterpret_cast<int>(self);
}
const char* __fastcall GetNameHook(CEngineObject* self) { return self->CEngineObject::GetName(); }
const char* __fastcall GetDescriptionHook(CEngineObject* self) { return self->CEngineObject::GetDescription(); }
} // namespace
EI_REPLACE(0x00402D40, CopyFromHook);
EI_REPLACE(0x00455980, ReleaseHook);
EI_REPLACE(0x00402CE0, GetNameHook);
EI_REPLACE(0x00402CF0, GetDescriptionHook);

CEngineObject* CEngineObject::DeletingDestruct(unsigned flags) {
    DestructBase();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00402D20, CEngineObject::DeletingDestruct);

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

void CEngineObject::ClearFields() {
    refCount_ = 0;
    id_ = -1;
    unknown_18 = 0;
    type_ = 0;
    flags_ = 0;
    owner_ = nullptr;
}
