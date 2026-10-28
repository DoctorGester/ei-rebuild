// STLport pair constructors and destructors at their addresses (stlp_pair.h).
#include "stlp_pair.h"

#include <memory>
#include <new>
#include <string>

namespace stlp {

StringListPair* StringListPair::Construct(const std::string& firstValue, const std::list<void*>& secondValue) {
    std::construct_at(&key, firstValue);
    std::construct_at(&mapped, secondValue);
    return this;
}

void StringListPair::Destruct() {
    std::destroy_at(&mapped);
    std::destroy_at(&key);
}

} // namespace stlp
