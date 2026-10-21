// Out-of-line copies of the RefPtr helpers (core_refcount.h) in 00410000-00430000 and
// 00470000-00480000: the compiler emitted one per template instantiation of the owning pointer, with
// byte-identical code. They are registered with the core implementation.
#include "core_refcount.h"
#include "engine_object.h"

#include <cstdint>

#include "ei/hook.h"
#include "lib/crt.h"

// The plain release of a RefPtr (the destructor of the instantiations without the owner's detach):
// drops one reference of the object, destroying it at zero. The pointer is left as it is.
// __fastcall shim of a thiscall without stack arguments. 21 copies.
void __fastcall RefPtrRelease(RefPtr* self) {
    CRefObject* object = self->ptr;
    if (!object) return;
    if (--object->refs_ == 0) delete object;   // slot 0 (the deleting destructor) with 1
}
EI_REPLACE(0x005B80D0, RefPtrRelease);   // the same, 007C2F64's exit function (no Ghidra function; S16b)

// 004AB960 (RET 4; S16a): counted_ptr<T>'s scalar deleting destructor (the release, then operator
// delete when bit 0 of `flags` is set). Returns the holder.
RefPtr* RefPtr::DeleteScalar(unsigned flags) {
    RefPtrRelease(this);
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REWRITTEN(0x004AB960, RefPtr::DeleteScalar);

// The holder cleared without a release (MOV [ECX],0). 17 copies.
void __fastcall RefPtrClear(RefPtr* self) { self->ptr = nullptr; }

// The default constructor (null), returning the holder. 10 copies (00478FB0: the loopback link's
// peer, net_link.cpp).
RefPtr* __fastcall RefPtrNull(RefPtr* self) {
    self->ptr = nullptr;
    return self;
}
EI_REPLACE(0x0041FF50, RefPtrNull);
EI_REPLACE(0x0041FF60, RefPtrNull);

// A holder of a raw pointer taken without a reference (ECX the holder, EDX the object), returning
// the holder (the original also zeroes a dead stack slot). 16 copies.
RefPtr* __fastcall RefPtrFromRaw(RefPtr* self, CRefObject* object) {
    self->ptr = object;
    return self;
}

// The released holder's scalar deleting destructor (0047A6D0: the release 0047A430, then the
// holder freed when bit 0 of the flags is set); returns the holder.
RefPtr* RefPtr::DeleteReleased(unsigned flags) {
    RefPtrRelease(this);
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REWRITTEN(0x0047A6D0, RefPtr::DeleteReleased);

// A holder of a raw pointer with a reference added, returning the holder. 2 copies.
RefPtr* RefPtr::FromRawCounted(CRefObject* object) {
    ptr = object;
    if (object) ++object->refs_;
    return this;
}

RefPtr* RefPtr::CopyCounted(const RefPtr* from) { return FromRawCounted(from->ptr); }

// The owning pointer's constructor from a raw pointer (RefPtr::Share, 00418BE0, which returns
// nothing) as the instantiations emit it out of line: the same body, returning the holder.
// 9 copies.
RefPtr* RefPtr::ShareReturn(CRefObject* object) {
    Share(object);
    return this;
}

// Assignment of an owning pointer (the original inlines Destroy and Share): from another holder
// (0042A770) and from a raw object (00438110). Nothing happens when it already holds the object.
RefPtr* RefPtr::AssignHolder(const RefPtr* other) {
    if (other->ptr == ptr) return this;
    Destroy();
    Share(other->ptr);
    return this;
}
RefPtr* RefPtr::AssignObject(CRefObject* object) {
    if (object == ptr) return this;
    Destroy();
    Share(object);
    return this;
}

// Holders of engine objects (a dword reference count at +4, released through slot 4).
// 00421350: release (a tail call of slot 4 when set).
void __fastcall ObjectHolderRelease(CEngineObject** self) {
    if (CEngineObject* object = *self) object->Release();
}

// 00429B50: assignment: the new object gains a reference, the old one is released.
void __fastcall ObjectHolderAssign(CEngineObject** self, void*, CEngineObject* object) {
    CEngineObject* previous = *self;
    *self = object;
    if (object) ++object->refCount_;
    if (previous) previous->Release();
}
