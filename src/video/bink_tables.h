#pragma once

// The constant tables of Bink 1 video (bink_tables.cpp).

#include <cstdint>

namespace bink {

struct HuffmanCode {
    std::uint8_t bits;     // the code, its first bit lowest
    std::uint8_t length;   // 1..7
};

// The 16 prefix codes a bundle's symbols are coded with, per tree the code of each symbol index.
extern const HuffmanCode kTreeCodes[16][16];
// The orders in which a run-fill block visits its 64 pixels (row-major positions).
extern const std::uint8_t kRunPatterns[16][64];
// Where the n-th coefficient read goes in the row-major 8x8 block, for DCT blocks and residues alike.
extern const std::uint8_t kScan[64];
// Per quantiser index the dequantising factor of each row-major coefficient, scaled by 2^11 (the inverse DCT's
// fixed point): intra blocks and inter blocks.
extern const std::int32_t kIntraQuant[16][64];
extern const std::int32_t kInterQuant[16][64];

} // namespace bink
