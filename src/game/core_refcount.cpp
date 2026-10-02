#include "core_refcount.h"

#include "ei/hook.h"
#include "ei/vcall.h"

namespace {
constexpr int kDeletingDestructor = 0;
constexpr int kShutdown = 2;
constexpr int kClone = 3;

// The original's inlined release. It also tests the pointer for null after decrementing through
// it, which can never matter.
void ReleaseObject(CRefObject* object) {
    if (--object->refs_ == 0) ei::VCall<void*>(object, kDeletingDestructor, 1);
}
} // namespace

void CRefObject::Release() { ReleaseObject(this); }
EI_REPLACE_METHOD(0x0040A3A0, CRefObject::Release);

int CRefObject::Shutdown() {
    ++refs_;
    int result = ei::VCall<int>(this, kShutdown);
    --refs_;
    return result;
}
EI_REPLACE_METHOD(0x00417DF0, CRefObject::Shutdown);

void RefPtr::Assign(CRefObject* object) {
    CRefObject* old = ptr;
    ptr = object;
    if (object) ++object->refs_;
    if (old) ReleaseObject(old);
}
EI_REPLACE_METHOD(0x00419CD0, RefPtr::Assign);

void RefPtr::Share(CRefObject* object) {
    if (!object || (object->flags7_ & CRefObject::kDetached)) {
        ptr = nullptr;
        return;
    }
    if (object->flags6_ & CRefObject::kCopyOnShare) object = ei::VCall<CRefObject*>(object, kClone);
    ptr = object;
    ++object->refs_;
    ptr->flags6_ |= CRefObject::kCopyOnShare;
}
EI_REPLACE_METHOD(0x00418BE0, RefPtr::Share);

void RefPtr::Destroy() {
    CRefObject* object = ptr;
    if (!object) return;
    if (object->refs_ != 1) {
        object->flags7_ |= CRefObject::kDetached;
        ++object->refs_;
        ei::VCall<int>(object, kShutdown);
        --object->refs_;
    }
    ReleaseObject(ptr);   // reloaded: the shutdown may have changed it
}
EI_REPLACE_METHOD(0x00413270, RefPtr::Destroy);
