// Out-of-line copies of the RefPtr helpers (core_refcount.h):
// the compiler emitted one per template instantiation of the owning pointer, with
// byte-identical code; here each is one function.
#include "core_refcount.h"
#include "engine_object.h"

#include <cstdint>

#include "lib/crt.h"

// The plain release of a RefPtr (the destructor of the instantiations without the owner's detach):
// drops one reference of the object, destroying it at zero. The pointer is left as it is.
// A __fastcall function standing for a thiscall without stack arguments. 21 copies.
void RefPtrRelease(RefPtr* self) {
    CRefObject* object = self->ptr;
    if (!object) return;
    if (--object->refs_ == 0) delete object;   // slot 0 (the deleting destructor) with 1
}

// The holder cleared without a release (MOV [ECX],0). 17 copies.
void RefPtrClear(RefPtr* self) { self->ptr = nullptr; }

// The default constructor (null), returning the holder. 10 copies (one is the loopback link's
// peer, net_link.cpp).
RefPtr* RefPtrNull(RefPtr* self) {
    self->ptr = nullptr;
    return self;
}

// A holder of a raw pointer taken without a reference (ECX the holder, EDX the object), returning
// the holder (the game also zeroes a dead stack slot). 16 copies.
RefPtr* RefPtrFromRaw(RefPtr* self, CRefObject* object) {
    self->ptr = object;
    return self;
}

// The released holder's scalar deleting destructor (the release, then the
// holder freed when bit 0 of the flags is set); returns the holder.
RefPtr* RefPtr::DeleteReleased(unsigned flags) {
    RefPtrRelease(this);
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

// A holder of a raw pointer with a reference added, returning the holder. 2 copies.
RefPtr* RefPtr::FromRawCounted(CRefObject* object) {
    ptr = object;
    if (object) ++object->refs_;
    return this;
}

// Share as the instantiations emit it out of line (9 copies), returning the holder.
RefPtr* RefPtr::ShareReturn(CRefObject* object) {
    Share(object);
    return this;
}

RefPtr* RefPtr::AssignObject(CRefObject* object) {
    if (object == ptr) return this;
    Destroy();
    Share(object);
    return this;
}

// Holders of engine objects (a dword reference count at +4, released through slot 4).
// Release (a tail call of slot 4 when set).
void ObjectHolderRelease(CEngineObject** self) {
    if (CEngineObject* object = *self) object->Release();
}
