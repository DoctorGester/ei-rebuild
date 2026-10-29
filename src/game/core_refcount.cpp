#include "core_refcount.h"

int CRefObject::Shutdown() {
    ++refs_;
    int result = OnShutdown();
    --refs_;
    return result;
}

void RefPtr::Assign(CRefObject* object) {
    CRefObject* old = ptr;
    ptr = object;
    if (object) object->AddRef();
    if (old) old->Release();
}

void RefPtr::Share(CRefObject* object) {
    if (!object || (object->flags7_ & CRefObject::kDetached)) {
        ptr = nullptr;
        return;
    }
    if (object->flags6_ & CRefObject::kCopyOnShare) object = object->Clone();
    ptr = object;
    object->AddRef();
    ptr->flags6_ |= CRefObject::kCopyOnShare;
}

void RefPtr::Destroy() {
    CRefObject* object = ptr;
    if (!object) return;
    if (object->refs_ != 1) {
        object->flags7_ |= CRefObject::kDetached;
        object->AddRef();
        object->OnShutdown();
        --object->refs_;
    }
    ptr->Release();   // reloaded: the shutdown may have changed it
}

RefPtr* RefPtr::ShareAssign(CRefObject* object) {
    if (object != ptr) {
        Destroy();
        Share(object);
    }
    return this;
}
