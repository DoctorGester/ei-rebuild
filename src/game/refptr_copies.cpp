// Out-of-line copies of the RefPtr helpers (core_refcount.h) in 00410000-00430000 and
// 00470000-00480000: the compiler emitted one per template instantiation of the owning pointer, with
// byte-identical code. They are registered with the core implementation.
#include "core_refcount.h"

#include "ei/hook.h"
#include "ei/vcall.h"

// RefPtr::Destroy (00413270): 21 more copies.
EI_REPLACE_METHOD(0x00417A30, RefPtr::Destroy);
EI_REPLACE_METHOD(0x00417A70, RefPtr::Destroy);
EI_REPLACE_METHOD(0x00419D00, RefPtr::Destroy);
EI_REPLACE_METHOD(0x00419F50, RefPtr::Destroy);
EI_REPLACE_METHOD(0x0041C910, RefPtr::Destroy);
EI_REPLACE_METHOD(0x0041C9D0, RefPtr::Destroy);
EI_REPLACE_METHOD(0x00421600, RefPtr::Destroy);
EI_REPLACE_METHOD(0x00421640, RefPtr::Destroy);
EI_REPLACE_METHOD(0x00421680, RefPtr::Destroy);
EI_REPLACE_METHOD(0x004216C0, RefPtr::Destroy);
EI_REPLACE_METHOD(0x00421700, RefPtr::Destroy);
EI_REPLACE_METHOD(0x00421740, RefPtr::Destroy);
EI_REPLACE_METHOD(0x00421780, RefPtr::Destroy);
EI_REPLACE_METHOD(0x004291A0, RefPtr::Destroy);
EI_REPLACE_METHOD(0x004291E0, RefPtr::Destroy);
EI_REPLACE_METHOD(0x00429220, RefPtr::Destroy);
EI_REPLACE_METHOD(0x00429260, RefPtr::Destroy);
EI_REPLACE_METHOD(0x0042A610, RefPtr::Destroy);
EI_REPLACE_METHOD(0x0047A3F0, RefPtr::Destroy);
EI_REPLACE_METHOD(0x0047C1E0, RefPtr::Destroy);
EI_REPLACE_METHOD(0x0047D560, RefPtr::Destroy);

// The plain release of a RefPtr (the destructor of the instantiations without the owner's detach):
// drops one reference of the object, destroying it at zero. The pointer is left as it is.
// __fastcall shim of a thiscall without stack arguments. 21 copies.
void __fastcall RefPtrRelease(RefPtr* self) {
    CRefObject* object = self->ptr;
    if (!object) return;
    if (--object->refs_ == 0) ei::VCall<void*>(object, 0, 1);   // slot 0: deleting destructor
}
EI_REPLACE(0x004141D0, RefPtrRelease);
EI_REPLACE(0x00415550, RefPtrRelease);
EI_REPLACE(0x004169F0, RefPtrRelease);
EI_REPLACE(0x00417080, RefPtrRelease);
EI_REPLACE(0x0041BC20, RefPtrRelease);
EI_REPLACE(0x0041C890, RefPtrRelease);
EI_REPLACE(0x0041C8D0, RefPtrRelease);
EI_REPLACE(0x0041C8F0, RefPtrRelease);
EI_REPLACE(0x0041DDA0, RefPtrRelease);
EI_REPLACE(0x0041E670, RefPtrRelease);
EI_REPLACE(0x0041E830, RefPtrRelease);
EI_REPLACE(0x00427240, RefPtrRelease);
EI_REPLACE(0x00427460, RefPtrRelease);
EI_REPLACE(0x00427540, RefPtrRelease);
EI_REPLACE(0x004275E0, RefPtrRelease);
EI_REPLACE(0x0042A9E0, RefPtrRelease);
EI_REPLACE(0x004781D0, RefPtrRelease);
EI_REPLACE(0x0047A430, RefPtrRelease);
EI_REPLACE(0x0047CA80, RefPtrRelease);
EI_REPLACE(0x0047D170, RefPtrRelease);
EI_REPLACE(0x0047D7B0, RefPtrRelease);
