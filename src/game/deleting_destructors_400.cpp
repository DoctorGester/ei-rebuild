// Scalar deleting destructors that only call their class's destructor body
// and free the memory when bit 0 of the flags is set (30 bytes each; counted pointer holders, session
// containers; the message classes' are their C++ destructors, net_messages.cpp). The body is our rewrite, named by
// its member or function. Two exception cleanup helpers
// are here too.
#include "base_manager.h"
#include "core_refcount.h"
#include "core_streams.h"
#include "game_lists.h"
#include "net_classes.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstdint>
#include <cstring>

namespace destructors400 {

// The destructor body on `self`: a member of T, or a __fastcall function taking the object.
template <class T, class R>
void RunBody(R (T::*body)(), void* self) { (static_cast<T*>(self)->*body)(); }
template <class T, class R>
void RunBody(R(__fastcall* body)(T*), void* self) { body(static_cast<T*>(self)); }

// Registered as DeletingDestructor<Body>::Delete::Delete: `this` is the object (RET 4); returns it.
template <auto Body>
struct DeletingDestructor {
    void* Delete(unsigned flags) {
        void* self = this;
        RunBody(Body, self);
        if (flags & 1) gamecrt::operator_delete(self);
        return self;
    }
};

// Exception cleanup steps (reached by a jump from the unwind funclets): the dword at +8 of ECX cleared.
void __fastcall CleanupClear8(void* object) {
    std::uint32_t zero = 0;
    std::memcpy(static_cast<std::uint8_t*>(object) + 8, &zero, 4);
}

} // namespace destructors400

using destructors400::CleanupClear8;
using destructors400::DeletingDestructor;
using namespace net;

// Counted pointer holders; the world range has a byte-identical copy
// of each.
EI_REWRITTEN(0x004292A0, DeletingDestructor<RefPtrRelease>::Delete);
EI_REWRITTEN(0x004292C0, DeletingDestructor<&RefPtr::Destroy>::Delete);
EI_REWRITTEN(0x0059DD90, DeletingDestructor<RefPtrRelease>::Delete);
EI_REWRITTEN(0x005DA270, DeletingDestructor<&RefPtr::Destroy>::Delete);
EI_REWRITTEN(0x0042A5D0, DeletingDestructor<&RefPtr::Destroy>::Delete);
EI_REWRITTEN(0x0042A5F0, DeletingDestructor<&RefPtr::Destroy>::Delete);
EI_REWRITTEN(0x0042B880, DeletingDestructor<&RefPtr::Destroy>::Delete);
EI_REWRITTEN(0x0042D6D0, DeletingDestructor<misc400::ManagerDestruct>::Delete);
EI_REWRITTEN(0x0043DCA0, DeletingDestructor<&RefPtr::Destroy>::Delete);

EI_REWRITTEN(0x004054F0, CleanupClear8);
EI_REWRITTEN(0x00405530, CleanupClear8);
EI_REWRITTEN(0x00689810, CleanupClear8);
EI_REWRITTEN(0x00689880, CleanupClear8);
EI_REWRITTEN(0x0059AF10, CleanupClear8);
EI_REWRITTEN(0x0059AFE0, CleanupClear8);
