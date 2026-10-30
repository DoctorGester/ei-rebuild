#pragma once

// Payloads of the "coder" family (vectors.h): the LZ coder of save games and network messages
// (src/game/net_coder.cpp, CoderState::Compress 00430450 and Decompress 00430960, and the code tables
// 0042FAE0 fills at start-up). Recorded by record_coder.cpp (the tag original-oracle), replayed by replay_coder.cpp. The coder
// records are variable-size: a head struct, then the input and output bytes.

#include "../../src/game/net_coder.h"

#include <cstddef>
#include <cstdint>

namespace ei::vectors::coder {

// "@CoderTables" (fixture): the tables as game.exe holds them, each from the address the original's code
// indexes from (the lengths' at 0079B844 and 0079B7BC, one entry before the tables, which start at length 1; the
// masks' at 0078DB24, one entry before (1 << 1) - 1). The replay checks that InitCodeTables fills ours
// (net_coder.h: g_lengthCodes ...) the same and that kCoderBitMasks is the game's, then leaves the game's in ours
// for the coder checks.
struct Tables {
    std::uint32_t lengthCodes[0x23];
    std::uint32_t lengthBits[0x23];
    std::uint32_t offsetCodes[0x22];
    std::uint32_t offsetBits[0x22];
    std::uint32_t masks[33];
};

// A GameBuffer as offsets from its start (base 0 only) and its class.
struct BufferState {
    std::int32_t cur, high, end;
    std::uint32_t flags;
    std::uint8_t growing;   // the growing buffer (0073D624), else the fixed view
    std::uint8_t pad[3];
};

// CoderState's scalars and its output cursor's pending bits.
struct Scalars {
    std::uint32_t pos;
    std::int32_t matchPos, matchLen;
    std::uint32_t prev;
    std::uint32_t acc;
    std::uint8_t bits;
    std::uint8_t pad[3];
};

constexpr std::uint32_t kTablesSize = offsetof(CoderState, out);   // window, chain and head

// CoderState::Compress: CompressHead, then the input (inSize bytes from its start), then what was
// written at the output's cursor (outSize bytes).
struct CompressHead {
    std::uint8_t tablesBefore[kTablesSize];
    Scalars before, after;
    std::uint64_t tablesAfter;   // HashBytes of window, chain and head afterwards
    BufferState inBefore, inAfter, outBefore, outAfter;
    std::uint32_t inSize, outSize;
};

// CoderState::Decompress: DecompressHead, then the input it read (inSize bytes from the input's
// cursor), then what it wrote at the output's cursor (outSize bytes). It uses the first 0x400 bytes
// of the window only.
struct DecompressHead {
    std::uint8_t windowBefore[0x400], windowAfter[0x400];
    std::uint32_t posBefore, posAfter;
    BufferState inBefore, inAfter, outBefore, outAfter;
    std::uint32_t inSize, outSize;
};

// Records larger than this (input of Compress, output of Decompress) are not kept: the save games'
// coder runs on whole worlds.
constexpr std::uint32_t kMaxBytes = 0x4000;

// Bucket by size: up to 64 bytes, up to 1 KB, larger.
inline unsigned SizeBucket(std::uint32_t size) { return size <= 64 ? 0 : size <= 1024 ? 1 : 2; }

} // namespace ei::vectors::coder
