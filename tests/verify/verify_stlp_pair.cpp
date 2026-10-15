// Checks for the STLport pair members (src/lib/stlp_pair.cpp).
//
// - The dword pairs: pure; the original builds into a scratch pair, ours into the caller's, the two
//   compared.
// - The string pairs' constructors: the original builds into the caller's memory, its blocks go
//   back on top of their free lists (the last allocated first), then ours: the same blocks (blocks
//   above 128 bytes come from operator new and may differ), lengths, capacities, bytes and dword.
// - The destructors free blocks, so ours runs alone: each freed pool block must be on top of its
//   free list afterwards when it is the last freed of its size.
#include "verify.h"

#include "stlp_list_verify.h"

#include "../../src/lib/stlp_pair.h"
#include "../../src/lib/stlp_string.h"

#include <cstdint>
#include <cstring>

namespace {

using stlp::PairAbi;
using stlp_verify::FreeList;
using stlp_verify::Name;

void* s_unused;

// A string's words and up to 64 bytes of its text.
struct Text {
    const char* start;
    std::size_t size;
    std::size_t capacity;
    char bytes[64];

    static Text Of(const stlp::string& s) {
        Text t{};
        t.start = stlp::StringAbi::Start(s);
        t.size = s.size();
        t.capacity = static_cast<std::size_t>(stlp::StringAbi::EndOfStorage(s) - t.start);
        if (t.start) std::memcpy(t.bytes, t.start, t.size < sizeof(t.bytes) ? t.size : sizeof(t.bytes));
        return t;
    }
    bool Same(const Text& ours) const {
        std::size_t n = size < sizeof(bytes) ? size : sizeof(bytes);
        return size == ours.size && capacity == ours.capacity && (capacity > 128 || start == ours.start) &&
               std::memcmp(bytes, ours.bytes, n) == 0 && ours.start && ours.start[ours.size] == 0;
    }
    void Free() const {
        if (start) stlp::deallocate(const_cast<char*>(start), capacity);
    }
};

// True when `p` (a block of `bytes`) is on top of its free list, or comes from operator new.
bool OnTop(const void* p, std::size_t bytes) {
    void** list = FreeList(bytes);
    return !p || !list || *list == p;
}

template <std::uintptr_t A>
struct PairCheck {
    static inline void* original = nullptr;

    template <typename Fn>
    static Fn Original() {
        return reinterpret_cast<Fn>(original);
    }

    static stlp::DwordPair* __fastcall ConstructDwords(stlp::DwordPair* self, void*, const std::uint32_t* first,
                                                       const std::uint32_t* second) {
        stlp::DwordPair theirs{};
        stlp::DwordPair* result = Original<decltype(&PairAbi::ConstructDwords)>()(&theirs, nullptr, first, second);
        PairAbi::ConstructDwords(self, nullptr, first, second);
        ei::verify::Report(Name<A>("stlp::pair<dword, dword>::pair"),
                           result == &theirs && theirs.first == self->first && theirs.second == self->second, "");
        return self;
    }

    static stlp::StringDwordPair* __fastcall ConstructStringDword(stlp::StringDwordPair* self, void*, const stlp::string* first,
                                                                  const std::uint32_t* second) {
        stlp::StringDwordPair* result = Original<decltype(&PairAbi::ConstructStringDword)>()(self, nullptr, first, second);
        Text theirs = Text::Of(self->first);
        std::uint32_t theirSecond = self->second;
        theirs.Free();
        PairAbi::ConstructStringDword(self, nullptr, first, second);
        ei::verify::Report(Name<A>("stlp::pair<string, dword>::pair"),
                           result == self && theirs.Same(Text::Of(self->first)) && theirSecond == self->second, "");
        return self;
    }

    static stlp::StringPair* __fastcall ConstructStrings(stlp::StringPair* self, void*, const stlp::string* first,
                                                         const stlp::string* second) {
        stlp::StringPair* result = Original<decltype(&PairAbi::ConstructStrings)>()(self, nullptr, first, second);
        Text theirFirst = Text::Of(self->first);
        Text theirSecond = Text::Of(self->second);
        theirSecond.Free();
        theirFirst.Free();
        PairAbi::ConstructStrings(self, nullptr, first, second);
        ei::verify::Report(Name<A>("stlp::pair<string, string>::pair"),
                           result == self && theirFirst.Same(Text::Of(self->first)) && theirSecond.Same(Text::Of(self->second)),
                           "");
        return self;
    }

    // Ours only: the first string's block is freed last, so it is on top of its list.
    static bool FreedInOrder(const Text& first) { return OnTop(first.start, first.capacity); }

    static void __fastcall DestroyStrings(stlp::StringPair* self) {
        Text first = Text::Of(self->first);
        PairAbi::DestroyStrings(self);
        ei::verify::Report(Name<A>("stlp::pair<string, string>::~pair"), FreedInOrder(first), "");
    }

    static stlp::StringPair* __fastcall DeletingDestructor(stlp::StringPair* self, void*, unsigned flags) {
        Text first = Text::Of(self->first);
        stlp::StringPair* result = PairAbi::DeletingDestructor(self, nullptr, flags);
        ei::verify::Report(Name<A>("stlp::pair<string, string> deleting destructor"), result == self && FreedInOrder(first), "");
        return result;
    }
};

} // namespace

// 00407DE0 / 0040A9D0: MOV EAX,ECX; MOV ECX,[ESP+4] (6 bytes).
EI_VERIFY(0x00407DE0, PairCheck<0x00407DE0>::ConstructDwords, 6, PairCheck<0x00407DE0>::original);
EI_VERIFY(0x0040A9D0, PairCheck<0x0040A9D0>::ConstructDwords, 6, PairCheck<0x0040A9D0>::original);
// PUSH -1; PUSH imm32 (7 bytes).
EI_VERIFY(0x005729D0, PairCheck<0x005729D0>::ConstructStringDword, 7, PairCheck<0x005729D0>::original);
EI_VERIFY(0x00689950, PairCheck<0x00689950>::ConstructStringDword, 7, PairCheck<0x00689950>::original);
EI_VERIFY(0x006BC140, PairCheck<0x006BC140>::ConstructStringDword, 7, PairCheck<0x006BC140>::original);
EI_VERIFY(0x0064F880, PairCheck<0x0064F880>::ConstructStringDword, 7, PairCheck<0x0064F880>::original);
EI_VERIFY(0x006A7710, PairCheck<0x006A7710>::ConstructStringDword, 7, PairCheck<0x006A7710>::original);
