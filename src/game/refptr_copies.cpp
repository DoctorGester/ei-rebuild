// Out-of-line copies of the RefPtr helpers (core_refcount.h):
// the compiler emitted one per template instantiation of the owning pointer, with
// byte-identical code; here each is one function.
#include "core_refcount.h"
#include "engine_object.h"

#include <cstdint>

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
