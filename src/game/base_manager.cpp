#include "base_manager.h"

#include "ei/hook.h"
#include "ei/vcall.h"

CEngineObject* CBaseManager::FindObject(unsigned id) const {
    auto* node = objects_.Find(id);
    return node ? node->value : nullptr;
}
EI_REPLACE_METHOD(0x00403F40, CBaseManager::FindObject);

// 00455980, vtable slot 4 of every engine object. When the last reference goes, the owning manager
// destroys the object (manager slot 24, 00403AE0 for CBaseManager).
namespace {
// The original leaves `this` in EAX, or the manager's result when the object is destroyed.
int ReleaseImpl(CEngineObject* self) {
    if (--self->refCount_ == 0) return ei::VCall<int>(self->owner_, 24, self);
    return reinterpret_cast<int>(self);
}
int __fastcall ReleaseHook(CEngineObject* self) { return ReleaseImpl(self); }
} // namespace

void CEngineObject::Release() { ReleaseImpl(this); }
EI_REPLACE(0x00455980, ReleaseHook);
