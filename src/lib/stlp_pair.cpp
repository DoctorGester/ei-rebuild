// STLport pair constructors and destructors at their addresses (stlp_pair.h).
#include "stlp_pair.h"

#include <memory>
#include <new>
#include <string>

namespace stlp {

StringListPair* StringListPair::Construct(const std::string& firstValue, const list<void*>& secondValue) {
    std::construct_at(&key, firstValue);
    mapped.Construct();
    list<void*>::InsertRange(mapped.End(), secondValue.First(), secondValue.End());
    return this;
}

void StringListPair::Destruct() {
    mapped.Destroy();
    std::destroy_at(&key);
}

} // namespace stlp
