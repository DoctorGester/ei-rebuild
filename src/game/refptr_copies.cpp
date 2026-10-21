// Out-of-line copies of the RefPtr helpers (core_refcount.h) in 00410000-00430000 and
// 00470000-00480000: the compiler emitted one per template instantiation of the owning pointer, with
// byte-identical code. They are registered with the core implementation.
#include "core_refcount.h"
#include "engine_object.h"

#include <cstdint>

#include "ei/hook.h"
#include "lib/crt.h"

EI_REWRITTEN(0x005587A0, RefPtr::Destroy);
EI_REWRITTEN(0x0055EDA0, RefPtr::Destroy);
EI_REWRITTEN(0x0055EDE0, RefPtr::Destroy);
EI_REWRITTEN(0x00561A20, RefPtr::Destroy);
EI_REWRITTEN(0x005BE240, RefPtr::Destroy);
EI_REWRITTEN(0x00417A30, RefPtr::Destroy);
EI_REWRITTEN(0x00417A70, RefPtr::Destroy);
EI_REWRITTEN(0x00419D00, RefPtr::Destroy);
EI_REWRITTEN(0x00419F50, RefPtr::Destroy);
EI_REWRITTEN(0x0041C910, RefPtr::Destroy);
EI_REWRITTEN(0x0041C9D0, RefPtr::Destroy);
EI_REWRITTEN(0x00421600, RefPtr::Destroy);
EI_REWRITTEN(0x00421640, RefPtr::Destroy);
EI_REWRITTEN(0x00421680, RefPtr::Destroy);
EI_REWRITTEN(0x004216C0, RefPtr::Destroy);
EI_REWRITTEN(0x00421700, RefPtr::Destroy);
EI_REWRITTEN(0x00421740, RefPtr::Destroy);
EI_REWRITTEN(0x00421780, RefPtr::Destroy);
EI_REWRITTEN(0x004291A0, RefPtr::Destroy);
EI_REWRITTEN(0x004291E0, RefPtr::Destroy);
EI_REWRITTEN(0x00429220, RefPtr::Destroy);
EI_REWRITTEN(0x00429260, RefPtr::Destroy);
EI_REWRITTEN(0x0042A610, RefPtr::Destroy);
EI_REWRITTEN(0x0047A3F0, RefPtr::Destroy);
EI_REWRITTEN(0x0047C1E0, RefPtr::Destroy);
EI_REWRITTEN(0x0047D560, RefPtr::Destroy);
EI_REWRITTEN(0x00436430, RefPtr::Destroy);
EI_REWRITTEN(0x00438AA0, RefPtr::Destroy);
EI_REWRITTEN(0x00438B30, RefPtr::Destroy);
// The plain release of a RefPtr (the destructor of the instantiations without the owner's detach):
// drops one reference of the object, destroying it at zero. The pointer is left as it is.
// __fastcall shim of a thiscall without stack arguments. 21 copies.
void __fastcall RefPtrRelease(RefPtr* self) {
    CRefObject* object = self->ptr;
    if (!object) return;
    if (--object->refs_ == 0) delete object;   // slot 0 (the deleting destructor) with 1
}
EI_REWRITTEN(0x004141D0, RefPtrRelease);
EI_REWRITTEN(0x00415550, RefPtrRelease);
EI_REWRITTEN(0x004169F0, RefPtrRelease);
EI_REWRITTEN(0x00417080, RefPtrRelease);
EI_REWRITTEN(0x0041BC20, RefPtrRelease);
EI_REWRITTEN(0x0041C890, RefPtrRelease);
EI_REWRITTEN(0x0041C8D0, RefPtrRelease);
EI_REWRITTEN(0x0041C8F0, RefPtrRelease);
EI_REWRITTEN(0x0041DDA0, RefPtrRelease);
EI_REWRITTEN(0x0041E670, RefPtrRelease);
EI_REWRITTEN(0x0041E830, RefPtrRelease);
EI_REWRITTEN(0x00412260, RefPtrRelease);
EI_REWRITTEN(0x004122B0, RefPtrRelease);
EI_REWRITTEN(0x00427240, RefPtrRelease);
EI_REWRITTEN(0x00427460, RefPtrRelease);
EI_REWRITTEN(0x00427540, RefPtrRelease);
EI_REWRITTEN(0x004275E0, RefPtrRelease);
EI_REWRITTEN(0x0042A9E0, RefPtrRelease);
EI_REWRITTEN(0x004781D0, RefPtrRelease);
EI_REWRITTEN(0x0047A430, RefPtrRelease);
EI_REWRITTEN(0x0047CA80, RefPtrRelease);
EI_REWRITTEN(0x0047D170, RefPtrRelease);
EI_REWRITTEN(0x0047D7B0, RefPtrRelease);
EI_REWRITTEN(0x00438A10, RefPtrRelease);
EI_REWRITTEN(0x00439910, RefPtrRelease);
EI_REWRITTEN(0x0059B4B0, RefPtrRelease);
EI_REWRITTEN(0x0069FD50, RefPtrRelease);
EI_REWRITTEN(0x004A5D70, RefPtrRelease);
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
EI_REWRITTEN(0x00421360, RefPtrClear);
EI_REWRITTEN(0x00421370, RefPtrClear);
EI_REWRITTEN(0x00421380, RefPtrClear);
EI_REWRITTEN(0x00421390, RefPtrClear);
EI_REWRITTEN(0x004213A0, RefPtrClear);
EI_REWRITTEN(0x004213B0, RefPtrClear);
EI_REWRITTEN(0x004213C0, RefPtrClear);
EI_REWRITTEN(0x004213D0, RefPtrClear);
EI_REWRITTEN(0x004213E0, RefPtrClear);
EI_REWRITTEN(0x004213F0, RefPtrClear);
EI_REWRITTEN(0x004214B0, RefPtrClear);
EI_REWRITTEN(0x004214C0, RefPtrClear);
EI_REWRITTEN(0x004214D0, RefPtrClear);
EI_REWRITTEN(0x004214E0, RefPtrClear);
EI_REWRITTEN(0x004214F0, RefPtrClear);
EI_REWRITTEN(0x00421550, RefPtrClear);
EI_REWRITTEN(0x00438A00, RefPtrClear);

// The default constructor (null), returning the holder. 10 copies (00478FB0: the loopback link's
// peer, net_link.cpp).
RefPtr* __fastcall RefPtrNull(RefPtr* self) {
    self->ptr = nullptr;
    return self;
}
EI_REPLACE(0x0041FF50, RefPtrNull);
EI_REPLACE(0x0041FF60, RefPtrNull);
EI_REWRITTEN(0x0041FF70, RefPtrNull);
EI_REWRITTEN(0x0041FF80, RefPtrNull);
EI_REWRITTEN(0x0041FFC0, RefPtrNull);
EI_REWRITTEN(0x0041FFD0, RefPtrNull);
EI_REWRITTEN(0x0041FFE0, RefPtrNull);
EI_REWRITTEN(0x00438A90, RefPtrNull);
EI_REWRITTEN(0x00438F50, RefPtrNull);
EI_REWRITTEN(0x00478FB0, RefPtrNull);
EI_REWRITTEN(0x005FC1C0, RefPtrNull);
EI_REWRITTEN(0x005683A0, RefPtrNull);
EI_REWRITTEN(0x0058DFE0, RefPtrNull);

// A holder of a raw pointer taken without a reference (ECX the holder, EDX the object), returning
// the holder (the original also zeroes a dead stack slot). 16 copies.
RefPtr* __fastcall RefPtrFromRaw(RefPtr* self, CRefObject* object) {
    self->ptr = object;
    return self;
}
EI_REWRITTEN(0x00427D50, RefPtrFromRaw);
EI_REWRITTEN(0x00427FD0, RefPtrFromRaw);
EI_REWRITTEN(0x00427FE0, RefPtrFromRaw);
EI_REWRITTEN(0x00428030, RefPtrFromRaw);
EI_REWRITTEN(0x00428040, RefPtrFromRaw);
EI_REWRITTEN(0x00428050, RefPtrFromRaw);
EI_REWRITTEN(0x00428060, RefPtrFromRaw);
EI_REWRITTEN(0x00428070, RefPtrFromRaw);
EI_REWRITTEN(0x00428080, RefPtrFromRaw);
EI_REWRITTEN(0x00428A10, RefPtrFromRaw);
EI_REWRITTEN(0x00428C00, RefPtrFromRaw);
EI_REWRITTEN(0x00428C10, RefPtrFromRaw);
EI_REWRITTEN(0x00428C20, RefPtrFromRaw);
EI_REWRITTEN(0x00428C30, RefPtrFromRaw);
EI_REWRITTEN(0x00428C40, RefPtrFromRaw);
EI_REWRITTEN(0x00428DE0, RefPtrFromRaw);
EI_REWRITTEN(0x0047A690, RefPtrFromRaw);
EI_REWRITTEN(0x0047A6A0, RefPtrFromRaw);

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
EI_REWRITTEN(0x0040A770, RefPtr::FromRawCounted);
EI_REWRITTEN(0x0041DD50, RefPtr::FromRawCounted);

RefPtr* RefPtr::CopyCounted(const RefPtr* from) { return FromRawCounted(from->ptr); }
EI_REWRITTEN(0x004A5D50, RefPtr::CopyCounted);
EI_REWRITTEN(0x004ABE30, RefPtr::CopyCounted);

// The owning pointer's constructor from a raw pointer (RefPtr::Share, 00418BE0, which returns
// nothing) as the instantiations emit it out of line: the same body, returning the holder.
// 9 copies.
RefPtr* RefPtr::ShareReturn(CRefObject* object) {
    Share(object);
    return this;
}
EI_REWRITTEN(0x004292F0, RefPtr::ShareReturn);
EI_REWRITTEN(0x00429350, RefPtr::ShareReturn);
EI_REWRITTEN(0x00429430, RefPtr::ShareReturn);
EI_REWRITTEN(0x00429710, RefPtr::ShareReturn);
EI_REWRITTEN(0x00429930, RefPtr::ShareReturn);
EI_REWRITTEN(0x00429990, RefPtr::ShareReturn);
EI_REWRITTEN(0x004299F0, RefPtr::ShareReturn);
EI_REWRITTEN(0x00429A50, RefPtr::ShareReturn);
EI_REWRITTEN(0x00429AB0, RefPtr::ShareReturn);

// Assignment of an owning pointer (the original inlines Destroy and Share): from another holder
// (0042A770) and from a raw object (00438110). Nothing happens when it already holds the object.
RefPtr* RefPtr::AssignHolder(const RefPtr* other) {
    if (other->ptr == ptr) return this;
    Destroy();
    Share(other->ptr);
    return this;
}
EI_REWRITTEN(0x0042A770, RefPtr::AssignHolder);
RefPtr* RefPtr::AssignObject(CRefObject* object) {
    if (object == ptr) return this;
    Destroy();
    Share(object);
    return this;
}
EI_REWRITTEN(0x00438110, RefPtr::AssignObject);

// Holders of engine objects (a dword reference count at +4, released through slot 4).
// 00421350: release (a tail call of slot 4 when set).
void __fastcall ObjectHolderRelease(CEngineObject** self) {
    if (CEngineObject* object = *self) object->Release();
}
EI_REWRITTEN(0x00511B80, ObjectHolderRelease);
EI_REWRITTEN(0x0051E3D0, ObjectHolderRelease);
EI_REWRITTEN(0x0052D670, ObjectHolderRelease);
EI_REWRITTEN(0x00552B80, ObjectHolderRelease);
EI_REWRITTEN(0x00556AA0, ObjectHolderRelease);
EI_REWRITTEN(0x00598960, ObjectHolderRelease);
EI_REWRITTEN(0x00598970, ObjectHolderRelease);
EI_REWRITTEN(0x00598980, ObjectHolderRelease);
EI_REWRITTEN(0x00598990, ObjectHolderRelease);
EI_REWRITTEN(0x00421350, ObjectHolderRelease);

// 00429B50: assignment: the new object gains a reference, the old one is released.
void __fastcall ObjectHolderAssign(CEngineObject** self, void*, CEngineObject* object) {
    CEngineObject* previous = *self;
    *self = object;
    if (object) ++object->refCount_;
    if (previous) previous->Release();
}
EI_REWRITTEN(0x00511B90, ObjectHolderAssign);
EI_REWRITTEN(0x0052C690, ObjectHolderAssign);
EI_REWRITTEN(0x00556C40, ObjectHolderAssign);
EI_REWRITTEN(0x00556C60, ObjectHolderAssign);
EI_REWRITTEN(0x00429B50, ObjectHolderAssign);
