#pragma once

// Helpers for the network verify wrappers (verify_net_*.cpp).
//
// Constructors of the session and message classes allocate (STLport nodes, string blocks, sockets),
// so the original and ours cannot produce identical bytes. CompareBuilt compares two objects built
// by the two implementations dword by dword: a dword must be equal in both, or point into its own
// object at the same offset, or be a pointer outside the exe image that corresponds one to one with
// the other object's (the same pointer in one object always pairs with the same pointer in the other).
#include "verify.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <utility>
#include <vector>

namespace netverify {

struct BuiltCompare {
    const std::uint8_t* expected;
    const std::uint8_t* actual;
    std::size_t size;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> pairs;   // expected pointer -> actual pointer
    char detail[256] = "";
    bool ok = true;

    static bool InImage(std::uint32_t v) { return v >= 0x00400000u && v < 0x00800000u; }

    void Fail(std::size_t offset, std::uint32_t e, std::uint32_t a) {
        if (ok) std::snprintf(detail, sizeof(detail), "+0x%X: expected %08X, got %08X", unsigned(offset), e, a);
        ok = false;
    }

    // Records that `e` (in the expected object's world) corresponds to `a`.
    bool Pair(std::uint32_t e, std::uint32_t a) {
        for (auto& p : pairs) {
            if (p.first == e) return p.second == a;
            if (p.second == a) return false;
        }
        pairs.emplace_back(e, a);
        return true;
    }

    // Compares the dword at `offset` of both objects.
    void Word(std::size_t offset) {
        std::uint32_t e, a;
        std::memcpy(&e, expected + offset, 4);
        std::memcpy(&a, actual + offset, 4);
        if (e == a) return;
        auto eb = reinterpret_cast<std::uint32_t>(expected), ab = reinterpret_cast<std::uint32_t>(actual);
        if (e - eb <= size && a - ab <= size && e - eb == a - ab) return;   // into the object itself
        if (e >= 0x10000u && a >= 0x10000u && !InImage(e) && !InImage(a) && Pair(e, a)) return;
        Fail(offset, e, a);
    }

    // Compares [from, to) except the dword offsets listed in `skip`.
    void Range(std::size_t from, std::size_t to, std::initializer_list<std::size_t> skip = {}) {
        for (std::size_t o = from; o + 4 <= to; o += 4) {
            bool skipped = false;
            for (std::size_t s : skip) skipped |= s == o;
            if (!skipped) Word(o);
        }
    }

    // Compares two other objects (e.g. ones both objects point to) with the same pointer pairing.
    void Nested(const void* e, const void* a, std::size_t n, const char* what) {
        if (!e || !a) {
            if (e != a) Note(what);
            return;
        }
        BuiltCompare inner{static_cast<const std::uint8_t*>(e), static_cast<const std::uint8_t*>(a), n};
        inner.pairs = pairs;
        inner.Range(0, n);
        pairs = inner.pairs;
        if (!inner.ok) Note(what, inner.detail);
    }

    void Note(const char* what, const char* more = "") {
        if (ok) std::snprintf(detail, sizeof(detail), "%s %s", what, more);
        ok = false;
    }
};

// The handler tables and the type registry of two sessions must hold the same entries in the same
// order (the containers themselves are distinct allocations).
inline void CompareSessionTables(BuiltCompare& cmp, const std::uint8_t* e, const std::uint8_t* a) {
    for (std::size_t v = 0x164; v < 0x2564; v += 12) {
        void* const* eb = *reinterpret_cast<void* const* const*>(e + v);
        void* const* ee = *reinterpret_cast<void* const* const*>(e + v + 4);
        void* const* ab = *reinterpret_cast<void* const* const*>(a + v);
        void* const* ae = *reinterpret_cast<void* const* const*>(a + v + 4);
        if (ee - eb != ae - ab || (eb != ee && std::memcmp(eb, ab, (ee - eb) * sizeof(void*)) != 0)) {
            char what[48];
            std::snprintf(what, sizeof(what), "handlers at +0x%X", unsigned(v));
            cmp.Note(what);
            return;
        }
    }
    // Hash tables at +0x2C and +0x40: same bucket count, same chains (key, value) per bucket.
    for (std::size_t t : {std::size_t(0x2C), std::size_t(0x40)}) {
        auto* ebk = *reinterpret_cast<std::uint32_t* const*>(e + t + 4);
        auto* ebe = *reinterpret_cast<std::uint32_t* const*>(e + t + 8);
        auto* abk = *reinterpret_cast<std::uint32_t* const*>(a + t + 4);
        auto* abe = *reinterpret_cast<std::uint32_t* const*>(a + t + 8);
        if (ebe - ebk != abe - abk) {
            cmp.Note("registry bucket count");
            return;
        }
        for (std::ptrdiff_t i = 0; i < ebe - ebk; ++i) {
            auto* en = reinterpret_cast<const std::uint32_t*>(ebk[i]);
            auto* an = reinterpret_cast<const std::uint32_t*>(abk[i]);
            while (en && an) {
                if (en[1] != an[1] || en[2] != an[2]) {
                    cmp.Note("registry entry");
                    return;
                }
                en = reinterpret_cast<const std::uint32_t*>(en[0]);
                an = reinterpret_cast<const std::uint32_t*>(an[0]);
            }
            if (en || an) {
                cmp.Note("registry chain length");
                return;
            }
        }
    }
}

} // namespace netverify
