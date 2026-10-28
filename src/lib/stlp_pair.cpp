// STLport pair constructors and destructors at their addresses (stlp_pair.h).
#include "stlp_pair.h"

#include "crt.h"

#include <new>

namespace stlp {

StringListPair* StringListPair::Construct(const string& firstValue, const list<void*>& secondValue) {
    new (&key) string(firstValue);
    mapped.Construct();
    list<void*>::InsertRange(mapped.End(), secondValue.First(), secondValue.End());
    return this;
}

void StringListPair::Destruct() {
    mapped.Destroy();
    key.~string();
}

void StringPair::Destruct() {
    second.~string();
    first.~string();
}

StringPair* StringPair::DeletingDestructor(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

} // namespace stlp
