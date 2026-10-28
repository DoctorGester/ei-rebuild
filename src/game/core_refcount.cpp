#include "core_refcount.h"


namespace {
// The release inlined at the call sites (delete calls slot 0 with 1, after the same null test).
void ReleaseObject(CRefObject* object) {
    if (--object->refs_ == 0) delete object;
}
} // namespace

void CRefObject::Release() { ReleaseObject(this); }

int CRefObject::Shutdown() {
    ++refs_;
    int result = OnShutdown();
    --refs_;
    return result;
}

void RefPtr::Assign(CRefObject* object) {
    CRefObject* old = ptr;
    ptr = object;
    if (object) ++object->refs_;
    if (old) ReleaseObject(old);
}

void RefPtr::Share(CRefObject* object) {
    if (!object || (object->flags7_ & CRefObject::kDetached)) {
        ptr = nullptr;
        return;
    }
    if (object->flags6_ & CRefObject::kCopyOnShare) object = object->Clone();
    ptr = object;
    ++object->refs_;
    ptr->flags6_ |= CRefObject::kCopyOnShare;
}

void RefPtr::Destroy() {
    CRefObject* object = ptr;
    if (!object) return;
    if (object->refs_ != 1) {
        object->flags7_ |= CRefObject::kDetached;
        ++object->refs_;
        object->OnShutdown();
        --object->refs_;
    }
    ReleaseObject(ptr);   // reloaded: the shutdown may have changed it
}

RefPtr* RefPtr::ShareAssign(CRefObject* object) {
    if (object != ptr) {
        Destroy();
        Share(object);
    }
    return this;
}
