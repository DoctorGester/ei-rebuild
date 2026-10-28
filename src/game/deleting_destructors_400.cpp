// Scalar deleting destructors that only call their class's destructor body and free the memory when bit 0 of
// the flags is set (30 bytes each; counted pointer holders, session containers; the message classes' are
// their C++ destructors, net_messages.cpp). The body is our rewrite, named by its member or function. Two
// exception cleanup helpers are here too.
#include "base_manager.h"
#include "core_refcount.h"
#include "core_streams.h"
#include "game_lists.h"
#include "net_classes.h"

#include "lib/crt.h"

#include <cstdint>
#include <cstring>

namespace destructors400 {

// The destructor body on `self`: a member of T, or a __fastcall function taking the object.
template <class T, class R>
void RunBody(R (T::*body)(), void* self) { (static_cast<T*>(self)->*body)(); }
template <class T, class R>
void RunBody(R(__fastcall* body)(T*), void* self) { body(static_cast<T*>(self)); }

// The scalar deleting destructor of Body: `this` is the object (RET 4); returns it.
template <auto Body>
struct DeletingDestructor {
    void* Delete(unsigned flags) {
        void* self = this;
        RunBody(Body, self);
        if (flags & 1) gamecrt::operator_delete(self);
        return self;
    }
};

} // namespace destructors400
