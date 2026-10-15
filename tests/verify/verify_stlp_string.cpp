// Checks for the STLport basic_string<char> members (src/lib/stlp_string.cpp).
//
// Per call:
// - operator< and __compare are pure: both run, the results compared.
// - The constructors and substr build into memory of the caller's: the original builds, its block
//   goes back on top of its free list, then ours: the same block (blocks above 128 bytes come from
//   operator new and may differ), the same length, capacity and bytes.
// - The rest change a live string (and may move its block), so ours runs alone and the result is
//   checked against the contents before: the bytes and terminator, and for append the capacity the
//   growth rule gives (size + max(size, n) + 1 when it does not fit).
// A start-up check runs the originals against ours: two strings driven by one random script
// (construct, assign, append, erase, reserve, push_back, substr, copy), after every step the same
// bytes, length and capacity.
#include "verify.h"

#include "stlp_list_verify.h"

#include "../../src/lib/stlp_string.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

using stlp::StringAbi;
using stlp_verify::Name;

std::size_t Size(const stlp::string& s) { return static_cast<std::size_t>(StringAbi::Finish(s) - StringAbi::Start(s)); }
std::size_t Capacity(const stlp::string& s) {
    return static_cast<std::size_t>(StringAbi::EndOfStorage(s) - StringAbi::Start(s));
}

// A string's state as bytes: length, capacity, contents with the terminator (up to 255 bytes).
struct State {
    std::size_t size = 0;
    std::size_t capacity = 0;
    char* start = nullptr;
    unsigned char bytes[256] = {};
    bool terminated = false;

    static State Of(const stlp::string& s, bool text = true) {
        State st;
        st.start = StringAbi::Start(s);
        st.size = Size(s);
        st.capacity = Capacity(s);
        if (text && st.start) {
            std::memcpy(st.bytes, st.start, std::min<std::size_t>(st.size, sizeof(st.bytes)));
            st.terminated = st.start[st.size] == 0;
        }
        return st;
    }
    bool SameText(const State& o) const {
        return size == o.size && capacity == o.capacity && terminated == o.terminated &&
               std::memcmp(bytes, o.bytes, std::min<std::size_t>(size, sizeof(bytes))) == 0;
    }
};

// Frees a block the original built (the block back on top of its free list).
void Free(const State& st) {
    if (st.start) stlp::deallocate(st.start, st.capacity);
}

bool SameBuild(const State& theirs, const State& ours) {
    return theirs.SameText(ours) && (theirs.capacity > 128 || theirs.start == ours.start);
}

template <std::uintptr_t A>
struct StringCheck {
    static inline void* original = nullptr;

    template <typename Fn>
    static Fn Original() {
        return reinterpret_cast<Fn>(original);
    }

    // --- Pure ---
    static bool __fastcall Less(const stlp::string* a, const stlp::string* b) {
        bool theirs = Original<decltype(&StringAbi::Less)>()(a, b);
        bool ours = StringAbi::Less(a, b);
        ei::verify::Check(Name<A>("stlp::operator<(string, string)"), theirs, ours, "");
        return ours;
    }
    static int __fastcall Compare(const char* f1, const char* l1, const char* f2, const char* l2) {
        int theirs = Original<decltype(&StringAbi::Compare)>()(f1, l1, f2, l2);
        int ours = StringAbi::Compare(f1, l1, f2, l2);
        ei::verify::Check(Name<A>("stlp::basic_string::_M_compare"), theirs, ours, "");
        return ours;
    }

    // --- Builds: the original, its block freed, then ours ---
    template <typename Build>
    static void Built(const char* kind, stlp::string* s, Build build, bool text = true) {
        build(true);
        State theirs = State::Of(*s, text);
        Free(theirs);
        build(false);
        State ours = State::Of(*s, text);
        ei::verify::Report(Name<A>(kind), SameBuild(theirs, ours), "");
    }
    static void __fastcall RangeInitialize(stlp::string* self, void*, const char* first, const char* last) {
        Built("stlp::basic_string::_M_range_initialize", self, [&](bool orig) {
            (orig ? Original<decltype(&StringAbi::RangeInitialize)>() : &StringAbi::RangeInitialize)(self, nullptr, first, last);
        });
    }
    static void __fastcall RangeInitializeTagged(stlp::string* self, void*, const char* first, const char* last, std::uint32_t tag) {
        Built("stlp::basic_string::_M_range_initialize", self, [&](bool orig) {
            (orig ? Original<decltype(&StringAbi::RangeInitializeTagged)>() : &StringAbi::RangeInitializeTagged)(self, nullptr, first, last, tag);
        });
    }
    static stlp::string* __fastcall CopyConstruct(stlp::string* self, void*, const stlp::string* from) {
        Built("stlp::basic_string::basic_string(const basic_string&)", self, [&](bool orig) {
            (orig ? Original<decltype(&StringAbi::CopyConstruct)>() : &StringAbi::CopyConstruct)(self, nullptr, from);
        });
        return self;
    }
    static void __fastcall ConstructAt(stlp::string* p, const stlp::string* from) {
        if (!p) {
            StringAbi::ConstructAt(p, from);
            ei::verify::Report(Name<A>("stlp::_Construct(string)"), true, "");
            return;
        }
        Built("stlp::_Construct(string)", p, [&](bool orig) {
            (orig ? Original<decltype(&StringAbi::ConstructAt)>() : &StringAbi::ConstructAt)(p, from);
        });
    }
    static stlp::string* __fastcall ConstructFromText(stlp::string* self, void*, const char* text, const void* alloc) {
        Built("stlp::basic_string::basic_string(const char*)", self, [&](bool orig) {
            (orig ? Original<decltype(&StringAbi::ConstructFromText)>() : &StringAbi::ConstructFromText)(self, nullptr, text, alloc);
        });
        return self;
    }
    static stlp::string* __fastcall ConstructDefault(stlp::string* self) {
        Built("stlp::basic_string::basic_string()", self, [&](bool orig) {
            (orig ? Original<decltype(&StringAbi::ConstructDefault)>() : &StringAbi::ConstructDefault)(self);
        });
        return self;
    }
    static stlp::string* __fastcall ConstructWithCapacity(stlp::string* self, void*, const void* alloc, std::size_t n) {
        Built("stlp::_String_base::_String_base(n)", self, [&](bool orig) {
            (orig ? Original<decltype(&StringAbi::ConstructWithCapacity)>() : &StringAbi::ConstructWithCapacity)(self, nullptr, alloc, n);
        }, false);
        return self;
    }
    static stlp::string* __fastcall Substr(const stlp::string* self, void*, stlp::string* out, std::size_t pos, std::size_t n) {
        Built("stlp::basic_string::substr", out, [&](bool orig) {
            (orig ? Original<decltype(&StringAbi::Substr)>() : &StringAbi::Substr)(self, nullptr, out, pos, n);
        });
        return out;
    }

    // --- Ours only: the contents against the ones before ---
    static bool Holds(const stlp::string& s, const char* text, std::size_t n) {
        return Size(s) == n && Capacity(s) >= n + 1 && std::memcmp(StringAbi::Start(s), text, n) == 0 &&
               StringAbi::Start(s)[n] == 0;
    }
    template <typename Op>
    static void Changed(const char* kind, stlp::string* s, const char* first, const char* last, bool append, Op op) {
        char expected[512];
        std::size_t before = Size(*s);
        std::size_t capacity = Capacity(*s);
        std::size_t n = static_cast<std::size_t>(last - first);
        bool checked = (append ? before : 0) + n < sizeof(expected);
        if (checked) {
            std::size_t at = append ? before : 0;
            if (append) std::memcpy(expected, StringAbi::Start(*s), before);
            std::memcpy(expected + at, first, n);
        }
        op();
        bool ok = true;
        if (checked) {
            std::size_t size = (append ? before : 0) + n;
            ok = Holds(*s, expected, size);
            if (append && n) ok = ok && Capacity(*s) == (before + n + 1 <= capacity ? capacity : before + std::max(before, n) + 1);
        }
        ei::verify::Report(Name<A>(kind), ok, "");
    }
    static stlp::string* __fastcall Append(stlp::string* self, void*, const char* first, const char* last, std::uint32_t tag) {
        stlp::string* result = nullptr;
        Changed("stlp::basic_string::append", self, first, last, true, [&] { result = StringAbi::Append(self, nullptr, first, last, tag); });
        return result;
    }
    static stlp::string* __fastcall AppendRange(stlp::string* self, void*, const char* first, const char* last) {
        stlp::string* result = nullptr;
        Changed("stlp::basic_string::append", self, first, last, true, [&] { result = StringAbi::AppendRange(self, nullptr, first, last); });
        return result;
    }
    static stlp::string* __fastcall AssignRange(stlp::string* self, void*, const char* first, const char* last) {
        stlp::string* result = nullptr;
        Changed("stlp::basic_string::assign", self, first, last, false, [&] { result = StringAbi::AssignRange(self, nullptr, first, last); });
        return result;
    }
    static stlp::string* __fastcall AssignRangeTagged(stlp::string* self, void*, const char* first, const char* last, std::uint32_t tag) {
        stlp::string* result = nullptr;
        Changed("stlp::basic_string::assign", self, first, last, false,
                [&] { result = StringAbi::AssignRangeTagged(self, nullptr, first, last, tag); });
        return result;
    }
    static stlp::string* __fastcall AssignString(stlp::string* self, void*, const stlp::string* from) {
        stlp::string* result = nullptr;
        if (from == self) {
            State before = State::Of(*self);
            result = StringAbi::AssignString(self, nullptr, from);
            ei::verify::Report(Name<A>("stlp::basic_string::operator="), before.SameText(State::Of(*self)), "");
            return result;
        }
        Changed("stlp::basic_string::operator=", self, StringAbi::Start(*from), StringAbi::Finish(*from), false,
                [&] { result = StringAbi::AssignString(self, nullptr, from); });
        return result;
    }
    static stlp::string* __fastcall AssignText(stlp::string* self, void*, const char* text) {
        stlp::string* result = nullptr;
        Changed("stlp::basic_string::operator=(const char*)", self, text, text + std::strlen(text), false,
                [&] { result = StringAbi::AssignText(self, nullptr, text); });
        return result;
    }
    static char* __fastcall Erase(stlp::string* self, void*, char* first, char* last) {
        char expected[512];
        std::size_t size = Size(*self);
        bool checked = size < sizeof(expected);
        std::size_t at = static_cast<std::size_t>(first - StringAbi::Start(*self));
        std::size_t gone = static_cast<std::size_t>(last - first);
        if (checked) {
            std::memcpy(expected, StringAbi::Start(*self), at);
            std::memcpy(expected + at, last, size - at - gone);
        }
        char* result = StringAbi::Erase(self, nullptr, first, last);
        ei::verify::Report(Name<A>("stlp::basic_string::erase"), result == first && (!checked || Holds(*self, expected, size - gone)), "");
        return result;
    }
    static void __fastcall Reserve(stlp::string* self, void*, std::size_t n) {
        State before = State::Of(*self);
        StringAbi::Reserve(self, nullptr, n);
        State after = State::Of(*self);
        bool ok = after.size == before.size && after.capacity == std::max(before.size, n) + 1 && after.terminated &&
                  std::memcmp(after.bytes, before.bytes, std::min<std::size_t>(before.size, sizeof(before.bytes))) == 0;
        ei::verify::Report(Name<A>("stlp::basic_string::reserve"), ok, "");
    }
    static void __fastcall PushBack(stlp::string* self, void*, char c) {
        State before = State::Of(*self);
        StringAbi::PushBack(self, nullptr, c);
        State after = State::Of(*self);
        bool ok = after.size == before.size + 1 && after.terminated && StringAbi::Start(*self)[before.size] == c &&
                  std::memcmp(after.bytes, before.bytes, std::min<std::size_t>(before.size, sizeof(before.bytes))) == 0;
        ei::verify::Report(Name<A>("stlp::basic_string::push_back"), ok, "");
    }
    static stlp::string* __fastcall DeletingDestructor(stlp::string* self, void*, unsigned flags) {
        stlp::string* result = StringAbi::DeletingDestructor(self, nullptr, flags);
        ei::verify::Report(Name<A>("stlp::basic_string::`scalar deleting destructor'"), result == self, "");
        return result;
    }
};

// --- Start-up check -----------------------------------------------------------------------------

struct Random {
    std::uint32_t state;
    std::uint32_t Next() {
        state = state * 1103515245u + 12345u;
        return state >> 8;
    }
};

template <std::uintptr_t A, typename Fn>
Fn OriginalOf(Fn) {
    return reinterpret_cast<Fn>(StringCheck<A>::original);
}

void Release(stlp::string* s) {
    if (StringAbi::Start(*s)) stlp::deallocate(StringAbi::Start(*s), Capacity(*s));
}

void StringStartupCheck() {
    const auto fromText = OriginalOf<0x0040C5A0>(&StringAbi::ConstructFromText);
    const auto assignText = OriginalOf<0x0041F550>(&StringAbi::AssignText);
    const auto assignRange = OriginalOf<0x00426A80>(&StringAbi::AssignRange);
    const auto assignTagged = OriginalOf<0x0042A4F0>(&StringAbi::AssignRangeTagged);
    const auto append = OriginalOf<0x0042B700>(&StringAbi::Append);
    const auto append2 = OriginalOf<0x0040C2A0>(&StringAbi::Append);
    const auto erase = OriginalOf<0x0040B3C0>(&StringAbi::Erase);
    const auto reserve = OriginalOf<0x0068C680>(&StringAbi::Reserve);
    const auto pushBack = OriginalOf<0x0069BF10>(&StringAbi::PushBack);
    const auto substr = OriginalOf<0x0041F5F0>(&StringAbi::Substr);
    const auto copy = OriginalOf<0x004A8930>(&StringAbi::CopyConstruct);
    const auto assignString = OriginalOf<0x0041F4E0>(&StringAbi::AssignString);
    const auto less = OriginalOf<0x00445780>(&StringAbi::Less);
    const auto compare = OriginalOf<0x00445810>(&StringAbi::Compare);

    static const char kText[] = "the quick brown fox jumps over the lazy dog; 0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ "
                                "abcdefghijklmnopqrstuvwxyz the quick brown fox jumps over the lazy dog again";
    const std::size_t kLength = sizeof(kText) - 1;
    alignas(4) unsigned char storage[2][sizeof(stlp::string)];
    stlp::string* s[2] = {reinterpret_cast<stlp::string*>(storage[0]), reinterpret_cast<stlp::string*>(storage[1])};
    int failedStep = -1;
    for (std::uint32_t seed = 1; seed <= 8 && failedStep < 0; ++seed) {
        Random random{seed};
        const char allocator = 0;   // the original reads the allocator (an empty byte)
        fromText(s[0], nullptr, "", &allocator);
        StringAbi::ConstructFromText(s[1], nullptr, "", &allocator);
        for (int step = 0; step < 200 && failedStep < 0; ++step) {
            std::uint32_t op = random.Next() % 10;
            std::uint32_t a = random.Next() % (kLength + 1);
            std::uint32_t b = a + random.Next() % (kLength + 1 - a) % 40;
            char c = static_cast<char>('a' + random.Next() % 26);
            std::uint32_t r = random.Next();
            bool same = true;
            for (int i = 0; i < 2; ++i) {
                bool orig = i == 0;
                stlp::string* t = s[i];
                std::size_t size = Size(*t);
                switch (op) {
                case 0: (orig ? append : &StringAbi::Append)(t, nullptr, kText + a, kText + b, 0); break;
                case 1: (orig ? append2 : &StringAbi::Append)(t, nullptr, kText + a, kText + b, 0); break;
                case 2:
                    if (r % 4 == 0) (orig ? assignText : &StringAbi::AssignText)(t, nullptr, kText + a);
                    else if (r % 4 == 1) (orig ? assignRange : &StringAbi::AssignRange)(t, nullptr, kText + a, kText + b);
                    else (orig ? assignTagged : &StringAbi::AssignRangeTagged)(t, nullptr, kText + a, kText + b, 0);
                    break;
                case 3:
                    if (size) {
                        std::size_t from = r % size;
                        std::size_t to = from + (r >> 8) % (size - from + 1);
                        char* result = (orig ? erase : &StringAbi::Erase)(t, nullptr, StringAbi::Start(*t) + from, StringAbi::Start(*t) + to);
                        same = same && result == StringAbi::Start(*t) + from;
                    }
                    break;
                case 4: (orig ? reserve : &StringAbi::Reserve)(t, nullptr, r % 64); break;
                case 5:
                case 6: (orig ? pushBack : &StringAbi::PushBack)(t, nullptr, c); break;
                case 7:
                case 8: {
                    // substr or a copy into a third string, compared with its source, then assigned back.
                    alignas(4) unsigned char spare[sizeof(stlp::string)];
                    stlp::string* u = reinterpret_cast<stlp::string*>(spare);
                    if (op == 7) {
                        std::size_t pos = size ? r % (size + 1) : 0;
                        (orig ? substr : &StringAbi::Substr)(t, nullptr, u, pos, (r >> 8) % 20);
                        same = same && Size(*u) == std::min<std::size_t>(size - pos, (r >> 8) % 20) &&
                               std::memcmp(StringAbi::Start(*u), StringAbi::Start(*t) + pos, Size(*u)) == 0;
                    } else {
                        (orig ? copy : &StringAbi::CopyConstruct)(u, nullptr, t);
                        same = same && Size(*u) == size && std::memcmp(StringAbi::Start(*u), StringAbi::Start(*t), size) == 0;
                    }
                    (orig ? assignString : &StringAbi::AssignString)(t, nullptr, u);
                    Release(u);
                    break;
                }
                case 9: {
                    stlp::string* other = s[1 - i];
                    bool l = (orig ? less : &StringAbi::Less)(t, other);
                    int cmp = (orig ? compare : &StringAbi::Compare)(StringAbi::Start(*t), StringAbi::Finish(*t),
                                                                     StringAbi::Start(*other), StringAbi::Finish(*other));
                    same = same && !l && cmp == 0;   // the two strings are equal at this point
                    break;
                }
                }
            }
            if (!same || !State::Of(*s[0]).SameText(State::Of(*s[1]))) failedStep = static_cast<int>(seed * 1000 + step);
        }
        Release(s[0]);
        Release(s[1]);
    }
    char detail[48] = "";
    if (failedStep >= 0) std::snprintf(detail, sizeof(detail), "seed / step %d", failedStep);
    ei::verify::Report("stlp::basic_string start-up (original against ours)", failedStep < 0, detail);
}

} // namespace

EI_STARTUP_CHECK(StringStartupCheck);

EI_VERIFY(0x00428DF0, StringCheck<0x00428DF0>::RangeInitialize, 5, StringCheck<0x00428DF0>::original);
EI_VERIFY(0x00428F10, StringCheck<0x00428F10>::RangeInitialize, 5, StringCheck<0x00428F10>::original);
EI_VERIFY(0x0042A2B0, StringCheck<0x0042A2B0>::RangeInitializeTagged, 5, StringCheck<0x0042A2B0>::original);
EI_VERIFY(0x0042A3D0, StringCheck<0x0042A3D0>::RangeInitializeTagged, 5, StringCheck<0x0042A3D0>::original);
EI_VERIFY(0x004A8930, StringCheck<0x004A8930>::CopyConstruct, 7, StringCheck<0x004A8930>::original);
EI_VERIFY(0x004F5DF0, StringCheck<0x004F5DF0>::ConstructAt, 7, StringCheck<0x004F5DF0>::original);
EI_VERIFY(0x0040C5A0, StringCheck<0x0040C5A0>::ConstructFromText, 7, StringCheck<0x0040C5A0>::original);
EI_VERIFY(0x0041F460, StringCheck<0x0041F460>::ConstructDefault, 7, StringCheck<0x0041F460>::original);
EI_VERIFY(0x006B6CC0, StringCheck<0x006B6CC0>::ConstructDefault, 7, StringCheck<0x006B6CC0>::original);
EI_VERIFY(0x00421800, StringCheck<0x00421800>::ConstructWithCapacity, 7, StringCheck<0x00421800>::original);
EI_VERIFY(0x0040C2A0, StringCheck<0x0040C2A0>::Append, 5, StringCheck<0x0040C2A0>::original);
EI_VERIFY(0x0042B700, StringCheck<0x0042B700>::Append, 5, StringCheck<0x0042B700>::original);
EI_VERIFY(0x0040C280, StringCheck<0x0040C280>::Append, 8, StringCheck<0x0040C280>::original);
EI_VERIFY(0x0042AFB0, StringCheck<0x0042AFB0>::Append, 8, StringCheck<0x0042AFB0>::original);
EI_VERIFY(0x00675690, StringCheck<0x00675690>::AppendRange, 8, StringCheck<0x00675690>::original);
EI_VERIFY(0x00426A80, StringCheck<0x00426A80>::AssignRange, 5, StringCheck<0x00426A80>::original);
EI_VERIFY(0x0042A4F0, StringCheck<0x0042A4F0>::AssignRangeTagged, 5, StringCheck<0x0042A4F0>::original);
EI_VERIFY(0x0041F4E0, StringCheck<0x0041F4E0>::AssignString, 5, StringCheck<0x0041F4E0>::original);
EI_VERIFY(0x0041F550, StringCheck<0x0041F550>::AssignText, 5, StringCheck<0x0041F550>::original);
EI_VERIFY(0x0040B3C0, StringCheck<0x0040B3C0>::Erase, 5, StringCheck<0x0040B3C0>::original);
EI_VERIFY(0x0041F5F0, StringCheck<0x0041F5F0>::Substr, 7, StringCheck<0x0041F5F0>::original);
EI_VERIFY(0x0068C680, StringCheck<0x0068C680>::Reserve, 6, StringCheck<0x0068C680>::original);
EI_VERIFY(0x0069BF10, StringCheck<0x0069BF10>::PushBack, 6, StringCheck<0x0069BF10>::original);
EI_VERIFY(0x00445780, StringCheck<0x00445780>::Less, 6, StringCheck<0x00445780>::original);
EI_VERIFY(0x00445810, StringCheck<0x00445810>::Compare, 6, StringCheck<0x00445810>::original);
EI_VERIFY(0x004DDC90, StringCheck<0x004DDC90>::DeletingDestructor, 0, StringCheck<0x004DDC90>::original);
