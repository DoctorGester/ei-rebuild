// lib::HashMap's bucket counts (hash_map.h).
#include "hash_map.h"

#include <algorithm>
#include <iterator>

namespace lib {

namespace {
// STLport's __stl_prime_list.
constexpr std::size_t kPrimes[28] = {
    53ul, 97ul, 193ul, 389ul, 769ul, 1543ul, 3079ul, 6151ul, 12289ul, 24593ul,
    49157ul, 98317ul, 196613ul, 393241ul, 786433ul, 1572869ul, 3145739ul, 6291469ul,
    12582917ul, 25165843ul, 50331653ul, 100663319ul, 201326611ul, 402653189ul,
    805306457ul, 1610612741ul, 3221225473ul, 4294967291ul};
} // namespace

std::size_t HashPrime(std::size_t hint) {
    const std::size_t* p = std::lower_bound(std::begin(kPrimes), std::end(kPrimes), hint);
    return p == std::end(kPrimes) ? kPrimes[27] : *p;
}

} // namespace lib
