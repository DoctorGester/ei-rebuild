#include "stlp.h"

#include "ei/hook.h"

#include <algorithm>
#include <iterator>

namespace stlp {

namespace {
// __node_alloc<true,0>::allocate / deallocate in game.exe, both __fastcall.
__declspec(naked) void* __fastcall NodeAllocate(std::size_t) EI_ORIGINAL(0x00405950)
__declspec(naked) void __fastcall NodeDeallocate(void*, std::size_t) EI_ORIGINAL(0x00404D70)

constexpr std::size_t kPrimes[28] = {
    53ul, 97ul, 193ul, 389ul, 769ul, 1543ul, 3079ul, 6151ul, 12289ul, 24593ul,
    49157ul, 98317ul, 196613ul, 393241ul, 786433ul, 1572869ul, 3145739ul, 6291469ul,
    12582917ul, 25165843ul, 50331653ul, 100663319ul, 201326611ul, 402653189ul,
    805306457ul, 1610612741ul, 3221225473ul, 4294967291ul};

constexpr char kNul = 0;
} // namespace

void* allocate(std::size_t bytes) { return bytes ? NodeAllocate(bytes) : nullptr; }
void deallocate(void* p, std::size_t bytes) { if (p) NodeDeallocate(p, bytes); }

std::size_t NextPrime(std::size_t n) {
    const std::size_t* p = std::lower_bound(std::begin(kPrimes), std::end(kPrimes), n);
    return p == std::end(kPrimes) ? kPrimes[27] : *p;
}

void string::InitRange(const char* first, const char* last) {
    std::size_t n = static_cast<std::size_t>(last - first);
    start_ = static_cast<char*>(allocate(n + 1));
    eos_ = start_ + n + 1;
    std::memmove(start_, first, n);
    finish_ = start_ + n;
    *finish_ = kNul;
}

void string::Reserve(string& s, std::size_t capacity) {
    s.start_ = s.finish_ = static_cast<char*>(allocate(capacity));
    s.eos_ = s.start_ + capacity;
    *s.finish_ = kNul;
}

void string::Assign(const char* first, const char* last) {
    char* cur = start_;
    while (first != last && cur != finish_) *cur++ = *first++;
    if (first != last) {
        Append(first, last);
    } else if (cur != finish_) {
        // erase(cur, end()): move the terminator down.
        std::memmove(cur, finish_, 1);
        finish_ = cur;
    }
}

void string::Append(const char* first, const char* last) {
    if (first == last) return;
    std::size_t size = this->size();
    std::size_t n = static_cast<std::size_t>(last - first);
    if (static_cast<std::size_t>(eos_ - start_) - 1 < size + n) {
        std::size_t capacity = size + std::max(n, size) + 1;
        char* fresh = static_cast<char*>(allocate(capacity));
        std::memcpy(fresh, start_, size);
        std::memcpy(fresh + size, first, n);
        fresh[size + n] = kNul;
        Release();
        start_ = fresh;
        finish_ = fresh + size + n;
        eos_ = fresh + capacity;
    } else {
        std::memcpy(finish_ + 1, first + 1, n - 1);
        finish_[n] = kNul;
        *finish_ = *first;
        finish_ += n;
    }
}

} // namespace stlp
