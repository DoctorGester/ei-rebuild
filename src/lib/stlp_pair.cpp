// STLport pair constructors and destructors at their addresses (stlp_pair.h).
#include "stlp_pair.h"

#include "crt.h"

#include "ei/hook.h"

#include <memory>
#include <new>
#include <string>

namespace stlp {

DwordPair* DwordPair::Construct(const std::uint32_t& firstValue, const std::uint32_t& secondValue) {
    first = firstValue;
    second = secondValue;
    return this;
}

StringDwordPair* StringDwordPair::Construct(const string& firstValue, const std::uint32_t& secondValue) {
    new (&first) string(firstValue);
    second = secondValue;
    return this;
}

void StringDwordElement::Copy(StringDwordPair* to, const StringDwordPair& from) {
    to->Construct(from.first, from.second);
}

StringPair* StringPair::Construct(const string& firstValue, const string& secondValue) {
    new (&first) string(firstValue);
    new (&second) string(secondValue);
    return this;
}

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

using stlp::DwordPair;
using stlp::StringDwordPair;
using stlp::StringPair;
using stlp::StringListPair;

EI_REWRITTEN(0x00407DE0, DwordPair::Construct);
EI_REWRITTEN(0x0040A9D0, DwordPair::Construct);
EI_REWRITTEN(0x005729D0, StringDwordPair::Construct);
EI_REWRITTEN(0x00689950, StringDwordPair::Construct);
EI_REWRITTEN(0x006BC140, StringDwordPair::Construct);
EI_REWRITTEN(0x0064F880, StringDwordPair::Construct);
EI_REWRITTEN(0x006A7710, StringDwordPair::Construct);
EI_REWRITTEN(0x004FB170, StringDwordPair::Construct);
EI_REWRITTEN(0x0069C1C0, StringListPair::Construct);
EI_REWRITTEN(0x004AD640, StringListPair::Destruct);
