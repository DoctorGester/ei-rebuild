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
// S12g: _M_allocate_block builds (the original first, its block freed); operator== is pure;
// both operator+ run ours only per call (two blocks each: a temporary and the result, which the
// original's run could not hand back in order) and against the original in a start-up check;
// __stl_throw_out_of_range is driven by that start-up check too (no scenario takes a substr past
// the end): each throw caught by a filter that keeps the thrown object's 0x10C bytes and its throw
// information, the two compared, and the temporary string's block back on its free list after the
// unwind.
#include "verify.h"

#include "stlp_list_verify.h"

#include "../../src/lib/stlp_string.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#define NOMINMAX
#include <windows.h>

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

    // --- S12g ---
    static bool __fastcall Equals(const stlp::string* a, const stlp::string* b) {
        bool theirs = Original<decltype(&StringAbi::Equals)>()(a, b);
        bool ours = StringAbi::Equals(a, b);
        ei::verify::Check(Name<A>("stlp::operator==(string, string)"), theirs, ours, "");
        return ours;
    }
    static void __fastcall AllocateBlock(stlp::string* self, void*, std::size_t n) {
        Built("stlp::_String_base::_M_allocate_block", self, [&](bool orig) {
            (orig ? Original<decltype(&StringAbi::AllocateBlock)>() : &StringAbi::AllocateBlock)(self, nullptr, n);
        }, false);
    }
    // operator+: `out` holds the head's bytes then the tail's, in a block of exactly size + 1.
    static void Joined(const char* kind, const stlp::string& out, const char* head, std::size_t headSize,
                       const char* tail, std::size_t tailSize) {
        bool ok = Size(out) == headSize + tailSize && Capacity(out) == headSize + tailSize + 1 &&
                  std::memcmp(StringAbi::Start(out), head, headSize) == 0 &&
                  std::memcmp(StringAbi::Start(out) + headSize, tail, tailSize) == 0 &&
                  StringAbi::Start(out)[headSize + tailSize] == 0;
        char detail[64] = "";
        if (!ok)
            std::snprintf(detail, sizeof(detail), "%u + %u bytes: size %u, capacity %u", static_cast<unsigned>(headSize),
                          static_cast<unsigned>(tailSize), static_cast<unsigned>(Size(out)),
                          static_cast<unsigned>(Capacity(out)));
        ei::verify::Report(Name<A>(kind), ok, detail);
    }
    static stlp::string* __fastcall PlusText(stlp::string* out, const stlp::string* a, const char* text) {
        const char* head = StringAbi::Start(*a);
        const std::size_t headSize = Size(*a);
        const std::size_t tailSize = std::strlen(text);
        stlp::string* result = StringAbi::PlusText(out, a, text);
        Joined("stlp::operator+(string, const char*)", *out, head, headSize, text, tailSize);
        return result;
    }
    static stlp::string* __fastcall PlusString(stlp::string* out, const stlp::string* a, const stlp::string* b) {
        stlp::string* result = StringAbi::PlusString(out, a, b);
        Joined("stlp::operator+(string, string)", *out, StringAbi::Start(*a), Size(*a), StringAbi::Start(*b), Size(*b));
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

// --- S12g: operator+, operator==, _M_allocate_block and the out_of_range throw ----------------------

// What a throw left: the thrown object's bytes and its throw information (MSVC's C++ exception code
// 0xE06D7363; ExceptionInformation[1] the object, [2] the throw information).
struct Thrown {
    bool caught = false;
    const void* info = nullptr;
    unsigned char object[0x10C] = {};
};

int KeepThrown(const EXCEPTION_POINTERS* pointers, Thrown* out) {
    const EXCEPTION_RECORD* record = pointers->ExceptionRecord;
    if (record->ExceptionCode != 0xE06D7363 || record->NumberParameters < 3) return EXCEPTION_CONTINUE_SEARCH;
    out->caught = true;
    out->info = reinterpret_cast<const void*>(record->ExceptionInformation[2]);
    std::memcpy(out->object, reinterpret_cast<const void*>(record->ExceptionInformation[1]), sizeof(out->object));
    return EXCEPTION_EXECUTE_HANDLER;
}

// No C++ objects in this frame (__try): the frames in between are unwound by their own handlers.
void CatchThrow(void(__fastcall* thrower)(), Thrown* out) {
    __try {
        thrower();
    } __except (KeepThrown(GetExceptionInformation(), out)) {
    }
}

bool SameThrow(const Thrown& theirs, const Thrown& ours) {
    return theirs.caught && ours.caught && theirs.info == ours.info &&
           std::memcmp(theirs.object, ours.object, sizeof(theirs.object)) == 0;
}

void(__fastcall* s_throwOutOfRange)();

// 004410A0: when the game takes it, both throws are caught and compared first, then ours throws for
// the game.
[[noreturn]] void __fastcall ThrowOutOfRangeVerify() {
    Thrown theirs;
    Thrown ours;
    CatchThrow(s_throwOutOfRange, &theirs);
    CatchThrow(&stlp::ThrowOutOfRange, &ours);
    ei::verify::Report("stlp::__stl_throw_out_of_range 004410A0", SameThrow(theirs, ours), "");
    stlp::ThrowOutOfRange();
}

void StringJoinStartupCheck() {
    const auto plusText = OriginalOf<0x00427D60>(&StringAbi::PlusText);
    const auto plusString = OriginalOf<0x00427E80>(&StringAbi::PlusString);
    const auto equals = OriginalOf<0x00427FA0>(&StringAbi::Equals);
    const auto allocateBlock = OriginalOf<0x00424E40>(&StringAbi::AllocateBlock);
    static const char* const kTexts[] = {"", "a", "basic_string", "the quick brown fox jumps over the lazy dog"};
    const char allocator = 0;
    alignas(4) unsigned char storage[4][sizeof(stlp::string)];
    auto* a = reinterpret_cast<stlp::string*>(storage[0]);
    auto* b = reinterpret_cast<stlp::string*>(storage[1]);
    auto* theirs = reinterpret_cast<stlp::string*>(storage[2]);
    auto* ours = reinterpret_cast<stlp::string*>(storage[3]);
    int failed = -1;
    for (int i = 0; i < 16; ++i) {
        StringAbi::ConstructFromText(a, nullptr, kTexts[i / 4], &allocator);
        StringAbi::ConstructFromText(b, nullptr, kTexts[i % 4], &allocator);
        for (int op = 0; op < 2; ++op) {
            if (op == 0) {
                plusText(theirs, a, kTexts[i % 4]);
                StringAbi::PlusText(ours, a, kTexts[i % 4]);
            } else {
                plusString(theirs, a, b);
                StringAbi::PlusString(ours, a, b);
            }
            bool same = State::Of(*theirs).SameText(State::Of(*ours)) && equals(theirs, ours) &&
                        StringAbi::Equals(theirs, ours) && StringAbi::Equals(a, b) == equals(a, b);
            if (!same && failed < 0) failed = i * 2 + op;
            Release(theirs);
            Release(ours);
        }
        Release(a);
        Release(b);
    }
    for (std::size_t n : {std::size_t(0), std::size_t(1), std::size_t(24)}) {
        allocateBlock(theirs, nullptr, n);
        StringAbi::AllocateBlock(ours, nullptr, n);
        bool same = State::Of(*theirs, false).SameText(State::Of(*ours, false)) && Size(*ours) == 0 &&
                    (n == 0) == (StringAbi::Start(*ours) == nullptr);
        if (!same && failed < 0) failed = 100 + static_cast<int>(n);
        Release(theirs);
        Release(ours);
    }
    char detail[32] = "";
    if (failed >= 0) std::snprintf(detail, sizeof(detail), "case %d", failed);
    ei::verify::Report("stlp::basic_string operator+ / == / _M_allocate_block start-up (original against ours)",
                       failed < 0, detail);

    // The temporary string's block is freed as each throw unwinds: the free list's top as before.
    void** list = stlp_verify::FreeList(sizeof("basic_string"));
    void* top = *list;
    Thrown expected;
    Thrown actual;
    CatchThrow(s_throwOutOfRange, &expected);
    bool unwound = *list == top;
    CatchThrow(&stlp::ThrowOutOfRange, &actual);
    unwound = unwound && *list == top;
    char where[64] = "";
    if (!unwound) std::snprintf(where, sizeof(where), "the free list's top %p, was %p", *list, top);
    ei::verify::Report("stlp::__stl_throw_out_of_range start-up (original against ours)",
                       SameThrow(expected, actual) && unwound, where);
}

} // namespace

EI_STARTUP_CHECK(StringStartupCheck);
EI_STARTUP_CHECK(StringJoinStartupCheck);

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
// S12g. push ecx; push ebp; mov ebp,[esp+0xC] (6 bytes); push -1; push imm32 (7); mov eax,[edx+4];
// push esi; mov esi,[ecx] (6).
EI_VERIFY(0x00424E40, StringCheck<0x00424E40>::AllocateBlock, 6, StringCheck<0x00424E40>::original);
EI_VERIFY(0x00427D60, StringCheck<0x00427D60>::PlusText, 7, StringCheck<0x00427D60>::original);
EI_VERIFY(0x00427E80, StringCheck<0x00427E80>::PlusString, 7, StringCheck<0x00427E80>::original);
EI_VERIFY(0x00427FA0, StringCheck<0x00427FA0>::Equals, 6, StringCheck<0x00427FA0>::original);
EI_VERIFY(0x004410A0, ThrowOutOfRangeVerify, 7, s_throwOutOfRange);
