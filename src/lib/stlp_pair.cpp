// STLport pair constructors and destructors at their addresses (stlp_pair.h).
#include "stlp_pair.h"

#include "ei/hook.h"

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

using stlp::DwordPair;
using stlp::StringListPair;

EI_REWRITTEN(0x0069C1C0, StringListPair::Construct);
EI_REWRITTEN(0x004AD640, StringListPair::Destruct);
