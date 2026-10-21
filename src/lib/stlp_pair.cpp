// STLport pair constructors and destructors at their addresses (stlp_pair.h).
#include "stlp_pair.h"

#include "crt.h"

#include "ei/hook.h"

#include <new>

namespace stlp {

DwordPair* __fastcall PairAbi::ConstructDwords(DwordPair* self, void*, const std::uint32_t* first, const std::uint32_t* second) {
    self->first = *first;
    self->second = *second;
    return self;
}

StringDwordPair* __fastcall PairAbi::ConstructStringDword(StringDwordPair* self, void*, const string* first,
                                                          const std::uint32_t* second) {
    new (&self->first) string(*first);
    self->second = *second;
    return self;
}

void StringDwordElement::Copy(StringDwordPair* to, const StringDwordPair& from) {
    PairAbi::ConstructStringDword(to, nullptr, &from.first, &from.second);
}

StringPair* __fastcall PairAbi::ConstructStrings(StringPair* self, void*, const string* first, const string* second) {
    new (&self->first) string(*first);
    new (&self->second) string(*second);
    return self;
}

StringListPair* __fastcall PairAbi::ConstructStringList(StringListPair* self, void*, const string* first,
                                                        const list<void*>* second) {
    new (&self->first) string(*first);
    self->second.Construct();
    list<void*>::InsertRange(self->second.End(), second->First(), second->End());
    return self;
}

void __fastcall PairAbi::DestroyStringList(StringListPair* self) {
    self->second.Destroy();
    self->first.~string();
}

void __fastcall PairAbi::DestroyStrings(StringPair* self) {
    self->second.~string();
    self->first.~string();
}

StringPair* __fastcall PairAbi::DeletingDestructor(StringPair* self, void*, unsigned flags) {
    DestroyStrings(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}

} // namespace stlp

using stlp::PairAbi;

