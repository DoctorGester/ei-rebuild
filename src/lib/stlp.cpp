#include "stlp.h"

#include "crt.h"

#include <algorithm>
#include <cstdint>
#include <iterator>

namespace stlp {

namespace {
// __node_alloc<true,0>::allocate / deallocate (libcrt::NodeAllocate / NodeDeallocate, crt.h).

constexpr std::size_t kPrimes[28] = {
    53ul, 97ul, 193ul, 389ul, 769ul, 1543ul, 3079ul, 6151ul, 12289ul, 24593ul,
    49157ul, 98317ul, 196613ul, 393241ul, 786433ul, 1572869ul, 3145739ul, 6291469ul,
    12582917ul, 25165843ul, 50331653ul, 100663319ul, 201326611ul, 402653189ul,
    805306457ul, 1610612741ul, 3221225473ul, 4294967291ul};

} // namespace

void* allocate(std::size_t bytes) { return bytes ? libcrt::NodeAllocate(bytes) : nullptr; }
void deallocate(void* p, std::size_t bytes) { if (p) libcrt::NodeDeallocate(p, bytes); }

std::size_t NextPrime(std::size_t n) {
    const std::size_t* p = std::lower_bound(std::begin(kPrimes), std::end(kPrimes), n);
    return p == std::end(kPrimes) ? kPrimes[27] : *p;
}

// lower_bound<unsigned*> (the last argument is the distance tag): STLport's binary
// search, halving the length, unsigned comparisons (the hashtable constructors' prime lookup).
const std::uint32_t* __fastcall LowerBoundDwords(const std::uint32_t* first, const std::uint32_t* last,
                                                 const std::uint32_t* value, int) {
    int length = static_cast<int>(last - first);
    while (length > 0) {
        const int half = length >> 1;
        if (first[half] < *value) {
            first += half + 1;
            length -= half + 1;
        } else {
            length = half;
        }
    }
    return first;
}

} // namespace stlp
