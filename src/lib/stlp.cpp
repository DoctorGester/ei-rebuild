#include "stlp.h"

#include "crt.h"
#include "hash_map.h"

#include <algorithm>
#include <cstdint>
#include <iterator>

namespace stlp {

void* allocate(std::size_t bytes) { return bytes ? libcrt::NodeAllocate(bytes) : nullptr; }
void deallocate(void* p, std::size_t bytes) { if (p) libcrt::NodeDeallocate(p, bytes); }

std::size_t NextPrime(std::size_t n) { return lib::HashPrime(n); }

// lower_bound<unsigned*> (the last argument is the distance tag): STLport's binary
// search, halving the length, unsigned comparisons (the hashtable constructors' prime lookup).
const std::uint32_t* LowerBoundDwords(const std::uint32_t* first, const std::uint32_t* last,
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
